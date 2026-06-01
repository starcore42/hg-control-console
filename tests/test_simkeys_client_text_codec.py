import struct
import unittest

from src.simkeys_app import simKeys_Client as simkeys


NJAL_TRITONE = "Nj\u00e1l Tritone"


class SimKeysClientTextCodecTests(unittest.TestCase):
    def test_decode_cstring_falls_back_to_nwn_single_byte_text(self):
        self.assertEqual(simkeys.decode_cstring(b"Nj\xe1l Tritone\x00ignored"), NJAL_TRITONE)

    def test_decode_cstring_accepts_utf8_text(self):
        self.assertEqual(
            simkeys.decode_cstring(NJAL_TRITONE.encode("utf-8") + b"\x00"),
            NJAL_TRITONE,
        )

    def test_chat_send_encodes_nwn_single_byte_text(self):
        class FakePipe:
            payload = None

            def xfer(self, op, payload=b""):
                self_outer.assertEqual(op, simkeys.OP_CHAT_SEND)
                self.payload = payload
                return op, struct.pack("iiii", 1, 2, 1, 0)

        self_outer = self
        pipe = FakePipe()
        result = simkeys.chat_send(pipe, f'/tell "{NJAL_TRITONE}" !target', 2)

        mode, length = struct.unpack_from("ii", pipe.payload, 0)
        text = pipe.payload[8:]
        self.assertEqual(result["success"], 1)
        self.assertEqual(mode, 2)
        self.assertEqual(length, len(text))
        self.assertEqual(text, b'/tell "Nj\xe1l Tritone" !target')

    def test_chat_poll_decodes_nwn_single_byte_text(self):
        class FakePipe:
            def xfer(self, op, payload=b""):
                self_outer.assertEqual(op, simkeys.OP_CHAT_POLL)
                text = b"Nj\xe1l Tritone: hello"
                data = struct.pack("ii", 42, 1) + struct.pack("ii", 42, len(text)) + text
                return op, data

        self_outer = self
        result = simkeys.chat_poll(FakePipe())

        self.assertEqual(result["latest_seq"], 42)
        self.assertEqual(result["lines"], [{"seq": 42, "text": f"{NJAL_TRITONE}: hello"}])

    def test_overlay_text_encodes_utf8_text(self):
        class FakePipe:
            payload = None

            def xfer(self, op, payload=b""):
                self_outer.assertEqual(op, simkeys.OP_OVERLAY_TEXT)
                self.payload = payload
                return op, struct.pack("iiii", 1, 200, 80, 0)

        self_outer = self
        pipe = FakePipe()
        text = "TIMERS\nB\u00e9hl SAFE 3:36"
        result = simkeys.overlay_show_text(pipe, text, overlay_id=7100, position="TL", font_size=16)

        header_size = simkeys.OVERLAY_TEXT_HEADER.size
        header = simkeys.OVERLAY_TEXT_HEADER.unpack(pipe.payload[:header_size])
        payload = pipe.payload[header_size:]
        self.assertEqual(result["success"], 1)
        self.assertEqual(header[0], 7100)
        self.assertEqual(header[-1], len(payload))
        self.assertEqual(payload, text.encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
