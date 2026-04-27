import json
import os
import re
import shutil
import tempfile
import threading
import time
import zipfile
from dataclasses import dataclass, field
from typing import Callable, Dict, Iterable, List, Optional, Tuple

from . import simkeys_hgx_combat as hgx_combat
from . import simkeys_hgx_data as hgx_data


DAMAGE_METER_DIR_NAME = "damage-meter"
DAMAGE_METER_ARCHIVE_DIR_NAME = "damage-meter-archives"
DAMAGE_METER_REPORT_DIR_NAME = "damage-meter-reports"
DAMAGE_METER_LOG_PATTERN = re.compile(r"^chat_\d+\.jsonl$", re.IGNORECASE)
KILL_LINE_MARKER = " killed "
DEATH_EVENT_RE = re.compile(
    r"^(?P<player>.+?)\s+(?P<action>respawn|averts death)\s*:\s*(?P<method>.+?)\s*:\s*\*success\*\s*$",
    re.IGNORECASE,
)
MAX_CHAT_LINE_LENGTH = 230
MERGE_TIME_WINDOW_SECONDS = 1.25
UNKNOWN_ACTOR_LABEL = "Unknown"
PROGRESS_EMIT_INTERVAL = 1000
PARAGON_PREFIXES = ("Elite", "Superior", "Paragon")
REPORT_ARCHIVE_TIMESTAMP_RE = re.compile(r"damage-meter_(?P<stamp>\d{8}_\d{6})(?:_\d+)?\.zip$", re.IGNORECASE)
CHAT_TIMESTAMP_RE = re.compile(
    r"^\[CHAT WINDOW TEXT\]\s*\[(?P<stamp>[A-Z][a-z]{2}\s+[A-Z][a-z]{2}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2})\]",
    re.IGNORECASE,
)
MONTH_NUMBER = {
    "jan": 1,
    "feb": 2,
    "mar": 3,
    "apr": 4,
    "may": 5,
    "jun": 6,
    "jul": 7,
    "aug": 8,
    "sep": 9,
    "oct": 10,
    "nov": 11,
    "dec": 12,
}

_DAMAGE_TYPE_LABEL_BY_ID = {
    value: name.replace("raw", "raw ").replace("negative", "negative energy").replace("positive", "positive energy").title()
    for name, value in hgx_data.DAMAGE_TYPE_NAME_TO_ID.items()
}
_DAMAGE_TYPE_LABEL_BY_ID.update({
    5: "Electrical",
    9: "Magical",
    10: "Negative",
    11: "Positive",
})


@dataclass(frozen=True)
class SavedChatRecord:
    sequence: int
    text: str
    pid: int = 0
    client_name: str = ""
    captured_at: float = 0.0


@dataclass
class DamageMeterActorStats:
    name: str
    raw_damage: int = 0
    raw_healing: int = 0
    counted_lines: int = 0
    healing_lines: int = 0
    damage_by_type: Dict[str, int] = field(default_factory=dict)
    healing_by_type: Dict[str, int] = field(default_factory=dict)
    targets: Dict[str, int] = field(default_factory=dict)

    @property
    def net(self) -> int:
        return self.raw_damage - self.raw_healing


@dataclass
class EnemyKillStats:
    base_name: str
    total: int = 0
    variants: Dict[str, int] = field(default_factory=dict)

    def sorted_variants(self) -> List[Tuple[str, int]]:
        return sorted(
            self.variants.items(),
            key=lambda item: (_enemy_variant_sort_key(item[0], self.base_name), item[0].casefold()),
        )


@dataclass
class DeathStats:
    name: str
    deaths: int = 0
    killed_by: Dict[str, int] = field(default_factory=dict)


@dataclass
class DamageMeterSummary:
    log_dir: str = ""
    lines_seen: int = 0
    damage_lines_seen: int = 0
    kill_lines_seen: int = 0
    counted_lines: int = 0
    ignored_lines: int = 0
    merged_observations: int = 0
    merged_kill_observations: int = 0
    enemy_kills_counted: int = 0
    deaths_counted: int = 0
    ambiguous_observations: int = 0
    resolved_ambiguous_events: int = 0
    unresolved_ambiguous_events: int = 0
    unknown_damage_types: int = 0
    raw_damage: int = 0
    raw_healing: int = 0
    damage_by_type: Dict[str, int] = field(default_factory=dict)
    healing_by_type: Dict[str, int] = field(default_factory=dict)
    actors: Dict[str, DamageMeterActorStats] = field(default_factory=dict)
    enemy_kills: Dict[str, EnemyKillStats] = field(default_factory=dict)
    deaths: Dict[str, DeathStats] = field(default_factory=dict)

    @property
    def net(self) -> int:
        return self.raw_damage - self.raw_healing

    def sorted_actors(self, key: str = "net") -> List[DamageMeterActorStats]:
        if key == "raw":
            sort_key = lambda item: (-item.raw_damage, item.name.casefold())
        elif key == "healing":
            sort_key = lambda item: (-item.raw_healing, item.name.casefold())
        else:
            sort_key = lambda item: (-item.net, item.name.casefold())
        return sorted(self.actors.values(), key=sort_key)

    def sorted_enemy_kills(self) -> List[EnemyKillStats]:
        return sorted(self.enemy_kills.values(), key=lambda item: (item.base_name.casefold(), -item.total))

    def sorted_deaths(self) -> List[DeathStats]:
        return sorted(self.deaths.values(), key=lambda item: (-item.deaths, item.name.casefold()))


@dataclass(frozen=True)
class DamageObservation:
    index: int
    sequence: int
    pid: int
    client_name: str
    captured_at: float
    event_time: Optional[float]
    source_key: str
    normalized_text: str
    damage: object
    component_signature: Tuple[Tuple[int, object], ...]

    @property
    def attacker(self) -> str:
        return self.damage.attacker

    @property
    def defender(self) -> str:
        return self.damage.defender

    @property
    def has_ambiguous_actor(self) -> bool:
        return _is_ambiguous_actor(self.attacker) or _is_ambiguous_actor(self.defender)


@dataclass(frozen=True)
class KillObservation:
    index: int
    sequence: int
    pid: int
    client_name: str
    captured_at: float
    event_time: Optional[float]
    source_key: str
    normalized_text: str
    killer: str
    victim: str


