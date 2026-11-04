#!/usr/bin/env python3
"""Synthetic file fixtures: no downloaded software or personal data.

Exercise Amiga OFS/FFS folders, extension blocks, and D64 chains using the
portable library API. --fixtures retains images for the TFT browser as well.
"""
import argparse
import pathlib
import subprocess
import tempfile

from test_image_catalog import amiga, amiga_word, amiga_checksum, cbm, put16


PAYLOAD = (b'Floppsy file browser fixture.\r\n' + bytes(range(256))) * 160


def amiga_file(ffs):
    image = amiga(ffs)
    # Root -> DOCS directory -> HELLO.TXT. The file is larger than one header's
    # 72 pointers, with deliberately noncontiguous data blocks.
    directory, header, extension = 881, 882, 883
    image[directory * 512:(directory + 1) * 512] = bytes(512)
    for block, kind, parent, name in ((directory, 2, 880, b'DOCS'),
                                     (header, -3, directory, b'HELLO.TXT')):
        base = block * 512
        amiga_word(image, base, 2)
        amiga_word(image, base + 4, block)
        amiga_word(image, base + 500, parent)
        amiga_word(image, base + 508, kind)
        image[base + 432] = len(name)
        image[base + 433:base + 433 + len(name)] = name
    amiga_word(image, directory * 512 + 24, header)
    amiga_checksum(image, directory * 512)
    amiga_word(image, header * 512 + 324, len(PAYLOAD))
    payload_size = 512 if ffs else 488
    chunks = [PAYLOAD[i:i + payload_size] for i in range(0, len(PAYLOAD), payload_size)]
    blocks = [10 + i * 2 for i in range(len(chunks))]
    amiga_word(image, header * 512 + 16, blocks[0])
    for n, chunk in enumerate(chunks):
        table = header if n < 72 else extension
        amiga_word(image, table * 512 + 308 - (n % 72) * 4, blocks[n])
        base = blocks[n] * 512
        if not ffs:
            for at, value in ((0, 8), (4, header), (8, n + 1),
                              (12, len(chunk)), (16, blocks[n + 1] if n + 1 < len(blocks) else 0)):
                amiga_word(image, base + at, value)
            image[base + 24:base + 24 + len(chunk)] = chunk
            amiga_checksum(image, base)
        else:
            image[base:base + len(chunk)] = chunk
    amiga_word(image, header * 512 + 8, min(72, len(blocks)))
    amiga_word(image, header * 512 + 504, extension)
    for at, value in ((0, 16), (4, extension), (8, len(blocks) - 72),
                      (500, header), (508, -3)):
        amiga_word(image, extension * 512 + at, value)
    amiga_checksum(image, header * 512)
    amiga_checksum(image, extension * 512)
    return image


def cbm_file():
    image = cbm()
    chunks = [PAYLOAD[i:i + 254] for i in range(0, len(PAYLOAD), 254)]
    # Keep all data below track 18. The PRG's leading bytes are deliberately
    # retained, as they are part of the file and must appear in the hex view.
    for block, chunk in enumerate(chunks):
        track, sector = divmod(block + 1, 21)
        link = bytes([track + 1, sector]) if block + 1 < len(chunks) else bytes([0, len(chunk) + 1])
        image[block * 256:block * 256 + 2] = link
        image[block * 256 + 2:block * 256 + 2 + len(chunk)] = chunk
    put16(image, 358 * 256 + 30, len(chunks))
    return image


def d81_file():
    image = bytearray(819200)
    header = 39 * 40 * 256
    image[header:header + 4] = bytes([40, 3, ord('D'), 0])
    image[header + 4:header + 20] = b'FLOPPSY D81'.ljust(16, b'\xa0')
    image[header + 25:header + 27] = b'3D'
    directory = header + 3 * 256
    image[directory:directory + 5] = bytes([0, 255, 0x82, 76, 0])
    image[directory + 5:directory + 21] = b'HELLO'.ljust(16, b'\xa0')
    chunks = [PAYLOAD[i:i + 254] for i in range(0, len(PAYLOAD), 254)]
    put16(image, directory + 30, len(chunks))
    # Put data near the far end of the image: this catches D64-sized visited
    # maps, 0/1-based track mistakes, and crossing both logical head halves.
    for n, chunk in enumerate(chunks):
        block = 3000 + n
        track, sector = divmod(block + 1, 40)
        link = bytes([track + 1, sector]) if n + 1 < len(chunks) else bytes([0, len(chunk) + 1])
        image[block * 256:block * 256 + 2] = link
        image[block * 256 + 2:block * 256 + 2 + len(chunk)] = chunk
    return image


