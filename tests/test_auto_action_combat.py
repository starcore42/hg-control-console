import time
import unittest

from src.simkeys_app.simkeys_script_host import (
    ChatLineEvent,
    AutoActionScript,
    AutoAttackScript,
    AutoCombatModeScript,
    parse_chat_line_event,
)


class FakeClient:
    pid = 1234
    display_name = "PlayerCharacter"
    character_name = "PlayerCharacter"
    query = {}


class FakeHost:
    def __init__(self):
        self.client = FakeClient()
        self.latest_sequence = 0
        self.events = []
        self.chats = []
        self.action_modes = []
        self.recovery_active = False

    def emit(self, level, message, script_id=None):
        self.events.append((level, message, script_id))

    def notify_state_changed(self):
        pass

    def send_chat(self, text, mode=2):
        self.chats.append((text, mode))
        return {"success": 1, "rc": 0, "err": 0}

    def send_console(self, text):
        self.chats.append((f"!echo {text}", 2))
        return {"success": 1, "rc": 0, "err": 0}

    def set_action_mode(self, mode, enabled=True):
        self.action_modes.append((int(mode), bool(enabled)))
        return {"success": 1, "mode": int(mode), "enabled": bool(enabled), "active": 1, "rc": 1, "err": 0}

    def set_combat_mode(self, mode, enabled=True):
        return self.set_action_mode(mode, enabled)

    def query_state(self):
        return {"module_base": 0x00400000, "player_object": 0x10000000, "player_creature": 0x20000000}

    def is_shifter_recovery_active(self):
        return self.recovery_active

    def is_auto_attack_paused(self):
        return False


class StopAfterFirstWait:
    def __init__(self):
        self.wait_calls = []

    def is_set(self):
        return False

    def wait(self, timeout):
        self.wait_calls.append(float(timeout))
        return True


