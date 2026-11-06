"""Simulated AirSense 10 device: state, command dispatch and frame generation.

The command/response grammar follows the supplied "Serial Protocol" document,
but the implemented subset is driven by what the AirBridge firmware actually
sends (see src/uart_arbiter.cpp, airsense_state.cpp, custom_settings.cpp,
air10_stored_uart.cpp, air10_clock.cpp, oxi_arbiter.cpp).

Key firmware expectations:
  * A Q command must be answered with an R frame whose payload is
    "<command> = <value>". capture_variable() checks payload[8:11] == " = "
    and reads the value from offset 11, so "G S #XXX" must be exactly 8 chars.
  * E frames may be "<command> = CODE" or a bare "CODE".
  * G V replies must be "FFFF" + NN(2 hex) + NN*4 hex samples + "FFFF".
  * Health polling is "G S #ROP" every 5 s; valid values are 0000/0001/0004.
  * Custom settings discovery: G S #LAN, G C &CSG, G C &CSG II, G C #VAR,
    G S #VAR, G C #VAR II.
  * Live streams: P S &TAG 1/0 plus G C &TAG schema, then L frames.
  * The firmware never sends the two-byte immediate O/P forms; the simulator
    still implements them so it can be used with a protocol-accurate host.
"""

from __future__ import annotations

import datetime
import os
import re
import time
from dataclasses import dataclass, field

import frame as qf

# ---------------------------------------------------------------------------
# Variable catalogue
# ---------------------------------------------------------------------------

_CATALOG_ROW = re.compile(
    r'\{\s*"([A-Z0-9]{3})"\s*,\s*"([^"]*)"\s*,\s*SET_(\w+)\s*,\s*'
    r'(-?\d+)\s*,\s*(\d+)\s*,\s*(NULL|"[^"]*")\s*\}'
)

# Fallback when src/settings_defs.h is not reachable (e.g. tool copied out).
_FALLBACK_VARS = {
    "IPC": ("Set Pressure", "SCALED", "Off,On"),
    "MOP": ("Therapy Mode", "ENUM", "CPAP,AutoSet,APAP,S,ST,T,VAuto,ASV,ASVAuto,iVAPS,PAC,AutoSet Her"),
    "EBE": ("Easy-Breathe", "ENUM", "Off,On"),
    "VTS": ("Trigger Sens.", "ENUM", "Very Low,Low,Med,High,Very High"),
    "VCS": ("Cycle Sens.", "ENUM", "Very Low,Low,Med,High,Very High"),
    "MSK": ("Mask Type", "ENUM", "Pillows,Full Face,Nasal,Pediatric"),
    "RMA": ("Ramp", "ENUM", "Off,On,Auto"),
    "SST": ("SmartStart", "ENUM", "Off,On"),
}


@dataclass
class Variable:
    name: str
    label: str
    kind: str = "num"          # num | enum | text
    value: int = 100
    minimum: int = 0
    maximum: int = 1000
    width: int = 4
    options: list = field(default_factory=list)
    text: str = ""

    def wire_value(self) -> str:
        if self.kind == "text":
            return self.text
        if self.kind == "enum":
            return "%0*X" % (self.width, self.value)
        return "%0*X" % (self.width, self.value)

    def capabilities(self) -> str:
        if self.kind == "enum":
            return "7"                       # ACT | VIS | EDT
        return "7 %0*X %0*X" % (self.width, self.minimum, self.width, self.maximum)


def _find_catalog() -> str | None:
    here = os.path.dirname(os.path.abspath(__file__))
    for _ in range(5):
        candidate = os.path.join(here, "src", "settings_defs.h")
        if os.path.isfile(candidate):
            return candidate
        here = os.path.dirname(here)
    return None


