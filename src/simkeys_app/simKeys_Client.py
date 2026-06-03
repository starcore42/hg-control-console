# simKeys_Client.py — sidecar client for \\.\pipe\simkeys_<pid>
# Commands:
#   query        -> print installation state plus a full diagnostic snapshot from inside NWN
#   snapshot     -> print only the full diagnostic snapshot
#   slot N       -> trigger quickbar slot 1..12 (mapped to VK_F1..VK_F12) on the game window thread
#   slot-page P N -> trigger quickbar page P slot N directly (page 0=base, 1=shift, 2=ctrl)
#   vk N         -> trigger an arbitrary virtual key on the same internal path
#   replay       -> replay the last successfully dispatched vk
#   setlog N     -> 0=errors, 1=info, 2=debug
#   chat-send T  -> send chat text through the in-game chat path (default mode 2)
#   chat-poll    -> fetch captured chat/log lines from the hook ring buffer
#   overlay-text -> render a small text overlay inside the game frame
#   move-to-location X Y Z -> call the in-game move-to-position function directly
#   set-action-mode M -> call the in-game SetMode/action-mode path directly

import argparse, errno, os, socket, struct, time
import ctypes as C

IS_WINDOWS = os.name == "nt"
if IS_WINDOWS:
    import ctypes.wintypes as W
    k32 = C.WinDLL("kernel32", use_last_error=True)
else:
    W = None
    k32 = None
INVALID_HANDLE_VALUE = C.c_void_p(-1).value
CHAR_NAME_CAPACITY = 128
NWN_TEXT_ENCODING = "cp1252"
ERROR_SUCCESS = 0
ERROR_FILE_NOT_FOUND = 2
ERROR_PATH_NOT_FOUND = 3
ERROR_ACCESS_DENIED = 5
ERROR_BROKEN_PIPE = 109
ERROR_SEM_TIMEOUT = 121
ERROR_PIPE_BUSY = 231
ERROR_PIPE_NOT_CONNECTED = 233
RETRYABLE_PIPE_OPEN_ERRORS = {
    ERROR_FILE_NOT_FOUND,
    ERROR_PATH_NOT_FOUND,
    ERROR_ACCESS_DENIED,
    ERROR_BROKEN_PIPE,
    ERROR_SEM_TIMEOUT,
    ERROR_PIPE_BUSY,
    ERROR_PIPE_NOT_CONNECTED,
}

def winerr(prefix, err=None):
    if err is None:
        err = C.get_last_error() if IS_WINDOWS else C.get_errno()
    if IS_WINDOWS and err:
        message = C.FormatError(err).strip()
    elif err:
        message = os.strerror(err)
    else:
        message = "no last-error information"
    return f"{prefix} (err={err}: {message})"

def default_linux_socket_dir():
    explicit = os.environ.get("SIMKEYS_LINUX_SOCKET_DIR")
    if explicit:
        return explicit
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        return os.path.join(runtime_dir, "hgcc")
    uid = os.getuid() if hasattr(os, "getuid") else os.getpid()
    return f"/tmp/hgcc-{uid}"

def linux_socket_path(pid):
    return os.path.join(default_linux_socket_dir(), f"simkeys_{int(pid)}.sock")

class WindowsPipe:
    def __init__(self, pid, timeout_ms=2000):
        self.path = r"\\.\pipe\simkeys_%d" % pid
        self.C, self.W = C, W
        self.CreateFileW = k32.CreateFileW
        self.WaitNamedPipeW = k32.WaitNamedPipeW
        self.CloseHandle = k32.CloseHandle
        self.ReadFile = k32.ReadFile
        self.WriteFile = k32.WriteFile
        self.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, W.LPVOID, W.DWORD, W.DWORD, W.HANDLE]
        self.CreateFileW.restype = W.HANDLE
        self.WaitNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD]
        self.WaitNamedPipeW.restype = W.BOOL
        self.CloseHandle.argtypes = [W.HANDLE]
        self.CloseHandle.restype = W.BOOL
        self.ReadFile.argtypes  = [W.HANDLE, W.LPVOID, W.DWORD, W.LPVOID, W.LPVOID]
        self.ReadFile.restype = W.BOOL
        self.WriteFile.argtypes = [W.HANDLE, W.LPCVOID, W.DWORD, W.LPVOID, W.LPVOID]
        self.WriteFile.restype = W.BOOL

        self.h = self._open_with_retry(timeout_ms)

    def _open_with_retry(self, timeout_ms):
        timeout_ms = max(int(timeout_ms), 0)
        deadline = time.monotonic() + (timeout_ms / 1000.0)
        last_error = ERROR_SUCCESS

        while True:
            handle = self.CreateFileW(self.path, 0xC0000000, 0, None, 3, 0, None)  # GENERIC_READ|WRITE
            if handle not in (None, 0, INVALID_HANDLE_VALUE):
                return handle

            last_error = C.get_last_error()
            if last_error not in RETRYABLE_PIPE_OPEN_ERRORS:
                break

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break

            if last_error == ERROR_PIPE_BUSY:
                wait_ms = max(1, min(int(remaining * 1000), 250))
                if self.WaitNamedPipeW(self.path, wait_ms):
                    continue

                wait_error = C.get_last_error()
                if wait_error not in (ERROR_SUCCESS, *RETRYABLE_PIPE_OPEN_ERRORS):
                    last_error = wait_error
                    break

            time.sleep(min(0.025, max(deadline - time.monotonic(), 0.0)))

        raise OSError(winerr(f"Could not open pipe after {timeout_ms} ms: {self.path}", last_error))

    def _write(self, b):
        n = self.W.DWORD()
        if not self.WriteFile(self.h, b, len(b), self.C.byref(n), None):
            raise OSError(winerr(f"WriteFile failed for {self.path}"))

    def _read(self, nbytes):
        chunks = bytearray()
        while len(chunks) < nbytes:
            want = nbytes - len(chunks)
            buf = (self.C.c_char * want)()
            n = self.W.DWORD()
            if not self.ReadFile(self.h, buf, want, self.C.byref(n), None) or n.value == 0:
                raise OSError(winerr(f"ReadFile failed for {self.path}"))
            chunks.extend(bytes(buf[:n.value]))
        return bytes(chunks)

    def xfer(self, opcode, payload=b""):
        hdr = struct.pack("II", opcode, len(payload))
        self._write(hdr + payload)
        op, sz = struct.unpack("II", self._read(8))
        data = self._read(sz) if sz else b""
        return op, data

    def close(self):
        if getattr(self, "h", None) not in (None, 0, INVALID_HANDLE_VALUE):
            self.CloseHandle(self.h)
            self.h = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

