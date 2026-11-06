"""Self-contained selftest for the AirSense 10 simulator.

Run: python tools/airsense_sim/selftest.py
No hardware, no third-party packages.
"""

from __future__ import annotations

import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import frame as qf                       # noqa: E402
from device import Device, CSG_RECORDS, SCHEMAS, load_catalog  # noqa: E402
from transport import Loopback           # noqa: E402


def parse_one(wire: bytes) -> qf.Frame:
    frames = qf.Parser().feed(wire)
    assert len(frames) == 1, "expected one frame, got %d" % len(frames)
    return frames[0]


def exchange(device: Device, command: str, frame_type: str = "Q"):
    """Push one command into the device and return the parsed reply frames."""
    incoming = parse_one(qf.build(frame_type, command.encode("ascii")))
    return [parse_one(wire) for wire in device.on_frame(incoming)]


def reply_value(frame: qf.Frame) -> str:
    assert frame.type == "R", "expected R frame, got %s" % frame.type
    assert b" = " in frame.payload, "missing echo separator: %r" % frame.payload
    return frame.text.split(" = ", 1)[1]


class FrameCodecTests(unittest.TestCase):
    def test_crc_standard_vector(self):
        # CRC-16/CCITT-FALSE check value for "123456789".
        self.assertEqual(qf.crc16(b"123456789"), 0x29B1)
        self.assertEqual(qf.crc16_reference(b"123456789"), 0x29B1)

    def test_crc_implementations_agree(self):
        for data in (b"", b"A", b"G S #ROP = 0000", bytes(range(64))):
            self.assertEqual(qf.crc16(data), qf.crc16_reference(data))

    def test_length_counts_escaped_wire_bytes(self):
        payload = b"A\x55B"                     # 3 bytes -> 4 escaped
        wire = qf.build("R", payload)
        declared = int(wire[2:5].decode("ascii"), 16)
        self.assertEqual(declared, len(wire))
        self.assertEqual(declared, qf.MIN_RAW + 4)
        self.assertEqual(int(wire[-4:].decode("ascii"), 16), qf.crc16(wire[:-4]))

    def test_roundtrip_with_escaping(self):
        payload = b"\x55G S #ROP = 0000\x55\x55"
        parsed = parse_one(qf.build("R", payload))
        self.assertEqual(parsed.type, "R")
        self.assertEqual(parsed.payload, payload)
        self.assertTrue(parsed.crc_ok)

    def test_immediate_frames(self):
        parsed = qf.Parser().feed(qf.immediate("O") + qf.immediate("P") + b"X")
        self.assertEqual([f.type for f in parsed], ["O", "P"])
        self.assertTrue(all(f.immediate for f in parsed))

    def test_bad_crc_is_reported(self):
        wire = bytearray(qf.build_text("R", "G S #ROP = 0000"))
        wire[-1] = ord("0") if wire[-1] != ord("0") else ord("1")
        self.assertFalse(parse_one(bytes(wire)).crc_ok)

    def test_resync_after_garbage(self):
        wire = b"\x00\x01\x02" + qf.build_text("Q", "G S #ROP")
        parsed = qf.Parser().feed(wire)
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0].text, "G S #ROP")

    def test_reject_oversize_payload(self):
        with self.assertRaises(qf.CodecError):
            qf.build("R", b"x" * (qf.MAX_PAYLOAD + 1))


