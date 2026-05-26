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

import argparse, struct, time
import ctypes as C
import ctypes.wintypes as W

k32 = C.WinDLL("kernel32", use_last_error=True)
INVALID_HANDLE_VALUE = C.c_void_p(-1).value
CHAR_NAME_CAPACITY = 128
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
        err = C.get_last_error()
    message = C.FormatError(err).strip() if err else "no last-error information"
    return f"{prefix} (err={err}: {message})"

class Pipe:
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

OP_QUERY=3000; OP_SLOT=3001; OP_VK=3002; OP_SETLOG=3003; OP_REPLAY=3004; OP_SNAPSHOT=3005; OP_CHAT_SEND=3006; OP_CHAT_POLL=3007; OP_SLOT_PAGE=3008; OP_OVERLAY_TEXT=3009; OP_OVERLAY_CLEAR=3010; OP_OVERLAY_CLEAR_ALL=3011; OP_MOVE_TO_LOCATION=3012; OP_SET_WALK_BYPASS=3013
QUERY_STRUCT_LEGACY = struct.Struct("<" + ("I" * 24) + ("i" * 10) + "I" + ("i" * 2) + ("I" * 4) + f"{CHAR_NAME_CAPACITY}s")
QUERY_STRUCT = struct.Struct("<" + ("I" * 24) + ("i" * 10) + "I" + ("i" * 2) + ("I" * 4) + "ifff" + f"{CHAR_NAME_CAPACITY}s")
OVERLAY_TEXT_HEADER = struct.Struct("<iiiiiIi")
OVERLAY_RESPONSE = struct.Struct("<iiii")
MOVE_TO_LOCATION_REQUEST = struct.Struct("<fffiIi")
MOVE_TO_LOCATION_RESPONSE = struct.Struct("<iiifff")
WALK_BYPASS_RESPONSE = struct.Struct("<iii")
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

def decode_cstring(b):
    return b.split(b"\x00", 1)[0].decode("utf-8", errors="replace")

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
    if len(data) == QUERY_STRUCT.size:
        unpacked = QUERY_STRUCT.unpack(data)
    elif len(data) == QUERY_STRUCT_LEGACY.size:
        legacy = QUERY_STRUCT_LEGACY.unpack(data)
        unpacked = legacy[:-1] + (0, 0.0, 0.0, 0.0, legacy[-1])
    else:
        raise RuntimeError(
            f"unexpected query payload size: got {len(data)}, expected {QUERY_STRUCT.size}"
        )
    (module_base, hook_proc, hwnd, current_proc, original_proc, main_tid, installed,
     expected_wndproc, expected_pre_dispatch, expected_dispatch_thunk, expected_dispatch_slot0,
     app_global_slot, app_holder, app_object, app_inner, dispatcher_ptr, gate90, gate94, gate98,
     quickbar_exec, quickbar_slot_dispatch, quickbar_panel_vtable, quickbar_slot_ptr, quickbar_this,
     quickbar_page, quickbar_slot, quickbar_slot_type, quickbar_calls, quickbar_scan_attempts, quickbar_scan_hits,
     last_vk, last_rc, last_error, log_level, player_object, identity_refresh_count, identity_error,
     quickbar_item_mask_low, quickbar_item_mask_high, quickbar_equipped_mask_low, quickbar_equipped_mask_high,
     position_valid, position_x, position_y, position_z,
     character_name_raw) = unpacked
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
        "character_name": decode_cstring(character_name_raw),
    }

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
    print(f"identity: player={phex(result['player_object'])} name={result['character_name'] or '<unknown>'} refreshes={result['identity_refresh_count']} err={result['identity_error']}{position_text}")
    print(f"last: vk={phex(result['last_vk'])} rc={result['last_rc']} err={result['last_error']}")
    print()
    cmd_snapshot(p)

def cmd_snapshot(p):
    _, data = p.xfer(OP_SNAPSHOT)
    text = data.decode("utf-8", errors="replace")
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
    payload = text.encode("utf-8", errors="replace")
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
        text = data[offset:offset + text_len].decode("utf-8", errors="replace")
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
    finally:
        p.close()