def run(executable, folder):
    checks = 0

    def check(name, image, path, offset=0, length=4096, valid=True, payload=PAYLOAD):
        nonlocal checks
        file = folder / name
        file.write_bytes(image)
        result = subprocess.run([executable, str(file), path, str(offset), str(length)],
                                capture_output=True, text=True, timeout=5)
        if valid:
            expected = payload[offset:offset + length]
            assert result.returncode == 0, (name, result.stderr)
            assert f'SIZE {len(payload)} READ {len(expected)}' in result.stdout
            assert expected.hex() in result.stdout, (name, offset, result.stdout)
        else:
            assert result.returncode != 0, (name, result.stdout)
        checks += 1

    for ffs in (False, True):
        name = 'FFS' if ffs else 'OFS'
        image = amiga_file(ffs)
        for offset in (0, 487, 511, 4090, 35000, 36860, len(PAYLOAD) - 31, len(PAYLOAD)):
            check(f'{name}.adf', image, '/DOCS/HELLO.TXT', offset)
        check(f'{name}-past-eof.adf', image, '/DOCS/HELLO.TXT', len(PAYLOAD) + 1, valid=False)
        broken = bytearray(image)
        amiga_word(broken, 883 * 512 + 504, 883)
        amiga_checksum(broken, 883 * 512)
        check(f'{name}-extension-loop.adf', broken, '/DOCS/HELLO.TXT', valid=False)
        broken = bytearray(image)
        amiga_word(broken, 882 * 512 + 308, 99999)
        amiga_checksum(broken, 882 * 512)
        check(f'{name}-bad-pointer.adf', broken, '/DOCS/HELLO.TXT', valid=False)
        broken = bytearray(image)
        broken[883 * 512 + 20] ^= 1
        check(f'{name}-bad-checksum.adf', broken, '/DOCS/HELLO.TXT', valid=False)
    # All four closed CBM file types expose the same unmodified byte stream.
    # REL coverage is linear data only: this does not test record/side-sector APIs.
    type_offset = 358 * 256 + 2
    for kind, name in enumerate(('SEQ', 'PRG', 'USR', 'REL'), 1):
        image = cbm_file()
        image[type_offset] = 0x80 + kind
        image_name = f'C64-{name}.d64'
        if kind == 2:
            image_name = 'C64.d64'  # Preserve the existing TFT fixture filename.
        for offset in (0, 253, 254, 4090, len(PAYLOAD) - 31, len(PAYLOAD)):
            check(image_name, image, '/HELLO', offset)
        check(f'C64-{name}-past-eof.d64', image, '/HELLO', len(PAYLOAD) + 1, valid=False)

        locked = bytearray(image)
        locked[type_offset] = 0xc0 + kind
        check(f'C64-{name}-locked.d64', locked, '/HELLO')
        unclosed = bytearray(image)
        unclosed[type_offset] = kind
        check(f'C64-{name}-unclosed.d64', unclosed, '/HELLO', valid=False)

        # A final-sector count of one represents zero payload bytes. Unused
        # bytes in the sector must never leak into the empty file's preview.
        empty = bytearray(image)
        empty[:2] = bytes([0, 1])
        put16(empty, 358 * 256 + 30, 1)
        check(f'C64-{name}-empty.d64', empty, '/HELLO', payload=b'')
        check(f'C64-{name}-empty-past-eof.d64', empty, '/HELLO', 1, valid=False)

    image = cbm_file()
    for kind in (0, 5, 6, 7):
        unsupported = bytearray(image)
        unsupported[type_offset] = 0x80 + kind
        check(f'C64-unsupported-{kind}.d64', unsupported, '/HELLO', valid=False)
    broken = bytearray(image); broken[:2] = bytes([1, 0])
    check('C64-loop.d64', broken, '/HELLO', valid=False)
    broken = bytearray(image); broken[:2] = bytes([36, 0])
    check('C64-outside.d64', broken, '/HELLO', valid=False)
    broken = bytearray(image); broken[:2] = bytes([0, 0])
    check('C64-bad-tail.d64', broken, '/HELLO', valid=False)
    with_errors = image + bytes([1]) * 683
    check('C64-error-table.d64', with_errors, '/HELLO')
    broken = bytearray(with_errors); broken[174848] = 5
    check('C64-recorded-error.d64', broken, '/HELLO', valid=False)

    image = d81_file()
    for offset in (0, 253, 254, 4090, 5080, 10160, len(PAYLOAD) - 31, len(PAYLOAD)):
        check('C1581.d81', image, '/HELLO', offset)
    check('C1581-past-eof.d81', image, '/HELLO', len(PAYLOAD) + 1, valid=False)
    check('C1581-truncated.d81', image[:-1], '/HELLO', valid=False)
    directory = (39 * 40 + 3) * 256
    for label, at, value in (
            ('loop', 3000 * 256, bytes([76, 0])),
            ('outside', 3000 * 256, bytes([81, 0])),
            ('bad-sector', 3000 * 256, bytes([80, 40])),
            ('bad-tail', 3000 * 256, bytes([0, 0])),
            ('bad-header', 39 * 40 * 256 + 2, b'A'),
            ('bad-directory', 39 * 40 * 256 + 1, bytes([40])),
            ('unclosed', directory + 2, bytes([2]))):
        broken = bytearray(image)
        broken[at:at + len(value)] = value
        check(f'C1581-{label}.d81', broken, '/HELLO', valid=False)
    with_errors = image + bytes([1]) * 3200
    check('C1581-error-table.d81', with_errors, '/HELLO')
    broken = bytearray(with_errors); broken[819200 + 3000] = 5
    check('C1581-recorded-error.d81', broken, '/HELLO', valid=False)
    print(f'PASS: {checks} file-content / corruption cases', end='\n\r')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--fixtures', type=pathlib.Path)
    args = parser.parse_args()
    if args.fixtures:
        args.fixtures.mkdir(parents=True, exist_ok=False)
        run(args.executable, args.fixtures)
    else:
        with tempfile.TemporaryDirectory() as temp:
            run(args.executable, pathlib.Path(temp))