class CatalogTests(unittest.TestCase):
    def test_catalog_parsed_from_firmware_header(self):
        variables = load_catalog()
        self.assertIn("IPC", variables)
        self.assertIn("MOP", variables)
        self.assertGreater(len(variables), 40)

    def test_csg_records_shape(self):
        self.assertTrue(all(r.startswith(("V4 ", "V8 ")) for r in CSG_RECORDS))
        # IPC, MPA, MPI (numerics) plus EBE (enum).
        self.assertEqual(len(CSG_RECORDS), 4)
        # Pressures must reach the UI with their unit: the stock catalogue has
        # no unit column, so anything outside the registry renders unitless.
        for record in CSG_RECORDS:
            if record.split()[3] in ("IPC", "MPA", "MPI"):
                self.assertIn("05:cmH2O", record)

    def test_csg_records_reference_matching_variable_types(self):
        # V4 needs a numeric G C #VAR ("F MIN MAX"); V8 needs enum flags ("F").
        variables = load_catalog()
        for record in CSG_RECORDS:
            tokens = record.split()
            name = tokens[3]
            self.assertIn(name, variables, name)
            expected = "num" if tokens[0] == "V4" else "enum"
            self.assertEqual(variables[name].kind, expected,
                             "%s is %s but %s needs %s" %
                             (name, variables[name].kind, tokens[0], expected))


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.dev = Device(scenario="full")

    def test_health_poll(self):
        reply = exchange(self.dev, "G S #ROP")[0]
        self.assertEqual(reply.text, "G S #ROP = 0000")
        # capture_variable() in the firmware reads the value from offset 11.
        self.assertEqual(reply.payload[8:11], b" = ")
        self.assertEqual(reply_value(reply), "0000")

    def test_identity_sequence(self):
        for name in ("STK", "SRN", "MID", "VID", "PNA", "MHR", "BLS", "MOP"):
            reply = exchange(self.dev, "G S #" + name)[0]
            self.assertEqual(reply.type, "R", name)
            self.assertTrue(reply.text.startswith("G S #%s = " % name))

    def test_clock_formats(self):
        dac = reply_value(exchange(self.dev, "G S #DAC")[0])
        tic = reply_value(exchange(self.dev, "G S #TIC")[0])
        self.assertRegex(dac, r"^\d{8}$")
        self.assertRegex(tic, r"^\d{6}$")

    def test_scale_and_readback(self):
        reply = exchange(self.dev, "P S #IPC 00C8")[0]
        self.assertEqual(reply_value(reply), "00C8")
        self.assertEqual(reply_value(exchange(self.dev, "G S #IPC")[0]), "00C8")

    def test_unknown_variable(self):
        reply = exchange(self.dev, "G S #ZZZ")[0]
        self.assertEqual(reply.type, "E")
        self.assertTrue(reply.text.endswith("6006"))

    def test_bad_hex_write(self):
        reply = exchange(self.dev, "P S #IPC ZZ")[0]
        self.assertEqual(reply.type, "E")
        self.assertTrue(reply.text.endswith("6031"))

    def test_capabilities_and_enum_options(self):
        caps = reply_value(exchange(self.dev, "G C #IPC")[0])
        self.assertEqual(caps.split()[0], "7")
        self.assertEqual(len(caps.split()), 3)
        option = reply_value(exchange(self.dev, "G C #EBE 00")[0])
        self.assertEqual(option.split()[1], "1")
        self.assertTrue(option.endswith("Off"))

    def test_csg_header_and_entries(self):
        header = reply_value(exchange(self.dev, "G C &CSG")[0])
        self.assertEqual(header, "02 %02X" % len(CSG_RECORDS))
        for index in range(len(CSG_RECORDS)):
            entry = reply_value(exchange(self.dev, "G C &CSG %02X" % index)[0])
            self.assertEqual(entry, CSG_RECORDS[index])
        missing = exchange(self.dev, "G C &CSG 63")[0]
        self.assertEqual(missing.type, "E")
        self.assertTrue(missing.text.endswith("6033"))

    def test_stream_schema_and_subscription(self):
        schema = reply_value(exchange(self.dev, "G C &TCE")[0])
        self.assertEqual(schema.split()[0], "%02X" % len(SCHEMAS["TCE"]))
        self.assertIn("MKP:03", schema)
        reply = exchange(self.dev, "P S &TCE 1")[0]
        self.assertEqual(reply_value(reply), "1")
        self.assertIn("TCE", self.dev.subscriptions)

    def test_live_l_frame(self):
        exchange(self.dev, "P S &TCE 1")
        wires = self.dev.tick(time.monotonic() + 10)
        self.assertEqual(len(wires), 1)
        parsed = parse_one(wires[0])
        self.assertEqual(parsed.type, "L")
        self.assertTrue(parsed.text.startswith("TCE"))
        expected_len = 5 + sum(width for _, width in SCHEMAS["TCE"])
        self.assertEqual(len(parsed.payload), expected_len)

    def test_stored_value_reply(self):
        reply = exchange(self.dev, "G V #AHI 4E20 0")[0]
        self.assertEqual(reply_value(reply), "FFFF010017FFFF")

    def test_stored_stream_with_immediate_ack(self):
        replies = exchange(self.dev, "G F &ERR")
        self.assertEqual(replies[0].type, "R")
        self.assertEqual(replies[0].text, "G F &ERR")
        self.assertEqual(replies[1].type, "K")
        ack = parse_one(qf.immediate("O") + b"X")
        frames = [parse_one(w) for w in self.dev.on_frame(ack)]
        self.assertEqual(frames[0].type, "K")

    def test_stored_stream_auto_continue_without_ack(self):
        # AirBridge never sends immediate O; the simulator must not stall.
        exchange(self.dev, "G F &ERR")
        frames = [parse_one(w) for w in self.dev.tick(time.monotonic() + 10)]
        self.assertEqual(frames[0].type, "K")
        self.assertGreaterEqual(self.dev.gf_auto_acks, 1)

    def test_oxh_injection_is_not_answered(self):
        payload = b"OXH0181064801010"  # OXH + seq + OXS + HRR + SAS + SAR + 10
        incoming = parse_one(qf.build("L", payload))
        self.assertEqual(self.dev.on_frame(incoming), [])
        self.assertEqual(self.dev.oxh_count, 1)

    def test_bootloader_minimum(self):
        self.assertEqual(reply_value(exchange(self.dev, "G S #BLS")[0]), "0000")
        self.assertEqual(reply_value(exchange(self.dev, "P S #BLL 0001")[0]), "0001")
        dump = qf.build("O", b"D" + (0x100).to_bytes(4, "little") + (8).to_bytes(2, "little"))
        out = self.dev.on_frame(parse_one(dump))
        self.assertEqual(len(out), 1)
        self.assertTrue(parse_one(out[0]).payload.startswith(b"d"))

    def test_flash_frame_is_accepted_silently(self):
        incoming = parse_one(qf.build("f", b"CDX\x00\x01\x03\x00\x00\x00\x10A\x00"))
        self.assertEqual(self.dev.on_frame(incoming), [])
        self.assertEqual(self.dev.flash_count, 1)

    def test_fault_injections(self):
        self.assertEqual(exchange(Device(faults={"rop": "timeout"}), "G S #ROP"), [])
        csg = exchange(Device(faults={"csg": "6009"}), "G C &CSG")[0]
        self.assertEqual(csg.type, "E")
        missing = exchange(Device(faults={"var.MOP": "6006"}), "G S #MOP")[0]
        self.assertEqual(missing.type, "E")
        self.assertEqual(reply_value(exchange(Device(faults={"csg-empty": "1"}),
                                              "G C &CSG")[0]), "02 00")

    def test_scenario_gating(self):
        basic = Device(scenario="basic")
        self.assertEqual(exchange(basic, "G C &CSG")[0].type, "E")
        self.assertEqual(exchange(basic, "G C &TCE")[0].type, "R")
        self.assertEqual(exchange(basic, "G V #AHI 4E20 0")[0].type, "E")