class UnixSocketPipe:
    def __init__(self, pid, timeout_ms=2000, path=None):
        self.path = path or linux_socket_path(pid)
        self.sock = self._open_with_retry(timeout_ms)

    def _open_with_retry(self, timeout_ms):
        timeout_ms = max(int(timeout_ms), 0)
        deadline = time.monotonic() + (timeout_ms / 1000.0)
        retryable = {
            errno.ENOENT,
            errno.ECONNREFUSED,
            errno.EACCES,
            errno.EAGAIN,
            errno.EWOULDBLOCK,
        }
        last_error = 0
        while True:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(max(timeout_ms / 1000.0, 0.001) if timeout_ms else None)
            try:
                sock.connect(self.path)
                sock.settimeout(None)
                return sock
            except OSError as exc:
                last_error = exc.errno or 0
                sock.close()
                if last_error not in retryable or time.monotonic() >= deadline:
                    break
                time.sleep(min(0.025, max(deadline - time.monotonic(), 0.0)))
        raise OSError(winerr(f"Could not open HGCC socket after {timeout_ms} ms: {self.path}", last_error))

    def _write(self, data):
        self.sock.sendall(data)

    def _read(self, nbytes):
        chunks = bytearray()
        while len(chunks) < nbytes:
            chunk = self.sock.recv(nbytes - len(chunks))
            if not chunk:
                raise OSError(f"Socket closed while reading {self.path}")
            chunks.extend(chunk)
        return bytes(chunks)

    def xfer(self, opcode, payload=b""):
        hdr = struct.pack("II", opcode, len(payload))
        self._write(hdr + payload)
        op, sz = struct.unpack("II", self._read(8))
        data = self._read(sz) if sz else b""
        return op, data

    def close(self):
        sock = getattr(self, "sock", None)
        if sock is not None:
            self.sock = None
            sock.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

Pipe = WindowsPipe if IS_WINDOWS else UnixSocketPipe

OP_QUERY=3000; OP_SLOT=3001; OP_VK=3002; OP_SETLOG=3003; OP_REPLAY=3004; OP_SNAPSHOT=3005; OP_CHAT_SEND=3006; OP_CHAT_POLL=3007; OP_SLOT_PAGE=3008; OP_OVERLAY_TEXT=3009; OP_OVERLAY_CLEAR=3010; OP_OVERLAY_CLEAR_ALL=3011; OP_MOVE_TO_LOCATION=3012; OP_SET_WALK_BYPASS=3013; OP_SET_ACTION_MODE=3014; OP_MAP_PIN=3015; OP_QUICKBAR_WEAPONS=3016
QUERY_STRUCT_LEGACY = struct.Struct("<" + ("I" * 24) + ("i" * 10) + "I" + ("i" * 2) + ("I" * 4) + f"{CHAR_NAME_CAPACITY}s")
QUERY_STRUCT = struct.Struct("<" + ("I" * 24) + ("i" * 10) + "I" + ("i" * 2) + ("I" * 4) + "ifff" + f"{CHAR_NAME_CAPACITY}s")
QUERY_STRUCT_WITH_CREATURE = struct.Struct("<" + ("I" * 24) + ("i" * 10) + ("I" * 2) + ("i" * 2) + ("I" * 4) + "ifff" + f"{CHAR_NAME_CAPACITY}s")
QUERY_STRUCT_WITH_HEALTH = struct.Struct(QUERY_STRUCT_WITH_CREATURE.format + "iiIIi")
TRIGGER_RESPONSE = struct.Struct("<iiiiii")
OVERLAY_TEXT_HEADER = struct.Struct("<iiiiiIi")
OVERLAY_RESPONSE = struct.Struct("<iiii")
MOVE_TO_LOCATION_REQUEST = struct.Struct("<fffiIi")
MOVE_TO_LOCATION_RESPONSE = struct.Struct("<iiifff")
WALK_BYPASS_RESPONSE = struct.Struct("<iii")
SET_ACTION_MODE_REQUEST = struct.Struct("<ii")
SET_ACTION_MODE_RESPONSE = struct.Struct("<iiiiii")
MAP_PIN_REQUEST_HEADER = struct.Struct("<ffi")
MAP_PIN_RESPONSE = struct.Struct("<iii")
QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT = 16
QUICKBAR_WEAPON_MAX_DAMAGE_ROWS = 16
QUICKBAR_WEAPON_RESPONSE_HEADER = struct.Struct("<iii")
QUICKBAR_WEAPON_PROPERTY_ROW = struct.Struct("<iiiiiiii")
QUICKBAR_WEAPON_ENTRY = struct.Struct(
    "<iiiiIIIIIiiiiiiiiiII"
    + ("i" * QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT)
    + ("i" * QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT)
    + "i"
    + ("iiiiiiii" * QUICKBAR_WEAPON_MAX_DAMAGE_ROWS)
)
OVERLAY_POSITIONS = {
    "ABSOLUTE": 0,
    "A": 0,
    "TOPLEFT": 1,
    "TOP_LEFT": 1,
    "TL": 1,
    "TOP": 2,
    "T": 2,
    "TOPRIGHT": 3,
    "TOP_RIGHT": 3,
    "TR": 3,
    "CENTERLEFT": 4,
    "CENTER_LEFT": 4,
    "CL": 4,
    "CENTER": 5,
    "C": 5,
    "CENTERRIGHT": 6,
    "CENTER_RIGHT": 6,
    "CR": 6,
    "BOTTOMLEFT": 7,
    "BOTTOM_LEFT": 7,
    "BL": 7,
    "BOTTOM": 8,
    "B": 8,
    "BOTTOMRIGHT": 9,
    "BOTTOM_RIGHT": 9,
    "BR": 9,
}