@dataclass
class KillEventCluster:
    observations: List[KillObservation] = field(default_factory=list)

    @property
    def representative(self) -> KillObservation:
        return max(
            self.observations,
            key=lambda observation: (
                _actor_specificity(observation.killer) + _actor_specificity(observation.victim),
                -observation.index,
            ),
        )

    @property
    def killer(self) -> str:
        return _choose_cluster_actor(observation.killer for observation in self.observations)

    @property
    def victim(self) -> str:
        return _choose_cluster_actor(observation.victim for observation in self.observations)


@dataclass(frozen=True)
class DeathObservation:
    index: int
    sequence: int
    pid: int
    client_name: str
    captured_at: float
    event_time: Optional[float]
    source_key: str
    normalized_text: str
    victim: str
    cause: str
    kind: str


@dataclass
class DeathEventCluster:
    observations: List[DeathObservation] = field(default_factory=list)

    @property
    def representative(self) -> DeathObservation:
        return max(
            self.observations,
            key=lambda observation: (
                1 if observation.kind == "kill" else 0,
                _actor_specificity(observation.cause),
                -observation.index,
            ),
        )

    @property
    def victim(self) -> str:
        return _choose_cluster_actor(observation.victim for observation in self.observations)

    @property
    def cause(self) -> str:
        kill_causes = [
            observation.cause
            for observation in self.observations
            if observation.kind == "kill" and observation.cause
        ]
        if kill_causes:
            return _choose_cluster_actor(kill_causes)
        return self.representative.cause or "Unknown"


@dataclass
class DamageEventCluster:
    observations: List[DamageObservation] = field(default_factory=list)

    @property
    def representative(self) -> DamageObservation:
        return max(
            self.observations,
            key=lambda observation: (
                _actor_specificity(observation.attacker) + _actor_specificity(observation.defender),
                -observation.index,
            ),
        )

    @property
    def attacker(self) -> str:
        return _choose_cluster_actor(observation.attacker for observation in self.observations)

    @property
    def defender(self) -> str:
        return _choose_cluster_actor(observation.defender for observation in self.observations)

    @property
    def has_ambiguous_observation(self) -> bool:
        return any(observation.has_ambiguous_actor for observation in self.observations)

    @property
    def resolved_ambiguous(self) -> bool:
        return (
            self.has_ambiguous_observation
            and not _is_ambiguous_actor(self.attacker)
            and not _is_ambiguous_actor(self.defender)
        )


class DamageMeterRecorder:
    def __init__(self, pid: int, log_dir: Optional[str] = None):
        self.pid = int(pid)
        self.log_dir = os.path.abspath(log_dir or session_log_dir())
        self.path = os.path.join(self.log_dir, f"chat_{self.pid}.jsonl")
        self._lock = threading.RLock()
        self._handle = None

    def record_event(self, sequence: int, raw_text: str, client_name: str = ""):
        text = str(raw_text or "")
        if not text:
            return

        payload = {
            "time": time.time(),
            "pid": self.pid,
            "client": str(client_name or ""),
            "seq": int(sequence or 0),
            "text": text,
        }
        with self._lock:
            if self._handle is None:
                os.makedirs(self.log_dir, exist_ok=True)
                self._handle = open(self.path, "a", encoding="utf-8", buffering=1)
            self._handle.write(json.dumps(payload, ensure_ascii=True, separators=(",", ":")) + "\n")

    def close(self):
        with self._lock:
            handle = self._handle
            self._handle = None
        if handle is not None:
            handle.close()


def project_root() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = (
        os.path.abspath(os.path.join(here, os.pardir, os.pardir)),
        os.path.abspath(os.path.join(here, os.pardir)),
    )
    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, "README.md")):
            return candidate
    return candidates[0]


def session_log_dir(root_dir: Optional[str] = None) -> str:
    return os.path.join(os.path.abspath(root_dir or project_root()), "logs", DAMAGE_METER_DIR_NAME)


def session_archive_dir(root_dir: Optional[str] = None) -> str:
    return os.path.join(os.path.abspath(root_dir or project_root()), "logs", DAMAGE_METER_ARCHIVE_DIR_NAME)


def session_report_dir(root_dir: Optional[str] = None) -> str:
    return os.path.join(os.path.abspath(root_dir or project_root()), "logs", DAMAGE_METER_REPORT_DIR_NAME)


def _archive_dir_for_log_dir(log_dir: str) -> str:
    parent = os.path.dirname(os.path.abspath(log_dir))
    return os.path.join(parent, DAMAGE_METER_ARCHIVE_DIR_NAME)


def _session_started_at(directory: str) -> float:
    path = os.path.join(directory, "session.json")
    try:
        with open(path, "r", encoding="utf-8-sig") as handle:
            payload = json.load(handle)
        return float(payload.get("started") or 0.0)
    except Exception:
        return 0.0


def _archive_timestamp(directory: str, paths: List[str]) -> str:
    started = _session_started_at(directory)
    if started <= 0.0:
        mtimes = []
        for path in paths:
            try:
                mtimes.append(os.path.getmtime(path))
            except OSError:
                pass
        started = min(mtimes) if mtimes else time.time()
    return time.strftime("%Y%m%d_%H%M%S", time.localtime(started))


def _unique_archive_path(directory: str, base_name: str) -> str:
    path = os.path.join(directory, base_name)
    if not os.path.exists(path):
        return path
    root, ext = os.path.splitext(base_name)
    index = 2
    while True:
        candidate = os.path.join(directory, f"{root}_{index}{ext}")
        if not os.path.exists(candidate):
            return candidate
        index += 1


def _archivable_session_paths(directory: str) -> List[str]:
    if not os.path.isdir(directory):
        return []
    paths = []
    for root, _dirs, names in os.walk(directory):
        for name in names:
            path = os.path.join(root, name)
            if os.path.isfile(path):
                paths.append(path)
    return sorted(paths)


def archive_session_logs(log_dir: Optional[str] = None, archive_dir: Optional[str] = None) -> str:
    directory = os.path.abspath(log_dir or session_log_dir())
    if not os.path.isdir(directory):
        return ""

    chat_paths = _saved_chat_log_paths(directory)
    if not chat_paths:
        return ""

    paths = _archivable_session_paths(directory)
    if not paths:
        return ""

    destination_dir = os.path.abspath(archive_dir or _archive_dir_for_log_dir(directory))
    os.makedirs(destination_dir, exist_ok=True)
    archive_name = f"damage-meter_{_archive_timestamp(directory, paths)}.zip"
    archive_path = _unique_archive_path(destination_dir, archive_name)
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in paths:
            archive.write(path, os.path.relpath(path, directory))
    return archive_path


