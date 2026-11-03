#!/usr/bin/env python3
"""Create good/bad sector-image fixtures and exercise the portable C++ reader.

These are our own minimal images, built from the published filesystem layouts.
No downloaded software is redistributed. Run with the image_catalog executable;
--fixtures keeps the generated good/bad files for reproducing a failure.
"""
import argparse
import pathlib
import struct
import subprocess
import tempfile


def put16(image, offset, value):
    struct.pack_into('<H', image, offset, value)


def put32(image, offset, value):
    struct.pack_into('<I', image, offset, value)


def amiga_word(image, offset, value):
    struct.pack_into('>I', image, offset, value & 0xffffffff)


def amiga_checksum(image, offset):
    amiga_word(image, offset + 20, 0)
    total = sum(struct.unpack_from('>128I', image, offset))
    amiga_word(image, offset + 20, -total)


def fat12():
    image = bytearray(1474560)
    image[:3] = b'\xeb\x3c\x90'
    put16(image, 11, 512)
    image[13] = 1
    put16(image, 14, 1)
    image[16] = 2
    put16(image, 17, 224)
    put16(image, 19, 2880)
    image[21] = 0xf0
    put16(image, 22, 9)
    image[510:512] = b'\x55\xaa'
    image[512:515] = b'\xf0\xff\xff'
    image[5120:5123] = b'\xf0\xff\xff'
    root = 19 * 512
    image[root:root + 11] = b'FIXTURE    '
    image[root + 11] = 8
    image[root + 32:root + 43] = b'HELLO   TXT'
    put32(image, root + 32 + 28, 123)
    return image


def amiga(ffs=False):
    image = bytearray(901120)
    image[:4] = b'DOS' + bytes([int(ffs)])
    root, entry = 880 * 512, 881 * 512
    amiga_word(image, root, 2)
    amiga_word(image, root + 12, 72)
    amiga_word(image, root + 24, 881)
    amiga_word(image, root + 508, 1)
    image[root + 432:root + 440] = b'\x07FIXTURE'
    amiga_word(image, entry, 2)
    amiga_word(image, entry + 4, 881)
    amiga_word(image, entry + 500, 880)
    amiga_word(image, entry + 508, -3)
    amiga_word(image, entry + 324, 123)
    image[entry + 432:entry + 442] = b'\x09HELLO.TXT'
    amiga_checksum(image, root)
    amiga_checksum(image, entry)
    return image


def cbm():
    image = bytearray(174848)
    bam = 357 * 256  # Track 18, sector 0: 17 tracks of 21 sectors.
    image[bam:bam + 4] = bytes([18, 1, 0x41, 0])
    image[bam + 0x90:bam + 0xa0] = b'FIXTURE' + b'\xa0' * 9
    image[bam + 0xa5:bam + 0xa7] = b'2A'
    entry = bam + 256 + 2
    image[entry:entry + 3] = bytes([0x82, 1, 0])
    image[entry + 3:entry + 19] = b'HELLO' + b'\xa0' * 11
    put16(image, entry + 28, 3)
    return image


def dos33():
    image = bytearray(143360)
    vtoc = 17 * 4096
    image[vtoc + 1:vtoc + 4] = bytes([17, 15, 3])
    image[vtoc + 6] = 1
    image[vtoc + 0x27] = 122
    image[vtoc + 0x34:vtoc + 0x38] = bytes([35, 16, 0, 1])
    entry = vtoc + 15 * 256 + 11
    image[entry:entry + 3] = bytes([1, 0, 0x82])
    image[entry + 3:entry + 33] = bytes(c | 128 for c in b'HELLO' + b' ' * 25)
    put16(image, entry + 33, 3)
    return image


def prodos():
    image = bytearray(143360)
    header = 1024 + 4
    image[header:header + 8] = b'\xf7FIXTURE'
    image[header + 0x1f:header + 0x21] = bytes([39, 13])
    put16(image, header + 0x21, 1)
    put16(image, header + 0x23, 6)
    put16(image, header + 0x25, 280)
    entry = header + 39
    image[entry:entry + 10] = b'\x19HELLO.TXT'
    put16(image, entry + 17, 7)
    put16(image, entry + 19, 1)
    put16(image, entry + 21, 123)
    image[entry + 30] = 0xe3
    return image