class EndToEndTests(unittest.TestCase):
    def test_loopback_roundtrip(self):
        link = Loopback()
        device = Device()
        parser = qf.Parser()
        link.host_write(qf.build_text("Q", "G S #ROP"))
        for incoming in parser.feed(link.read()):
            for wire in device.on_frame(incoming):
                link.write(wire)
        host_frames = qf.Parser().feed(link.host_read())
        self.assertEqual(len(host_frames), 1)
        self.assertEqual(host_frames[0].text, "G S #ROP = 0000")
        self.assertTrue(host_frames[0].crc_ok)

    def test_multiple_commands_on_one_stream(self):
        link = Loopback()
        device = Device()
        parser = qf.Parser()
        for command in ("G S #ROP", "G S #SRN", "P S #IPC 00C8", "G C &CSG"):
            link.host_write(qf.build_text("Q", command))
        for incoming in parser.feed(link.read()):
            for wire in device.on_frame(incoming):
                link.write(wire)
        host_frames = qf.Parser().feed(link.host_read())
        self.assertEqual(len(host_frames), 4)
        self.assertTrue(all(f.crc_ok for f in host_frames))
        self.assertEqual(host_frames[3].text, "G C &CSG = 02 04")

    def test_firmware_startup_sequence_on_one_stream(self):
        """All frames AirBridge sends during poll + discovery + setup, at once."""
        commands = [
            "G S #ROP", "G S #ROP", "G S #ROP",
            "G S #STK", "G S #SRN", "G S #MID", "G S #VID",
            "G S #PNA", "G S #MOP", "G S #MHR", "G S #BLS",
            "G S #LAN", "G C &CSG",
            "G C &CSG 00", "G C &CSG 01", "G C &CSG 02",
            "G C #IPC", "G S #IPC", "G C #EBE", "G S #EBE", "G C #EBE 00",
            "G S #MOP", "G S #TMU", "G S #AOA", "G C #ALR",
            "G S #DAC", "G S #TIC", "P S #DAC 06112026", "P S #TIC 193000",
            "P S &TCE 1", "G C &TCE", "G V #AHI 4E20 0",
        ]
        link = Loopback()
        device = Device(scenario="full")
        parser = qf.Parser()
        stream = b"".join(qf.build_text("Q", c) for c in commands)
        stream += qf.build("L", b"OXH0181064801010")  # oximetry injection
        link.host_write(stream)
        for incoming in parser.feed(link.read()):
            for wire in device.on_frame(incoming):
                link.write(wire)
        replies = qf.Parser().feed(link.host_read())
        self.assertEqual(len(replies), len(commands))
        self.assertTrue(all(f.type == "R" for f in replies))
        self.assertTrue(all(f.crc_ok for f in replies))
        self.assertEqual(device.commands, len(commands))
        self.assertEqual(device.oxh_count, 1)
        self.assertIn("TCE", device.subscriptions)


class TransportTests(unittest.TestCase):
    def test_invalid_serial_port_fails_cleanly(self):
        if sys.platform != "win32":
            self.skipTest("Win32 COM API only")
        from transport import SerialPort
        with self.assertRaises(OSError):
            SerialPort("COM_DOES_NOT_EXIST_9999")


if __name__ == "__main__":
    unittest.main(verbosity=2)