def _report_timestamp_for_summary(summary: DamageMeterSummary) -> str:
    source = os.path.abspath(str(getattr(summary, "log_dir", "") or ""))
    if source and os.path.isfile(source):
        match = REPORT_ARCHIVE_TIMESTAMP_RE.search(os.path.basename(source))
        if match is not None:
            return match.group("stamp")
        try:
            return time.strftime("%Y%m%d_%H%M%S", time.localtime(os.path.getmtime(source)))
        except OSError:
            pass
    if source and os.path.isdir(source):
        started = _session_started_at(source)
        if started > 0.0:
            return time.strftime("%Y%m%d_%H%M%S", time.localtime(started))
    return time.strftime("%Y%m%d_%H%M%S", time.localtime(time.time()))


def save_summary_text(
    summary: DamageMeterSummary,
    text: str,
    output_dir: Optional[str] = None,
) -> str:
    destination_dir = os.path.abspath(output_dir or session_report_dir())
    os.makedirs(destination_dir, exist_ok=True)
    report_name = f"damage-meter-report_{_report_timestamp_for_summary(summary)}.txt"
    report_path = _unique_archive_path(destination_dir, report_name)
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write(str(text or ""))
        if not str(text or "").endswith("\n"):
            handle.write("\n")
    return report_path


def reset_session_logs(log_dir: Optional[str] = None) -> str:
    directory = os.path.abspath(log_dir or session_log_dir())
    if os.path.isdir(directory):
        archive_session_logs(directory)
        for name in os.listdir(directory):
            path = os.path.join(directory, name)
            if os.path.isdir(path):
                shutil.rmtree(path)
            elif name.lower().endswith((".jsonl", ".json", ".txt", ".log")):
                os.remove(path)
    else:
        os.makedirs(directory, exist_ok=True)

    with open(os.path.join(directory, "session.json"), "w", encoding="utf-8") as handle:
        json.dump({"started": time.time()}, handle, ensure_ascii=True, indent=2)
        handle.write("\n")
    return directory


ProgressCallback = Optional[Callable[[dict], None]]


def _emit_progress(
    progress_callback: ProgressCallback,
    phase: str,
    current: int = 0,
    total: int = 0,
    percent: Optional[float] = None,
):
    if progress_callback is None:
        return

    total_value = max(int(total or 0), 0)
    current_value = max(int(current or 0), 0)
    if percent is None and total_value > 0:
        percent = (float(current_value) / float(total_value)) * 100.0
    if percent is not None:
        percent = min(max(float(percent), 0.0), 100.0)
    progress_callback({
        "phase": str(phase or ""),
        "current": current_value,
        "total": total_value,
        "percent": percent,
    })


def _scale_progress_event(event: dict, start_percent: float, end_percent: float) -> dict:
    total = int(event.get("total") or 0)
    current = int(event.get("current") or 0)
    percent = event.get("percent")
    if total > 0:
        fraction = float(current) / float(total)
    elif percent is not None:
        fraction = float(percent) / 100.0
    else:
        fraction = 0.0
    scaled = dict(event)
    scaled["percent"] = float(start_percent) + ((float(end_percent) - float(start_percent)) * min(max(fraction, 0.0), 1.0))
    return scaled


def _saved_chat_log_paths(directory: str) -> List[str]:
    if not os.path.isdir(directory):
        return []
    return [
        os.path.join(directory, name)
        for name in sorted(os.listdir(directory))
        if DAMAGE_METER_LOG_PATTERN.match(name) and os.path.isfile(os.path.join(directory, name))
    ]


def _count_saved_chat_log_lines(paths: List[str], progress_callback: ProgressCallback = None) -> int:
    total = 0
    file_count = len(paths)
    _emit_progress(progress_callback, "Counting logs", 0, file_count)
    for index, path in enumerate(paths, start=1):
        with open(path, "rb") as handle:
            for _line in handle:
                total += 1
        _emit_progress(progress_callback, "Counting logs", index, file_count)
    return total


def iter_saved_chat_records(log_dir: Optional[str] = None) -> Iterable[SavedChatRecord]:
    directory = os.path.abspath(log_dir or session_log_dir())
    yield from _iter_saved_chat_records_from_paths(_saved_chat_log_paths(directory))

def _iter_saved_chat_records_from_paths(
    paths: List[str],
    progress_callback: ProgressCallback = None,
    total_lines: int = 0,
) -> Iterable[SavedChatRecord]:
    lines_read = 0
    _emit_progress(progress_callback, "Reading logs", 0, total_lines)
    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line_number, raw_line in enumerate(handle, start=1):
                lines_read += 1
                if lines_read == 1 or lines_read % PROGRESS_EMIT_INTERVAL == 0:
                    _emit_progress(progress_callback, "Reading logs", lines_read, total_lines)
                line = raw_line.rstrip("\r\n")
                if not line:
                    continue
                try:
                    payload = json.loads(line)
                except json.JSONDecodeError:
                    yield SavedChatRecord(sequence=line_number, text=line)
                    continue

                if not isinstance(payload, dict):
                    continue
                yield SavedChatRecord(
                    sequence=int(payload.get("seq") or line_number),
                    text=str(payload.get("text") or ""),
                    pid=int(payload.get("pid") or 0),
                    client_name=str(payload.get("client") or ""),
                    captured_at=float(payload.get("time") or 0.0),
                )
    _emit_progress(progress_callback, "Reading logs", lines_read, total_lines)


def analyze_session_logs(
    log_dir: Optional[str] = None,
    character_db=None,
    progress_callback: ProgressCallback = None,
) -> DamageMeterSummary:
    directory = os.path.abspath(log_dir or session_log_dir())
    paths = _saved_chat_log_paths(directory)

    def counting_progress(event: dict):
        _emit_progress(
            progress_callback,
            event.get("phase", "Counting logs"),
            event.get("current", 0),
            event.get("total", 0),
            _scale_progress_event(event, 0.0, 8.0).get("percent"),
        )

    def reading_progress(event: dict):
        _emit_progress(
            progress_callback,
            event.get("phase", "Reading logs"),
            event.get("current", 0),
            event.get("total", 0),
            _scale_progress_event(event, 8.0, 45.0).get("percent"),
        )

    def analysis_progress(event: dict):
        phase = str(event.get("phase") or "")
        if phase == "Merging duplicate views":
            scaled = _scale_progress_event(event, 45.0, 75.0)
        elif phase == "Classifying damage":
            scaled = _scale_progress_event(event, 75.0, 98.0)
        else:
            scaled = event
        _emit_progress(
            progress_callback,
            scaled.get("phase", phase),
            scaled.get("current", 0),
            scaled.get("total", 0),
            scaled.get("percent"),
        )

    total_lines = _count_saved_chat_log_lines(paths, counting_progress)
    records = _iter_saved_chat_records_from_paths(paths, reading_progress, total_lines)
    summary = analyze_chat_records(
        records,
        character_db=character_db,
        log_dir=directory,
        progress_callback=analysis_progress,
    )
    _emit_progress(progress_callback, "Done", 1, 1, 100.0)
    return summary


