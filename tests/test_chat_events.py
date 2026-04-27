import os
import time
import unittest

from src.simkeys_app import simkeys_hgx_combat as combat
from src.simkeys_app import simkeys_hgx_data as hgx_data
from src.simkeys_app.simkeys_script_host import (
    ActiveOverlayTimer,
    AutoAAScript,
    AutoAttackScript,
    AutoDrinkScript,
    AutoFollowScript,
    ChatLineEvent,
    ClientScriptBase,
    ClientScriptHost,
    InGameTimersScript,
    WeaponDamageEstimate,
    WeaponRecommendation,
    _default_status_rules_dir,
    _load_hgx_spell_timer_specs,
    _load_status_timer_rules,
    _spell_key,
    parse_chat_line_event,
)


class FakeClient:
    pid = 1234
    display_name = "Starcore-StormReaper [2.0]"
    character_name = "Starcore-StormReaper [2.0]"
    query = {}


class FakeHost:
    def __init__(self):
        self.client = FakeClient()
        self.latest_sequence = 0
        self.events = []
        self.chats = []
        self.overlays = []
        self.slots = []
        self.mask = 1 << 0
        self.recovery_until = 0.0
        self.recovery_reason = ""
        self.auto_attack_pause_until = 0.0
        self.auto_attack_pause_reason = ""

    def emit(self, level, message, script_id=None):
        self.events.append((level, message, script_id))

    def notify_state_changed(self):
        pass

    def format_slot(self, page, slot):
        return f"F{slot}" if page == 0 else f"P{page}F{slot}"

    def send_chat(self, text, mode=2):
        self.chats.append(text)
        return {"success": 1, "rc": 0, "err": 0}

    def send_console(self, text):
        self.chats.append(f"!echo {text}")
        return {"success": 1, "rc": 0, "err": 0}

    def trigger_slot(self, slot, page=0):
        self.slots.append((page, slot))
        if slot == 2:
            self.mask = 1 << 1
        return {"success": 1, "rc": 0, "aux_rc": 0, "path": 1, "err": 0}

    def query_state(self):
        return {"quickbar_equipped_mask": self.mask}

    def show_overlay_text(self, text, **kwargs):
        self.overlays.append((text, kwargs))
        return {"success": 1, "rc": 0, "err": 0}

    def clear_overlay(self, overlay_id):
        self.overlays.append(("", {"overlay_id": overlay_id}))
        return {"success": 1, "rc": 0, "err": 0}

    def set_shifter_recovery_active(self, active, reason="", ttl_seconds=5.0):
        if active:
            self.recovery_until = max(self.recovery_until, time.monotonic() + max(float(ttl_seconds), 0.5))
            self.recovery_reason = str(reason or "shifter form recovery")
        else:
            self.recovery_until = 0.0
            self.recovery_reason = ""

    def is_shifter_recovery_active(self):
        if time.monotonic() < self.recovery_until:
            return True
        self.recovery_until = 0.0
        self.recovery_reason = ""
        return False

    def set_auto_attack_pause(self, active, reason="", ttl_seconds=5.0):
        if active:
            self.auto_attack_pause_until = max(
                self.auto_attack_pause_until,
                time.monotonic() + max(float(ttl_seconds), 0.1),
            )
            self.auto_attack_pause_reason = str(reason or "Paused")
        else:
            self.auto_attack_pause_until = 0.0
            self.auto_attack_pause_reason = ""

    def is_auto_attack_paused(self):
        if time.monotonic() < self.auto_attack_pause_until:
            return True
        self.auto_attack_pause_until = 0.0
        self.auto_attack_pause_reason = ""
        return False


class RecordingScript(ClientScriptBase):
    script_id = "recording"

    def __init__(self, event_types):
        super().__init__(FakeClient(), {}, None)
        self.event_types = tuple(event_types)
        self.events = []

    def chat_event_types(self):
        return self.event_types

    def on_chat_event(self, event: ChatLineEvent):
        self.events.append(event)

    def on_chat_line(self, sequence: int, text: str):
        raise AssertionError("router should use on_chat_event")


