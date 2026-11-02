#!/usr/bin/env python3
"""Generate track oracles with the independent Greaseweazle command-line tool.

Requires --gw, a new --work directory, and Greaseweazle's Python src directory
on PYTHONPATH for its SCP reader/PLL. Normal regression tests need neither.
Only deterministic test patterns are used; no downloaded software is included.
"""
import argparse
import gzip
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from greaseweazle.image.scp import SCP
from greaseweazle.track import PLLTrack

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gw', required=True)
parser.add_argument('--work', required=True, type=Path)
parser.add_argument('--output', type=Path, default=Path(__file__).with_name('codec_fixtures'))
args = parser.parse_args()
args.work.mkdir(parents=True, exist_ok=False)
args.output.mkdir(parents=True, exist_ok=True)


def pattern(size, sector_bytes):
    return bytes((i * 17 + i // sector_bytes) % 256 for i in range(size))


def d64_offset(cylinder):
    return sum(21 if c < 17 else 19 if c < 24 else 18 if c < 30 else 17
               for c in range(cylinder)) * 256


dos_order = [0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15]
cases = [
    ('mfm1440', 0, 'ibm.1440', 'img', 1474560, 512, 0, 0, 18, 1.0, 0, list(range(1, 19))),
    ('mfm720', 0, 'ibm.720', 'img', 737280, 512, 40, 1, 9, 2.0, 81 * 9 * 512, list(range(1, 10))),
    ('fm26', 1, 'fixture.fm', 'img', 26 * 128, 128, 0, 0, 26, 2.0, 0, list(range(1, 27))),
    ('amiga-dd', 2, 'amiga.amigados', 'adf', 901120, 512, 40, 1, 11, 14e6 / 7093790, 81 * 11 * 512, list(range(11))),
    ('amiga-hd', 2, 'amiga.amigados_hd', 'adf', 1802240, 512, 79, 0, 22, 7e6 / 7093790, 158 * 22 * 512, list(range(22))),
    ('c64-zone1', 3, 'commodore.1541', 'd64', 174848, 256, 0, 0, 21, 3.25, d64_offset(0), list(range(21))),
    ('c64-zone2', 3, 'commodore.1541', 'd64', 174848, 256, 17, 0, 19, 3.5, d64_offset(17), list(range(19))),
    ('c64-zone3', 3, 'commodore.1541', 'd64', 174848, 256, 24, 0, 18, 3.75, d64_offset(24), list(range(18))),
    ('c64-zone4', 3, 'commodore.1541', 'd64', 174848, 256, 30, 0, 17, 4.0, d64_offset(30), list(range(17))),
    ('apple-dos33', 4, 'apple2.appledos.140', 'do', 143360, 256, 17, 0, 16, 3.92, 17 * 4096, dos_order),
]
definitions = args.work / 'fm.cfg'
definitions.write_text('disk fixture.fm\n cyls = 1\n heads = 1\n tracks * ibm.fm\n  secs = 26\n  bps = 128\n  rate = 250\n  rpm = 300\n end\nend\n')
manifest = {}
for name, encoding, fmt, extension, size, sector_bytes, cylinder, head, sectors, clock, source_offset, order in cases:
    source = args.work / f'{name}.{extension}'
    source.write_bytes(pattern(size, sector_bytes))
    scp_path = args.work / f'{name}.scp'
    command = [args.gw, 'convert', '--no-clobber', '--format', fmt,
               '--tracks', f'c={cylinder}:h={head}']
    if fmt == 'fixture.fm':
        command += ['--diskdefs', str(definitions)]
    subprocess.run(command + [str(source), str(scp_path)], check=True)
    flux = SCP.from_file(str(scp_path), None, {}).get_track(cylinder, head)
    raw = PLLTrack(clock=clock * 1e-6, data=flux)
    bits, _ = raw.get_all_data()
    bit_data = bits.tobytes()
    flux_data = struct.pack('<' + 'I' * len(flux.list), *map(round, flux.list))
    (args.output / f'{name}.bits').write_bytes(bit_data)
    (args.output / f'{name}.flux32.gz').write_bytes(gzip.compress(flux_data, mtime=0))
    manifest[name] = dict(encoding=encoding, format=fmt, cylinder=cylinder,
                          head=head, sectors=sectors, sector_bytes=sector_bytes,
                          clock_us=clock, sample_frequency=flux.sample_freq,
                          bit_count=len(bits), source_offset=source_offset,
                          sector_order=order, bits_sha256=hashlib.sha256(bit_data).hexdigest(),
                          flux_sha256=hashlib.sha256(flux_data).hexdigest())
    # Also retain a raw replay file beside the generated SCP, outside Git.
    # This is the library's greasepack format, NOT the USB GW command stream.
    # Carry conversion error forward to preserve total timing at 24 MHz.
    flux.cue_at_index()
    writeout = flux.flux_for_writeout(cue_at_index=True)
    packed, remainder = bytearray(), 0.0
    for interval in writeout.list:
        scaled = interval * 24000000 / writeout.sample_freq + remainder
        ticks = round(scaled)
        remainder = scaled - ticks
        if not 20 <= ticks < 65535:
            raise ValueError('Interval outside the bench writer range')
        if ticks < 250:
            packed.append(ticks)
        elif ticks < 1525:
            packed.extend((250 + (ticks - 250) // 255, 1 + (ticks - 250) % 255))
        else:
            packed.extend((255, 2))
            packed.extend(1 | ((ticks >> shift) & 127) * 2 for shift in (0, 7, 14, 21))
    (args.work / f'{name}.fluxpack').write_bytes(packed)
    print(f'Generated {name}: {len(bits)} bits', end='\n\r')
(args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