def run_tests(executable, folder):
    checks = 0

    def check(name, image, expected=None):
        nonlocal checks
        path = folder / name
        path.write_bytes(image)
        result = subprocess.run([executable, str(path)], capture_output=True,
                                text=True, timeout=5)
        if expected is None:
            assert result.returncode == 1, (name, result.stdout, result.stderr)
        else:
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            for text in expected:
                assert text in result.stdout, (name, text, result.stdout)
        checks += 1

    check('fat12-good.img', fat12(), ['FAT12', 'FIXTURE', 'HELLO.TXT\t123 bytes'])
    check('amiga-ofs-good.adf', amiga(), ['Amiga OFS', 'FIXTURE', 'HELLO.TXT\t123 bytes'])
    check('amiga-ffs-good.adf', amiga(True), ['Amiga FFS', 'HELLO.TXT\t123 bytes'])
    check('cbm-good.d64', cbm(), ['Commodore 1541 DOS', 'HELLO\t3 blocks'])
    check('dos33-good.do', dos33(), ['Apple II DOS 3.3', 'HELLO\t3 blocks\tlocked'])
    check('prodos-good.po', prodos(), ['Apple ProDOS', 'FIXTURE', 'HELLO.TXT\t123 bytes'])

    # Invalid geometry, checksum, and linked-directory pointers must not escape
    # the image bounds or loop indefinitely. The CLI propagates library errors.
    image = fat12(); put16(image, 22, 1)
    check('fat12-short-fat.img', image)
    image = fat12(); image[13] = 3
    check('fat12-invalid-cluster.img', image)
    image = fat12(); put16(image, 19, 65535)
    check('fat12-outside-image.img', image)
    image = amiga(); image[880 * 512 + 20] ^= 1
    check('amiga-bad-root-checksum.adf', image)
    image = amiga(); image[881 * 512 + 20] ^= 1
    check('amiga-bad-entry-checksum.adf', image)
    image = amiga(); amiga_word(image, 881 * 512 + 496, 881)
    amiga_checksum(image, 881 * 512)
    check('amiga-directory-loop.adf', image)
    image = amiga(); amiga_word(image, 880 * 512 + 24, 0xffffffff)
    amiga_checksum(image, 880 * 512)
    check('amiga-directory-outside.adf', image)
    image = cbm(); image[358 * 256:358 * 256 + 2] = bytes([18, 1])
    check('cbm-directory-loop.d64', image)
    image = cbm(); image[358 * 256:358 * 256 + 2] = bytes([36, 0])
    check('cbm-directory-outside.d64', image)
    image = dos33(); image[17 * 4096 + 15 * 256 + 11] = 40
    check('dos33-file-outside.do', image)
    image = dos33(); image[17 * 4096 + 2] = 16
    check('dos33-invalid-sector.do', image)
    image = dos33()
    catalog = 17 * 4096 + 15 * 256
    image[catalog + 1:catalog + 3] = bytes([17, 15])
    for slot in range(7):
        image[catalog + 11 + slot * 35] = 255  # Deleted, so scan the whole chain.
    check('dos33-directory-loop.do', image)
    image = prodos(); put16(image, 1026, 2)
    check('prodos-directory-loop.po', image)
    image = prodos(); put16(image, 1026, 280)
    check('prodos-directory-outside.po', image)
    image = prodos(); put16(image, 1026, 3)
    check('prodos-broken-back-pointer.po', image)
    for size in (0, 511, 1024, 4096):
        check(f'truncated-{size}.img', fat12()[:size])
    check('unformatted.img', bytes(1474560))
    print(f'PASS: {checks} good/bad sector-image cases', end='\n\r')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--fixtures', type=pathlib.Path)
    args = parser.parse_args()
    if args.fixtures:
        args.fixtures.mkdir(parents=True, exist_ok=False)
        run_tests(args.executable, args.fixtures)
    else:
        with tempfile.TemporaryDirectory(prefix='floppsy-catalog-') as tmp:
            run_tests(args.executable, pathlib.Path(tmp))