def _safe_extract_zip(archive: zipfile.ZipFile, destination: str):
    destination_root = os.path.abspath(destination)
    for member in archive.infolist():
        target_path = os.path.abspath(os.path.join(destination_root, member.filename))
        if target_path != destination_root and not target_path.startswith(destination_root + os.sep):
            raise ValueError(f"Archive member escapes destination: {member.filename}")
        archive.extract(member, destination_root)


def analyze_archived_session(
    archive_path: str,
    character_db=None,
    progress_callback: ProgressCallback = None,
) -> DamageMeterSummary:
    archive_path = os.path.abspath(str(archive_path or ""))
    if not archive_path or not os.path.isfile(archive_path):
        raise FileNotFoundError(archive_path)
    if not zipfile.is_zipfile(archive_path):
        raise ValueError(f"Not a damage meter archive: {archive_path}")

    _emit_progress(progress_callback, "Extracting archive", 0, 1, 0.0)
    with tempfile.TemporaryDirectory(prefix="simkeys-damage-meter-") as tmpdir:
        with zipfile.ZipFile(archive_path, "r") as archive:
            _safe_extract_zip(archive, tmpdir)
        _emit_progress(progress_callback, "Extracting archive", 1, 1, 5.0)

        def archive_progress(event: dict):
            scaled = _scale_progress_event(event, 5.0, 100.0)
            _emit_progress(
                progress_callback,
                scaled.get("phase", event.get("phase", "")),
                scaled.get("current", 0),
                scaled.get("total", 0),
                scaled.get("percent"),
            )

        summary = analyze_session_logs(
            tmpdir,
            character_db=character_db,
            progress_callback=archive_progress,
        )
    summary.log_dir = archive_path
    return summary


def analyze_chat_records(
    records: Iterable[object],
    character_db=None,
    log_dir: str = "",
    progress_callback: ProgressCallback = None,
) -> DamageMeterSummary:
    db = character_db or hgx_data.load_default_database()
    summary = DamageMeterSummary(log_dir=os.path.abspath(log_dir) if log_dir else "")
    observations = []
    kill_observations = []
    death_observations = []
    for index, record in enumerate(records, start=1):
        if index == 1 or index % PROGRESS_EMIT_INTERVAL == 0:
            _emit_progress(progress_callback, "Parsing damage lines", index, 0)
        sequence, text, pid, client_name, captured_at = _record_parts(record, index)
        if not text:
            continue

        summary.lines_seen += 1
        source_key = _source_key(pid, client_name)
        event_time = _event_time(text, captured_at)
        parsed_kill = _parse_kill_line(text)
        if parsed_kill is not None:
            killer, victim = parsed_kill
            summary.kill_lines_seen += 1
            kill_observations.append(
                KillObservation(
                    index=index,
                    sequence=sequence,
                    pid=pid,
                    client_name=client_name,
                    captured_at=captured_at,
                    event_time=event_time,
                    source_key=source_key,
                    normalized_text=hgx_combat.normalize_chat_line(text),
                    killer=killer,
                    victim=victim,
                )
            )
            if _is_party_death(killer, victim, db):
                death_observations.append(
                    DeathObservation(
                        index=index,
                        sequence=sequence,
                        pid=pid,
                        client_name=client_name,
                        captured_at=captured_at,
                        event_time=event_time,
                        source_key=source_key,
                        normalized_text=hgx_combat.normalize_chat_line(text),
                        victim=victim,
                        cause=killer,
                        kind="kill",
                    )
                )

        parsed_death = _parse_death_event_line(text)
        if parsed_death is not None:
            player, method = parsed_death
            if _is_party_actor(player, db):
                death_observations.append(
                    DeathObservation(
                        index=index,
                        sequence=sequence,
                        pid=pid,
                        client_name=client_name,
                        captured_at=captured_at,
                        event_time=event_time,
                        source_key=source_key,
                        normalized_text=hgx_combat.normalize_chat_line(text),
                        victim=player,
                        cause=method,
                        kind="death",
                    )
                )

        damage = hgx_combat.parse_damage_line(text)
        if damage is None:
            continue

        summary.damage_lines_seen += 1
        observation = DamageObservation(
            index=index,
            sequence=sequence,
            pid=pid,
            client_name=client_name,
            captured_at=captured_at,
            event_time=_event_time(text, captured_at),
            source_key=_source_key(pid, client_name),
            normalized_text=damage.normalized_text,
            damage=damage,
            component_signature=_component_signature(damage),
        )
        observations.append(observation)
        if observation.has_ambiguous_actor:
            summary.ambiguous_observations += 1

    _emit_progress(progress_callback, "Parsing damage lines", summary.lines_seen, summary.lines_seen)
    kill_clusters = _merge_kill_observations(kill_observations)
    summary.merged_kill_observations = max(len(kill_observations) - len(kill_clusters), 0)
    for cluster in kill_clusters:
        victim = cluster.victim
        if not _is_enemy(victim, db):
            continue
        _add_enemy_kill(summary, victim)

    death_clusters = _merge_death_observations(death_observations)
    for cluster in death_clusters:
        victim = cluster.victim
        if not _is_party_actor(victim, db):
            continue
        _add_death(summary, victim, cluster.cause)

    clusters = _merge_damage_observations(observations, progress_callback=progress_callback)
    summary.merged_observations = max(len(observations) - len(clusters), 0)
    total_clusters = len(clusters)
    _emit_progress(progress_callback, "Classifying damage", 0, total_clusters)
    for cluster_index, cluster in enumerate(clusters, start=1):
        if cluster_index == 1 or cluster_index % PROGRESS_EMIT_INTERVAL == 0 or cluster_index == total_clusters:
            _emit_progress(progress_callback, "Classifying damage", cluster_index, total_clusters)
        damage = cluster.representative.damage
        attacker = cluster.attacker
        defender = cluster.defender
        if cluster.has_ambiguous_observation:
            if cluster.resolved_ambiguous:
                summary.resolved_ambiguous_events += 1
            else:
                summary.unresolved_ambiguous_events += 1

        if not _is_party_damage_to_enemy(attacker, defender, db):
            summary.ignored_lines += 1
            continue

        outcome = _classify_damage_line(damage, db, defender_name=defender)
        if outcome is None:
            summary.ignored_lines += 1
            continue

        actor_name = UNKNOWN_ACTOR_LABEL if _is_ambiguous_actor(attacker) else attacker
        actor = _get_actor_stats(summary, actor_name)
        actor.counted_lines += 1
        summary.counted_lines += 1
        actor.targets[defender] = actor.targets.get(defender, 0) + 1

        raw_damage, raw_healing, damage_by_type, healing_by_type, unknown_types = outcome
        summary.raw_damage += raw_damage
        summary.raw_healing += raw_healing
        summary.unknown_damage_types += unknown_types
        actor.raw_damage += raw_damage
        actor.raw_healing += raw_healing
        if raw_healing > 0:
            actor.healing_lines += 1
        _merge_counts(summary.damage_by_type, damage_by_type)
        _merge_counts(summary.healing_by_type, healing_by_type)
        _merge_counts(actor.damage_by_type, damage_by_type)
        _merge_counts(actor.healing_by_type, healing_by_type)

    return summary


