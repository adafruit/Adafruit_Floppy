#!/usr/bin/env python3
"""Capture/identify a track, or explicitly replay packed flux to scratch media.

Input writes are the library's packed 24 MHz intervals, not a Greaseweazle USB
command stream. host_src/make_codec_fixtures.py generates replay files from
independent Greaseweazle encodings. Preserve captures and compare payloads on
the host; an acknowledged write alone is not verification.
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

import serial


def report(message):
    print(message, end='\n\r', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['read', 'timed', 'write'])
    parser.add_argument('--port', required=True)
    parser.add_argument('--cylinder', type=int, default=0)
    parser.add_argument('--head', type=int, default=0)
    parser.add_argument('--hd', action='store_true')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--destructive', action='store_true')
    parser.add_argument('--fixture', help='compare every observed byte with this codec fixture name')
    formats = ['ibm.720', 'ibm.1440', 'amiga.amigados', 'amiga.amigados_hd']
    parser.add_argument('--format', choices=formats,
                        help='decode a known format directly; omit for autodetection')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.cylinder < 80 or args.head not in (0, 1):
        parser.error('invalid cylinder/head')
    if args.command == 'write' and (not args.destructive or not args.input):
        parser.error('write requires --input and --destructive (disposable disk only)')
    payload = args.input.read_bytes() if args.input else b''
    if args.command == 'write' and not 0 < len(payload) <= 300000:
        parser.error('packed write data must be 1..300000 bytes')
    fixture = None
    if args.fixture:
        manifest = Path(__file__).resolve().parents[3] / 'host_src/codec_fixtures/manifest.json'
        fixture = json.loads(manifest.read_text()).get(args.fixture)
        if not fixture:
            parser.error('unknown codec fixture')
        if (args.cylinder, args.head) != (fixture['cylinder'], fixture['head']):
            parser.error('cylinder/head must match the fixture headers')
    args.output.mkdir(parents=True, exist_ok=False)
    log, sectors = [], []
    result = dict(command=args.command, cylinder=args.cylinder, head=args.head,
                  high_density=args.hd, complete=False)
    format_hint = formats.index(args.format) + 1 if args.format else 0
    if payload:
        result['write_sha256'] = hashlib.sha256(payload).hexdigest()

    try:
        with serial.Serial(args.port, 115200, timeout=1, write_timeout=10) as port:
            def line():
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline:
                    value = port.readline().strip()
                    if value:
                        text = value.decode('ascii')
                        log.append(text)
                        return text
                raise TimeoutError('serial response')

            def exact(length):
                data, deadline = bytearray(), time.monotonic() + 30
                while len(data) < length and time.monotonic() < deadline:
                    data.extend(port.read(length - len(data)))
                if len(data) != length:
                    (args.output / 'partial-transfer.bin').write_bytes(data)
                    raise TimeoutError(f'binary transfer: {len(data)} / {length}')
                return bytes(data)

            time.sleep(0.4)
            port.reset_input_buffer()
            command = {'read': 'R', 'timed': 'T', 'write': 'W'}[args.command]
            port.write(f'{command} {args.cylinder} {args.head} {int(args.hd)} {len(payload)} {format_hint}\n'.encode())
            while True:
                text = line()
                fields = text.split()
                if fields[0] in ('FAIL', 'REJECT'):
                    raise RuntimeError(text)
                if fields[0] == 'READY':
                    assert int(fields[1]) == len(payload)
                    port.write(payload)
                elif fields[0] == 'CAPTURE':
                    count, timed, *indexes = map(int, fields[1:])
                    assert 0 < count <= 900000 and exact(1) == b'\r'
                    data = exact(count * 4)
                    (args.output / 'timestamps.u32').write_bytes(data)
                    samples = struct.unpack('<' + 'I' * count, data)
                    intervals = [(a - b) & 0xffffffff for a, b in zip(samples, samples[1:])]
                    (args.output / 'capture.flux32').write_bytes(struct.pack('<' + 'I' * len(intervals), *intervals))
                    result.update(count=count, timed=bool(timed), indexes=indexes,
                                  sample_frequency=40000000,
                                  capture_sha256=hashlib.sha256(data).hexdigest())
                    report(f'Captured {count} transitions at 40 MHz; motor stopped')
                elif fields[0] == 'DETECT':
                    result['detection'] = dict(found=bool(int(fields[1])), encoding=int(fields[2]),
                                              sectors=int(fields[3]), cylinder=int(fields[4]),
                                              head=int(fields[5]), clock_us=float(fields[6]),
                                              sector_bytes=int(fields[7]))
                    report(text)
                elif fields[0] == 'FORMAT':
                    result['format_hint'] = dict(name=args.format, encoding=int(fields[1]),
                                                 clock_us=float(fields[2]))
                    report(text)
                elif fields[0] == 'SECTOR':
                    c, h, r, length, volume, deleted = map(int, fields[1:])
                    assert 0 < length <= 1024 and exact(1) == b'\r'
                    data = exact(length)
                    name = f'c{c:02}h{h}s{r:02}-{len(sectors):03}.bin'
                    (args.output / name).write_bytes(data)
                    sectors.append(dict(cylinder=c, head=h, sector=r, bytes=length,
                                        volume=volume, deleted=bool(deleted), file=name,
                                        sha256=hashlib.sha256(data).hexdigest()))
                elif fields[0] == 'RESULT':
                    result['decoded'], result['bad_headers'], result['bad_data'] = map(int, fields[1:])
                    report(text)
                elif fields[0] == 'DECODE':
                    unique, decoded, bad_headers, bad_data = map(int, fields[2:])
                    result.setdefault('decode_passes', []).append(
                        dict(profile=fields[1], unique_sectors=unique, decoded=decoded,
                             bad_headers=bad_headers, bad_data=bad_data))
                    report(text)
                elif fields[0] == 'DONE':
                    result['complete'] = True
                    break
                else:
                    report(text)
    finally:
        result['sectors'] = sectors
        (args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        (args.output / 'serial.log').write_text('\n\r'.join(log) + '\n\r')
    report(f'Complete: {len(sectors)} checksum-valid sector observations retained')
    if fixture:
        good, wrong = set(), []
        for sector in sectors:
            identity = (sector['cylinder'], sector['head'], sector['sector'])
            if identity[:2] != (fixture['cylinder'], fixture['head']) or identity[2] not in fixture['sector_order']:
                wrong.append(sector['file'])
                continue
            logical = fixture['sector_order'].index(identity[2])
            start = fixture['source_offset'] + logical * fixture['sector_bytes']
            expected = bytes((i * 17 + i // fixture['sector_bytes']) % 256
                             for i in range(start, start + fixture['sector_bytes']))
            if (args.output / sector['file']).read_bytes() != expected or sector['deleted']:
                wrong.append(sector['file'])
            else:
                good.add(identity[2])
        observed_format = result.get('detection', result.get('format_hint', {}))
        passed = (len(good) == fixture['sectors'] and not wrong and
                  observed_format.get('encoding') == fixture['encoding'])
        comparison = dict(fixture=args.fixture, passed=passed, correct_sectors=len(good),
                          expected_sectors=fixture['sectors'], wrong_observations=wrong)
        (args.output / 'comparison.json').write_text(json.dumps(comparison, indent=2) + '\n')
        report(f"{'PASS' if passed else 'FAIL'} payload comparison: {len(good)}/{fixture['sectors']} sectors, {len(wrong)} conflicting observations")
        if not passed:
            raise SystemExit(1)


if __name__ == '__main__':
    main()
