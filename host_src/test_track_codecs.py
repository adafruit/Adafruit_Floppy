#!/usr/bin/env python3
"""Check the portable Arduino codecs against independent Greaseweazle fixtures.

Normal runs use only Python's standard library and the compiled C++ host adapter.
Use --fixtures NEW_DIRECTORY to retain derived good/bad bitstream and flux files.
"""
import argparse
import gzip
import hashlib
import json
import pathlib
import random
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).with_name('codec_fixtures')


def pack_bits(bits):
    padded = bits.ljust((len(bits) + 7) // 8 * 8, '0')
    return int(padded, 2).to_bytes(len(padded) // 8, 'big') if padded else b''


def records(path):
    data = path.read_bytes()
    offset = 0
    while offset < len(data):
        cylinder, head, sector, size, volume, deleted = struct.unpack_from('<BBBHHB', data, offset)
        offset += 8
        payload = data[offset:offset + size]
        assert len(payload) == size
        yield cylinder, head, sector, payload, volume, deleted
        offset += size


def run_tests(executable, folder):
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    checks = 0

    def run(name, meta, mode, source, count, expected_full=True, require_missing=False, autodetect=False):
        nonlocal checks
        path = folder / name
        path.write_bytes(source)
        output = folder / (name + '.sectors')
        args = [executable, '-1' if autodetect else str(meta['encoding']), mode, str(path),
                str(count if mode == 'bits' else meta['sample_frequency']),
                str(meta['clock_us']), str(output)]
        result = subprocess.run(args, capture_output=True, text=True, timeout=15)
        assert result.returncode in (0, 1), (name, result.stdout, result.stderr)
        found = set()
        for cylinder, head, sector, payload, _, deleted in records(output):
            assert cylinder == meta['cylinder'] and head == meta['head'], (name, cylinder, head)
            assert sector in meta['sector_order'] and not deleted, (name, sector, deleted)
            logical_sector = meta['sector_order'].index(sector)
            start = meta['source_offset'] + logical_sector * meta['sector_bytes']
            expected = bytes((i * 17 + i // meta['sector_bytes']) % 256
                             for i in range(start, start + meta['sector_bytes']))
            assert payload == expected, (name, sector, 'payload differs')
            found.add(sector)
        if expected_full:
            assert len(found) == meta['sectors'], (name, found, result.stdout)
            if autodetect:
                assert f"ENCODING {meta['encoding']} " in result.stdout, (name, result.stdout)
        if require_missing:
            assert len(found) < meta['sectors'], (name, 'damaged sector accepted', result.stdout)
        checks += 1
        return found, result.stdout

    for name, meta in manifest.items():
        bits = (ROOT / (name + '.bits')).read_bytes()
        flux = gzip.decompress((ROOT / (name + '.flux32.gz')).read_bytes())
        assert hashlib.sha256(bits).hexdigest() == meta['bits_sha256']
        assert hashlib.sha256(flux).hexdigest() == meta['flux_sha256']
        count = meta['bit_count']
        run(name + '-good.bits', meta, 'bits', bits, count)
        run(name + '-good.flux32', meta, 'flux', flux, 0)
        run(name + '-autodetect.flux32', meta, 'flux', flux, 0, autodetect=True)

        # A capture can start at any bit and have no INDEX. Both revolutions
        # still contain every complete sector after this arbitrary rotation.
        bit_string = ''.join(f'{value:08b}' for value in bits)[:count]
        rotated = bit_string[7301:] + bit_string[:7301]
        run(name + '-indexless.bits', meta, 'bits', pack_bits(rotated), len(rotated))
        shifted = '000' + bit_string
        run(name + '-unaligned.bits', meta, 'bits', pack_bits(shifted), len(shifted))

        # Independently disturb the timing: 3% slow rotation, plus small
        # edge-to-edge jitter. The native C++ PLL must still recover all bytes.
        intervals = struct.unpack('<' + 'I' * (len(flux) // 4), flux)
        rng = random.Random(2350)
        jitter = meta['sample_frequency'] * meta['clock_us'] * 1e-6 * 0.02
        disturbed = [max(1, round(value * 1.03 + rng.uniform(-jitter, jitter)))
                     for value in intervals]
        run(name + '-slow-jitter.flux32', meta, 'flux',
            struct.pack('<' + 'I' * len(disturbed), *disturbed), 0)

        # Insert a brief reversal pair without changing the elapsed time. The
        # scanner's low-pass filter must remove it, retaining every data byte.
        glitched = []
        for index, value in enumerate(intervals):
            if index % 113 == 50 and value > 20:
                first = value // 4
                glitched.extend([first, 2, value - first - 2])
            else:
                glitched.append(value)
        run(name + '-short-glitches.flux32', meta, 'flux',
            struct.pack('<' + 'I' * len(glitched), *glitched), 0, autodetect=True)

        # Flip one data bit in the first sector of each revolution, preserving
        # its original checksum. No returned sector may contain wrong bytes.
        markers = {
            0: f'{0x448944894489:048b}' + f'{0x5545:016b}',  # MFM FB
            1: f'{0xf56f:016b}',
            2: f'{0x44894489:032b}',
            3: '1111111111' + '0101010111',  # GCR 07
            4: f'{0xd5aaad:024b}',
        }
        marker = markers[meta['encoding']]
        positions = []
        position = bit_string.find(marker)
        while position >= 0:
            positions.append(position)
            position = bit_string.find(marker, position + len(marker))
        assert len(positions) >= meta['sectors'] * 2, (name, len(positions))
        data_offset = {0: 15, 1: 15, 2: 448 + 7, 3: 1, 4: 7}[meta['encoding']]
        damaged = list(bit_string)
        bad_positions = []
        for position in positions[::meta['sectors']]:
            target = position + len(marker) + data_offset
            if target < len(damaged):
                damaged[target] = '0' if damaged[target] == '1' else '1'
                bad_positions.append(target)
        run(name + '-bad-data.bits', meta, 'bits', pack_bits(''.join(damaged)), count,
            expected_full=False, require_missing=True)

        # A missing-transition burst combines eight adjacent flux intervals.
        # A single missing clock edge need not change data, so damage enough
        # transitions to affect payload bits. Damage both revolutions.
        targets = [round(p * meta['clock_us'] * meta['sample_frequency'] / 1e6)
                   for p in bad_positions]
        bad_flux, elapsed, target_index, i = [], 0, 0, 0
        while i < len(intervals):
            elapsed += intervals[i]
            if target_index < len(targets) and elapsed >= targets[target_index] and i + 8 <= len(intervals):
                bad_flux.append(sum(intervals[i:i + 8]))
                elapsed += sum(intervals[i + 1:i + 8])
                i += 8
                target_index += 1
            else:
                bad_flux.append(intervals[i])
                i += 1
        run(name + '-missing-edge.flux32', meta, 'flux',
            struct.pack('<' + 'I' * len(bad_flux), *bad_flux), 0,
            expected_full=False, require_missing=True)
        for limit in (0, 1, 15, 31, 63, 100):
            run(f'{name}-truncated-{limit}.bits', meta, 'bits', bits[:(limit + 7) // 8], limit,
                expected_full=False, require_missing=True)
        print(f'PASS {name}: bits, flux, INDEX-less, alignment, jitter, damaged data, missing edge, truncation', end='\n\r')
    print(f'PASS: {checks} independent track-codec cases', end='\n\r')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--fixtures', type=pathlib.Path)
    args = parser.parse_args()
    if args.fixtures:
        args.fixtures.mkdir(parents=True, exist_ok=False)
        run_tests(args.executable, args.fixtures)
    else:
        with tempfile.TemporaryDirectory(prefix='floppsy-codecs-') as tmp:
            run_tests(args.executable, pathlib.Path(tmp))
