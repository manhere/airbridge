"""PC-side AirSense 10 simulator for AirBridge testing.

Examples:
  python tools/airsense_sim/sim.py --port COM3 --scenario full -v
  python tools/airsense_sim/sim.py --port /dev/ttyUSB0 --scenario clinical
  python tools/airsense_sim/sim.py --tcp 127.0.0.1:5760 --scenario live
  python tools/airsense_sim/selftest.py

Wiring (env:esp32s3-devkitc-n16r8-nosd):
  USB-UART TX -> ESP32 GPIO44 (AB_AS10_RX_GPIO)
  USB-UART RX -> ESP32 GPIO43 (AB_AS10_TX_GPIO)
  GND common, 3.3 V logic, 57600 8N1. Do not use 5 V TX.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import frame as qf                     # noqa: E402
from device import Device              # noqa: E402
from transport import SerialPort, TcpServer  # noqa: E402

SCENARIOS = ("basic", "clinical", "live", "stored", "full")


def parse_faults(items):
    faults = {}
    for item in items:
        if "=" not in item:
            raise SystemExit("bad --fault %r, expected KEY=VALUE" % item)
        key, value = item.split("=", 1)
        faults[key.strip()] = value.strip()
    return faults


def corrupt_crc(wire: bytes) -> bytes:
    """Flip the last CRC hex character to exercise the host CRC check."""
    if not wire:
        return wire
    last = wire[-1]
    replacement = ord("0") if last != ord("0") else ord("1")
    return wire[:-1] + bytes([replacement])


def drain_log(device, verbose: bool):
    if not device.log:
        return
    if verbose:
        for kind, text in device.log:
            stamp = time.strftime("%H:%M:%S")
            print("[%s] %-2s %s" % (stamp, kind, text), flush=True)
    device.log.clear()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Simulate an AirSense 10 on the AirBridge service UART.")
    ap.add_argument("--port", help="serial port, e.g. COM3 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--tcp", metavar="HOST:PORT",
                    help="listen on TCP instead of a serial port")
    ap.add_argument("--scenario", choices=SCENARIOS, default="full",
                    help="which feature groups are enabled (default: full)")
    ap.add_argument("--rop", choices=("0000", "0001", "0004"), default="0000",
                    help="reported therapy state: 0000 standby, 0001 therapy, 0004 calibration")
    ap.add_argument("--stream-period", type=float, default=0.2,
                    help="seconds between live L frames per subscribed tag")
    ap.add_argument("--catalog", help="path to src/settings_defs.h")
    ap.add_argument("--fault", action="append", default=[], metavar="KEY=VALUE",
                    help="inject a fault, repeatable. Keys: rop=timeout, "
                         "csg=6009, csg-empty=1, gv=6009, enum-hole=1, "
                         "crc=bad, var.XXX=6006, var.*=6009")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every received command and sent frame")
    args = ap.parse_args()

    if not args.port and not args.tcp:
        ap.error("one of --port or --tcp is required")

    faults = parse_faults(args.fault)
    break_crc = faults.pop("crc", None) == "bad"

    device = Device(
        scenario=args.scenario,
        faults=faults,
        catalog_path=args.catalog,
        stream_period=args.stream_period,
    )
    device.rop = args.rop

    if args.tcp:
        host, _, port = args.tcp.rpartition(":")
        transport = TcpServer(host or "0.0.0.0", int(port))
        link = "tcp://%s" % args.tcp
    else:
        try:
            transport = SerialPort(args.port, args.baud)
        except OSError as exc:
            print("cannot open %s: %s" % (args.port, exc), file=sys.stderr)
            return 1
        link = "%s @ %d 8N1" % (args.port, args.baud)

    parser = qf.Parser()
    print("AirSense 10 simulator | %s | scenario=%s | rop=%s" %
          (link, args.scenario, args.rop))
    print("Press Ctrl+C to stop and print counters.")

    def emit(wire: bytes):
        drain_log(device, args.verbose)
        if break_crc:
            wire = corrupt_crc(wire)
        transport.write(wire)

    try:
        while True:
            data = transport.read(4096)
            if data:
                for incoming in parser.feed(data):
                    for wire in device.on_frame(incoming):
                        emit(wire)
            for wire in device.tick():
                emit(wire)
            drain_log(device, args.verbose)
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        drain_log(device, args.verbose)
        transport.close()
        print("\nCounters:")
        for key, value in device.summary().items():
            print("  %-16s %s" % (key, value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