class ChatEventTests(unittest.TestCase):
    def test_spell_effect_keys_ignore_apostrophes(self):
        self.assertEqual(_spell_key("Tenser's Transformation"), _spell_key("Tensers Transformation"))
        self.assertEqual(_spell_key("Nature's Balance"), _spell_key("Natures Balance"))

    def test_default_spell_timer_rules_include_shadow_evade_and_aura_fear(self):
        xml_files = sorted(
            name
            for name in os.listdir(_default_status_rules_dir())
            if name.lower().endswith(".xml")
        )
        self.assertEqual(xml_files, ["statusrules.xml"])
        specs = {
            _spell_key(spec.spell): spec.effect
            for spec in _load_hgx_spell_timer_specs(_default_status_rules_dir())
        }
        self.assertEqual(specs[_spell_key("Shadow Evade")], "Shadow Evade")
        self.assertEqual(specs[_spell_key("Aura Fear")], "Aura Fear")
        aura = next(
            spec
            for spec in _load_hgx_spell_timer_specs(_default_status_rules_dir())
            if _spell_key(spec.spell) == _spell_key("Aura Fear")
        )
        self.assertIn("surrounded by an aura", aura.trigger_pattern)

    def test_parse_combat_and_shifter_events(self):
        attack = parse_chat_line_event(1, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] Rapid Shot : Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        self.assertIn("attack", attack.kinds)
        self.assertEqual(attack.attack.attacker, "Starcore-StormReaper [2.0]")
        self.assertEqual(attack.attack.defender, "Dummy")
        self.assertEqual(attack.attack.attack_mode, "Rapid Shot")

        damage = parse_chat_line_event(2, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 42 (12 fire 30 physical)")
        self.assertIn("damage", damage.kinds)
        self.assertEqual(damage.damage.total, 42)
        self.assertEqual([component.type_name for component in damage.damage.components], ["Fire", "Physical"])

        shifted = parse_chat_line_event(3, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:02] Starcore-StormReaper [2.0] shifts into undead form.")
        self.assertIn("shifter_state", shifted.kinds)
        self.assertEqual(shifted.shifter_shift_actor, "Starcore-StormReaper [2.0]")

        essence = parse_chat_line_event(4, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:03] You have 419/420 essence points remaining.")
        self.assertIn("shifter_state", essence.kinds)
        self.assertEqual(essence.shifter_essence_current, 419)
        self.assertEqual(essence.shifter_essence_maximum, 420)

        player_hide = parse_chat_line_event(5, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:04] Acquired Item: Player Hide")
        self.assertTrue(player_hide.player_hide)
        self.assertIn("player_hide", player_hide.kinds)

        already_poly = parse_chat_line_event(6, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:05] You cannot change shape while already polymorphed!")
        self.assertTrue(already_poly.shifter_already_polymorphed)
        self.assertIn("shifter_state", already_poly.kinds)

        shadow_evade = parse_chat_line_event(6, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:05] Starcore-SD [4.0] casts Shadow Evade")
        self.assertIn("spell_cast", shadow_evade.kinds)
        self.assertEqual(shadow_evade.spell_caster, "Starcore-SD [4.0]")
        self.assertEqual(shadow_evade.spell_name, "Shadow Evade")

        aura_fear = parse_chat_line_event(7, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:06] Starcore-DSM [1.4] is surrounded by an aura.")
        self.assertIn("ability_trigger", aura_fear.kinds)
        self.assertIn("spell_cast", aura_fear.kinds)
        self.assertEqual(aura_fear.spell_caster, "Starcore-DSM [1.4]")
        self.assertEqual(aura_fear.spell_name, "Aura Fear")

    def test_ingame_timers_queries_aura_fear_and_reads_effect_duration(self):
        host = FakeHost()
        host.client.display_name = "Starcore-DSM [1.4]"
        host.client.character_name = "Starcore-DSM [1.4]"
        script = InGameTimersScript(host.client, {}, host)
        script.on_start()

        self.assertTrue(script._handle_spell_cast_line("Starcore-DSM [1.4] is surrounded by an aura.", 100.0))
        self.assertIn(_spell_key("Aura Fear"), script.pending_effect_queries)

        self.assertTrue(script._handle_effect_timer_line("#198 Aura Fear [4m39s left]", 101.0))
        self.assertNotIn(_spell_key("Aura Fear"), script.pending_effect_queries)
        timer = script.active["spell:aura fear"]
        self.assertEqual(timer.label, "Aura Fear")
        self.assertEqual(timer.duration_seconds, 279.0)

    def test_ingame_timers_reads_effect_duration_with_source_suffix(self):
        host = FakeHost()
        host.client.display_name = "Starcore-Ranger [4.3]"
        host.client.character_name = "Starcore-Ranger [4.3]"
        script = InGameTimersScript(host.client, {}, host)
        spec = next(
            spec
            for spec in _load_hgx_spell_timer_specs(_default_status_rules_dir())
            if _spell_key(spec.spell) == _spell_key("Invisibility Purge")
        )
        script.spell_specs_by_key = {spec.key: spec}
        script.spell_specs_by_effect_key = {_spell_key(spec.effect): spec}

        self.assertTrue(script._handle_spell_cast_line("Starcore-Ranger [4.3] casts Invisibility Purge", 100.0))
        self.assertIn(_spell_key("Invisibility Purge"), script.pending_effect_queries)

        effects = (
            "[Server] Effects on you:\n"
            "    #91 Invisibility Purge [11m34s left] (Shaundakul's Sense)"
        )
        self.assertTrue(script._handle_effect_timer_line(effects, 101.0))
        self.assertNotIn(_spell_key("Invisibility Purge"), script.pending_effect_queries)
        timer = script.active["spell:invisibility purge"]
        self.assertEqual(timer.label, "Invisibility Purge")
        self.assertEqual(timer.description, "Invisibility Purge")
        self.assertEqual(timer.duration_seconds, 694.0)

    def test_ingame_timers_reads_aura_fear_from_real_effects_block_shape(self):
        host = FakeHost()
        host.client.display_name = "Starcore-DSM [1.4]"
        host.client.character_name = "Starcore-DSM [1.4]"
        config = {
            "spell_timers": "Death Ward=Death Ward"
        }
        script = InGameTimersScript(host.client, config, host)
        script.on_start()
        self.assertIn(_spell_key("Aura Fear"), {spec.key for spec in script.spell_specs})
        self.assertIn(_spell_key("Aura Fear"), script.spell_specs_by_effect_key)

        effects = (
            "<ca><cb>[Server] <cc>Effects on you:\n"
            "<cd>    #369 <ce>Energy Buffer [58m5s left]\n"
            "<cf>    #198 <cg>Aura Fear [4m41s left]\n"
            "</c></c>"
        )
        event = parse_chat_line_event(12235, effects)
        self.assertIn("effect_timer", event.kinds)

        script.on_chat_event(event)
        timer = script.active["spell:aura fear"]
        self.assertEqual(timer.label, "Aura Fear")
        self.assertEqual(timer.description, "Aura Fear")
        self.assertEqual(timer.duration_seconds, 281.0)
        self.assertIn("Aura Fear 4:41", host.overlays[-1][0])

    def test_ingame_timers_effects_snapshot_syncs_statusrule_effects_only(self):
        host = FakeHost()
        script = InGameTimersScript(host.client, {}, host)
        script.on_start()

        effects = (
            "Effects on you:\n"
            "    #42 Divine Power [9m47s left]\n"
            "    #369 Energy Buffer [58m5s left]\n"
        )
        self.assertTrue(script._handle_effect_timer_line(effects, 100.0))
        self.assertIn("spell:storm of vengeance", script.active)
        self.assertNotIn("spell:energy buffer", script.active)
        timer = script.active["spell:storm of vengeance"]
        self.assertEqual(timer.label, "Storm of Vengeance")
        self.assertEqual(timer.description, "Divine Power")

    def test_ingame_timers_effects_snapshot_removes_missing_spell_timers_only(self):
        host = FakeHost()
        script = InGameTimersScript(host.client, {}, host)
        script.on_start()

        first_effects = (
            "Effects on you:\n"
            "    #198 Aura Fear [4m41s left]\n"
            "    #477 Shadow Evade [9m52s left]\n"
        )
        self.assertTrue(script._handle_effect_timer_line(first_effects, 100.0))
        script.active["text:infected"] = ActiveOverlayTimer(
            label="Infected",
            description="Infected",
            expires_at=time.monotonic() + 600.0,
            duration_seconds=600.0,
            color_rgb=0xFF6666,
            disable_on_death=True,
            disable_on_rest=False,
            source="statusrules.xml",
        )
        self.assertIn("spell:shadow evade", script.active)

        second_effects = (
            "Effects on you:\n"
            "    #198 Aura Fear [4m36s left]\n"
        )
        self.assertTrue(script._handle_effect_timer_line(second_effects, 105.0))
        self.assertIn("spell:aura fear", script.active)
        self.assertNotIn("spell:shadow evade", script.active)
        self.assertIn("text:infected", script.active)

    def test_ingame_timers_loads_hgx_state_rules(self):
        rules = _load_status_timer_rules(_default_status_rules_dir())

        self.assertTrue(any(rule.kind == "state" and rule.text == "Infected" for rule in rules))
        self.assertTrue(any(
            rule.kind == "state"
            and rule.text == "Infected"
            and rule.scope == "party"
            and rule.pattern.search("The slaad drives its claws into you and implants something in you!")
            and rule.disable_pattern is not None
            and rule.disable_pattern.search("The continual contagion is lifted!")
            for rule in rules
        ))

    def test_ingame_timers_slaad_disease_is_shared_to_all_timer_overlays(self):
        host_a = FakeHost()
        host_a.client.display_name = "Starcore-Lash-Quasi [1.0]"
        host_a.client.character_name = "Starcore-Lash-Quasi [1.0]"
        host_b = FakeHost()
        host_b.client.display_name = "Starcore-Bard [5.0]"
        host_b.client.character_name = "Starcore-Bard [5.0]"
        script_a = InGameTimersScript(host_a.client, {}, host_a)
        script_b = InGameTimersScript(host_b.client, {}, host_b)
        script_a.on_start()
        script_b.on_start()
        try:
            script_a.on_chat_event(parse_chat_line_event(
                1,
                "The slaad drives its claws into you and implants something in you!",
            ))

            self.assertIn("Infected: Starcore-Lash-Quasi [1.0]", host_a.overlays[-1][0])
            self.assertIn("Infected: Starcore-Lash-Quasi [1.0]", host_b.overlays[-1][0])
            self.assertNotIn("Infected: Starcore-Bard [5.0]", host_b.overlays[-1][0])

            script_a.on_chat_event(parse_chat_line_event(2, "You feel something vile hatching inside your body!"))
            self.assertEqual(host_a.overlays[-1][0].count("Infected"), 1)
        finally:
            script_a.on_stop()
            script_b.on_stop()

    def test_ingame_timers_party_disease_clears_on_named_death(self):
        host_a = FakeHost()
        host_a.client.display_name = "Starcore-Lash-Quasi [1.0]"
        host_a.client.character_name = "Starcore-Lash-Quasi [1.0]"
        host_b = FakeHost()
        host_b.client.display_name = "Starcore-Bard [5.0]"
        host_b.client.character_name = "Starcore-Bard [5.0]"
        script_a = InGameTimersScript(host_a.client, {}, host_a)
        script_b = InGameTimersScript(host_b.client, {}, host_b)
        script_a.on_start()
        script_b.on_start()
        try:
            script_a.on_chat_event(parse_chat_line_event(
                1,
                "The slaad drives its claws into you and implants something in you!",
            ))
            self.assertIn("Infected: Starcore-Lash-Quasi [1.0]", host_b.overlays[-1][0])

            script_b.on_chat_event(parse_chat_line_event(2, "Someone killed Starcore-Lash-Quasi [1.0]"))

            self.assertNotIn("Infected: Starcore-Lash-Quasi [1.0]", host_a.overlays[-1][0])
            self.assertNotIn("Infected: Starcore-Lash-Quasi [1.0]", host_b.overlays[-1][0])
        finally:
            script_a.on_stop()
            script_b.on_stop()

    def test_ingame_timers_limbo_deaths_run_in_parallel(self):
        host = FakeHost()
        script = InGameTimersScript(
            host.client,
            {
                "limbo_duration_seconds": 300.0,
                "limbo_names": "Alice [1.0]",
            },
            host,
        )
        script.on_start()

        self.assertTrue(script._handle_limbo_line("Raja killed Alice [1.0]", 100.0))
        self.assertTrue(script._handle_limbo_line("Sulfuron killed Alice [1.0]", 130.0))

        limbo_timers = [
            timer
            for timer in script.active.values()
            if timer.source == script.LIMBO_SOURCE and timer.label == "Alice [1.0]"
        ]
        self.assertEqual(len(limbo_timers), 2)
        self.assertEqual(sorted(round(timer.expires_at) for timer in limbo_timers), [400, 430])
        self.assertEqual(script.limbo_count, 2)

        self.assertTrue(script._handle_limbo_line("Alice [1.0] respawn : Raise Dead : *success*", 140.0))
        limbo_timers = sorted(
            (
                timer
                for timer in script.active.values()
                if timer.source == script.LIMBO_SOURCE and timer.label == "Alice [1.0]"
            ),
            key=lambda timer: timer.expires_at,
        )
        self.assertEqual([timer.state for timer in limbo_timers], ["limbo", "recovered"])

    def test_ingame_timers_limbo_expiry_removes_only_that_countdown(self):
        host = FakeHost()
        script = InGameTimersScript(
            host.client,
            {
                "limbo_duration_seconds": 300.0,
                "limbo_names": "Alice [1.0]",
            },
            host,
        )
        script.on_start()

        now = time.monotonic()
        self.assertTrue(script._handle_limbo_line("Raja killed Alice [1.0]", now - 301.0))
        self.assertTrue(script._handle_limbo_line("Sulfuron killed Alice [1.0]", now - 100.0))
        self.assertEqual(
            len([timer for timer in script.active.values() if timer.source == script.LIMBO_SOURCE]),
            2,
        )

        script.on_tick()

        limbo_timers = [
            timer
            for timer in script.active.values()
            if timer.source == script.LIMBO_SOURCE and timer.label == "Alice [1.0]"
        ]
        self.assertEqual(len(limbo_timers), 1)
        self.assertEqual(limbo_timers[0].description, "killed by Sulfuron")

    def test_host_routes_typed_events_without_broadcasting_to_every_script(self):
        delivered = []
        host = ClientScriptHost(FakeClient(), delivered.append)
        damage_script = RecordingScript(("damage",))
        attack_script = RecordingScript(("attack",))
        raw_script = RecordingScript(("raw",))
        host.scripts = {
            "damage": damage_script,
            "attack": attack_script,
            "raw": raw_script,
        }

        damage_event = parse_chat_line_event(10, "Starcore-StormReaper [2.0] damages Dummy : 7 (7 fire)")
        host._dispatch_chat_event(damage_event)
        self.assertEqual([event.sequence for event in damage_script.events], [10])
        self.assertEqual(attack_script.events, [])
        self.assertEqual([event.sequence for event in raw_script.events], [10])

        attack_event = parse_chat_line_event(11, "Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        host._dispatch_chat_event(attack_event)
        self.assertEqual([event.sequence for event in damage_script.events], [10])
        self.assertEqual([event.sequence for event in attack_script.events], [11])
        self.assertEqual([event.sequence for event in raw_script.events], [10, 11])

    def test_overlay_and_password_are_handled_before_script_dispatch(self):
        delivered = []
        host = ClientScriptHost(FakeClient(), delivered.append)
        raw_script = RecordingScript(("raw",))
        host.scripts = {"raw": raw_script}

        overlay = parse_chat_line_event(20, "\x1eSIMKEYS_OVERLAY_TOGGLE:auto_aa", password_prompt_text=host.PASSWORD_PROMPT_TEXT)
        stopped = host._process_chat_event(overlay, dispatch=True)
        self.assertFalse(stopped)
        self.assertEqual(raw_script.events, [])
        self.assertEqual(delivered[-1]["type"], "overlay-script-toggle")
        self.assertEqual(delivered[-1]["script_id"], "auto_aa")

        password = parse_chat_line_event(
            21,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] You must speak your password before you can continue.",
            password_prompt_text=host.PASSWORD_PROMPT_TEXT,
        )
        stopped = host._process_chat_event(password, dispatch=True)
        self.assertTrue(stopped)
        self.assertEqual(raw_script.events, [])
        self.assertFalse(host.scripts)

    def test_auto_damage_shifter_sequence_from_parsed_events(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
                "swap_cooldown_seconds": 0.1,
            },
            host,
        )
        script.on_start()
        script.on_chat_event(parse_chat_line_event(1, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:00] Starcore-StormReaper [2.0] shifts into undead form."))
        self.assertEqual(script.shifter_shift_state, "shifted")

        self.assertTrue(script._request_weapon_swap(script.weapon_bindings["W2"], "Dummy", "learn"))
        self.assertEqual(host.chats[:2], ["!lock opponent", "!cancel poly"])

        script.on_chat_event(parse_chat_line_event(2, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Acquired Item: Player Hide"))
        script.on_tick()
        self.assertEqual(host.slots[-1], (0, 2))

        script.on_chat_event(parse_chat_line_event(3, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:02] Weapon equipped as a one-handed weapon."))
        self.assertEqual(host.slots[-1], (0, 9))

        script.on_chat_event(parse_chat_line_event(4, "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:03] You have 419/420 essence points remaining."))
        self.assertEqual(host.chats[-1], "!action attack locked")
        self.assertEqual(script.shifter_swap_stage, "")
        self.assertEqual(script.shifter_shift_state, "shifted")

    def test_shifter_player_hide_in_recent_combat_starts_form_recovery(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.shifter_shift_state = "shifted"
        script.on_chat_event(parse_chat_line_event(1, "Balor attacks Starcore-StormReaper [2.0] : *hit*"))
        script.on_chat_event(parse_chat_line_event(2, "Acquired Item: Player Hide"))

        self.assertEqual(script.shifter_swap_stage, "reshifting")
        self.assertEqual(host.slots[-1], (0, 9))
        self.assertTrue(host.is_shifter_recovery_active())

    def test_shifter_already_polymorphed_confirms_form_recovery(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.shifter_shift_state = "unshifted"
        script.shifter_last_combat_at = time.monotonic()

        self.assertTrue(script._request_shifter_form_recovery("recent combat"))
        self.assertEqual(host.slots[-1], (0, 9))
        self.assertTrue(host.is_shifter_recovery_active())

        script.on_chat_event(parse_chat_line_event(2, "You cannot change shape while already polymorphed!"))

        self.assertEqual(script.shifter_shift_state, "shifted")
        self.assertEqual(script.shifter_swap_stage, "")
        self.assertFalse(host.is_shifter_recovery_active())
        self.assertEqual(host.chats[-1], "!action attack locked")

    def test_shifter_quickbar_weapon_indicator_in_combat_triggers_form_check(self):
        host = FakeHost()
        host.mask = 1 << 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.shifter_shift_state = "shifted"
        script.shifter_last_shift_at = time.monotonic() - 10.0
        script.on_chat_event(parse_chat_line_event(1, "Pit Fiend attacks someone : *hit*"))
        script.on_tick()

        self.assertEqual(script.shifter_shift_state, "unshifted")
        self.assertEqual(script.shifter_swap_stage, "reshifting")
        self.assertEqual(host.slots[-1], (0, 9))

    def test_shifter_recovery_pauses_follow_and_drink(self):
        host = FakeHost()
        host.set_shifter_recovery_active(True, "test", ttl_seconds=5.0)

        follow = AutoFollowScript(host.client, {"echo_console": False}, host)
        follow.enabled = True
        follow.follow_cues = follow.DEFAULT_FOLLOW_CUES
        self.assertTrue(follow._handle_follow_line("Starcore-Lead [1.0]: follow me"))
        self.assertEqual(host.chats, [])
        self.assertIn("paused", follow.status_text)

        drink = AutoDrinkScript(host.client, {"slot": 2, "echo_console": False}, host)
        drink.enabled = True
        drink._read_health_snapshot = lambda: (1, 100, 1.0, "test")
        drink._poll_health_once()
        self.assertEqual(host.slots, [])
        self.assertIn("Paused", drink.status_text)

    def test_autodrink_echo_uses_client_echo_command(self):
        host = FakeHost()
        drink = AutoDrinkScript(
            host.client,
            {"slot": 2, "echo_console": True, "lock_target": False, "threshold_percent": 80.0},
            host,
        )
        drink.enabled = True
        drink._read_health_snapshot = lambda: (50, 100, 50.0, "test")

        drink._poll_health_once()

        self.assertEqual(host.slots, [(0, 2)])
        self.assertTrue(any(text.startswith("!echo HGCC autodrink 50/100") for text in host.chats))
        self.assertFalse(any(text.startswith("##HGCC") for text in host.chats))

    def test_autodrink_cooldown_pauses_auto_attack(self):
        host = FakeHost()
        drink = AutoDrinkScript(
            host.client,
            {
                "slot": 2,
                "echo_console": False,
                "lock_target": False,
                "resume_attack": False,
                "threshold_percent": 80.0,
                "cooldown_seconds": 0.1,
            },
            host,
        )
        drink.enabled = True
        drink._read_health_snapshot = lambda: (50, 100, 50.0, "test")

        drink._poll_health_once()

        self.assertEqual(host.slots, [(0, 2)])
        self.assertTrue(host.is_auto_attack_paused())
        self.assertEqual(host.auto_attack_pause_reason, "AutoDrink cooldown")
        time.sleep(0.25)
        self.assertFalse(host.is_auto_attack_paused())

    def test_auto_attack_skips_commands_during_autodrink_cooldown(self):
        host = FakeHost()
        host.set_auto_attack_pause(True, "AutoDrink cooldown", ttl_seconds=1.0)
        attack = AutoAttackScript(host.client, {"cooldown_seconds": 0.1}, host)

        attack.on_start()
        deadline = time.monotonic() + 0.5
        while "AutoDrink cooldown" not in attack.status_text and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertIn("AutoDrink cooldown", attack.status_text)
        time.sleep(0.15)
        attack.on_stop()

        self.assertEqual(host.chats, [])

    def test_shifter_mode_only_swaps_when_current_weapon_heals(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
                "shifter_healing_only": True,
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "W1"
        script._profile_learning_complete = lambda profile: True
        script._next_weapon_to_learn = lambda target: None
        script.db.lookup = lambda name: True

        def recommendation(binding, score, healing=()):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=score,
                selection_damage=score,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(3,),
                estimated_components=((3, 100),),
                healing_types=tuple(healing),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
            )

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 20, ()),
            recommendation(script.weapon_bindings["W2"], 100, ()),
        ]
        script._handle_weapon_attack(attack)
        self.assertEqual(host.chats, [])
        self.assertEqual(host.slots, [])
        self.assertIn("keep W1", script.status_text)

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 0, (3,)),
            recommendation(script.weapon_bindings["W2"], 50, ()),
        ]
        script._handle_weapon_attack(attack)
        self.assertEqual(host.chats[:2], ["!lock opponent", "!cancel poly"])

    def test_shifter_mode_swaps_for_large_damage_gain_by_default(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "W1"
        script._profile_learning_complete = lambda profile: True
        script._next_weapon_to_learn = lambda target: None
        script.db.lookup = lambda name: True

        def recommendation(binding, score, healing=()):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=score,
                selection_damage=score,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(3,),
                estimated_components=((3, 100),),
                healing_types=tuple(healing),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
            )

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 20, ()),
            recommendation(script.weapon_bindings["W2"], 100, ()),
        ]

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        script._handle_weapon_attack(attack)

        self.assertEqual(host.chats[:2], ["!lock opponent", "!cancel poly"])

    def test_shifter_mode_holds_when_damage_gain_is_below_threshold(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
                "shifter_min_swap_gain_percent": 300.0,
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "W1"
        script._profile_learning_complete = lambda profile: True
        script._next_weapon_to_learn = lambda target: None
        script.db.lookup = lambda name: True

        def recommendation(binding, score):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=score,
                selection_damage=score,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(3,),
                estimated_components=((3, 100),),
                healing_types=(),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
            )

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 100),
            recommendation(script.weapon_bindings["W2"], 350),
        ]

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        script._handle_weapon_attack(attack)

        self.assertEqual(host.chats, [])
        self.assertEqual(host.slots, [])
        self.assertIn("< 300.0", script.status_text)

    def test_shifter_unarmed_unshifted_recovers_to_least_healing_weapon(self):
        host = FakeHost()
        host.mask = 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "Unarmed"
        script.shifter_shift_state = "unshifted"
        script._profile_learning_complete = lambda profile: True
        script._next_weapon_to_learn = lambda target: None
        script.db.lookup = lambda name: True

        def recommendation(binding, healing_score):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=0,
                selection_damage=0,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(4,),
                estimated_components=((4, 100),),
                healing_types=(4,),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
                healing_score=healing_score,
            )

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 600),
            recommendation(script.weapon_bindings["W2"], 200),
        ]

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        script._handle_weapon_attack(attack)

        self.assertEqual(host.chats, ["!lock opponent"])
        self.assertEqual(host.slots[-1], (0, 2))
        self.assertEqual(script.pending_weapon_key, "W2")
        self.assertFalse(script.pending_weapon_unarm)
        self.assertFalse(script.shifter_pending_unarm)
        self.assertIn("recover unarmed", script.status_text)

    def test_shifter_no_safe_weapon_uses_least_healing_instead_of_unarmed(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "W1"
        script.shifter_shift_state = "shifted"
        script._profile_learning_complete = lambda profile: True
        script._next_weapon_to_learn = lambda target: None
        script.db.lookup = lambda name: True

        def recommendation(binding, healing_score):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=0,
                selection_damage=0,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(4,),
                estimated_components=((4, 100),),
                healing_types=(4,),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
                healing_score=healing_score,
            )

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 600),
            recommendation(script.weapon_bindings["W2"], 200),
        ]

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Dummy : *hit*")
        script._handle_weapon_attack(attack)

        self.assertEqual(host.chats[:2], ["!lock opponent", "!cancel poly"])
        self.assertEqual(script.shifter_pending_source_key, "W2")
        self.assertFalse(script.pending_weapon_unarm)
        self.assertFalse(script.shifter_pending_unarm)
        self.assertIn("least healing", script.status_text)

    def test_shifter_learning_keeps_current_weapon_when_shifted_mask_is_empty(self):
        host = FakeHost()
        host.mask = 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.current_weapon_key = "W1"
        script.shifter_shift_state = "shifted"

        attack = combat.parse_attack_line("Starcore-StormReaper [2.0] attacks Barbazu : *hit*")
        script._handle_weapon_attack(attack)

        self.assertEqual(host.chats, [])
        self.assertEqual(host.slots, [])
        self.assertEqual(script.current_weapon_key, "W1")
        self.assertIn("learning W1", script.status_text)

    def test_shifter_recovers_unknown_weapon_from_outgoing_damage(self):
        host = FakeHost()
        host.mask = 1 << 0  # Stale shifted quickbar mask says W1.
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.shifter_shift_state = "shifted"
        script.current_weapon_key = "Unknown"
        script.weapon_external_unknown = True
        script.weapon_external_unknown_feedback = "weapon equipped"
        script.weapon_profiles["W1"].stable_signature = (4,)
        script.weapon_profiles["W1"].stable_signature_observations = 2
        script.weapon_profiles["W2"].stable_signature = (6,)
        script.weapon_profiles["W2"].stable_signature_observations = 2

        damage = parse_chat_line_event(
            10,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 42 (12 fire 30 physical)",
        )
        script.on_chat_event(damage)

        self.assertEqual(script.current_weapon_key, "W2")
        self.assertFalse(script.weapon_external_unknown)
        self.assertNotIn("unknown after external swap", script.status_text)

    def test_shifter_recovers_previous_unknown_weapon_from_new_outgoing_signature(self):
        host = FakeHost()
        host.mask = 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.shifter_shift_state = "shifted"
        script.current_weapon_key = "W1"
        script._mark_external_weapon_unknown("weapon equipped")

        damage = parse_chat_line_event(
            10,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 42 (12 fire 30 physical)",
        )
        script.on_chat_event(damage)

        self.assertEqual(script.current_weapon_key, "W1")
        self.assertFalse(script.weapon_external_unknown)
        self.assertEqual(script.weapon_profiles["W1"].current_signature, (6,))
        self.assertEqual(script.weapon_profiles["W1"].observations, 1)

    def test_shifter_recovers_unknown_unarmed_from_physical_only_damage(self):
        host = FakeHost()
        host.mask = 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.shifter_shift_state = "shifted"
        script.current_weapon_key = "W1"
        script._mark_external_weapon_unknown("weapon equipped")

        damage = parse_chat_line_event(
            10,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 30 (30 physical)",
        )
        script.on_chat_event(damage)

        self.assertEqual(script.current_weapon_key, "Unarmed")
        self.assertFalse(script.weapon_external_unknown)
        self.assertIn("unarmed detected", script.status_text)

    def test_shifter_damage_recovery_requires_unique_learned_signature(self):
        host = FakeHost()
        host.mask = 0
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.shifter_shift_state = "shifted"
        script.current_weapon_key = "Unknown"
        script.weapon_external_unknown = True
        script.weapon_external_unknown_feedback = "weapon equipped"
        script.weapon_profiles["W1"].stable_signature = (6,)
        script.weapon_profiles["W1"].stable_signature_observations = 2
        script.weapon_profiles["W2"].stable_signature = (6,)
        script.weapon_profiles["W2"].stable_signature_observations = 2

        damage = parse_chat_line_event(
            10,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 42 (12 fire 30 physical)",
        )
        script.on_chat_event(damage)

        self.assertEqual(script.current_weapon_key, "Unknown")
        self.assertTrue(script.weapon_external_unknown)
        self.assertIn("unknown after external swap", script.status_text)

    def test_weapon_swap_rejects_shift_ctrl_weapon_slots(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "S+F1",
            },
            host,
        )

        with self.assertRaisesRegex(RuntimeError, "base F1-F12"):
            script.on_start()

    def test_weapon_mode_immediately_swaps_after_observed_healing_damage(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.weapon_profiles["W1"].stable_signature = (4,)
        script.weapon_profiles["W1"].stable_signature_observations = 2
        script.weapon_profiles["W1"].type_estimates[4] = WeaponDamageEstimate(base_estimate=50.0, observations=2)
        script.weapon_profiles["W2"].stable_signature = (6,)
        script.weapon_profiles["W2"].stable_signature_observations = 2
        script.weapon_profiles["W2"].type_estimates[6] = WeaponDamageEstimate(base_estimate=50.0, observations=2)

        damage = parse_chat_line_event(
            10,
            (
                "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] "
                "Starcore-StormReaper [2.0] damages Superior Algid Reaver : 42 (12 cold 30 physical)"
            ),
        )
        script.on_chat_event(damage)

        self.assertEqual(host.slots[-1], (0, 2))
        self.assertEqual(script.pending_weapon_key, "W2")
        self.assertIn("escape healing (Cold)", script.status_text)

    def test_weapon_mode_does_not_double_press_when_healing_damage_arrives_during_pending_swap(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.weapon_profiles["W1"].stable_signature = (4,)
        script.weapon_profiles["W1"].stable_signature_observations = 2
        script.weapon_profiles["W1"].type_estimates[4] = WeaponDamageEstimate(base_estimate=50.0, observations=2)
        script.weapon_profiles["W2"].stable_signature = (6,)
        script.weapon_profiles["W2"].stable_signature_observations = 2
        script.weapon_profiles["W2"].type_estimates[6] = WeaponDamageEstimate(base_estimate=50.0, observations=2)
        script.pending_weapon_key = "W2"
        script.pending_weapon_ready_at = time.monotonic() + 5.0

        damage = parse_chat_line_event(
            10,
            (
                "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] "
                "Starcore-StormReaper [2.0] damages Superior Algid Reaver : 42 (12 cold 30 physical)"
            ),
        )
        script.on_chat_event(damage)

        self.assertEqual(host.slots, [])
        self.assertEqual(script.pending_weapon_key, "W2")
        self.assertIn("awaiting W2/F2", script.status_text)

    def test_weapon_selection_uses_equal_type_model_instead_of_learned_amounts(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]

        fsp_profile = script.weapon_profiles["W1"]
        fsp_profile.stable_signature = (fire, sonic, positive)
        fsp_profile.stable_signature_observations = 12
        for damage_type in fsp_profile.stable_signature:
            fsp_profile.type_estimates[damage_type] = WeaponDamageEstimate(base_estimate=900.0, observations=20)

        cem_profile = script.weapon_profiles["W2"]
        cem_profile.stable_signature = (cold, electrical, magical)
        cem_profile.stable_signature_observations = 2
        for damage_type in cem_profile.stable_signature:
            cem_profile.type_estimates[damage_type] = WeaponDamageEstimate(base_estimate=1.0, observations=20)

        candidates = script._weapon_candidates_for_target("Black Slaad")
        by_key = {candidate.binding.key: candidate for candidate in candidates}

        self.assertEqual(by_key["W1"].expected_damage, 59)
        self.assertEqual(by_key["W2"].expected_damage, 106)
        self.assertEqual(script._choose_best_weapon(candidates).binding.key, "W2")

    def test_weapon_learning_ignores_self_spell_damage_without_attack_result(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = (fire, sonic, positive)
        profile.stable_signature_observations = 8

        script.on_chat_event(parse_chat_line_event(1, "Starcore-StormReaper [2.0] casts Epic Spell: Hellball"))
        for sequence in range(2, 14):
            script.on_chat_event(
                parse_chat_line_event(
                    sequence,
                    "Starcore-StormReaper [2.0] damages Hamatula: 600 (600 Electrical)",
                )
            )

        self.assertEqual(profile.stable_signature, (fire, sonic, positive))
        self.assertEqual(profile.mismatch_streak, 0)
        self.assertNotIn((electrical,), profile.signature_counts)

    def test_weapon_learning_accepts_attack_damage_after_self_spell(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        profile = script.weapon_profiles["W1"]

        script.on_chat_event(parse_chat_line_event(1, "Starcore-StormReaper [2.0] casts Epic Spell: Hellball"))
        script.on_chat_event(
            parse_chat_line_event(2, "Starcore-StormReaper [2.0] attacks Hamatula : *hit* : (16 + 108 = 124)")
        )
        script.on_chat_event(
            parse_chat_line_event(
                3,
                "Starcore-StormReaper [2.0] damages Hamatula: 119 (68 Physical 0 Fire 20 Positive Energy 31 Sonic)",
            )
        )
        script.on_chat_event(
            parse_chat_line_event(4, "Starcore-StormReaper [2.0] attacks Hamatula : *hit* : (4 + 103 = 107)")
        )
        script.on_chat_event(
            parse_chat_line_event(
                5,
                "Starcore-StormReaper [2.0] damages Hamatula: 138 (72 Physical 0 Fire 20 Positive Energy 46 Sonic)",
            )
        )

        self.assertEqual(profile.stable_signature, (fire, sonic, positive))

    def test_weapon_selection_actual_damage_only_conservatively_nudges_model(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]

        fsp_signature = (fire, sonic, positive)
        cem_signature = (cold, electrical, magical)
        fsp_profile = script.weapon_profiles["W1"]
        fsp_profile.stable_signature = fsp_signature
        fsp_profile.stable_signature_observations = 40
        cem_profile = script.weapon_profiles["W2"]
        cem_profile.stable_signature = cem_signature
        cem_profile.stable_signature_observations = 2

        target_key = script._profile_target_key("Black Slaad")
        observed_map = fsp_profile.target_damage_observations.setdefault(target_key, {})
        for _ in range(40):
            script._apply_observed_damage_map(observed_map, fsp_signature, 500, False)

        candidates = script._weapon_candidates_for_target("Black Slaad")
        by_key = {candidate.binding.key: candidate for candidate in candidates}

        self.assertEqual(by_key["W1"].expected_damage, 59)
        self.assertLess(by_key["W1"].selection_damage, by_key["W2"].selection_damage)
        self.assertEqual(script._choose_best_weapon(candidates).binding.key, "W2")

    def test_weapon_actual_damage_skips_unreliable_resistance_targets(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        signature = (fire, sonic, positive)
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = signature
        profile.stable_signature_observations = 2
        target_key = script._profile_target_key("Black Slaad")
        damage_line = combat.parse_damage_line(
            "Starcore-StormReaper [2.0] damages Black Slaad: "
            "117 (66 Physical 30 Fire 21 Positive Energy 0 Sonic)"
        )

        script._record_profile_target_actual_damage(profile, target_key, signature, damage_line)

        self.assertEqual(script._profile_target_actual_damage(profile, "Black Slaad", signature), (None, 0))

    def test_weapon_actual_damage_records_reliable_target_samples(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]
        signature = (cold, electrical, magical)
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = signature
        profile.stable_signature_observations = 2
        target_key = script._profile_target_key("Black Slaad")
        damage_line = combat.parse_damage_line(
            "Starcore-StormReaper [2.0] damages Black Slaad: "
            "107 (27 Physical 25 Cold 35 Electrical 20 Magical)"
        )

        script._record_profile_target_actual_damage(profile, target_key, signature, damage_line)

        self.assertEqual(script._profile_target_actual_damage(profile, "Black Slaad", signature), (80, 1))

    def test_weapon_static_baseline_uses_calibrated_signature_size_totals(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        acid = hgx_data.DAMAGE_TYPE_NAME_TO_ID["acid"]
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]

        self.assertEqual(script._model_components_for_signature((cold,)), {cold: 60.0})
        self.assertEqual(
            {damage_type: round(amount) for damage_type, amount in script._model_components_for_signature((cold, electrical)).items()},
            {cold: 80, electrical: 80},
        )
        self.assertEqual(
            {damage_type: round(amount) for damage_type, amount in script._model_components_for_signature((cold, electrical, magical)).items()},
            {cold: 77, electrical: 77, magical: 77},
        )
        self.assertEqual(
            {damage_type: round(amount) for damage_type, amount in script._model_components_for_signature((acid, fire, magical, positive)).items()},
            {acid: 58, fire: 58, magical: 58, positive: 58},
        )

    def test_weapon_element_modifier_requires_large_reliable_sample_window(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = (cold, electrical, magical)
        profile.stable_signature_observations = 2

        profile.type_modifier_samples[cold] = [100.0] * 49
        components = script._profile_component_estimates(profile)
        self.assertEqual({round(value) for value in components.values()}, {77})

        profile.type_modifier_samples[cold].append(100.0)
        components = script._profile_component_estimates(profile)

        self.assertEqual(round(components[cold]), 96)
        self.assertEqual(round(components[electrical]), 77)
        self.assertEqual(round(components[magical]), 77)

    def test_weapon_element_modifier_samples_skip_unreliable_targets(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        fire = hgx_data.DAMAGE_TYPE_NAME_TO_ID["fire"]
        sonic = hgx_data.DAMAGE_TYPE_NAME_TO_ID["sonic"]
        positive = hgx_data.DAMAGE_TYPE_NAME_TO_ID["positive"]
        signature = (fire, sonic, positive)
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = signature
        profile.stable_signature_observations = 2
        damage_line = combat.parse_damage_line(
            "Starcore-StormReaper [2.0] damages Black Slaad: "
            "117 (66 Physical 30 Fire 21 Positive Energy 0 Sonic)"
        )

        script._record_profile_type_modifier_observation(profile, damage_line, signature)

        self.assertEqual(profile.type_modifier_samples, {})

    def test_weapon_element_modifier_samples_record_reliable_targets(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        cold = hgx_data.DAMAGE_TYPE_NAME_TO_ID["cold"]
        electrical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["electrical"]
        magical = hgx_data.DAMAGE_TYPE_NAME_TO_ID["magical"]
        signature = (cold, electrical, magical)
        profile = script.weapon_profiles["W1"]
        profile.stable_signature = signature
        profile.stable_signature_observations = 2
        damage_line = combat.parse_damage_line(
            "Starcore-StormReaper [2.0] damages Black Slaad: "
            "107 (27 Physical 25 Cold 35 Electrical 20 Magical)"
        )

        script._record_profile_type_modifier_observation(profile, damage_line, signature)

        self.assertEqual(set(profile.type_modifier_samples), {cold, electrical, magical})
        self.assertEqual({damage_type: len(samples) for damage_type, samples in profile.type_modifier_samples.items()}, {cold: 1, electrical: 1, magical: 1})

    def test_shifter_observed_healing_damage_uses_least_healing_instead_of_unarmed(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "weapon_slot_2": "F2",
                "shift_slot": "F9",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.shifter_shift_state = "shifted"
        script._observed_healing_damage_types = lambda damage_line: (4,)

        def recommendation(binding, healing_score):
            return WeaponRecommendation(
                binding=binding,
                expected_damage=0,
                selection_damage=0,
                actual_damage=None,
                actual_observations=0,
                matched_name="Dummy",
                paragon_ranks=0,
                learned_types=(4,),
                estimated_components=((4, 100),),
                healing_types=(4,),
                ignored_types=(),
                special_name="",
                signature_observations=2,
                estimate_observations=1,
                healing_score=healing_score,
            )

        script._weapon_candidates_for_target = lambda name: [
            recommendation(script.weapon_bindings["W1"], 600),
            recommendation(script.weapon_bindings["W2"], 200),
        ]

        damage = parse_chat_line_event(
            10,
            "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] Starcore-StormReaper [2.0] damages Dummy : 42 (12 cold 30 physical)",
        )
        self.assertTrue(script._shift_away_from_observed_healing_damage(script.weapon_profiles["W1"], damage.damage))

        self.assertEqual(host.chats[:2], ["!lock opponent", "!cancel poly"])
        self.assertEqual(script.shifter_pending_source_key, "W2")
        self.assertFalse(script.pending_weapon_unarm)
        self.assertFalse(script.shifter_pending_unarm)
        self.assertIn("least healing", script.status_text)

    def test_weapon_mode_unarms_after_observed_healing_damage_when_no_safe_weapon_exists(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "current_weapon": "W1",
            },
            host,
        )
        script.on_start()
        script.db = hgx_data.load_character_database()
        script.weapon_profiles["W1"].stable_signature = (4,)
        script.weapon_profiles["W1"].stable_signature_observations = 2
        script.weapon_profiles["W1"].type_estimates[4] = WeaponDamageEstimate(base_estimate=50.0, observations=2)

        damage = parse_chat_line_event(
            10,
            (
                "[CHAT WINDOW TEXT] [Sun Apr 26 12:00:01] "
                "Starcore-StormReaper [2.0] damages Superior Algid Reaver : 42 (12 cold 30 physical)"
            ),
        )
        script.on_chat_event(damage)

        self.assertEqual(host.slots[-1], (0, 1))
        self.assertTrue(script.pending_weapon_unarm)
        self.assertIn("unarm healing (Cold)", script.status_text)

    def test_shifter_shift_ability_can_still_use_shift_or_ctrl_slot(self):
        host = FakeHost()
        script = AutoAAScript(
            host.client,
            {
                "mode": AutoAAScript.MODE_SHIFTER_WEAPON_SWAP,
                "weapon_slot_1": "F1",
                "shift_slot": "S+F9",
            },
            host,
        )

        script.on_start()

        self.assertEqual(script.shifter_shift_page, 1)
        self.assertEqual(script.shifter_shift_slot, 9)


if __name__ == "__main__":
    unittest.main()