def phex(x): return f"0x{x:08X}"
def as_int(x): 
    s = str(x).lower()
    return int(s, 16) if s.startswith("0x") else int(s)

def overlay_position_value(value):
    if isinstance(value, int):
        if 0 <= value <= 9:
            return value
        raise ValueError("overlay position must be between 0 and 9")
    key = str(value or "").strip().upper().replace("-", "_").replace(" ", "_")
    compact = key.replace("_", "")
    if key in OVERLAY_POSITIONS:
        return OVERLAY_POSITIONS[key]
    if compact in OVERLAY_POSITIONS:
        return OVERLAY_POSITIONS[compact]
    raise ValueError(f"unknown overlay position: {value}")

def decode_nwn_text(data):
    raw = bytes(data or b"")
    if not raw:
        return ""
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode(NWN_TEXT_ENCODING, errors="replace")

def encode_nwn_text(text):
    return str(text or "").encode(NWN_TEXT_ENCODING, errors="replace")

def decode_cstring(b):
    return decode_nwn_text(b.split(b"\x00", 1)[0])

def quickbar_bit(page, slot):
    return int(page) * 12 + (int(slot) - 1)

def quickbar_mask_has(mask, page, slot):
    bit = quickbar_bit(page, slot)
    return bit >= 0 and (int(mask) & (1 << bit)) != 0

def quickbar_mask_slots(mask):
    slots = []
    for page in range(3):
        for slot in range(1, 13):
            if quickbar_mask_has(mask, page, slot):
                slots.append((page, slot))
    return slots

def format_quickbar_slots(mask):
    labels = {0: "Base", 1: "Shift", 2: "Ctrl"}
    return ", ".join(f"{labels.get(page, page)}-{slot}" for page, slot in quickbar_mask_slots(mask))

def query_state(p):
    _, data = p.xfer(OP_QUERY)
    health = (0, 0, 0, 0, 0)
    health_available = False
    if len(data) == QUERY_STRUCT_WITH_HEALTH.size:
        unpacked = QUERY_STRUCT_WITH_HEALTH.unpack(data)
        health = unpacked[-5:]
        unpacked = unpacked[:-5]
        health_available = True
    elif len(data) == QUERY_STRUCT_WITH_CREATURE.size:
        unpacked = QUERY_STRUCT_WITH_CREATURE.unpack(data)
    elif len(data) == QUERY_STRUCT.size:
        unpacked = QUERY_STRUCT.unpack(data)
        unpacked = unpacked[:35] + (0,) + unpacked[35:]
    elif len(data) == QUERY_STRUCT_LEGACY.size:
        legacy = QUERY_STRUCT_LEGACY.unpack(data)
        unpacked = legacy[:35] + (0,) + legacy[35:-1] + (0, 0.0, 0.0, 0.0, legacy[-1])
    else:
        raise RuntimeError(
            f"unexpected query payload size: got {len(data)}, expected {QUERY_STRUCT_WITH_HEALTH.size}"
        )
    (module_base, hook_proc, hwnd, current_proc, original_proc, main_tid, installed,
     expected_wndproc, expected_pre_dispatch, expected_dispatch_thunk, expected_dispatch_slot0,
     app_global_slot, app_holder, app_object, app_inner, dispatcher_ptr, gate90, gate94, gate98,
     quickbar_exec, quickbar_slot_dispatch, quickbar_panel_vtable, quickbar_slot_ptr, quickbar_this,
     quickbar_page, quickbar_slot, quickbar_slot_type, quickbar_calls, quickbar_scan_attempts, quickbar_scan_hits,
     last_vk, last_rc, last_error, log_level, player_object, player_creature, identity_refresh_count, identity_error,
     quickbar_item_mask_low, quickbar_item_mask_high, quickbar_equipped_mask_low, quickbar_equipped_mask_high,
     position_valid, position_x, position_y, position_z,
     character_name_raw) = unpacked
    (player_current_hp, player_max_hp, player_current_hp_address, player_max_hp_address, player_hp_error) = health
    quickbar_item_mask = (int(quickbar_item_mask_high) << 32) | int(quickbar_item_mask_low)
    quickbar_equipped_mask = (int(quickbar_equipped_mask_high) << 32) | int(quickbar_equipped_mask_low)
    return {
        "module_base": module_base,
        "hook_proc": hook_proc,
        "hwnd": hwnd,
        "current_proc": current_proc,
        "original_proc": original_proc,
        "main_tid": main_tid,
        "installed": installed,
        "expected_wndproc": expected_wndproc,
        "expected_pre_dispatch": expected_pre_dispatch,
        "expected_dispatch_thunk": expected_dispatch_thunk,
        "expected_dispatch_slot0": expected_dispatch_slot0,
        "app_global_slot": app_global_slot,
        "app_holder": app_holder,
        "app_object": app_object,
        "app_inner": app_inner,
        "dispatcher_ptr": dispatcher_ptr,
        "gate90": gate90,
        "gate94": gate94,
        "gate98": gate98,
        "quickbar_exec": quickbar_exec,
        "quickbar_slot_dispatch": quickbar_slot_dispatch,
        "quickbar_panel_vtable": quickbar_panel_vtable,
        "quickbar_slot_ptr": quickbar_slot_ptr,
        "quickbar_this": quickbar_this,
        "player_object": player_object,
        "player_creature": player_creature,
        "quickbar_page": quickbar_page,
        "quickbar_slot": quickbar_slot,
        "quickbar_slot_type": quickbar_slot_type,
        "quickbar_calls": quickbar_calls,
        "quickbar_scan_attempts": quickbar_scan_attempts,
        "quickbar_scan_hits": quickbar_scan_hits,
        "quickbar_item_mask": quickbar_item_mask,
        "quickbar_item_mask_low": quickbar_item_mask_low,
        "quickbar_item_mask_high": quickbar_item_mask_high,
        "quickbar_equipped_mask": quickbar_equipped_mask,
        "quickbar_equipped_mask_low": quickbar_equipped_mask_low,
        "quickbar_equipped_mask_high": quickbar_equipped_mask_high,
        "quickbar_equipped_slots": quickbar_mask_slots(quickbar_equipped_mask),
        "last_vk": last_vk,
        "last_rc": last_rc,
        "last_error": last_error,
        "log_level": log_level,
        "identity_refresh_count": identity_refresh_count,
        "identity_error": identity_error,
        "position_valid": bool(position_valid),
        "position_x": float(position_x),
        "position_y": float(position_y),
        "position_z": float(position_z),
        "position": (float(position_x), float(position_y), float(position_z)) if position_valid else None,
        "player_current_hp": int(player_current_hp),
        "player_max_hp": int(player_max_hp),
        "player_current_hp_address": player_current_hp_address,
        "player_max_hp_address": player_max_hp_address,
        "player_hp_error": int(player_hp_error),
        "player_hp_available": health_available,
        "character_name": decode_cstring(character_name_raw),
    }

