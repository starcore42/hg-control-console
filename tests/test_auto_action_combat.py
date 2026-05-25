import time
import unittest
import sys
import os

# Add the project root to sys.path to allow imports from src
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.simkeys_app import simkeys_hgx_combat as combat
from src.simkeys_app.simkeys_script_host import AutoActionScript, parse_chat_line_event

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
        self.recovery_active = False

    def emit(self, level, message, script_id=None):
        self.events.append((level, message, script_id))

    def notify_state_changed(self):
        pass

    def is_shifter_recovery_active(self):
        return self.recovery_active

class TestAutoActionCombat(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()
        self.config = {"mode": "Knockdown", "cooldown_seconds": 1.0}
        self.script = AutoActionScript(self.host.client, self.config, self.host)

    def test_combat_tracking(self):
        self.script.on_start()
        self.script.enabled = True
        
        # Initially no combat
        self.assertEqual(self.script.last_combat_at, 0.0)
        
        # Attack a monster (Aboleth is in data/characters.d/10-public-aboleth.xml)
        event = parse_chat_line_event(1, "PlayerCharacter attacks Aboleth")
        self.script.on_chat_event(event)
        
        self.assertGreater(self.script.last_combat_at, 0.0)
        
        # Reset and test non-monster
        self.script.last_combat_at = 0.0
        # "PlayerCharacter attacks FriendlyNPC" (Assuming FriendlyNPC is not in characters.d)
        event = parse_chat_line_event(2, "PlayerCharacter attacks FriendlyNPC")
        self.script.on_chat_event(event)
        self.assertEqual(self.script.last_combat_at, 0.0)
        
        # Test other character attacking
        # "OtherPlayer attacks Aboleth"
        event = parse_chat_line_event(3, "OtherPlayer attacks Aboleth")
        self.script.on_chat_event(event)
        self.assertEqual(self.script.last_combat_at, 0.0)

    def test_damage_tracking(self):
        self.script.on_start()
        self.script.enabled = True
        
        # Damage a monster
        event = parse_chat_line_event(1, "PlayerCharacter damages Aboleth: 10 (10 physical)")
        self.script.on_chat_event(event)
        
        self.assertGreater(self.script.last_combat_at, 0.0)

if __name__ == '__main__':
    unittest.main()
