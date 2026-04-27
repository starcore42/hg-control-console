import os
import tempfile
import unittest

from src.simkeys_app import simkeys_damage_meter as meter
from src.simkeys_app import simkeys_hgx_data as hgx_data


class DamageMeterTests(unittest.TestCase):
    def make_db(self, directory):
        with open(os.path.join(directory, "test.xml"), "w", encoding="utf-8") as handle:
            handle.write(
                """<characters>
  <creature name="Acid Blob">
    <damageImmunities>
      <damage type="Acid" immunity="0" resistance="0" healing="6" />
      <damage type="Fire" immunity="0" resistance="0" />
      <damage type="Cold" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Training Dummy">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Advespa">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Swarm Master">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Superior Swarm Master">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Elite Swarm Master">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Raja">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
  <creature name="Ignored Spectator" type="Ignore">
    <damageImmunities>
      <damage type="Fire" immunity="0" resistance="0" />
    </damageImmunities>
  </creature>
</characters>
"""
            )
        return hgx_data.load_character_database(directory)

    def test_analyze_party_damage_and_enemy_healing(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    "Alice damages Acid Blob : 100 (10 acid 90 fire)",
                    "Bob damages Acid Blob : 30 (30 cold)",
                    "Bob damages Acid Blob : 9 (9 physical)",
                    "Acid Blob damages Alice : 12 (12 acid)",
                    "Alice damages Bob : 8 (8 fire)",
                    "Alice damages Ignored Spectator : 20 (20 fire)",
                ],
                character_db=db,
            )

        self.assertEqual(summary.raw_damage, 129)
        self.assertEqual(summary.raw_healing, 60)
        self.assertEqual(summary.net, 69)
        self.assertEqual(summary.counted_lines, 3)
        self.assertEqual(summary.damage_by_type, {"Fire": 90, "Cold": 30, "Physical": 9})
        self.assertEqual(summary.healing_by_type, {"Acid": 60})
        self.assertEqual(summary.actors["Alice"].raw_damage, 90)
        self.assertEqual(summary.actors["Alice"].raw_healing, 60)
        self.assertEqual(summary.actors["Alice"].net, 30)
        self.assertEqual(summary.actors["Bob"].net, 39)

    def test_recorder_session_logs_are_analyzed(self):
        class Event:
            sequence = 41
            raw_text = "Alice damages Acid Blob : 15 (5 acid 10 fire)"

        with tempfile.TemporaryDirectory() as tmpdir:
            db_dir = os.path.join(tmpdir, "chars")
            log_dir = os.path.join(tmpdir, "logs")
            os.makedirs(db_dir)
            db = self.make_db(db_dir)
            meter.reset_session_logs(log_dir)
            recorder = meter.DamageMeterRecorder(1234, log_dir)
            recorder.record_event(Event.sequence, Event.raw_text, "Alice")
            recorder.close()

            summary = meter.analyze_session_logs(log_dir, character_db=db)

        self.assertEqual(summary.lines_seen, 1)
        self.assertEqual(summary.raw_damage, 10)
        self.assertEqual(summary.raw_healing, 30)
        self.assertEqual(summary.net, -20)

    def test_reset_archives_previous_session_logs(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db_dir = os.path.join(tmpdir, "chars")
            log_dir = os.path.join(tmpdir, "logs", "damage-meter")
            archive_dir = os.path.join(tmpdir, "logs", "damage-meter-archives")
            os.makedirs(db_dir)
            db = self.make_db(db_dir)
            meter.reset_session_logs(log_dir)
            recorder = meter.DamageMeterRecorder(1234, log_dir)
            recorder.record_event(1, "Alice damages Acid Blob : 15 (5 acid 10 fire)", "Alice")
            recorder.close()

            meter.reset_session_logs(log_dir)

            archives = sorted(name for name in os.listdir(archive_dir) if name.endswith(".zip"))
            self.assertEqual(len(archives), 1)
            self.assertRegex(archives[0], r"^damage-meter_\d{8}_\d{6}\.zip$")
            self.assertFalse([name for name in os.listdir(log_dir) if name.startswith("chat_")])

            summary = meter.analyze_archived_session(os.path.join(archive_dir, archives[0]), character_db=db)

        self.assertEqual(summary.lines_seen, 1)
        self.assertEqual(summary.raw_damage, 10)
        self.assertEqual(summary.raw_healing, 30)
        self.assertEqual(summary.net, -20)

    def test_session_log_analysis_reports_progress(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db_dir = os.path.join(tmpdir, "chars")
            log_dir = os.path.join(tmpdir, "logs")
            os.makedirs(db_dir)
            db = self.make_db(db_dir)
            meter.reset_session_logs(log_dir)
            recorder = meter.DamageMeterRecorder(1234, log_dir)
            recorder.record_event(1, "Alice damages Acid Blob : 15 (5 acid 10 fire)", "Alice")
            recorder.close()

            events = []
            summary = meter.analyze_session_logs(log_dir, character_db=db, progress_callback=events.append)

        self.assertEqual(summary.counted_lines, 1)
        self.assertTrue(events)
        self.assertEqual(events[-1]["phase"], "Done")
        self.assertEqual(events[-1]["percent"], 100.0)
        phases = {event["phase"] for event in events}
        self.assertIn("Counting logs", phases)
        self.assertIn("Reading logs", phases)
        self.assertIn("Merging duplicate views", phases)
        self.assertIn("Classifying damage", phases)

    def test_enemy_counts_party_damage_breakdown_and_deaths(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    {"time": 100.0, "pid": 1, "text": "Alice [1.0] damages Acid Blob : 100 (10 acid 60 fire 30 cold)"},
                    {"time": 100.1, "pid": 1, "text": "Alice [1.0] killed Advespa"},
                    {"time": 101.0, "pid": 1, "text": "Bob [1.0] killed Superior Swarm Master"},
                    {"time": 101.1, "pid": 2, "text": "Bob [1.0] killed Superior Swarm Master"},
                    {"time": 102.0, "pid": 1, "text": "Bob [1.0] killed Elite Swarm Master"},
                    {"time": 103.0, "pid": 1, "text": "Raja killed Alice [1.0]"},
                    {"time": 103.1, "pid": 1, "text": "Alice [1.0] averts death : Possum's Farce : *success*"},
                    {"time": 104.0, "pid": 1, "text": "Raja killed Bob [1.0]"},
                ],
                character_db=db,
            )

        self.assertEqual(summary.enemy_kills_counted, 3)
        self.assertEqual(summary.merged_kill_observations, 1)
        self.assertEqual(summary.enemy_kills["advespa"].variants, {"Advespa": 1})
        swarm = summary.enemy_kills["swarm master"]
        self.assertEqual(swarm.total, 2)
        self.assertEqual(swarm.variants, {"Superior Swarm Master": 1, "Elite Swarm Master": 1})
        self.assertEqual(summary.actors["Alice [1.0]"].damage_by_type, {"Fire": 60, "Cold": 30})
        self.assertEqual(summary.actors["Alice [1.0]"].healing_by_type, {"Acid": 60})
        self.assertEqual(summary.deaths_counted, 2)
        self.assertEqual(summary.deaths["Alice [1.0]"].killed_by, {"Raja": 1})
        self.assertEqual(summary.deaths["Bob [1.0]"].killed_by, {"Raja": 1})

        text = meter.format_summary_text(summary)
        self.assertIn("Party Damage Breakdown", text)
        self.assertIn("Enemy Counts", text)
        self.assertIn("Swarm Master: 2", text)
        self.assertIn("Party Deaths", text)
        self.assertIn("Alice [1.0]:", text)

    def test_save_summary_text_uses_run_timestamp(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            log_dir = os.path.join(tmpdir, "logs", "damage-meter")
            report_dir = os.path.join(tmpdir, "logs", "damage-meter-reports")
            os.makedirs(log_dir)
            with open(os.path.join(log_dir, "session.json"), "w", encoding="utf-8") as handle:
                handle.write('{"started": 1777240838.0}\n')
            summary = meter.DamageMeterSummary(log_dir=log_dir)

            path = meter.save_summary_text(summary, "hello", output_dir=report_dir)

            self.assertTrue(os.path.isfile(path))
            self.assertRegex(os.path.basename(path), r"^damage-meter-report_\d{8}_\d{6}\.txt$")
            with open(path, "r", encoding="utf-8") as handle:
                self.assertEqual(handle.read(), "hello\n")

    def test_multi_client_duplicate_views_count_once(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    {
                        "pid": 1001,
                        "time": 100.0,
                        "text": "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] Alice damages Acid Blob : 100 (10 acid 90 fire)",
                    },
                    {
                        "pid": 1002,
                        "time": 100.1,
                        "text": "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] Alice damages Acid Blob : 100 (10 acid 90 fire)",
                    },
                ],
                character_db=db,
            )

        self.assertEqual(summary.damage_lines_seen, 2)
        self.assertEqual(summary.merged_observations, 1)
        self.assertEqual(summary.counted_lines, 1)
        self.assertEqual(summary.raw_damage, 90)
        self.assertEqual(summary.raw_healing, 60)
        self.assertEqual(summary.actors["Alice"].counted_lines, 1)

    def test_someone_view_is_resolved_from_another_client(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    {
                        "pid": 1001,
                        "time": 100.0,
                        "text": "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] Alice damages Acid Blob : 25 (25 fire)",
                    },
                    {
                        "pid": 1002,
                        "time": 100.2,
                        "text": "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] someone damages Acid Blob : 25 (25 fire)",
                    },
                ],
                character_db=db,
            )

        self.assertEqual(summary.counted_lines, 1)
        self.assertEqual(summary.merged_observations, 1)
        self.assertEqual(summary.ambiguous_observations, 1)
        self.assertEqual(summary.resolved_ambiguous_events, 1)
        self.assertEqual(summary.raw_damage, 25)
        self.assertIn("Alice", summary.actors)
        self.assertNotIn(meter.UNKNOWN_ACTOR_LABEL, summary.actors)

    def test_unresolved_someone_attacker_counts_unknown_against_known_enemy(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    {
                        "pid": 1002,
                        "time": 100.2,
                        "text": "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] someone damages Acid Blob : 25 (25 fire)",
                    },
                ],
                character_db=db,
            )

        self.assertEqual(summary.counted_lines, 1)
        self.assertEqual(summary.unresolved_ambiguous_events, 1)
        self.assertEqual(summary.raw_damage, 25)
        self.assertEqual(summary.actors[meter.UNKNOWN_ACTOR_LABEL].raw_damage, 25)

    def test_chat_report_lines(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            db = self.make_db(tmpdir)
            summary = meter.analyze_chat_records(
                [
                    "Alice damages Acid Blob : 100 (10 acid 90 fire)",
                    "Bob damages Training Dummy : 25 (25 fire)",
                ],
                character_db=db,
            )

        net_lines = meter.chat_report_lines(summary, "net")
        healing_lines = meter.chat_report_lines(summary, "healing")
        breakdown_lines = meter.chat_report_lines(summary, "breakdown")

        self.assertEqual(len(net_lines), 1)
        self.assertIn("Net damage: 55", net_lines[0])
        self.assertIn("Enemy healing: 60", healing_lines[0])
        self.assertTrue(any("Damage elements:" in line for line in breakdown_lines))
        self.assertTrue(all(len(line) <= meter.MAX_CHAT_LINE_LENGTH for line in net_lines + healing_lines + breakdown_lines))

    def test_default_character_data_sbikta_heals_on_cold(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        profile = db._resolve_combat_profile("Sbikta")
        cold_type = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]

        self.assertIsNotNone(profile)
        self.assertEqual(profile.healing[cold_type], 4)

        summary = meter.analyze_chat_records(
            ["Alice damages Sbikta : 10 (10 cold)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 0)
        self.assertEqual(summary.raw_healing, 40)
        self.assertEqual(summary.healing_by_type, {"Cold": 40})

    def test_default_character_data_beshi_bak_heals_on_electric(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        profile = db._resolve_combat_profile("Beshi'bak")
        electric_type = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]

        self.assertIsNotNone(profile)
        self.assertEqual(profile.healing[electric_type], 4)

        summary = meter.analyze_chat_records(
            ["Alice damages Beshi'bak : 10 (10 electrical)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 0)
        self.assertEqual(summary.raw_healing, 40)
        self.assertEqual(summary.healing_by_type, {"Electrical": 40})

    def test_default_character_data_dogruuk_heals_on_cold(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        profile = db._resolve_combat_profile("Dogruuk")
        cold_type = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]

        self.assertIsNotNone(profile)
        self.assertEqual(profile.healing[cold_type], 4)

        summary = meter.analyze_chat_records(
            ["Alice damages Dogruuk : 10 (10 cold)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 0)
        self.assertEqual(summary.raw_healing, 40)
        self.assertEqual(summary.healing_by_type, {"Cold": 40})

    def test_default_character_data_drakiz_niz_heals_on_cold(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        profile = db._resolve_combat_profile("Drakiz'niz")
        cold_type = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]

        self.assertIsNotNone(profile)
        self.assertEqual(profile.healing[cold_type], 4)

        summary = meter.analyze_chat_records(
            ["Alice damages Drakiz'niz : 10 (10 cold)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 0)
        self.assertEqual(summary.raw_healing, 40)
        self.assertEqual(summary.healing_by_type, {"Cold": 40})

    def test_default_character_data_omnimentals_do_not_heal(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        elemental_types = [
            hgx_data.DAMAGE_TYPE_NAME_TO_ID[name]
            for name in ("acid", "cold", "electrical", "fire", "sonic")
        ]

        for creature_name in ("Omnimental", "Greater Omnimental", "Superior Omnimental"):
            with self.subTest(creature_name=creature_name):
                profile = db._resolve_combat_profile(creature_name)
                self.assertIsNotNone(profile)
                self.assertTrue(all(profile.healing[damage_type] == 0 for damage_type in elemental_types))

        summary = meter.analyze_chat_records(
            ["Alice damages Superior Omnimental : 50 (10 acid 10 cold 10 electrical 10 fire 10 sonic)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 50)
        self.assertEqual(summary.raw_healing, 0)
        self.assertEqual(summary.healing_by_type, {})

    def test_default_character_data_amorphions_do_not_heal(self):
        db = hgx_data.load_character_database(hgx_data.default_character_data_dir())
        elemental_types = [
            hgx_data.DAMAGE_TYPE_NAME_TO_ID[name]
            for name in ("acid", "cold", "electrical", "fire", "sonic")
        ]

        for creature_name in ("Amorphion", "Greater Amorphion", "Superior Amorphion", "Elite Amorphion"):
            with self.subTest(creature_name=creature_name):
                profile = db._resolve_combat_profile(creature_name)
                self.assertIsNotNone(profile)
                self.assertTrue(all(profile.healing[damage_type] == 0 for damage_type in elemental_types))

        summary = meter.analyze_chat_records(
            ["Alice damages Elite Amorphion : 50 (10 acid 10 cold 10 electrical 10 fire 10 sonic)"],
            character_db=db,
        )

        self.assertEqual(summary.raw_damage, 50)
        self.assertEqual(summary.raw_healing, 0)
        self.assertEqual(summary.healing_by_type, {})


if __name__ == "__main__":
    unittest.main()