def _quickbar_weapon_detail_payload(detail_slots=None):
    if not detail_slots:
        return b""
    low = 0
    high = 0
    for page, slot in detail_slots:
        bit_index = int(page) * 12 + (int(slot) - 1)
        if bit_index < 0 or bit_index >= 36:
            continue
        if bit_index < 32:
            low |= 1 << bit_index
        else:
            high |= 1 << (bit_index - 32)
    return struct.pack("II", low, high)


def quickbar_weapon_info(p, detail_slots=None):
    _, data = p.xfer(OP_QUICKBAR_WEAPONS, _quickbar_weapon_detail_payload(detail_slots))
    if len(data) == TRIGGER_RESPONSE.size:
        success, vk, rc, aux_rc, err, path = TRIGGER_RESPONSE.unpack(data)
        return {
            "success": False,
            "supported": False,
            "count": 0,
            "last_error": int(err),
            "trigger_response": {
                "success": success,
                "vk": vk,
                "rc": rc,
                "aux_rc": aux_rc,
                "err": err,
                "path": path,
            },
            "entries": [],
            "entries_by_slot": {},
        }
    if len(data) < QUICKBAR_WEAPON_RESPONSE_HEADER.size:
        raise RuntimeError(
            f"unexpected quickbar-weapons payload size: got {len(data)}, expected at least {QUICKBAR_WEAPON_RESPONSE_HEADER.size}"
        )

    success, count, last_error = QUICKBAR_WEAPON_RESPONSE_HEADER.unpack_from(data, 0)
    available = (len(data) - QUICKBAR_WEAPON_RESPONSE_HEADER.size) // QUICKBAR_WEAPON_ENTRY.size
    count = max(0, min(int(count), available))
    entries = []
    offset = QUICKBAR_WEAPON_RESPONSE_HEADER.size
    for _ in range(count):
        values = QUICKBAR_WEAPON_ENTRY.unpack_from(data, offset)
        offset += QUICKBAR_WEAPON_ENTRY.size
        (
            page,
            slot,
            bit_index,
            slot_type,
            slot_ptr,
            primary_item_id,
            secondary_item_id,
            primary_item_ptr,
            secondary_item_ptr,
            primary_equipped,
            secondary_equipped,
            primary_active_property_count,
            primary_passive_property_count,
            secondary_active_property_count,
            secondary_passive_property_count,
            primary_detail_requested,
            secondary_detail_requested,
            damage_row_count,
            primary_damage_mask,
            secondary_damage_mask,
        ) = values[:20]
        primary_damage_amounts = tuple(values[20:20 + QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT])
        secondary_damage_amounts = tuple(values[20 + QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT:20 + QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT * 2])
        error_index = 20 + QUICKBAR_WEAPON_DAMAGE_TYPE_COUNT * 2
        error = int(values[error_index])
        rows_flat = values[error_index + 1:]
        stored_rows = min(max(int(damage_row_count), 0), QUICKBAR_WEAPON_MAX_DAMAGE_ROWS)
        damage_rows = []
        for row_index in range(stored_rows):
            row_offset = row_index * 8
            (
                hand,
                list_kind,
                property_name,
                subtype,
                cost_table,
                cost_value,
                param1,
                param1_value,
            ) = rows_flat[row_offset:row_offset + 8]
            damage_rows.append({
                "hand": int(hand),
                "list": int(list_kind),
                "property_name": int(property_name),
                "subtype": int(subtype),
                "cost_table": int(cost_table),
                "cost_value": int(cost_value),
                "param1": int(param1),
                "param1_value": int(param1_value),
            })
        entries.append({
            "page": int(page),
            "slot": int(slot),
            "bit_index": int(bit_index),
            "slot_type": int(slot_type),
            "slot_ptr": int(slot_ptr),
            "primary_item_id": int(primary_item_id),
            "secondary_item_id": int(secondary_item_id),
            "primary_item_ptr": int(primary_item_ptr),
            "secondary_item_ptr": int(secondary_item_ptr),
            "primary_equipped": int(primary_equipped),
            "secondary_equipped": int(secondary_equipped),
            "primary_active_property_count": int(primary_active_property_count),
            "primary_passive_property_count": int(primary_passive_property_count),
            "secondary_active_property_count": int(secondary_active_property_count),
            "secondary_passive_property_count": int(secondary_passive_property_count),
            "primary_detail_requested": int(primary_detail_requested),
            "secondary_detail_requested": int(secondary_detail_requested),
            "damage_row_count": int(damage_row_count),
            "primary_damage_mask": int(primary_damage_mask),
            "secondary_damage_mask": int(secondary_damage_mask),
            "primary_damage_amounts": primary_damage_amounts,
            "secondary_damage_amounts": secondary_damage_amounts,
            "error": error,
            "damage_rows": damage_rows,
            "damage_rows_truncated": int(damage_row_count) > len(damage_rows),
        })

    return {
        "success": bool(success),
        "supported": True,
        "count": count,
        "last_error": int(last_error),
        "entries": entries,
        "entries_by_slot": {(entry["page"], entry["slot"]): entry for entry in entries},
    }