def format_summary_text(summary: DamageMeterSummary, actor_limit: int = 1000) -> str:
    lines = [
        f"Lines: {summary.lines_seen:,}   damage views: {summary.damage_lines_seen:,}   events: {summary.counted_lines:,}",
        f"Totals: net {summary.net:,}   raw {summary.raw_damage:,}   enemy healing {summary.raw_healing:,}",
        f"Kills: enemies {summary.enemy_kills_counted:,}   kill views {summary.kill_lines_seen:,}   party deaths {summary.deaths_counted:,}",
    ]
    merge_parts = []
    if summary.merged_observations:
        merge_parts.append(f"merged duplicate views {summary.merged_observations:,}")
    if summary.merged_kill_observations:
        merge_parts.append(f"merged duplicate kill views {summary.merged_kill_observations:,}")
    if summary.ambiguous_observations:
        merge_parts.append(
            f"someone views {summary.ambiguous_observations:,}"
            f" ({summary.resolved_ambiguous_events:,} resolved events, {summary.unresolved_ambiguous_events:,} unresolved)"
        )
    if merge_parts:
        lines.append("Multi-client merge: " + "; ".join(merge_parts))
    if summary.ignored_lines:
        lines.append(f"Ignored: {summary.ignored_lines:,} non-party or non-enemy damage lines")
    if summary.unknown_damage_types:
        lines.append(f"Unknown damage components: {summary.unknown_damage_types:,}")

    actors = summary.sorted_actors("net")
    if not actors:
        lines.append("")
        lines.append("No party damage against characters.d enemies has been saved this session yet.")
    else:
        lines.append("")
        lines.append("Damage Summary")
        lines.append(f"{'Name':<32} {'Net':>10} {'Raw':>10} {'Healing':>10} {'Hits':>6}")
        lines.append("-" * 74)
        for actor in actors[:actor_limit]:
            lines.append(
                f"{_trim(actor.name, 32):<32} "
                f"{actor.net:>10,} {actor.raw_damage:>10,} {actor.raw_healing:>10,} {actor.counted_lines:>6,}"
            )

        if len(actors) > actor_limit:
            lines.append(f"... {len(actors) - actor_limit} more")

        lines.append("")
        lines.append("Damage by element: " + _format_counts(summary.damage_by_type))
        lines.append("Healing by element: " + _format_counts(summary.healing_by_type))

        lines.append("")
        lines.append("Party Damage Breakdown")
        for actor in actors[:actor_limit]:
            lines.append(f"{actor.name}:")
            lines.append(f"  Damage: {_format_counts(actor.damage_by_type, limit=24)}")
            if actor.raw_healing:
                lines.append(f"  Enemy healing caused: {_format_counts(actor.healing_by_type, limit=24)}")

    lines.append("")
    lines.append("Enemy Counts")
    enemy_groups = summary.sorted_enemy_kills()
    if not enemy_groups:
        lines.append("-")
    else:
        for group in enemy_groups:
            lines.append(f"{group.base_name}: {group.total:,}")
            for variant_name, count in group.sorted_variants():
                lines.append(f"  {variant_name}: {count:,}")

    lines.append("")
    lines.append("Party Deaths")
    death_stats = summary.sorted_deaths()
    if not death_stats:
        lines.append("-")
    else:
        for death in death_stats:
            lines.append(f"{death.name}: {death.deaths:,}")
            lines.append(f"  To: {_format_counts(death.killed_by, limit=24)}")

    return "\n".join(lines)


def chat_report_lines(summary: DamageMeterSummary, report_type: str, actor_limit: int = 8) -> List[str]:
    report_type = str(report_type or "").strip().lower()
    if not summary or not summary.actors:
        return ["HGCC damage: no party damage against enemies recorded this session."]

    if report_type == "raw":
        return [_limited_line("Raw damage", summary.raw_damage, _actor_fragments(summary.sorted_actors("raw"), "raw_damage", actor_limit))]
    if report_type == "healing":
        return [_limited_line("Enemy healing", summary.raw_healing, _actor_fragments(summary.sorted_actors("healing"), "raw_healing", actor_limit))]
    if report_type in ("breakdown", "elements", "element"):
        lines = []
        lines.append(_limited_counts_line("Damage elements", summary.damage_by_type))
        if summary.raw_healing:
            lines.append(_limited_counts_line("Healing elements", summary.healing_by_type))
        return lines

    return [
        _limited_line(
            "Net damage",
            summary.net,
            _actor_fragments(summary.sorted_actors("net"), "net", actor_limit),
            suffix=f"raw {summary.raw_damage:,}, healing {summary.raw_healing:,}",
        )
    ]


def _record_parts(record: object, fallback_sequence: int) -> Tuple[int, str, int, str, float]:
    if isinstance(record, SavedChatRecord):
        return record.sequence, record.text, record.pid, record.client_name, record.captured_at
    if isinstance(record, dict):
        return (
            int(record.get("seq") or fallback_sequence),
            str(record.get("text") or ""),
            int(record.get("pid") or 0),
            str(record.get("client") or record.get("client_name") or ""),
            float(record.get("time") or record.get("captured_at") or 0.0),
        )
    return fallback_sequence, str(record or ""), 0, "", 0.0