class TestAutoActionCombat(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()
        self.config = {"mode": "Knockdown", "cooldown_seconds": 1.0}
        self.script = AutoActionScript(self.host.client, self.config, self.host)
        self.script.enabled = True

    def test_combat_tracking(self):
        self.assertEqual(self.script.last_combat_at, 0.0)

        event = parse_chat_line_event(1, "PlayerCharacter attacks Aboleth")
        self.script.on_chat_event(event)

        self.assertGreater(self.script.last_combat_at, 0.0)

        self.script.last_combat_at = 0.0
        event = parse_chat_line_event(2, "PlayerCharacter attacks FriendlyNPC")
        self.script.on_chat_event(event)
        self.assertEqual(self.script.last_combat_at, 0.0)

        event = parse_chat_line_event(3, "OtherPlayer attacks Aboleth")
        self.script.on_chat_event(event)
        self.assertEqual(self.script.last_combat_at, 0.0)

    def test_damage_tracking(self):
        event = parse_chat_line_event(1, "PlayerCharacter damages Aboleth: 10 (10 physical)")
        self.script.on_chat_event(event)

        self.assertGreater(self.script.last_combat_at, 0.0)

    def test_combat_window_uses_six_second_minimum(self):
        self.script.last_combat_at = 100.0

        self.assertEqual(self.script._combat_window_seconds(), 6.0)
        self.assertTrue(self.script._combat_is_recent(now=105.9))
        self.assertFalse(self.script._combat_is_recent(now=106.1))

    def test_combat_window_extends_for_longer_cooldowns(self):
        self.script.config["cooldown_seconds"] = 12.5
        self.script.last_combat_at = 100.0

        self.assertEqual(self.script._combat_window_seconds(), 12.5)
        self.assertTrue(self.script._combat_is_recent(now=112.4))
        self.assertFalse(self.script._combat_is_recent(now=112.6))


class TestAutoAttackCombat(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()
        self.script = AutoAttackScript(self.host.client, {"cooldown_seconds": 1.0}, self.host)
        self.script.enabled = True

    def test_combat_tracking_uses_any_parsed_attack_line(self):
        self.assertEqual(self.script.last_combat_at, 0.0)

        event = parse_chat_line_event(1, "PlayerCharacter attacks Aboleth")
        self.script.on_chat_event(event)

        self.assertGreater(self.script.last_combat_at, 0.0)

        self.script.last_combat_at = 0.0
        event = parse_chat_line_event(2, "PlayerCharacter attacks FriendlyNPC")
        self.script.on_chat_event(event)
        self.assertGreater(self.script.last_combat_at, 0.0)

        self.script.last_combat_at = 0.0
        event = parse_chat_line_event(3, "OtherPlayer attacks Aboleth")
        self.script.on_chat_event(event)
        self.assertGreater(self.script.last_combat_at, 0.0)

    def test_damage_tracking_uses_any_parsed_damage_line(self):
        event = parse_chat_line_event(1, "PlayerCharacter damages Aboleth: 10 (10 physical)")
        self.script.on_chat_event(event)

        self.assertGreater(self.script.last_combat_at, 0.0)

        self.script.last_combat_at = 0.0
        event = parse_chat_line_event(2, "OtherPlayer damages Aboleth: 10 (10 physical)")
        self.script.on_chat_event(event)

        self.assertGreater(self.script.last_combat_at, 0.0)

    def test_combat_window_uses_six_second_minimum(self):
        self.script.last_combat_at = 100.0

        self.assertEqual(self.script._combat_window_seconds(), 6.0)
        self.assertTrue(self.script._combat_is_recent(now=105.9))
        self.assertFalse(self.script._combat_is_recent(now=106.1))

    def test_combat_window_extends_for_longer_cooldowns(self):
        self.script.config["cooldown_seconds"] = 12.5
        self.script.last_combat_at = 100.0

        self.assertEqual(self.script._combat_window_seconds(), 12.5)
        self.assertTrue(self.script._combat_is_recent(now=112.4))
        self.assertFalse(self.script._combat_is_recent(now=112.6))

    def test_loop_waits_without_sending_until_combat_is_recent(self):
        self.script.loop_stop = StopAfterFirstWait()

        self.script._run_loop()

        self.assertEqual(self.host.chats, [])
        self.assertEqual(self.script.status_text, "Waiting for combat")

    def test_loop_sends_when_combat_is_recent(self):
        self.script.last_combat_at = time.monotonic()
        self.script.loop_stop = StopAfterFirstWait()

        self.script._run_loop()

        self.assertEqual(self.host.chats, [(AutoAttackScript.COMMAND, 2)])
        self.assertEqual(self.script.status_text, "Running: Auto Attack")


class TestAutoCombatMode(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()
        self.config = {"mode": AutoCombatModeScript.MODE_DEFENSIVE_CASTING, "cooldown_seconds": 1.0}
        self.script = AutoCombatModeScript(self.host.client, self.config, self.host)
        self.script.enabled = True

    def test_defensive_casting_triggers_on_observed_attack(self):
        self.script._read_defensive_casting_status = lambda: 0

        event = parse_chat_line_event(1, "OtherPlayer attacks Aboleth : *hit*")
        self.script.on_chat_event(event)

        self.assertEqual(self.host.action_modes, [(10, True)])
        self.assertEqual(self.host.chats, [])

    def test_defensive_casting_triggers_on_observed_damage(self):
        self.script._read_defensive_casting_status = lambda: 0

        event = parse_chat_line_event(1, "OtherPlayer damages Aboleth: 10 (10 physical)")
        self.script.on_chat_event(event)

        self.assertEqual(self.host.action_modes, [(10, True)])

    def test_defensive_casting_tick_upkeep_triggers_without_combat(self):
        self.script._read_defensive_casting_status = lambda: 0

        self.script.on_tick()

        self.assertEqual(self.host.action_modes, [(10, True)])
        self.assertEqual(self.script.last_defender, "upkeep")

    def test_defensive_casting_tick_upkeep_uses_six_second_default_cooldown(self):
        host = FakeHost()
        script = AutoCombatModeScript(host.client, {"mode": AutoCombatModeScript.MODE_DEFENSIVE_CASTING}, host)
        script.enabled = True
        script._read_defensive_casting_status = lambda: 0

        script.on_tick()

        self.assertEqual(host.action_modes, [(10, True)])
        self.assertGreaterEqual(script.cooldown_until - time.monotonic(), 5.5)
        self.assertLessEqual(script.cooldown_until - time.monotonic(), 6.1)

    def test_defensive_casting_does_not_need_chat_feed_for_upkeep(self):
        self.assertFalse(self.script.needs_chat_feed())

    def test_defensive_casting_does_not_trigger_when_active(self):
        self.script._read_defensive_casting_status = lambda: 1

        event = parse_chat_line_event(1, "OtherPlayer attacks Aboleth : *hit*")
        self.script.on_chat_event(event)

        self.assertEqual(self.host.action_modes, [])

    def test_defensive_casting_invalid_status_does_not_count_as_active(self):
        self.script.defensive_casting_address = 0x10000184
        self.script.defensive_casting_player_object = 0x10000000
        self.script.defensive_casting_creature = 0x10000000
        self.script._read_defensive_casting_status = lambda: 255

        event = parse_chat_line_event(1, "OtherPlayer attacks Aboleth : *hit*")
        self.script.on_chat_event(event)

        self.assertEqual(self.host.action_modes, [(10, True)])

    def test_defensive_casting_client_probe_retries_after_cooldown(self):
        self.host.query_state = lambda: {"module_base": 0x00400000, "player_object": 0x10000000, "player_creature": 0}
        self.script._read_u32 = lambda address: 0 if address == 0x10000184 else 255

        self.script.on_tick()

        self.assertEqual(self.host.action_modes, [(10, True)])
        self.assertTrue(self.script.defensive_casting_shadow_active)

        self.script.on_tick()
        self.assertEqual(self.host.action_modes, [(10, True)])

        self.script.cooldown_until = 0.0
        self.script.on_tick()

        self.assertEqual(self.host.action_modes, [(10, True), (10, True)])

    def test_attack_modes_still_require_local_attacker(self):
        self.config["mode"] = AutoCombatModeScript.MODE_EXPERTISE

        event = parse_chat_line_event(1, "OtherPlayer attacks Aboleth : *hit*")
        self.script.on_chat_event(event)

        self.assertEqual(self.host.chats, [])

    def test_attack_modes_do_not_subscribe_to_raw_lines(self):
        self.config["mode"] = AutoCombatModeScript.MODE_EXPERTISE
        event = ChatLineEvent(
            sequence=1,
            raw_text="OtherPlayer attacks Aboleth : *hit*",
            normalized="OtherPlayer attacks Aboleth : *hit*",
            kinds=(),
        )

        self.assertFalse(self.script.wants_chat_event(event))