def load_catalog(path: str | None = None) -> dict:
    """Parse VAR_CATALOG from src/settings_defs.h; no duplicate table here."""
    path = path or _find_catalog()
    variables = {}
    if path and os.path.isfile(path):
        with open(path, "r", encoding="utf-8", errors="replace") as source:
            text = source.read()
        for name, label, kind, _scale, _decimals, options in _CATALOG_ROW.findall(text):
            if kind == "ENUM":
                labels = [x for x in options.strip('"').split(",") if x]
                variables[name] = Variable(
                    name, label, "enum", value=0, maximum=max(0, len(labels) - 1),
                    width=2, options=labels,
                )
            else:
                variables[name] = Variable(
                    name, label, "num", value=100, minimum=0, maximum=1000, width=4,
                )
    else:
        for name, (label, kind, options) in _FALLBACK_VARS.items():
            labels = options.split(",")
            variables[name] = Variable(
                name, label, "enum" if kind == "ENUM" else "num",
                value=0 if kind == "ENUM" else 100,
                maximum=len(labels) - 1 if kind == "ENUM" else 1000,
                width=2 if kind == "ENUM" else 4,
                options=labels if kind == "ENUM" else [],
            )
    return variables


# Live-stream layouts, matching fallback_schema() in src/air10_stream.cpp.
SCHEMAS = {
    "TCE": [("MKP", 3), ("RFL", 3), ("LYK", 2)],
    "APN": [("AET", 4), ("DUR", 2)],
    "CSN": [("CET", 5), ("CSR", 4)],
    "PBT": [("MV5", 2), ("RRR", 2), ("LKF", 2), ("TIP", 3), ("TEP", 3)],
    "BRH": [("TID", 2), ("ATP", 3)],
}

STORED_TAGS = ("ABR", "TXC", "TXH", "TXE", "TXW", "TRR", "DLL", "ERR", "ELI", "ZRL")

# Custom-settings registry records (protocol version 02).
CSG_RECORDS = (
    "V4 04 00000FFF IPC 0032 0001 01 05:cmH2O 01 03:Set Set Pressure",
    "V4 00 00000FFF MPA 0001 0001 00 00: 00 Max Pressure",
    "V8 02 00000FFF EBE 01 07:Comfort Easy-Breathe",
)


# ---------------------------------------------------------------------------
# Device
# ---------------------------------------------------------------------------

