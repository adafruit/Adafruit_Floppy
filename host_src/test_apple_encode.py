"""Check Apple six-and-two flux, including 10-cell syncs, against Greaseweazle.

Requires Greaseweazle on PYTHONPATH; does not access hardware.
Usage: python test_apple_encode.py /path/to/test_apple_encode
"""
import pathlib
import subprocess
import sys
import tempfile

from greaseweazle.codec.apple2.apple2_gcr import Apple2GCRDef


def unpack(data):
    """Standard Apple tracks need only one- and two-byte packed intervals."""
    result = []
    source = iter(data)
    for value in source:
        assert 0 < value < 255
        if value >= 250:
            value = 250 + (value - 250) * 255 + next(source) - 1
        result.append(value)
    return result


order = (0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15)
config = Apple2GCRDef("apple2.appledos.140")
config.add_param("secs", ",".join(map(str, order)))
config.add_param("clock", 3.92)
config.finalise()
checks = 0
with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    for cylinder in (0, 17, 34):
        for pattern in (0, 1, 2):
            logical = bytes(4096) if pattern == 0 else bytes([255]) * 4096
            if pattern == 2:
                logical = bytes((i * 17 + i // 256 + cylinder) % 256 for i in range(4096))
            volume = (0, 254, 255)[pattern]
            reference = config.mk_track(cylinder, 0)
            reference.set_vol_id(volume)
            reference.set_img_track(logical)
            bits = reference.master_track().bits
            physical = bytearray(4096)
            for logical_id, physical_id in enumerate(order):
                physical[physical_id * 256:(physical_id + 1) * 256] = logical[logical_id * 256:(logical_id + 1) * 256]
            source, output = folder / "track.bin", folder / "flux.bin"
            source.write_bytes(physical)
            for period in (180000, 200000, 220000):
                subprocess.run([sys.argv[1], str(source), str(output),
                                str(cylinder), str(period), str(volume)], check=True)
                edges = [i + 1 for i, value in enumerate(bits * 2) if value]
                ticks = [(edge * period * 24 + len(bits) // 2) // len(bits) for edge in edges]
                expected = [ticks[0]] + [b - a for a, b in zip(ticks, ticks[1:])]
                assert unpack(output.read_bytes()) == expected, (cylinder, pattern, period)
                checks += 1
print(f"PASS: {checks} Apple II flux streams match Greaseweazle exactly", end="\n\r")