ITEMPROP_DAMAGE_TYPE_LABELS = {
    0: "Bludgeoning",
    1: "Piercing",
    2: "Slashing",
    3: "Subdual",
    4: "Physical",
    5: "Magical",
    6: "Acid",
    7: "Cold",
    8: "Divine",
    9: "Electrical",
    10: "Fire",
    11: "Negative",
    12: "Positive",
    13: "Sonic",
    14: "Ectoplasmic",
    15: "Psionic",
}

ITEMPROP_DAMAGE_COST_LABELS = {
    1: "1",
    2: "2",
    3: "3",
    4: "4",
    5: "5",
    6: "1d4",
    7: "1d6",
    8: "1d8",
    9: "1d10",
    10: "2d6",
    11: "2d8",
    12: "2d4",
    13: "2d10",
    14: "1d12",
    15: "2d12",
    16: "6",
    17: "7",
    18: "8",
    19: "9",
    20: "10",
    21: "11",
    22: "12",
    23: "13",
    24: "14",
    25: "15",
    26: "16",
    27: "17",
    28: "18",
    29: "19",
    30: "20",
    38: "1d12",
    39: "2d12",
    40: "3d12",
    41: "4d12",
    42: "5d12",
    43: "6d12",
    44: "7d12",
    45: "8d12",
    46: "9d12",
    47: "10d12",
    48: "1d20",
    49: "2d20",
    50: "3d20",
    51: "4d20",
    52: "5d20",
    53: "6d20",
    54: "7d20",
    55: "8d20",
    56: "9d20",
    57: "10d20",
    78: "3d12",
    79: "4d12",
    80: "5d12",
    81: "6d12",
    82: "7d12",
    83: "8d12",
    84: "9d12",
    85: "10d12",
    124: "11d12",
    125: "12d12",
    126: "13d12",
    127: "14d12",
    128: "15d12",
    129: "16d12",
    130: "17d12",
    131: "18d12",
    132: "19d12",
    133: "20d12",
}

ITEMPROP_DAMAGE_PROPERTY_LABELS = {
    16: "Damage Bonus",
    17: "Damage Bonus vs Alignment Group",
    18: "Damage Bonus vs Racial Group",
    19: "Damage Bonus vs Specific Alignment",
}


def _format_quickbar_weapon_prop_count(value):
    try:
        value = int(value)
    except (TypeError, ValueError):
        return "?"
    return "?" if value < 0 else str(value)


def _format_quickbar_weapon_damage_row(row):
    property_name = int(row.get("property_name") or 0)
    subtype = int(row.get("subtype") or 0)
    cost_value = int(row.get("cost_value") or 0)
    active = int(row.get("param1") or 0)
    param1_value = int(row.get("param1_value") or 0)
    property_label = ITEMPROP_DAMAGE_PROPERTY_LABELS.get(property_name, f"prop={property_name}")
    damage_type_value = subtype if property_name == 16 else param1_value
    damage_type = ITEMPROP_DAMAGE_TYPE_LABELS.get(damage_type_value, f"type={damage_type_value}")
    damage_amount = ITEMPROP_DAMAGE_COST_LABELS.get(cost_value, f"cost={cost_value}")
    active_text = f" active={active}" if active else ""
    return (
        f"{property_label} {damage_type} {damage_amount} "
        f"(prop={property_name} subtype={subtype} cost={cost_value} param={param1_value}{active_text})"
    )


def format_quickbar_weapon_rows(entry):
    rows = list((entry or {}).get("damage_rows") or [])
    if not rows:
        return "damage=<none>"
    parts = []
    for row in rows:
        hand = "main" if row.get("hand") == 1 else "off"
        list_value = row.get("list")
        if list_value == 1:
            list_kind = "A"
        elif list_value == 3 and int(row.get("param1") or 0):
            list_kind = "T"
        elif list_value == 3:
            list_kind = "D"
        else:
            list_kind = "P"
        parts.append(f"{hand}/{list_kind}:{_format_quickbar_weapon_damage_row(row)}")
    if (entry or {}).get("damage_rows_truncated"):
        parts.append("...")
    return "damage=[" + "; ".join(parts) + "]"

def format_quickbar_weapon_entry(entry):
    if not entry:
        return "no hook data"
    primary_active_props = _format_quickbar_weapon_prop_count(entry.get("primary_active_property_count", -1))
    primary_passive_props = _format_quickbar_weapon_prop_count(entry.get("primary_passive_property_count", -1))
    primary_detail_requested = " detailReq=1" if entry.get("primary_detail_requested", 0) else ""
    primary = (
        f"primary id={phex(entry.get('primary_item_id', 0))} ptr={phex(entry.get('primary_item_ptr', 0))} "
        f"equipped={entry.get('primary_equipped', 0)} props=A{primary_active_props}/P{primary_passive_props} "
        f"mask=0x{entry.get('primary_damage_mask', 0):04X}{primary_detail_requested}"
    )
    secondary = ""
    if entry.get("secondary_item_id", 0) and entry.get("secondary_item_id", 0) != 0x7F000000:
        secondary_active_props = _format_quickbar_weapon_prop_count(entry.get("secondary_active_property_count", -1))
        secondary_passive_props = _format_quickbar_weapon_prop_count(entry.get("secondary_passive_property_count", -1))
        secondary_detail_requested = " detailReq=1" if entry.get("secondary_detail_requested", 0) else ""
        secondary = (
            f"; secondary id={phex(entry.get('secondary_item_id', 0))} ptr={phex(entry.get('secondary_item_ptr', 0))} "
            f"equipped={entry.get('secondary_equipped', 0)} props=A{secondary_active_props}/P{secondary_passive_props} "
            f"mask=0x{entry.get('secondary_damage_mask', 0):04X}{secondary_detail_requested}"
        )
    error = f"; err={entry.get('error', 0)}" if entry.get("error", 0) else ""
    return (
        f"slotType={entry.get('slot_type', -1)} slotPtr={phex(entry.get('slot_ptr', 0))}; "
        f"{primary}{secondary}; {format_quickbar_weapon_rows(entry)}{error}"
    )

