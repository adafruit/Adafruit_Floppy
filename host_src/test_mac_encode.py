#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Adafruit Industries
# SPDX-License-Identifier: MIT
"""Compare every encoded transition with Greaseweazle, then check its sectors.

Synthetic bytes only; no copyrighted disk images or personal files. Requires
Greaseweazle's compiled Mac extension, like the independent Amiga encoder test.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile

from greaseweazle.codec.macintosh.mac_gcr import MacGCRDef
from greaseweazle import optimised


def unpack(data):
    result = []
    pos = 0
    while pos < len(data):
        value = data[pos]
        pos += 1
        if value >= 250:
            assert value < 255
            value = 250 + (value - 250) * 255 + data[pos] - 1
            pos += 1
        result.append(value)
    return result


encoder = str(pathlib.Path(sys.argv[1]).resolve())
decoder = str(pathlib.Path(sys.argv[2]).resolve())
cases = 0
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    source, output = root / "source", root / "flux"
    bitsfile, decoded = root / "bits", root / "decoded"
    # Both boundaries of every speed zone and both disk sidedness markers.
    for cylinder in (0, 15, 16, 31, 32, 47, 48, 63, 64, 79):
        clock = (2.63, 2.89, 3.20, 3.57, 3.98)[cylinder // 16]
        for heads, head in ((1, 0), (2, 0), (2, 1)):
            config = MacGCRDef("mac.gcr")
            for key, value in (("secs", 12 - cylinder // 16), ("clock", clock),
                               ("format", "0x02" if heads == 1 else "0x22"),
                               ("interleave", 2)):
                config.add_param(key, value)
            config.finalise()
            track = config.mk_track(cylinder, head)
            data = bytes((i * 17 + i // 256 + cylinder + head) % 256
                         for i in range(track.nsec * 512))
            source.write_bytes(data)
            track.set_img_track(data)
            bits = track.master_track().bits
            bitsfile.write_bytes(bits.tobytes())
            result = subprocess.run([decoder, "5", "bits", str(bitsfile),
                                     str(len(bits)), str(clock), str(decoded)],
                                    capture_output=True, check=True)
            records = decoded.read_bytes()
            found = {}
            while records:
                c, h, number, size, volume, deleted = struct.unpack("<BBBHHB", records[:8])
                assert (c, h, size, volume, deleted) == (cylinder, head, 512, config.format, 0)
                found[number] = records[8:520]
                records = records[520:]
            assert b"".join(found[i] for i in range(track.nsec)) == data

            for period in (198000, 200000, 202000):
                subprocess.run([encoder, str(source), str(output), str(cylinder),
                                str(head), str(heads), str(period)], check=True)
                actual = unpack(output.read_bytes())
                edges = [i + 1 for i, value in enumerate(bits * 2) if value]
                ticks = [(edge * period * 24 + len(bits) // 2) // len(bits) for edge in edges]
                expected = [ticks[0]] + [b - a for a, b in zip(ticks, ticks[1:])]
                assert actual == expected, (cylinder, head, heads, period)
                cases += 1

            # Damaged payload/checksum and a truncated sector cannot be
            # delivered. Do not rely on our encoder to create these fixtures.
            damaged = bytearray(bits.tobytes())
            mark = damaged.find(b"\xd5\xaa\xad")
            damaged[mark + 5] = 0  # Not a legal GCR code.
            bitsfile.write_bytes(damaged)
            bad = subprocess.run([decoder, "5", "bits", str(bitsfile),
                                  str(len(bits)), str(clock), str(decoded)],
                                 capture_output=True, check=True)
            assert b"BADDATA 1" in bad.stdout
            assert len(decoded.read_bytes()) == (track.nsec - 1) * 520
            cases += 1

            # Legal GCR substitutions must still fail their checksums. Also
            # reject a truncated field without reading past the supplied bits.
            for kind in ("checksum", "header", "truncated"):
                changed = bytearray(bits.tobytes())
                mark = changed.find(b"\xd5\xaa\xad")
                if kind == "checksum":
                    index = mark + 4 + 702
                    value = optimised.decode_mac_gcr(bytes([changed[index]]))[0] ^ 1
                    changed[index] = optimised.encode_mac_gcr(bytes([value]))[0]
                elif kind == "header":
                    index = changed.find(b"\xd5\xaa\x96") + 7
                    value = optimised.decode_mac_gcr(bytes([changed[index]]))[0] ^ 1
                    changed[index] = optimised.encode_mac_gcr(bytes([value]))[0]
                else:
                    changed = changed[:mark + 100]
                bitsfile.write_bytes(changed)
                bad = subprocess.run([decoder, "5", "bits", str(bitsfile),
                                      str(len(changed) * 8 - (3 if kind == "truncated" else 0)),
                                      str(clock), str(decoded)], capture_output=True)
                assert bad.returncode == (1 if kind == "truncated" else 0), bad.stderr
                assert len(decoded.read_bytes()) == (0 if kind == "truncated" else (track.nsec - 1) * 520)
                cases += 1

            # Nonzero tag bytes have their own public callback pointer and
            # optional encoder input; raw IMG/DSK files do not contain tags.
            for sector in range(track.nsec):
                tags = bytes((i * 13 + 7) % 256 for i in range(sector * 12, (sector + 1) * 12))
                track.sector[sector] = tags + data[sector * 512:(sector + 1) * 512]
            tagged = track.master_track().bits * 2
            subprocess.run([encoder, str(source), str(output), str(cylinder),
                            str(head), str(heads), "200000", "tags"], check=True)
            edges = [i + 1 for i, value in enumerate(tagged) if value]
            ticks = [(edge * 4800000 + len(tagged) // 4) // (len(tagged) // 2) for edge in edges]
            assert unpack(output.read_bytes()) == [ticks[0]] + [b - a for a, b in zip(ticks, ticks[1:])]
            cases += 1
            # A flux capture need not begin at INDEX or a byte boundary.
            # Use Greaseweazle's bit cells to make a rotated two-turn capture.
            rotated = bits[137:] + bits[:137]
            edges = [i + 1 for i, value in enumerate(rotated * 2) if value]
            ticks = [(edge * 8000000 + len(bits) // 2) // len(bits) for edge in edges]
            intervals = [ticks[0]] + [b - a for a, b in zip(ticks, ticks[1:])]
            output.write_bytes(struct.pack('<' + 'I' * len(intervals), *intervals))
            detection = subprocess.run([decoder, "-1", "flux", str(output),
                                        "40000000", str(clock), str(decoded)],
                                       capture_output=True, check=True)
            assert b"ENCODING 5" in detection.stdout, detection.stdout
            assert f"UNIQUE {track.nsec} ".encode() in detection.stdout, detection.stdout
            cases += 1
print(f"PASS: {cases} Mac oracle cases (all five zones, 400K/800K)", end="\n\r")
