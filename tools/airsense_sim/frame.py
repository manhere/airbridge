"""AirSense 10 qframe codec.

Wire format: [0x55] [type] [len:3 hex-ASCII] [payload, 0x55-escaped] [crc:4 hex-ASCII]

The length counts *all* wire bytes: sync, type, length, escaped payload and the
four CRC characters. CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, MSB first,
no final XOR) covers sync through the escaped payload, not the CRC characters.

Mirrors src/qframe.cpp and src/crc.cpp in the AirBridge firmware.
"""

from __future__ import annotations

from dataclasses import dataclass, field

SYNC = 0x55
MIN_RAW = 9
MAX_RAW = 600
MAX_PAYLOAD = 502
_HEX_BYTES = b"0123456789ABCDEFabcdef"
_IMMEDIATE_TYPES = (ord("O"), ord("P"))


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE, MSB first, no output XOR."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def crc16_reference(data: bytes) -> int:
    """Independent table-driven implementation, used by the selftest."""
    table = []
    for i in range(256):
        value = i << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 else (value << 1) & 0xFFFF
        table.append(value)
    crc = 0xFFFF
    for byte in data:
        crc = ((crc << 8) ^ table[((crc >> 8) ^ byte) & 0xFF]) & 0xFFFF
    return crc


def _is_hex(value: int) -> bool:
    return value in _HEX_BYTES


def escape(payload: bytes) -> bytes:
    """Every 0x55 inside the payload is doubled on the wire."""
    return payload.replace(bytes([SYNC]), bytes([SYNC, SYNC]))


def unescape(wire: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(wire):
        if wire[i] == SYNC and i + 1 < len(wire) and wire[i + 1] == SYNC:
            out.append(SYNC)
            i += 2
        else:
            out.append(wire[i])
            i += 1
    return bytes(out)


@dataclass
class Frame:
    type: str
    payload: bytes = b""
    immediate: bool = False
    crc_ok: bool = True
    raw: bytes = b""

    @property
    def text(self) -> str:
        return self.payload.decode("ascii", "replace")


class CodecError(ValueError):
    pass


def build(frame_type, payload=b"") -> bytes:
    """Build one complete frame. frame_type is a one-character str or an int."""
    if isinstance(frame_type, str):
        if len(frame_type) != 1:
            raise CodecError("frame type must be one byte")
        frame_type = ord(frame_type)
    if isinstance(payload, str):
        payload = payload.encode("ascii")
    payload = bytes(payload)
    if len(payload) > MAX_PAYLOAD:
        raise CodecError("payload too large: %d" % len(payload))
    wire_payload = escape(payload)
    total = MIN_RAW + len(wire_payload)
    if total > MAX_RAW:
        raise CodecError("frame too large: %d" % total)
    head = bytes([SYNC, frame_type]) + ("%03X" % total).encode("ascii") + wire_payload
    return head + ("%04X" % crc16(head)).encode("ascii")


def build_text(frame_type, text: str) -> bytes:
    return build(frame_type, text.encode("ascii"))


def immediate(frame_type: str) -> bytes:
    """Two-byte immediate frame, e.g. O = stored-stream acknowledgement."""
    if frame_type not in ("O", "P"):
        raise CodecError("only O and P have immediate forms")
    return bytes([SYNC, ord(frame_type)])


class Parser:
    """Incremental parser that resynchronises on every 0x55.

    Handles full frames, the two-byte immediate O/P forms, 0x55 escaping and
    bad CRCs. A frame with a bad CRC is still reported (crc_ok=False) so the
    caller can decide whether to log or ignore it.
    """

    def __init__(self):
        self.buf = bytearray()
        self.errors = 0

    def reset(self):
        self.buf.clear()

    def feed(self, data: bytes) -> list:
        self.buf.extend(data)
        frames = []
        for _ in range(100000):
            frame, consumed = self._next()
            if frame is not None:
                frames.append(frame)
            if frame is None and consumed == 0:
                break
        return frames

    def _next(self):
        buf = self.buf
        if not buf:
            return None, 0
        if buf[0] != SYNC:
            index = buf.find(SYNC)
            if index < 0:
                buf.clear()
                return None, 0
            del buf[:index]
            return None, index

        if len(buf) < 2:
            return None, 0
        frame_type = buf[1]

        # Immediate O/P: 0x55 'O' followed by something that cannot be a
        # length digit (very often the next frame's 0x55 sync byte).
        if (frame_type in _IMMEDIATE_TYPES and len(buf) >= 3
                and not _is_hex(buf[2])):
            raw = bytes(buf[:2])
            del buf[:2]
            return Frame(chr(frame_type), b"", immediate=True, raw=raw), 2

        if len(buf) < 5:
            return None, 0
        if not all(_is_hex(c) for c in buf[2:5]):
            del buf[0]
            self.errors += 1
            return None, 1
        total = int(bytes(buf[2:5]).decode("ascii"), 16)
        if total < MIN_RAW or total > MAX_RAW:
            del buf[0]
            self.errors += 1
            return None, 1
        if len(buf) < total:
            return None, 0

        raw = bytes(buf[:total])
        crc_text = raw[-4:]
        if not all(_is_hex(c) for c in crc_text):
            del buf[0]
            self.errors += 1
            return None, 1
        crc_ok = int(crc_text.decode("ascii"), 16) == crc16(raw[:-4])
        frame = Frame(
            type=chr(frame_type),
            payload=unescape(raw[5:-4]),
            raw=raw,
            crc_ok=crc_ok,
        )
        del buf[:total]
        if not crc_ok:
            self.errors += 1
        return frame, total