def cmd_query(p):
    result = query_state(p)
    print(f"moduleBase={phex(result['module_base'])} hwnd={phex(result['hwnd'])} mainTid={result['main_tid']} installed={result['installed']} logLevel={result['log_level']}")
    print(f"wndproc: current={phex(result['current_proc'])} hook={phex(result['hook_proc'])} original={phex(result['original_proc'])} expected_nwn={phex(result['expected_wndproc'])}")
    print(f"path: preDispatch={phex(result['expected_pre_dispatch'])} dispatcherThunk={phex(result['expected_dispatch_thunk'])} dispatcherSlot0={phex(result['expected_dispatch_slot0'])}")
    print(f"engine: appGlobalSlot={phex(result['app_global_slot'])} appHolder={phex(result['app_holder'])} appObject={phex(result['app_object'])} appInner={phex(result['app_inner'])} dispatcher={phex(result['dispatcher_ptr'])} gate90={phex(result['gate90'])} gate94={phex(result['gate94'])} gate98={phex(result['gate98'])}")
    equipped_slots = format_quickbar_slots(result["quickbar_equipped_mask"]) or "-"
    print(f"quickbar: exec={phex(result['quickbar_exec'])} slotDispatch={phex(result['quickbar_slot_dispatch'])} panelVtable={phex(result['quickbar_panel_vtable'])} capturedThis={phex(result['quickbar_this'])} page={result['quickbar_page']} slot={result['quickbar_slot']} slotPtr={phex(result['quickbar_slot_ptr'])} slotType={result['quickbar_slot_type']} calls={result['quickbar_calls']} scanAttempts={result['quickbar_scan_attempts']} scanHits={result['quickbar_scan_hits']} itemMask=0x{result['quickbar_item_mask']:09X} equippedMask=0x{result['quickbar_equipped_mask']:09X} equipped={equipped_slots}")
    position = result.get("position")
    position_text = f" pos=({position[0]:.3f}, {position[1]:.3f}, {position[2]:.3f})" if position else " pos=<unknown>"
    print(f"identity: player={phex(result['player_object'])} creature={phex(result['player_creature'])} name={result['character_name'] or '<unknown>'} refreshes={result['identity_refresh_count']} err={result['identity_error']}{position_text}")
    print(f"health: available={int(result['player_hp_available'])} current={result['player_current_hp']} max={result['player_max_hp']} currentAddr={phex(result['player_current_hp_address'])} maxAddr={phex(result['player_max_hp_address'])} err={result['player_hp_error']}")
    print(f"last: vk={phex(result['last_vk'])} rc={result['last_rc']} err={result['last_error']}")
    print()
    cmd_snapshot(p)

def cmd_snapshot(p):
    _, data = p.xfer(OP_SNAPSHOT)
    text = decode_nwn_text(data)
    print(text.rstrip())

def cmd_replay(p):
    _, data = p.xfer(OP_REPLAY)
    success, vk, rc, aux_rc, err, path = struct.unpack("iiiiii", data)
    print(f"replay: success={success} vk={phex(vk)} rc={rc} aux={aux_rc} path={path} err={err}")

def cmd_slot(p, slot):
    _, data = p.xfer(OP_SLOT, struct.pack("i", slot))
    success, vk, rc, aux_rc, err, path = struct.unpack("iiiiii", data)
    print(f"slot={slot} success={success} vk={phex(vk)} rc={rc} aux={aux_rc} path={path} err={err}")

def cmd_slot_page(p, page, slot):
    _, data = p.xfer(OP_SLOT_PAGE, struct.pack("ii", slot, page))
    success, vk, rc, aux_rc, err, path = struct.unpack("iiiiii", data)
    print(f"page={page} slot={slot} success={success} vk={phex(vk)} rc={rc} aux={aux_rc} path={path} err={err}")

def cmd_vk(p, vk):
    _, data = p.xfer(OP_VK, struct.pack("i", vk))
    success, out_vk, rc, aux_rc, err, path = struct.unpack("iiiiii", data)
    print(f"vk={phex(vk)} success={success} dispatched={phex(out_vk)} rc={rc} aux={aux_rc} path={path} err={err}")

def cmd_setlog(p, level):
    _, data = p.xfer(OP_SETLOG, struct.pack("i", level))
    (actual,) = struct.unpack("i", data)
    print("log level set to", actual)

def chat_send(p, text, mode=2):
    payload = encode_nwn_text(text)
    _, data = p.xfer(OP_CHAT_SEND, struct.pack("ii", mode, len(payload)) + payload)
    success, actual_mode, rc, err = struct.unpack("iiii", data)
    return {
        "success": success,
        "mode": actual_mode,
        "rc": rc,
        "err": err,
    }

def move_to_location(p, x, y, z, client_side=1, action_object_id=0x7F000000, bypass_no_walk=False):
    payload = MOVE_TO_LOCATION_REQUEST.pack(
        float(x),
        float(y),
        float(z),
        1 if client_side else 0,
        int(action_object_id) & 0xFFFFFFFF,
        1 if bypass_no_walk else 0,
    )
    _, data = p.xfer(OP_MOVE_TO_LOCATION, payload)
    success, rc, err, out_x, out_y, out_z = MOVE_TO_LOCATION_RESPONSE.unpack(data)
    return {
        "success": success,
        "rc": rc,
        "err": err,
        "x": out_x,
        "y": out_y,
        "z": out_z,
    }

