#!/usr/bin/env python3
"""Synthetic file fixtures: no downloaded software or personal data.

Exercise Amiga OFS/FFS folders, HFS extents, and D64/D81 chains using the
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


def hfs_file(catalog_overflow=False):
    """Classic HFS: two catalog leaves, a folder, and fragmented data fork.

    Use three initial extents plus an overflow record. Resource bytes are
    different so a mistaken fork selection cannot pass a data-fork comparison.
    """
    image = bytearray(1474560)
    payload = PAYLOAD

    def be16(at, value):
        image[at:at + 2] = value.to_bytes(2, 'big')

    def be32(at, value):
        image[at:at + 4] = value.to_bytes(4, 'big')

    def node(at, kind, records, forward=0, backward=0):
        be32(at, forward)
        be32(at + 4, backward)
        image[at + 8:at + 10] = bytes([kind, 1 if kind == 255 else 0])
        be16(at + 10, len(records))
        offset = 14
        for i, record in enumerate(records):
            be16(at + 510 - i * 2, offset)
            image[at + offset:at + offset + len(record)] = record
            offset += len(record)
        be16(at + 510 - len(records) * 2, offset)

    def tree_header(at, first, last, total):
        header = bytearray(106)
        header[10:14] = first.to_bytes(4, 'big')
        header[14:18] = last.to_bytes(4, 'big')
        header[18:20] = (512).to_bytes(2, 'big')
        header[22:26] = total.to_bytes(4, 'big')
        node(at, 1, [header, bytes(128), bytes(256)])

    def catalog(parent, name, kind, cnid):
        key = bytes([6 + len(name), 0]) + parent.to_bytes(4, 'big') + bytes([len(name)]) + name
        key += bytes(len(key) % 2)
        body = bytearray(70 if kind == 1 else 102)
        body[:2] = (kind * 256).to_bytes(2, 'big')
        at = 6 if kind == 1 else 20
        body[at:at + 4] = cnid.to_bytes(4, 'big')
        return key, body

    mdb = 1024
    be16(mdb, 0x4244)
    be16(mdb + 18, 2874)
    be32(mdb + 20, 512)
    be16(mdb + 28, 4)
    image[mdb + 36:mdb + 48] = b'\x0bFLOPPSY HFS'
    be32(mdb + 130, 1024)
    be16(mdb + 136, 2)
    be32(mdb + 146, 1536)
    be16(mdb + 150, 4)
    be16(mdb + 152, 2 if catalog_overflow else 3)
    tree_header(2048, 1, 1, 2)
    tree_header(4096, 1, 2, 3)
    root_key, root_body = catalog(1, b'FLOPPSY HFS', 1, 2)
    dir_key, dir_body = catalog(2, b'DOCS', 1, 16)
    node(4608, 255, [root_key + root_body, dir_key + dir_body], forward=2)
    key, body = catalog(16, b'HELLO.TXT', 2, 17)
    blocks = (len(payload) + 511) // 512
    body[26:30] = len(payload).to_bytes(4, 'big')
    body[30:34] = (blocks * 512).to_bytes(4, 'big')
    body[36:40] = (19).to_bytes(4, 'big')
    body[40:44] = (512).to_bytes(4, 'big')
    body[86:90] = (500).to_bytes(2, 'big') + (1).to_bytes(2, 'big')
    extents = [(20, 2), (25, 1), (30, 2), (40, blocks - 5)]
    for i, (start, count) in enumerate(extents[:3]):
        body[74 + i * 4:78 + i * 4] = start.to_bytes(2, 'big') + count.to_bytes(2, 'big')
    node(7168 if catalog_overflow else 5120, 255, [key + body], backward=1)
    overflow = bytes([7, 0]) + (17).to_bytes(4, 'big') + (5).to_bytes(2, 'big')
    overflow += (40).to_bytes(2, 'big') + (blocks - 5).to_bytes(2, 'big') + bytes(8)
    records = [overflow]
    if catalog_overflow:
        catalog_extent = bytes([7, 0]) + (4).to_bytes(4, 'big') + (2).to_bytes(2, 'big')
        catalog_extent += (10).to_bytes(2, 'big') + (1).to_bytes(2, 'big') + bytes(8)
        records.insert(0, catalog_extent)
    node(2560, 255, records)
    offset = 0
    for start, count in extents:
        chunk = payload[offset:offset + count * 512]
        at = 2048 + start * 512
        image[at:at + len(chunk)] = chunk
        offset += len(chunk)
    image[2048 + 500 * 512:2048 + 500 * 512 + 19] = b'RESOURCE FORK ONLY!!'
    return image, payload


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

    image, payload = hfs_file()
    for offset in (0, 511, 1023, 1535, 2559, 2560, 4090, len(payload) - 31, len(payload)):
        check('HFS.img', image, '/DOCS/HELLO.TXT', offset, payload=payload)
    check('HFS-past-eof.img', image, '/DOCS/HELLO.TXT', len(payload) + 1,
          valid=False, payload=payload)
    resource_only = bytearray(image)
    body = 5120 + 14 + 16
    resource_only[body + 26:body + 34] = bytes(8)
    resource_only[body + 74:body + 86] = bytes(12)
    check('HFS-empty-data-fork.img', resource_only, '/DOCS/HELLO.TXT', payload=b'')
    # Reject broken tree links, record bounds, and both initial/overflow
    # extents. These bytes are generated here, not copied from a personal disk.
    for name, at, value in (
            ('bad-node', 4608 + 8, b'\x00'),
            ('bad-backlink', 5120 + 4, (2).to_bytes(4, 'big')),
            ('bad-record', 5120 + 510, (500).to_bytes(2, 'big')),
            ('bad-initial', 5120 + 14 + 16 + 74, (3000).to_bytes(2, 'big')),
            ('bad-overflow', 2560 + 14 + 8, (3000).to_bytes(2, 'big')),
            ('missing-overflow', 2560 + 14 + 6, (6).to_bytes(2, 'big')),
            ('zero-allocation', 1024 + 20, bytes(4)),
            ('node-size', 4096 + 32, (1024).to_bytes(2, 'big'))):
        broken = bytearray(image)
        broken[at:at + len(value)] = value
        check(f'HFS-{name}.img', broken, '/DOCS/HELLO.TXT', valid=False)
    broken = bytearray(image)
    broken[5120:5124] = (1).to_bytes(4, 'big')
    check('HFS-cycle.img', broken, '/MISSING', valid=False)
    check('HFS-truncated.img', image[:6000], '/DOCS/HELLO.TXT', valid=False)
    fragmented_catalog, payload = hfs_file(catalog_overflow=True)
    check('HFS-catalog-overflow.img', fragmented_catalog, '/DOCS/HELLO.TXT', payload=payload)
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
