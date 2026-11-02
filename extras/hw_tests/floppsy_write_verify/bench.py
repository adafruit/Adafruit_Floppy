#!/usr/bin/env python3
"""Run the Floppsy engineering sketch, retaining files and a complete serial log.

Install pyserial. This script never formats a host disk. Destructive commands
require --destructive and affect the floppy connected to Floppsy. Native .flux24
files store one unsigned 24 MHz interval per byte; the adjacent JSON records
index offset, sector validity and checksums. They are not SCP files.
"""
import argparse
import hashlib
import json
import re
import sys
import time
from pathlib import Path

import serial
from serial.tools import list_ports


def report(message):
    print(message, end="\n\r", flush=True)


def scp_write_data(path, cylinder, head):
    """Use Greaseweazle's SCP reader; preserve intervals, never decode sectors."""
    from greaseweazle.image.scp import SCP

    track = SCP.from_file(str(path), None, {}).get_track(cylinder, head)
    if track is None:
        raise ValueError("SCP has no track at the requested cylinder/head")
    track.cue_at_index()
    if track.splice not in (None, 0):
        raise ValueError("this bench only supports raw tracks spliced at INDEX")
    writeout = track.flux_for_writeout(cue_at_index=True)
    data = bytearray()
    remainder = 0.0
    for interval in writeout.list:
        # Carry rounding error forward when converting SCP's 40 MHz to the
        # native writer's 24 MHz. Do not normalise away recorded timing jitter.
        scaled = interval * 24000000 / writeout.sample_freq + remainder
        ticks = round(scaled)
        remainder = scaled - ticks
        if not 20 <= ticks < 0xffff:
            raise ValueError("SCP interval is outside this bench writer's range")
        if ticks < 250:
            data.append(ticks)
        elif ticks < 1525:
            data.extend((250 + (ticks - 250) // 255, 1 + (ticks - 250) % 255))
        else:
            # Library greasepack Space stores a complete interval. This is
            # deliberately NOT the USB Greaseweazle command-stream encoding.
            data.extend((255, 2))
            data.extend(1 | ((ticks >> shift) & 127) * 2 for shift in (0, 7, 14, 21))
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["write-img", "write-track", "read-img", "patterns",
                        "capture", "replay", "detect", "patch", "erase", "protect"])
    parser.add_argument("--port")
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory for this run; never overwritten")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--cylinder", type=int, default=5)
    parser.add_argument("--head", type=int, default=0)
    parser.add_argument("--timed", action="store_true")
    parser.add_argument("--packed", action="store_true",
                        help="replay library greasepack intervals at 24 MHz (not a USB stream)")
    parser.add_argument("--destructive", action="store_true")
    parser.add_argument("--write-retries", type=int, choices=(0, 1, 2), default=0,
                        help="explicit IMG rewrite limit; failed captures are retained")
    args = parser.parse_args()
    if args.command in ("write-img", "write-track", "patterns", "replay", "patch", "erase") and not args.destructive:
        parser.error("this command requires --destructive and a disposable floppy")
    if not 0 <= args.cylinder < 80 or args.head not in (0, 1):
        parser.error("this bench uses 80 cylinders and two heads")
    source = args.input.read_bytes() if args.input else None
    source_sha256 = hashlib.sha256(source).hexdigest() if source else None
    if args.command == "replay" and args.input and args.input.suffix.lower() == ".scp":
        try:
            source = scp_write_data(args.input, args.cylinder, args.head)
        except (ImportError, ValueError) as error:
            parser.error(f"SCP input requires Greaseweazle's Python package: {error}")
        args.packed = True
    if args.command in ("write-img", "write-track") and (source is None or len(source) != 1474560):
        parser.error("IMG writing requires an exact 1,474,560-byte --input IMG")
    if args.command == "replay" and (source is None or not 0 < len(source) <= 300000):
        parser.error("replay requires 1..300000 bytes of --input native flux")
    if args.port is None:
        ports = [p.device for p in list_ports.comports()
                 if p.vid == 0x239a and p.pid == 0x8151]
        if len(ports) != 1:
            parser.error("specify --port; exactly one Floppsy was not found")
        args.port = ports[0]

    commands = {"write-img": f"F 0 0 {args.write_retries}",
                "write-track": f"I {args.cylinder} {args.head} {args.write_retries}",
                "read-img": "V", "patterns": "B",
                "capture": f"R {args.cylinder} {args.head} {int(args.timed)}",
                "replay": f"X {args.cylinder} {args.head} {len(source or b'')} {int(args.packed)}",
                "detect": "D", "patch": f"P {args.cylinder} {args.head}",
                "erase": f"E {args.cylinder} {args.head}", "protect": "Q"}
    args.output.mkdir(parents=True, exist_ok=False)
    result = {"command": args.command, "serial_command": commands[args.command],
              "input_sha256": source_sha256,
              "payload_sha256": hashlib.sha256(source).hexdigest() if source else None,
              "packed": args.packed,
              "complete": False, "write_retries": args.write_retries,
              "failed_verifications": [], "writes": [], "reads": [],
              "tracks": [], "captures": []}
    log = []
    image = bytearray()

    with serial.Serial(args.port, 115200, timeout=1, write_timeout=10) as device:
        def line():
            data = bytearray()
            deadline = time.monotonic() + 30
            while not data.endswith(b"\n\r") and time.monotonic() < deadline:
                data.extend(device.read_until(b"\n\r"))
            if not data.endswith(b"\n\r"):
                raise RuntimeError(f"serial line timeout: {data[:100]!r}")
            # Legacy block-driver diagnostics use CRLF; normalize only text.
            return data[:-2].decode("ascii").strip()

        def exact(count):
            data = bytearray()
            deadline = time.monotonic() + 30
            while len(data) < count and time.monotonic() < deadline:
                data.extend(device.read(min(16384, count - len(data))))
            if len(data) != count:
                raise RuntimeError(f"binary timeout: {len(data)}/{count} bytes")
            return bytes(data)

        try:
            time.sleep(1)
            device.reset_input_buffer()
            device.write((commands[args.command] + "\n").encode("ascii"))
            while True:
                text = line()
                log.append(text)
                report(text)
                written = re.fullmatch(r"PASS C(\d+) H(\d) pattern (\d):.*", text)
                if written:
                    cylinder, head, pattern = map(int, written.groups())
                    result["writes"].append({"cylinder": cylinder, "head": head,
                                             "pattern": pattern})
                read = re.fullmatch(r"READ (\d+) (\d) attempt=(\d) sectors=(\d+)", text)
                if read:
                    cylinder, head, attempt, sectors = map(int, read.groups())
                    result["reads"].append({"cylinder": cylinder, "head": head,
                                            "attempt": attempt, "sectors": sectors})
                detected = re.search(r"DETECTED (\d+) (\d+) (\d+)$", text)
                if detected:
                    size, cylinders, sectors = map(int, detected.groups())
                    result["detected"] = {"bytes": size, "cylinders": cylinders,
                                          "heads": 2, "sectors_per_track": sectors}
                if text.endswith("UNKNOWN"):
                    result["detected"] = None
                protected = re.search(r"WRITE_PROTECTED ([01])$", text)
                if protected:
                    result["write_protected"] = bool(int(protected[1]))
                if text.endswith("PATCH PASS sector 8 changed; 17 neighbours identical"):
                    result["patch_verified"] = True
                if text.startswith("FAIL:"):
                    raise RuntimeError(text)
                if text.startswith("REJECT"):
                    result["rejected"] = text
                if text.startswith("VERIFY FAILED"):
                    result["failed_verifications"].append(text)
                match = re.fullmatch(r"IMAGE C(\d+) H(\d) READY", text)
                if match:
                    cylinder, head = map(int, match.groups())
                    offset = (cylinder * 2 + head) * 9216
                    device.write(source[offset:offset + 9216])
                elif text.startswith("RAW READY "):
                    device.write(source)
                elif text.startswith("SECTORS "):
                    _, cylinder, head, count, length = text.split()
                    cylinder, head, count, length = map(int, (cylinder, head, count, length))
                    if length != 9216 or cylinder * 2 + head != len(image) // 9216:
                        raise RuntimeError("unexpected sector record order or size")
                    image.extend(exact(length))
                    if line() != "END SECTORS":
                        raise RuntimeError("missing sector record terminator")
                    result["tracks"].append({"cylinder": cylinder, "head": head, "sectors": count})
                elif text.startswith("FLUX "):
                    _, cylinder, head, count, index, sectors, mask, rate = text.split()
                    count = int(count)
                    raw, decoded = exact(count), exact(9216)
                    if line() != "END FLUX":
                        raise RuntimeError("missing flux record terminator")
                    stem = f"c{int(cylinder):02d}h{head}"
                    if (args.output / (stem + ".flux24")).exists():
                        stem += f"-capture-{len(result['captures']) + 1}"
                    (args.output / (stem + ".flux24")).write_bytes(raw)
                    (args.output / (stem + ".img")).write_bytes(decoded)
                    metadata = {"cylinder": int(cylinder), "head": int(head),
                                "sample_frequency": int(rate), "index_offset": int(index),
                                "timed": args.timed, "sectors": int(sectors),
                                "valid_mask": int(mask, 16), "interval_count": count,
                                "zero_intervals": raw.count(0), "saturated_intervals": raw.count(255),
                                "flux_sha256": hashlib.sha256(raw).hexdigest(),
                                "sector_sha256": hashlib.sha256(decoded).hexdigest()}
                    (args.output / (stem + ".json")).write_text(json.dumps(metadata, indent=2))
                    result["captures"].append(metadata)
                if text == "DONE":
                    result["complete"] = True
                    break
            if "rejected" in result:
                raise RuntimeError(result["rejected"])
            if args.command == "read-img":
                if len(image) != 1474560 or any(t["sectors"] != 18 for t in result["tracks"]):
                    raise RuntimeError("incomplete sector image; inspect results.json")
                result["image_sha256"] = hashlib.sha256(image).hexdigest()
                if source is not None:
                    result["image_matches_input"] = image == source
                    if image != source:
                        raise RuntimeError("sector image differs from input")
        except Exception as error:
            result["error"] = str(error)
            raise
        finally:
            if image:
                (args.output / "readback.img").write_bytes(image)
            (args.output / "serial.log").write_text("\n\r".join(log), newline="")
            (args.output / "results.json").write_text(json.dumps(result, indent=2))
    report(json.dumps(result))


if __name__ == "__main__":
    sys.stdout.reconfigure(newline="")
    main()