def set_walk_bypass(p, enabled):
    _, data = p.xfer(OP_SET_WALK_BYPASS, struct.pack("i", 1 if enabled else 0))
    success, active, err = WALK_BYPASS_RESPONSE.unpack(data)
    return {
        "success": success,
        "enabled": bool(active),
        "err": err,
    }

def set_action_mode(p, mode, enabled=True):
    payload = SET_ACTION_MODE_REQUEST.pack(int(mode), 1 if enabled else 0)
    _, data = p.xfer(OP_SET_ACTION_MODE, payload)
    success, actual_mode, actual_enabled, active, rc, err = SET_ACTION_MODE_RESPONSE.unpack(data)
    return {
        "success": success,
        "mode": actual_mode,
        "enabled": bool(actual_enabled),
        "active": active,
        "rc": rc,
        "err": err,
    }

def set_combat_mode(p, mode, enabled=True):
    return set_action_mode(p, mode, enabled)

def add_map_pin(p, x, y, text):
    payload = encode_nwn_text(text)
    if len(payload) >= 512:
        payload = payload[:511]
    request = MAP_PIN_REQUEST_HEADER.pack(float(x), float(y), len(payload)) + payload
    _, data = p.xfer(OP_MAP_PIN, request)
    success, rc, err = MAP_PIN_RESPONSE.unpack(data)
    return {
        "success": success,
        "rc": rc,
        "err": err,
    }

def chat_poll(p, after=0, max_lines=20):
    _, data = p.xfer(OP_CHAT_POLL, struct.pack("ii", after, max_lines))
    if len(data) < 8:
        raise RuntimeError(f"unexpected chat-poll payload size: got {len(data)}, expected at least 8")

    latest_seq, count = struct.unpack_from("ii", data, 0)
    offset = 8
    lines = []
    for _ in range(count):
        if offset + 8 > len(data):
            raise RuntimeError("chat-poll payload ended before line header")
        seq, text_len = struct.unpack_from("ii", data, offset)
        offset += 8
        if text_len < 0 or offset + text_len > len(data):
            raise RuntimeError("chat-poll payload ended before line text")
        text = decode_nwn_text(data[offset:offset + text_len])
        offset += text_len
        lines.append({
            "seq": seq,
            "text": text,
        })
    return {
        "latest_seq": latest_seq,
        "lines": lines,
    }

def overlay_show_text(p, text, overlay_id=1000, position="TR", offset_x=0, offset_y=0, font_size=16, color=0xFFFFFF):
    payload = str(text or "").encode("utf-8", errors="replace")
    if len(payload) >= 4096:
        payload = payload[:4095]
    header = OVERLAY_TEXT_HEADER.pack(
        int(overlay_id),
        overlay_position_value(position),
        int(offset_x),
        int(offset_y),
        int(font_size),
        int(color) & 0xFFFFFF,
        len(payload),
    )
    _, data = p.xfer(OP_OVERLAY_TEXT, header + payload)
    success, width, height, err = OVERLAY_RESPONSE.unpack(data)
    return {
        "success": success,
        "width": width,
        "height": height,
        "err": err,
    }

def overlay_clear(p, overlay_id=1000):
    _, data = p.xfer(OP_OVERLAY_CLEAR, struct.pack("i", int(overlay_id)))
    success, width, height, err = OVERLAY_RESPONSE.unpack(data)
    return {
        "success": success,
        "width": width,
        "height": height,
        "err": err,
    }

def overlay_clear_all(p):
    _, data = p.xfer(OP_OVERLAY_CLEAR_ALL)
    success, width, height, err = OVERLAY_RESPONSE.unpack(data)
    return {
        "success": success,
        "width": width,
        "height": height,
        "err": err,
    }

def cmd_chat_send(p, text, mode):
    result = chat_send(p, text, mode)
    print(f"chat-send: success={result['success']} mode={result['mode']} rc={result['rc']} err={result['err']}")

def cmd_move_to_location(p, x, y, z, client_side, action_object_id, bypass_no_walk):
    result = move_to_location(
        p,
        x,
        y,
        z,
        client_side=client_side,
        action_object_id=as_int(action_object_id),
        bypass_no_walk=bypass_no_walk,
    )
    print(
        "move-to-location: "
        f"success={result['success']} rc={result['rc']} err={result['err']} "
        f"pos=({result['x']:.3f}, {result['y']:.3f}, {result['z']:.3f})"
    )

def cmd_set_walk_bypass(p, enabled):
    result = set_walk_bypass(p, bool(enabled))
    print(f"set-walk-bypass: success={result['success']} enabled={int(result['enabled'])} err={result['err']}")

def cmd_set_action_mode(p, mode, enabled):
    result = set_action_mode(p, int(mode), bool(enabled))
    print(
        "set-action-mode: "
        f"success={result['success']} mode={result['mode']} enabled={int(result['enabled'])} "
        f"active={result['active']} rc={result['rc']} err={result['err']}"
    )

def cmd_set_combat_mode(p, mode, enabled):
    cmd_set_action_mode(p, mode, enabled)

def cmd_map_pin(p, x, y, text):
    result = add_map_pin(p, x, y, text)
    print(f"map-pin: success={result['success']} rc={result['rc']} err={result['err']}")

def _parse_quickbar_detail_slot(value):
    text = str(value or "").strip()
    if not text:
        raise ValueError("empty slot")
    lowered = text.lower().replace(" ", "")
    if "/" in lowered:
        lowered = lowered.rsplit("/", 1)[-1]
    if lowered.startswith("ctrl+f"):
        return 2, int(lowered[6:])
    if lowered.startswith("shift+f"):
        return 1, int(lowered[7:])
    if lowered.startswith("f"):
        return 0, int(lowered[1:])
    if ":" in lowered:
        page, slot = lowered.split(":", 1)
        return int(page), int(slot)
    raise ValueError(f"expected page:slot, F#, Shift+F#, or Ctrl+F#: {value!r}")


