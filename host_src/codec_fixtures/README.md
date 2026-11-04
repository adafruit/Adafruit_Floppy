# Portable track decoder fixtures

These contain deterministic test patterns, not downloaded software. Greaseweazle
1.23 independently encoded each sector image, saved SCP flux, and recovered the
golden packed bits. `manifest.json` records exact bit lengths, capture timebases,
sector order, pattern offsets, and SHA-256 hashes. `.flux32.gz` contains gzip
compressed little-endian 32-bit transition intervals; it is not an SCP container.

Run from `host_src`:

```sh
make check-codecs CODEC_FLAGS='-O2 -fsanitize=address,undefined -g'
```

The C++ under test is the same allocation-free code used on the microcontroller.
The Python harness compares every returned payload byte, not just checksums or
sector counts. It checks IBM MFM HD/DD, IBM FM, Amiga DD/HD, all four Commodore
1541 zones, and Apple II DOS 3.3 six-and-two GCR. The 150 cases include flux and
bit input, automatic identification, arbitrary INDEX-less start positions, bit
alignment, rotation drift/jitter, short glitches, corrupt data, missing
transitions, and truncation. Separate C++ cases check invalid parameters and
buffer boundaries. No drive, Arduino installation, network, or Greaseweazle is
needed for ordinary runs.

The Amiga encoder has an additional independent check (requires Greaseweazle
on `PYTHONPATH`): `make check-amiga-encode`. It compares every generated flux
interval with Greaseweazle for DD/HD, both heads, cylinders 0/39/79, zero/ones/
patterned sectors and 180/200/220 ms rotations (54 cases). The C++ adapter also
checks invalid arguments and output-capacity rejection under sanitizers.

To retain the generated good and bad files for replay/debugging:

```sh
python3 test_track_codecs.py ./track_codec --fixtures /tmp/new-codec-fixtures
```

To regenerate the independent goldens, put Greaseweazle's `src` directory on
`PYTHONPATH`, then run `make_codec_fixtures.py --gw /path/to/gw --work NEW_DIRECTORY`.
Generation requires Greaseweazle; normal tests use the checked-in files. The
fixture generator does not use our decoder or encoder to create expectations.

## API boundaries

`Adafruit_FloppyBitstream` converts positive timestamp intervals into packed,
MSB-first bits. Its optional short-pulse filter and PLL update rule follow
[Greaseweazle](https://github.com/keirf/greaseweazle)'s public-domain algorithms.
`Adafruit_FloppyCodec::decode()` reports sectors with valid on-disk checksums.
Repeated sectors remain separate observations. Callers must compare repeated
payloads and retain conflicts; a checksum, particularly an XOR checksum, is not
proof that damaged media returned the correct bytes.

`detect()` tries common bit rates and requires at least two distinct, consistent
sector IDs. The result describes one track's encoding and layout. It does not
infer total disk geometry, identify a filesystem, or manufacture INDEX pulses.
Its reported bit period is a successful PLL setting, not an RPM measurement.
Use the separate image/catalog API after assembling sectors in the correct
image order. `encodeAmiga()` separately generates standard DD/HD tracks from
sector data; it does not reproduce custom protection or nonzero sector labels.
Apple 13-sector GCR, Macintosh GCR, and C64/Apple encoders remain unsupported.

Format references:

- [Amiga disk format](https://adflib.github.io/FAQ/adf_info.html)
- [Commodore 1541 manual](https://project64.c64.org/Hardware/Peripherals/vic1541.pdf)
- [Apple DOS notes](https://ciderpress2.com/formatdoc/DOS-notes.html)
- [WOZ2 preservation format](https://applesaucefdc.com/woz/reference2/)

Real-image validation also used local Internet Archive downloads: Fred Fish 857
ADF, 64 Collection Number 5 D64, DOS 3.3 Sample Programs DSK, and Font Mechanic
WOZ. The first three were re-encoded independently by Greaseweazle and recovered
byte-for-byte across every track. The WOZ's original track bitstreams yielded all
560 checksum-valid sectors and a DOS catalog; there was no independent sector
image to compare its payloads against. Those third-party images are not included.
