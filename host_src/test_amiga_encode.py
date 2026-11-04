"""Compare native Amiga flux encoding with Greaseweazle's independent encoder.

Requires Greaseweazle on PYTHONPATH; generated files stay in a temporary folder.
Usage: python test_amiga_encode.py /path/to/test_amiga_encode
"""
import pathlib
import subprocess
import sys
import tempfile

from greaseweazle.codec.amiga.amigados import AmigaDOS_DD, AmigaDOS_HD

checks = 0
with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    for sectors, codec in ((11, AmigaDOS_DD), (22, AmigaDOS_HD)):
        for cylinder, head in ((0, 0), (39, 1), (79, 1)):
            for pattern in (0, 1, 2):
                data = bytes(sectors * 512) if pattern == 0 else bytes([255]) * (sectors * 512)
                if pattern == 2:
                    data = bytes((i * 17 + i // 512) % 256 for i in range(sectors * 512))
                reference = codec(cylinder, head)
                reference.set_img_track(data)
                bits = reference.master_track().bits
                source, output = folder / 'track.bin', folder / 'flux.bin'
                source.write_bytes(data)
                for period in (180000, 200000, 220000):
                    subprocess.run([sys.argv[1], str(source), str(output), str(sectors),
                                    str(cylinder), str(head), str(period)], check=True)
                    # Round absolute edge times, preserving fractional ticks;
                    # comparing every pulse checks gaps, syncs and all payloads.
                    expected = bytearray()
                    previous = 0
                    for cell, transition in enumerate(bits + bits, 1):
                        if transition:
                            edge = (cell * period * 24 + len(bits) // 2) // len(bits)
                            expected.append(edge - previous)
                            previous = edge
                    assert output.read_bytes() == expected, (sectors, cylinder, head, pattern, period)
                    checks += 1
print(f'PASS: {checks} Amiga DD/HD flux streams match Greaseweazle exactly', end='\n\r')
