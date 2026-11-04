#!/usr/bin/env python3
"""Generate independent IBM track-layout oracles with Greaseweazle.

Run with Greaseweazle's src directory on PYTHONPATH. Normal C tests only need
the committed .bits files, not Python or Greaseweazle. These contain our own
deterministic test data, not downloaded software or a captured user's disk.
"""
import hashlib
import json
from pathlib import Path

from greaseweazle.codec.codec import get_diskdef
from greaseweazle.codec.ibm.ibm import IBMTrack_FixedDef

root = Path(__file__).with_name("layout_fixtures")
root.mkdir(exist_ok=True)
manifest = {}
for name, sectors, size, fm in [("1440", 18, 512, False),
                                ("720", 9, 512, False),
                                ("fm26", 26, 128, True),
                                ("1581", 10, 512, False)]:
    if fm:
        definition = IBMTrack_FixedDef("ibm.fm")
        for key, value in [("secs", "26"), ("bps", "128"),
                           ("rate", "250"), ("rpm", "360")]:
            definition.add_param(key, value)
        definition.finalise()
        track = definition.mk_track(43, 0)
    elif name == "1581":
        track = get_diskdef("commodore.1581").mk_track(43, 0)
    else:
        track = get_diskdef("ibm." + name).mk_track(43, 0)
    data = bytes((i * 17 + i // size) % 256 for i in range(sectors * size))
    track.set_img_track(data)
    bits = track.master_track().bits.tobytes()
    filename = name + ".bits"
    (root / filename).write_bytes(bits)
    manifest[filename] = {"sha256": hashlib.sha256(bits).hexdigest(),
                          "bytes": len(bits), "cylinder": 43, "head": 0,
                          "sectors": sectors, "sector_bytes": size}
    print(f"{filename}: {len(bits)} bytes", end="\n\r")
(root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