class Device:
    def __init__(self, scenario: str = "full", faults: dict | None = None,
                 catalog_path: str | None = None, stream_period: float = 0.2):
        self.scenario = scenario
        self.faults = faults or {}
        self.vars = load_catalog(catalog_path)
        self.stream_period = stream_period

        self.rop = "0000"
        self.identity = {
            "SRN": "23123456789",
            "PNA": "AirSense 10 AutoSet",
            "MID": "0028",
            "VID": "0402",
            "STK": "00000001",
            "MHR": "04D2",
            "BLS": "0000",
            "SSD": "%04X" % self._therapy_day(),
            "ZEN": "0001",
            "DSD": "%04X" % self._therapy_day(),
            "SEP": "0000",
            "LSD": "%04X" % self._therapy_day(),
            "ZRM": "0000",
            "MST": "0000",
            "LAN": "0000",
            "BID": "SX567-0402",
            "SID": "SX567-0402",
            "CID": "SX567-0402",
            "PST": "0",
            "BLE": "0000",
            "BDD": "0000",
        }
        self.clock_text = {"DAC": None, "TIC": None}

        self.subscriptions = {}
        self.stream_seq = {}
        self.next_stream_at = {}
        self.gf_queue = []
        self.gf_waiting = False
        self.gf_sent_at = 0.0
        self.gf_auto_acks = 0
        self.oxh_count = 0
        self.flash_count = 0
        self.commands = 0
        self.responses = 0
        self.parser_errors = 0
        self.log = []

    # ---- helpers -----------------------------------------------------
    @staticmethod
    def _therapy_day() -> int:
        return (datetime.date.today() - datetime.date(1970, 1, 1)).days

    def _clock(self):
        now = datetime.datetime.now()
        dac = self.clock_text["DAC"] or "%02d%02d%04d" % (now.day, now.month, now.year)
        tic = self.clock_text["TIC"] or "%02d%02d%02d" % (now.hour, now.minute, now.second)
        return dac, tic

    def _reply(self, command: str, value: str = None) -> bytes:
        payload = command if value is None else "%s = %s" % (command, value)
        self.responses += 1
        self.log.append(("R", payload))
        return qf.build_text("R", payload)

    def _error(self, command: str, code: str) -> bytes:
        self.responses += 1
        payload = "%s = %s" % (command, code) if command else code
        self.log.append(("E", payload))
        return qf.build_text("E", payload)

    def _fault_for(self, name: str) -> str | None:
        return self.faults.get("var." + name) or self.faults.get("var.*")

    # ---- frame input -------------------------------------------------
    def on_frame(self, f: qf.Frame) -> list:
        """Return a list of wire frames to send back."""
        if not f.crc_ok:
            self.parser_errors += 1
            self.log.append(("!", "bad CRC"))
            return []

        if f.immediate:
            if f.type == "P":
                self.gf_queue.clear()
                self.gf_waiting = False
                self.log.append(("rx", "immediate P (parser reset)"))
                return []
            if f.type == "O":
                self.log.append(("rx", "immediate O (stored stream ack)"))
                return self._gf_advance()
            return []

        if f.type == "Q":
            command = f.text.strip()
            self.commands += 1
            self.log.append(("Q", command))
            return self.on_command(command)

        if f.type == "L":
            # Oximetry injection from AirBridge: OXH + seq + OXS + HRR + SAS + SAR + "10"
            self.oxh_count += 1
            self.log.append(("L", f.text))
            return []

        if f.type == "f":
            self.flash_count += 1
            return []

        if f.type == "O":
            return self._bootloader_dump(f.payload)

        return []

    # ---- command dispatch -------------------------------------------
    def on_command(self, command: str) -> list:
        # The CDX dispatcher upper-cases the first eight characters; bytes
        # after the prefix keep their original spelling.
        cmd = (command[:8].upper() + command[8:]
               if len(command) >= 8 else command.upper())

        if cmd.startswith("G S #"):
            return self._cmd_get(cmd)
        if cmd.startswith("P S #"):
            return self._cmd_set(cmd)
        if cmd.startswith("G C #"):
            return self._cmd_caps(cmd)
        if cmd.startswith("G C &CSG"):
            return self._cmd_csg(cmd)
        if cmd.startswith("G C &"):
            return self._cmd_schema(cmd)
        if cmd.startswith("P S &"):
            return self._cmd_subscribe(cmd)
        if cmd.startswith("G F &"):
            return self._cmd_stored_stream(cmd)
        if cmd.startswith("G V #"):
            return self._cmd_stored_value(cmd)
        if cmd.startswith("P F *"):
            return self._cmd_flash_select(cmd)
        return [self._error(cmd, "600E")]

    # G S #VAR ---------------------------------------------------------
    def _cmd_get(self, command: str) -> list:
        match = re.fullmatch(r"G S #([A-Z0-9]{3})", command)
        if not match:
            return [self._error(command, "6008")]
        name = match.group(1)
        fault = self._fault_for(name)
        if fault == "timeout":
            return []
        if fault:
            return [self._error(command, fault)]

        if name == "ROP":
            if self.faults.get("rop") == "timeout":
                return []
            return [self._reply(command, self.rop)]
        if name in ("DAC", "TIC"):
            dac, tic = self._clock()
            return [self._reply(command, dac if name == "DAC" else tic)]
        if name == "AOA":
            return [self._reply(command, "0000")]
        if name == "TMU":
            return [self._reply(command, "0000")]
        if name in self.identity:
            return [self._reply(command, self.identity[name])]
        if name in self.vars:
            return [self._reply(command, self.vars[name].wire_value())]
        return [self._error(command, "6006")]

    # P S #VAR VALUE ---------------------------------------------------
    def _cmd_set(self, command: str) -> list:
        match = re.fullmatch(r"P S #([A-Z0-9]{3}) (\S+)", command)
        if not match:
            return [self._error(command, "600E")]
        name, text = match.group(1), match.group(2)
        fault = self._fault_for(name)
        if fault:
            return [self._error(command, fault)]

        # Bootloader / LCD / report selector writes have no backing variable;
        # the real device simply echoes the accepted value.
        if name in ("RES", "BLL", "BDD", "PIP", "LCA", "LCT", "SEP", "SET", "ROP"):
            if name == "ROP":
                self.rop = text
                return [self._reply(command, self.rop)]
            return [self._reply(command, text)]
        if name in ("DAC", "TIC"):
            if not text.isdigit() or len(text) != (8 if name == "DAC" else 6):
                return [self._error(command, "6031")]
            self.clock_text[name] = text
            return [self._reply(command, text)]
        if name in self.identity:
            self.identity[name] = text
            return [self._reply(command, text)]
        if name in self.vars:
            var = self.vars[name]
            try:
                value = int(text, 16)
            except ValueError:
                return [self._error(command, "6031")]
            if var.kind == "enum" and value > var.maximum:
                return [self._error(command, "6033")]
            var.value = value
            return [self._reply(command, var.wire_value())]
        return [self._error(command, "6006")]

    # G C #VAR / G C #VAR II -------------------------------------------
    def _cmd_caps(self, command: str) -> list:
        match = re.fullmatch(r"G C #([A-Z0-9]{3})(?: ([0-9A-Fa-f]{2}))?", command)
        if not match:
            return [self._error(command, "600E")]
        name, index = match.group(1), match.group(2)
        if name == "ALR":
            return [self._reply(command, "7")]
        var = self.vars.get(name)
        if var is None:
            return [self._error(command, "6006")]
        if index is None:
            return [self._reply(command, var.capabilities())]
        idx = int(index, 16)
        if self.faults.get("enum-hole") and idx == 2:
            return [self._error(command, "6033")]
        if var.kind != "enum" or idx > var.maximum:
            return [self._error(command, "6033")]
        return [self._reply(command, "%02X 1 %s" % (len(var.options), var.options[idx]))]

    # G C &CSG / G C &CSG II ------------------------------------------
    def _cmd_csg(self, command: str) -> list:
        if self.scenario not in ("clinical", "full"):
            return [self._error(command, "6009")]
        if self.faults.get("csg"):
            return [self._error(command, self.faults["csg"])]
        if self.faults.get("csg-empty"):
            return [self._reply(command, "02 00")]
        if re.fullmatch(r"G C &CSG", command):
            return [self._reply(command, "02 %02X" % len(CSG_RECORDS))]
        match = re.fullmatch(r"G C &CSG ([0-9A-Fa-f]{2})", command)
        if not match:
            return [self._error(command, "600E")]
        idx = int(match.group(1), 16)
        if idx >= len(CSG_RECORDS):
            return [self._error(command, "6033")]
        return [self._reply(command, CSG_RECORDS[idx])]

    # G C &TAG ---------------------------------------------------------
    def _cmd_schema(self, command: str) -> list:
        match = re.fullmatch(r"G C &([A-Z0-9]{3})", command)
        if not match:
            return [self._error(command, "6009")]
        tag = match.group(1)
        fields = SCHEMAS.get(tag)
        if fields is None:
            return [self._error(command, "6009")]
        body = " ".join("%s:%02X" % (name, width) for name, width in fields)
        return [self._reply(command, "%02X %s" % (len(fields), body))]

    # P S &TAG 0/1 -----------------------------------------------------
    def _cmd_subscribe(self, command: str) -> list:
        match = re.fullmatch(r"P S &([A-Z0-9]{3}) ([01])", command)
        if not match:
            return [self._error(command, "600E")]
        tag, on = match.group(1), match.group(2) == "1"
        if tag not in SCHEMAS:
            return [self._error(command, "6009")]
        if self.scenario not in ("live", "full"):
            return [self._error(command, "6009")]
        if on:
            self.subscriptions[tag] = True
            self.stream_seq[tag] = 0
            self.next_stream_at[tag] = time.monotonic()
        else:
            self.subscriptions.pop(tag, None)
            self.next_stream_at.pop(tag, None)
        return [self._reply(command, "1" if on else "0")]

    # G V #VAR DDDD C --------------------------------------------------
    def _cmd_stored_value(self, command: str) -> list:
        if self.scenario not in ("stored", "full"):
            return [self._error(command, "6009")]
        if self.faults.get("gv"):
            return [self._error(command, self.faults["gv"])]
        match = re.fullmatch(r"G V #([A-Z0-9]{3}) ([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{1,4})", command)
        if not match:
            return [self._error(command, "6034")]
        name = match.group(1)
        # Day report fields carry exactly one sample per day (sleep_report.cpp
        # rejects sample_count != 1); ONT/OFT hold per-day therapy sessions and
        # legitimately return several.
        if name in ("ONT", "OFT"):
            samples = [8, 32]           # minutes of two therapy sessions
        else:
            samples = [23] if name == "AHI" else [10]
        value = "FFFF%02X%sFFFF" % (
            len(samples), "".join("%04X" % s for s in samples)
        )
        return [self._reply(command, value)]

    # G F &TAG [arg] ---------------------------------------------------
    def _cmd_stored_stream(self, command: str) -> list:
        if self.scenario not in ("stored", "full"):
            return [self._error(command, "6009")]
        match = re.fullmatch(r"G F &([A-Z0-9]{3})(?: ([0-9A-Fa-f]{1,4}))?", command)
        if not match:
            return [self._error(command, "600E")]
        tag, arg = match.group(1), match.group(2)
        if tag not in STORED_TAGS:
            return [self._error(command, "6009")]
        if arg == "0000":
            self.gf_queue.clear()
            self.gf_waiting = False
            return [self._reply(command)]
        day = "%04X" % self._therapy_day()
        count = 2
        payloads = [
            "%s%05X%s00000A1B2C3D" % (tag, i + 1, day) for i in range(count)
        ]
        payloads.append("%s99999FFFF0%04X" % (tag, count))
        self.gf_queue = payloads
        self.gf_waiting = False
        out = [self._reply(command)]       # immediate R echoes the request only
        out.extend(self._gf_advance())
        return out

    def _gf_advance(self) -> list:
        self.gf_waiting = False
        if not self.gf_queue:
            return []
        payload = self.gf_queue.pop(0)
        self.responses += 1
        self.log.append(("K", payload))
        if self.gf_queue:
            self.gf_waiting = True
            self.gf_sent_at = time.monotonic()
        return [qf.build_text("K", payload)]

    # Bootloader -------------------------------------------------------
    def _cmd_flash_select(self, command: str) -> list:
        self.log.append(("PF", command))
        rest = command.split("*", 1)[1].split() if "*" in command else []
        block = rest[0] if rest else ""
        return [self._reply("P F *" + block if block else command)]

    def _bootloader_dump(self, payload: bytes) -> list:
        if len(payload) < 7 or payload[0] != ord("D"):
            return []
        offset = int.from_bytes(payload[1:5], "little")
        length = int.from_bytes(payload[5:7], "little")
        data = bytes((offset + i) & 0xFF for i in range(min(length, 240)))
        reply = b"d" + payload[1:7] + data
        self.responses += 1
        return [qf.build("O", reply)]

    # ---- periodic output ---------------------------------------------
    def tick(self, now: float = None) -> list:
        now = time.monotonic() if now is None else now
        out = []

        # Stored-stream acknowledgement: the AirBridge firmware never sends the
        # immediate O frame, so continue automatically after the firmware's
        # 350 ms interframe idle timeout instead of stalling the transfer.
        if self.gf_waiting and now - self.gf_sent_at >= 0.35:
            self.gf_auto_acks += 1
            out.extend(self._gf_advance())

        # Live L frames for subscribed channels.
        for tag in list(self.subscriptions):
            if now < self.next_stream_at.get(tag, 0):
                continue
            fields = SCHEMAS[tag]
            seq = self.stream_seq.get(tag, 0)
            values = []
            for i, (_name, width) in enumerate(fields):
                value = (seq * 7 + i * 13 + 1) % (16 ** min(width, 6))
                values.append("%0*X" % (width, value))
            payload = "%s%02X%s" % (tag, seq & 0xFF, "".join(values))
            out.append(qf.build_text("L", payload))
            self.responses += 1
            self.stream_seq[tag] = (seq + 1) & 0xFF
            self.next_stream_at[tag] = now + self.stream_period
        return out

    def summary(self) -> dict:
        return {
            "commands": self.commands,
            "responses": self.responses,
            "oxh_frames": self.oxh_count,
            "flash_frames": self.flash_count,
            "subscriptions": sorted(self.subscriptions),
            "gf_auto_acks": self.gf_auto_acks,
            "parser_errors": self.parser_errors,
        }