def _is_party_damage_to_enemy(attacker: str, defender: str, db) -> bool:
    if _is_ambiguous_actor(defender):
        return False
    return not _is_enemy(attacker, db) and _is_enemy(defender, db)


def _is_party_actor(name: str, db) -> bool:
    return bool(str(name or "").strip()) and not _is_ambiguous_actor(name) and not _is_enemy(name, db)


def _looks_like_player_name(name: str) -> bool:
    return bool(re.search(r"\[[0-9]+(?:\.[0-9]+)?\]", str(name or "")))


def _is_party_death(killer: str, victim: str, db) -> bool:
    if not _is_party_actor(victim, db):
        return False
    return _is_enemy(killer, db) or _is_ambiguous_actor(killer) or _looks_like_player_name(victim)


def _is_enemy(name: str, db) -> bool:
    record = db.lookup(name)
    if record is None:
        return False
    return int(getattr(record, "character_type", 0) or 0) >= 0


def _parse_kill_line(text: str) -> Optional[Tuple[str, str]]:
    normalized = hgx_combat.normalize_chat_line(text)
    if not normalized:
        return None
    if normalized.startswith("You have the following accomplishments"):
        return None
    if "You cannot gain experience, tags, or random loot from monsters killed in a different area." in normalized:
        return None

    lowered = normalized.lower()
    marker_at = lowered.find(KILL_LINE_MARKER)
    if marker_at <= 0:
        return None

    killer = hgx_combat.normalize_actor_name(normalized[:marker_at])
    victim = hgx_combat.normalize_actor_name(normalized[marker_at + len(KILL_LINE_MARKER):])
    if not killer or not victim:
        return None
    return killer, victim


def _parse_death_event_line(text: str) -> Optional[Tuple[str, str]]:
    normalized = hgx_combat.normalize_chat_line(text)
    match = DEATH_EVENT_RE.match(normalized)
    if match is None:
        return None
    player = hgx_combat.normalize_actor_name(match.group("player"))
    method = hgx_combat.normalize_actor_name(match.group("method")) or "Unknown"
    if not player:
        return None
    return player, method


def _add_enemy_kill(summary: DamageMeterSummary, enemy_name: str):
    base_name, variant_name = _enemy_base_and_variant(enemy_name)
    key = base_name.casefold()
    stats = summary.enemy_kills.get(key)
    if stats is None:
        stats = EnemyKillStats(base_name=base_name)
        summary.enemy_kills[key] = stats
    stats.total += 1
    stats.variants[variant_name] = int(stats.variants.get(variant_name, 0)) + 1
    summary.enemy_kills_counted += 1


def _add_death(summary: DamageMeterSummary, victim: str, cause: str):
    name = hgx_combat.normalize_actor_name(victim)
    cause_name = hgx_combat.normalize_actor_name(cause) or "Unknown"
    if not name:
        return
    stats = summary.deaths.get(name)
    if stats is None:
        stats = DeathStats(name=name)
        summary.deaths[name] = stats
    stats.deaths += 1
    stats.killed_by[cause_name] = int(stats.killed_by.get(cause_name, 0)) + 1
    summary.deaths_counted += 1


def _enemy_base_and_variant(enemy_name: str) -> Tuple[str, str]:
    variant = hgx_combat.normalize_actor_name(enemy_name)
    base = variant
    changed = True
    while changed:
        changed = False
        for prefix in PARAGON_PREFIXES:
            marker = prefix + " "
            if base.casefold().startswith(marker.casefold()):
                base = base[len(marker):].strip()
                changed = True
                break
    return base or variant, variant or "Unknown"


def _enemy_variant_sort_key(variant_name: str, base_name: str) -> Tuple[int, str]:
    variant_key = variant_name.casefold()
    base_key = base_name.casefold()
    if variant_key == base_key:
        return 0, variant_key
    for index, prefix in enumerate(PARAGON_PREFIXES, start=1):
        if variant_key == f"{prefix} {base_name}".casefold():
            return index, variant_key
    return len(PARAGON_PREFIXES) + 1, variant_key


def _classify_damage_line(damage, db, defender_name: Optional[str] = None):
    stats = db.effective_stats(defender_name or damage.defender)
    if stats is None:
        return None

    raw_damage = 0
    raw_healing = 0
    damage_by_type: Dict[str, int] = {}
    healing_by_type: Dict[str, int] = {}
    unknown_types = 0

    for component in damage.components:
        amount = int(component.amount or 0)
        if amount <= 0:
            continue
        label = _damage_type_label(component)
        damage_type = component.damage_type
        healing_multiplier = 0
        if isinstance(damage_type, int) and 0 <= damage_type < len(stats.healing):
            healing_multiplier = int(stats.healing[damage_type] or 0)
        elif label.lower() == "unknown":
            unknown_types += 1

        if healing_multiplier > 0:
            healing_amount = amount * healing_multiplier
            raw_healing += healing_amount
            _add_count(healing_by_type, label, healing_amount)
        else:
            raw_damage += amount
            _add_count(damage_by_type, label, amount)

    return raw_damage, raw_healing, damage_by_type, healing_by_type, unknown_types


def _merge_damage_observations(
    observations: List[DamageObservation],
    progress_callback: ProgressCallback = None,
) -> List[DamageEventCluster]:
    clusters: List[DamageEventCluster] = []
    ordered = sorted(
        observations,
        key=lambda observation: (
            observation.event_time if observation.event_time is not None else float("inf"),
            observation.index,
        ),
    )

    timed_clusters_by_signature: Dict[Tuple[int, Tuple[Tuple[int, object], ...]], List[DamageEventCluster]] = {}
    timed_clusters_by_text: Dict[Tuple[int, Tuple[Tuple[int, object], ...], str], List[DamageEventCluster]] = {}
    untimed_clusters_by_text: Dict[Tuple[int, Tuple[Tuple[int, object], ...], str], List[DamageEventCluster]] = {}
    total = len(ordered)
    _emit_progress(progress_callback, "Merging duplicate views", 0, total)
    for index, observation in enumerate(ordered, start=1):
        if index == 1 or index % PROGRESS_EMIT_INTERVAL == 0 or index == total:
            _emit_progress(progress_callback, "Merging duplicate views", index, total)

        signature_key = _observation_merge_signature_key(observation)
        text_key = (signature_key[0], signature_key[1], observation.normalized_text)
        if observation.event_time is None:
            candidates = list(untimed_clusters_by_text.get(text_key, ()))
            candidates.extend(timed_clusters_by_text.get(text_key, ()))
        else:
            candidates = timed_clusters_by_signature.get(signature_key, [])
            _prune_timed_cluster_candidates(candidates, observation.event_time)

        cluster = _find_matching_cluster(candidates, observation)
        if cluster is None:
            cluster = DamageEventCluster(observations=[observation])
            clusters.append(cluster)
            if observation.event_time is None:
                untimed_clusters_by_text.setdefault(text_key, []).append(cluster)
            else:
                timed_clusters_by_signature.setdefault(signature_key, []).append(cluster)
                timed_clusters_by_text.setdefault(text_key, []).append(cluster)
        else:
            cluster.observations.append(observation)
    return clusters