def cmd_quickbar_weapons(p, detail_slots=None):
    parsed_detail_slots = [_parse_quickbar_detail_slot(slot) for slot in (detail_slots or [])]
    result = quickbar_weapon_info(p, parsed_detail_slots)
    print(
        "quickbar-weapons: "
        f"success={int(result['success'])} supported={int(result.get('supported', False))} "
        f"count={result.get('count', 0)} err={result.get('last_error', 0)}"
    )
    for entry in result.get("entries") or []:
        if entry.get("slot_type") != 1 and not entry.get("primary_item_id"):
            continue
        print(f"page={entry['page']} slot={entry['slot']}: {format_quickbar_weapon_entry(entry)}")

def cmd_chat_poll(p, after, max_lines):
    result = chat_poll(p, after, max_lines)
    print(f"chat-poll: latest_seq={result['latest_seq']} count={len(result['lines'])}")
    for line in result["lines"]:
        print(f"[{line['seq']}] {line['text']}")

def cmd_overlay_text(p, text, overlay_id, position, offset_x, offset_y, font_size, color):
    result = overlay_show_text(
        p,
        text,
        overlay_id=overlay_id,
        position=position,
        offset_x=offset_x,
        offset_y=offset_y,
        font_size=font_size,
        color=as_int(color),
    )
    print(f"overlay-text: success={result['success']} size={result['width']}x{result['height']} err={result['err']}")

def cmd_overlay_clear(p, overlay_id):
    result = overlay_clear(p, overlay_id)
    print(f"overlay-clear: success={result['success']} err={result['err']}")

def cmd_overlay_clear_all(p):
    result = overlay_clear_all(p)
    print(f"overlay-clear-all: success={result['success']} err={result['err']}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("query")
    sub.add_parser("snapshot")
    sub.add_parser("replay")
    s1 = sub.add_parser("slot"); s1.add_argument("slot", type=int, choices=range(1, 13))
    s2 = sub.add_parser("slot-page"); s2.add_argument("page", type=int, choices=[0, 1, 2]); s2.add_argument("slot", type=int, choices=range(1, 13))
    s3 = sub.add_parser("vk"); s3.add_argument("vk")
    s4 = sub.add_parser("setlog"); s4.add_argument("level", type=int, choices=[0,1,2])
    s5 = sub.add_parser("chat-send"); s5.add_argument("text"); s5.add_argument("--mode", type=int, default=2)
    s6 = sub.add_parser("chat-poll"); s6.add_argument("--after", type=int, default=0); s6.add_argument("--max", type=int, default=20)
    s7 = sub.add_parser("overlay-text"); s7.add_argument("text"); s7.add_argument("--id", type=int, default=1000); s7.add_argument("--position", default="TR"); s7.add_argument("--x", type=int, default=0); s7.add_argument("--y", type=int, default=0); s7.add_argument("--font", type=int, default=16); s7.add_argument("--color", default="0xFFFFFF")
    s8 = sub.add_parser("overlay-clear"); s8.add_argument("--id", type=int, default=1000)
    sub.add_parser("overlay-clear-all")
    s9 = sub.add_parser("move-to-location"); s9.add_argument("x", type=float); s9.add_argument("y", type=float); s9.add_argument("z", type=float); s9.add_argument("--client-side", type=int, choices=[0, 1], default=1); s9.add_argument("--action-object-id", default="0x7F000000"); s9.add_argument("--bypass-no-walk", action="store_true")
    s10 = sub.add_parser("set-walk-bypass"); s10.add_argument("enabled", type=int, choices=[0, 1])
    s11 = sub.add_parser("set-action-mode"); s11.add_argument("mode", type=int, choices=range(0, 13)); s11.add_argument("--off", action="store_true")
    s12 = sub.add_parser("set-combat-mode"); s12.add_argument("mode", type=int, choices=range(0, 13)); s12.add_argument("--off", action="store_true")
    s13 = sub.add_parser("map-pin"); s13.add_argument("x", type=float); s13.add_argument("y", type=float); s13.add_argument("text")
    quickbar_parser = sub.add_parser("quickbar-weapons")
    quickbar_parser.add_argument("--detail-slot", action="append", default=[])
    a = ap.parse_args()

    p = Pipe(a.pid)
    try:
        if a.cmd == "query":   cmd_query(p)
        elif a.cmd == "snapshot": cmd_snapshot(p)
        elif a.cmd == "replay": cmd_replay(p)
        elif a.cmd == "slot": cmd_slot(p, a.slot)
        elif a.cmd == "slot-page": cmd_slot_page(p, a.page, a.slot)
        elif a.cmd == "vk": cmd_vk(p, as_int(a.vk))
        elif a.cmd == "setlog": cmd_setlog(p, a.level)
        elif a.cmd == "chat-send": cmd_chat_send(p, a.text, a.mode)
        elif a.cmd == "chat-poll": cmd_chat_poll(p, a.after, a.max)
        elif a.cmd == "overlay-text": cmd_overlay_text(p, a.text, a.id, a.position, a.x, a.y, a.font, a.color)
        elif a.cmd == "overlay-clear": cmd_overlay_clear(p, a.id)
        elif a.cmd == "overlay-clear-all": cmd_overlay_clear_all(p)
        elif a.cmd == "move-to-location": cmd_move_to_location(p, a.x, a.y, a.z, a.client_side, a.action_object_id, a.bypass_no_walk)
        elif a.cmd == "set-walk-bypass": cmd_set_walk_bypass(p, a.enabled)
        elif a.cmd == "set-action-mode": cmd_set_action_mode(p, a.mode, not a.off)
        elif a.cmd == "set-combat-mode": cmd_set_combat_mode(p, a.mode, not a.off)
        elif a.cmd == "map-pin": cmd_map_pin(p, a.x, a.y, a.text)
        elif a.cmd == "quickbar-weapons": cmd_quickbar_weapons(p, a.detail_slot)
    finally:
        p.close()
