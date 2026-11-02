# Real-drive MFM regression files

These are test data written by Adafruit, captured on Floppsy RP2350 from a Sony
3.5-inch drive on 2026-11-01. They contain no customer or commercial disk data.
The code and fixtures are MIT licensed. `manifest.json` records their hashes.

Run `make check-files` from `host_src`. This needs a C compiler, but no hardware
or Greaseweazle installation. Assertions must remain enabled. The test clears
sector data and validity before each decode, checks CRC acceptance, and compares
every valid sector against its independent source bytes.

| File | Contents and expected result |
| --- | --- |
| `clean.flux24` | Indexed C5/H0 capture: all 18 sectors match `c05h0.img`. |
| `borderline.flux24` | Timed C5/H0 capture: all 18 sectors must match. Before the phase-error fix in #70, sector 8 was lost; Greaseweazle recovered it from these same intervals. This is recoverable jitter, not corrupt data. |
| `bad-crc.flux24` | Derived from `clean`: swap intervals 29559 and 29560 (zero-based) inside sector 8's data. Total duration is unchanged. Sector 8 must be rejected; the other 17 must match. |
| `missing-dam.flux24` | Actual failed C1/H0 write/read. Both decoders reject sector 10; the other 17 sectors match `c01h0.img`. A later rewrite recovered this track. This file does not establish whether the drive or medium caused the failure. |
| `truncated.flux24` | First half of `clean`: only 10 sectors are recoverable. Must never report a complete track. |
| `c05h0.img`, `c01h0.img` | Exactly 18 x 512 bytes in sector-number order; source data, not a filesystem image. |
| `wrong-byte.img` | `c05h0.img` with byte 3707 XOR 1. IMG has no stored sector CRC: comparison to the source oracle must catch this corruption. |
| `clean.scp` | One C5/H0 track converted from `clean.flux24` using Greaseweazle. For testing standard-file raw replay. |
| `boot-1440.img` | One valid 512-byte DOS boot sector from the bench's generated FAT12 image. |
| `boot-zero-heads.img` | Same boot sector with BPB head count zero. Geometry detection must reject it without division by zero. |

## Timing and provenance

`.flux24` is a deliberately small native fixture format: one unsigned byte per
flux interval, in **24 MHz ticks**. These files have no zero or saturated (255)
intervals. Do not feed them directly to a 40 MHz SCP writer or to the Greaseweazle
USB protocol. They are not KryoFlux `.raw` streams.

`clean` retains 83,476 intervals; the next INDEX was recorded at interval offset
75,853. `borderline` retains 94,865 intervals and an INDEX offset of 63,689. The
timed capture starts at an arbitrary angular position and is a decoder fixture,
not an index-aligned replay source. The original capture windows were 220 and
250 ms respectively. Native interval timing is quantized; these files do not
claim to reproduce analog pulse shape or weak-bit behaviour.

For `clean.scp`, the INDEX timestamp is approximated by summing native intervals
up to offset 75,853 (4,826,873 ticks); it is not a separate precision INDEX
measurement. Greaseweazle converts the interval clock to SCP's 40 MHz with carried
rounding error. The surplus fraction of a revolution is omitted in SCP.

The original captures were independently decoded with Greaseweazle source
`26690f8`. Both good captures recovered all 9,216 source bytes. The two damaged
captures recovered 17/18 sectors. `test_mfm_files.c` also software-encodes the
C1/H0 random source data to distinguish a valid input pattern from a failed
physical recording.

For real-disk replay, use `extras/hw_tests/floppsy_write_verify/bench.py` with a
disposable disk. The normal self-test never performs these writes.