def _merge_kill_observations(observations: List[KillObservation]) -> List[KillEventCluster]:
    clusters: List[KillEventCluster] = []
    candidates: List[KillEventCluster] = []
    for observation in sorted(
        observations,
        key=lambda item: (item.event_time if item.event_time is not None else float("inf"), item.index),
    ):
        if observation.event_time is not None:
            _prune_timed_cluster_candidates(candidates, observation.event_time)
        cluster = _find_matching_kill_cluster(candidates, observation)
        if cluster is None:
            cluster = KillEventCluster(observations=[observation])
            clusters.append(cluster)
            candidates.append(cluster)
        else:
            cluster.observations.append(observation)
    return clusters


def _find_matching_kill_cluster(
    clusters: List[KillEventCluster],
    observation: KillObservation,
) -> Optional[KillEventCluster]:
    best_cluster = None
    best_score = None
    for cluster in clusters:
        if not _kill_cluster_can_accept(cluster, observation):
            continue
        score = _kill_cluster_match_score(cluster, observation)
        if best_score is None or score > best_score:
            best_score = score
            best_cluster = cluster
    return best_cluster


def _kill_cluster_can_accept(cluster: KillEventCluster, observation: KillObservation) -> bool:
    if any(existing.source_key == observation.source_key for existing in cluster.observations):
        return False
    reference = cluster.representative
    if not _times_compatible(reference, observation):
        return False
    if not _actors_compatible(cluster.killer, observation.killer):
        return False
    if not _actors_compatible(cluster.victim, observation.victim):
        return False
    return True


def _kill_cluster_match_score(cluster: KillEventCluster, observation: KillObservation) -> Tuple[int, float, int]:
    reference = cluster.representative
    shared_specific_names = 0
    if _same_specific_actor(cluster.killer, observation.killer):
        shared_specific_names += 1
    if _same_specific_actor(cluster.victim, observation.victim):
        shared_specific_names += 1
    if reference.event_time is None or observation.event_time is None:
        time_distance = 0.0 if reference.normalized_text == observation.normalized_text else MERGE_TIME_WINDOW_SECONDS
    else:
        time_distance = abs(reference.event_time - observation.event_time)
    specificity = _actor_specificity(cluster.killer) + _actor_specificity(cluster.victim)
    return shared_specific_names, -time_distance, specificity


def _merge_death_observations(observations: List[DeathObservation]) -> List[DeathEventCluster]:
    clusters: List[DeathEventCluster] = []
    candidates: List[DeathEventCluster] = []
    for observation in sorted(
        observations,
        key=lambda item: (item.event_time if item.event_time is not None else float("inf"), item.index),
    ):
        if observation.event_time is not None:
            _prune_timed_cluster_candidates(candidates, observation.event_time)
        cluster = _find_matching_death_cluster(candidates, observation)
        if cluster is None:
            cluster = DeathEventCluster(observations=[observation])
            clusters.append(cluster)
            candidates.append(cluster)
        else:
            cluster.observations.append(observation)
    return clusters


def _find_matching_death_cluster(
    clusters: List[DeathEventCluster],
    observation: DeathObservation,
) -> Optional[DeathEventCluster]:
    best_cluster = None
    best_score = None
    for cluster in clusters:
        if not _death_cluster_can_accept(cluster, observation):
            continue
        score = _death_cluster_match_score(cluster, observation)
        if best_score is None or score > best_score:
            best_score = score
            best_cluster = cluster
    return best_cluster


def _death_cluster_can_accept(cluster: DeathEventCluster, observation: DeathObservation) -> bool:
    if any(existing.source_key == observation.source_key and existing.kind == observation.kind for existing in cluster.observations):
        return False
    reference = cluster.representative
    if not _times_compatible(reference, observation):
        return False
    if not _actors_compatible(cluster.victim, observation.victim):
        return False
    return True


def _death_cluster_match_score(cluster: DeathEventCluster, observation: DeathObservation) -> Tuple[int, float, int]:
    reference = cluster.representative
    shared_specific_names = 1 if _same_specific_actor(cluster.victim, observation.victim) else 0
    if reference.event_time is None or observation.event_time is None:
        time_distance = 0.0 if reference.normalized_text == observation.normalized_text else MERGE_TIME_WINDOW_SECONDS
    else:
        time_distance = abs(reference.event_time - observation.event_time)
    has_kill = 1 if any(existing.kind == "kill" for existing in cluster.observations) or observation.kind == "kill" else 0
    return shared_specific_names, -time_distance, has_kill


def _observation_merge_signature_key(observation: DamageObservation) -> Tuple[int, Tuple[Tuple[int, object], ...]]:
    return int(observation.damage.total), observation.component_signature


def _cluster_latest_time(cluster: DamageEventCluster) -> Optional[float]:
    times = [
        float(observation.event_time)
        for observation in cluster.observations
        if observation.event_time is not None
    ]
    if not times:
        return None
    return max(times)


def _prune_timed_cluster_candidates(candidates: List[DamageEventCluster], event_time: float):
    minimum_time = float(event_time) - MERGE_TIME_WINDOW_SECONDS
    candidates[:] = [
        cluster
        for cluster in candidates
        if (_cluster_latest_time(cluster) is None or float(_cluster_latest_time(cluster)) >= minimum_time)
    ]


def _find_matching_cluster(clusters: List[DamageEventCluster], observation: DamageObservation) -> Optional[DamageEventCluster]:
    best_cluster = None
    best_score = None
    for cluster in clusters:
        if not _cluster_can_accept(cluster, observation):
            continue
        score = _cluster_match_score(cluster, observation)
        if best_score is None or score > best_score:
            best_score = score
            best_cluster = cluster
    return best_cluster


