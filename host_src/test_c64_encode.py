"""Compare every encoded edge with Greaseweazle, including sync and gaps.

Requires Greaseweazle on PYTHONPATH. No media or drive is accessed.
Usage: python test_c64_encode.py /path/to/test_c64_encode
"""
import pathlib
import subprocess
import sys
import tempfile

from greaseweazle.codec.commodore.c64_gcr import C64GCRDef


def unpack(data):
    """These GCR streams need only the one- and two-byte interval encodings."""
    result = []
    source = iter(data)
    for value in source:
        assert 0 < value < 255
        if value >= 250:
            value = 250 + (value - 250) * 255 + next(source) - 1
        result.append(value)
    return result


checks = 0
with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    for cylinder in (0, 16, 17, 23, 24, 29, 30, 34):
        sectors, clock = ((21, 3.25) if cylinder < 17 else
                          (19, 3.50) if cylinder < 24 else
                          (18, 3.75) if cylinder < 30 else (17, 4.0))
        config = C64GCRDef("commodore.1541")
        config.add_param("secs", sectors)
        config.add_param("clock", clock)
        config.finalise()
        for pattern in (0, 1, 2):
            data = bytes(sectors * 256) if pattern == 0 else bytes([255]) * (sectors * 256)
            if pattern == 2:
                data = bytes((i * 17 + i // 256 + cylinder) % 256 for i in range(sectors * 256))
            disk_id = (0, 0xffff, 0x4137)[pattern]
            reference = config.mk_track(cylinder, 0)
            reference.set_disk_id(disk_id)
            reference.set_img_track(data)
            bits = reference.master_track().bits
            source, output = folder / "track.bin", folder / "flux.bin"
            source.write_bytes(data)
            for period in (180000, 200000, 220000):
                subprocess.run([sys.argv[1], str(source), str(output),
                                str(cylinder), str(period), str(disk_id)], check=True)
                edges = [i + 1 for i, value in enumerate(bits * 2) if value]
                ticks = [(edge * period * 24 + len(bits) // 2) // len(bits) for edge in edges]
                expected = [ticks[0]] + [b - a for a, b in zip(ticks, ticks[1:])]
                assert unpack(output.read_bytes()) == expected, (cylinder, pattern, period)
                checks += 1
print(f"PASS: {checks} C64 flux streams match Greaseweazle exactly (all four zones)", end="\n\r")