def _cluster_can_accept(cluster: DamageEventCluster, observation: DamageObservation) -> bool:
    if any(existing.source_key == observation.source_key for existing in cluster.observations):
        return False

    reference = cluster.representative
    if reference.damage.total != observation.damage.total:
        return False
    if reference.component_signature != observation.component_signature:
        return False
    if not _times_compatible(reference, observation):
        return False
    if not _actors_compatible(cluster.attacker, observation.attacker):
        return False
    if not _actors_compatible(cluster.defender, observation.defender):
        return False
    return True


def _cluster_match_score(cluster: DamageEventCluster, observation: DamageObservation) -> Tuple[int, float]:
    reference = cluster.representative
    shared_specific_names = 0
    if _same_specific_actor(cluster.attacker, observation.attacker):
        shared_specific_names += 1
    if _same_specific_actor(cluster.defender, observation.defender):
        shared_specific_names += 1

    if reference.event_time is None or observation.event_time is None:
        time_distance = 0.0 if reference.normalized_text == observation.normalized_text else MERGE_TIME_WINDOW_SECONDS
    else:
        time_distance = abs(reference.event_time - observation.event_time)
    specificity = _actor_specificity(cluster.attacker) + _actor_specificity(cluster.defender)
    return shared_specific_names, -time_distance, specificity


def _times_compatible(left: DamageObservation, right: DamageObservation) -> bool:
    if left.event_time is not None and right.event_time is not None:
        return abs(left.event_time - right.event_time) <= MERGE_TIME_WINDOW_SECONDS
    return left.normalized_text == right.normalized_text


def _actors_compatible(left: str, right: str) -> bool:
    if _is_ambiguous_actor(left) or _is_ambiguous_actor(right):
        return True
    return _actor_key(left) == _actor_key(right)


def _same_specific_actor(left: str, right: str) -> bool:
    if _is_ambiguous_actor(left) or _is_ambiguous_actor(right):
        return False
    return _actor_key(left) == _actor_key(right)


def _choose_cluster_actor(names: Iterable[str]) -> str:
    specific = []
    seen = set()
    for name in names:
        if _is_ambiguous_actor(name):
            continue
        key = _actor_key(name)
        if not key or key in seen:
            continue
        seen.add(key)
        specific.append(str(name or "").strip())
    if len(specific) == 1:
        return specific[0]
    if len(specific) > 1:
        return specific[0]
    return "someone"


def _actor_specificity(name: str) -> int:
    return 0 if _is_ambiguous_actor(name) else 1


def _is_ambiguous_actor(name: str) -> bool:
    return _actor_key(name) == "someone"


def _actor_key(name: str) -> str:
    return hgx_combat.normalize_actor_name(name).casefold()


def _component_signature(damage) -> Tuple[Tuple[int, object], ...]:
    signature = []
    for component in damage.components:
        damage_type = component.damage_type
        if damage_type is None:
            damage_type = str(component.type_name or "").casefold()
        signature.append((int(component.amount or 0), damage_type))
    return tuple(signature)


def _event_time(text: str, captured_at: float = 0.0) -> Optional[float]:
    timestamp = _chat_timestamp_seconds(text)
    if timestamp is not None:
        return timestamp
    captured = float(captured_at or 0.0)
    if captured > 0.0:
        return captured
    return None


def _chat_timestamp_seconds(text: str) -> Optional[float]:
    match = CHAT_TIMESTAMP_RE.search(str(text or ""))
    if match is None:
        return None
    parts = str(match.group("stamp") or "").split()
    try:
        month = MONTH_NUMBER[parts[1].casefold()]
        day = int(parts[2])
        hours, minutes, seconds = (int(part) for part in parts[3].split(":"))
    except (IndexError, KeyError, ValueError):
        return None
    return float((((month * 32) + day) * 86400) + (hours * 3600) + (minutes * 60) + seconds)


def _source_key(pid: int, client_name: str) -> str:
    if int(pid or 0):
        return f"pid:{int(pid)}"
    name = str(client_name or "").strip()
    if name:
        return f"client:{name.casefold()}"
    return "input"


def _damage_type_label(component) -> str:
    damage_type = component.damage_type
    if isinstance(damage_type, int) and damage_type in _DAMAGE_TYPE_LABEL_BY_ID:
        return _DAMAGE_TYPE_LABEL_BY_ID[damage_type]
    return str(component.type_name or "Unknown").strip() or "Unknown"


def _get_actor_stats(summary: DamageMeterSummary, name: str) -> DamageMeterActorStats:
    key = str(name or "").strip()
    actor = summary.actors.get(key)
    if actor is None:
        actor = DamageMeterActorStats(name=key)
        summary.actors[key] = actor
    return actor


def _add_count(target: Dict[str, int], key: str, value: int):
    if value == 0:
        return
    target[key] = int(target.get(key, 0)) + int(value)


def _merge_counts(target: Dict[str, int], source: Dict[str, int]):
    for key, value in source.items():
        _add_count(target, key, value)


def _format_counts(values: Dict[str, int], limit: int = 14) -> str:
    if not values:
        return "-"
    parts = [
        f"{key} {value:,}"
        for key, value in sorted(values.items(), key=lambda item: (-item[1], item[0].casefold()))[:limit]
    ]
    if len(values) > limit:
        parts.append(f"+{len(values) - limit} more")
    return ", ".join(parts)


def _actor_fragments(actors: List[DamageMeterActorStats], attr: str, limit: int) -> List[str]:
    fragments = []
    for actor in actors:
        value = int(getattr(actor, attr))
        if value == 0 and attr != "net":
            continue
        fragments.append(f"{_trim(actor.name, 18)} {value:,}")
        if len(fragments) >= limit:
            break
    return fragments


def _limited_line(label: str, total: int, fragments: List[str], suffix: str = "") -> str:
    base = f"{label}: {total:,}"
    if suffix:
        base = f"{base} ({suffix})"
    if fragments:
        base = f"{base}; " + ", ".join(fragments)
    return _trim(base, MAX_CHAT_LINE_LENGTH)


def _limited_counts_line(label: str, values: Dict[str, int]) -> str:
    return _trim(f"{label}: {_format_counts(values, limit=10)}", MAX_CHAT_LINE_LENGTH)


def _trim(text: str, limit: int) -> str:
    value = str(text or "")
    if len(value) <= limit:
        return value
    if limit <= 3:
        return value[:limit]
    return value[: limit - 3].rstrip() + "..."
