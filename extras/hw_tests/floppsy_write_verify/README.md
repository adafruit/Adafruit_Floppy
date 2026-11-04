# Floppsy destructive bench tests

Use a disposable **1.44 MB HD disk**, a 300 RPM drive with INDEX, and a straight
cable on DS1. Enable WRITE on the board and close the disk's write-protect tab.
The test requires the 5 V rail; supply 12 V separately if the drive needs it.
Hold BOOT to cancel between operations. Failures stop the motor and isolate the
cable; reset after a `FAIL` before another command. Nothing writes at startup.

Build `floppsy_write_verify.ino` for Adafruit Floppsy RP2350 at 240 MHz with
Adafruit TinyUSB. It also requires Adafruit INA3221.
Install Python `pyserial`, then run the host script. Each invocation requires a
**new** output directory and saves the serial log, JSON results, and any captures.
Use `--port COM52` (or your board's port) when auto-detection is ambiguous.

```sh
python bench.py write-img --input source.img --output write --destructive
python bench.py read-img --input source.img --output read
python bench.py detect --output geometry
python bench.py patterns --output patterns --destructive
python bench.py patch --cylinder 1 --head 0 --output patch --destructive
python bench.py capture --cylinder 1 --head 0 --timed --output timed
```

IMG input must be exactly 1,474,560 bytes (80 cylinders, two heads, 18 x 512-byte
sectors). Full reads compare every byte when `--input` is supplied. `patterns`
writes `00`, `FF`, `55`, `AA`, and a fixed-seed random pattern to both heads at
cylinders 0, 40, and 79. `patch` changes only sector 8, then freshly captures and
verifies all 18 sectors, including the unchanged 17 neighbours.

`write-track` accepts the same full IMG but writes only `--cylinder` / `--head`.
IMG writes permit an explicit `--write-retries 1` or `2` (default zero). Each
failed verification is logged and a new flux capture is saved before rewriting.
Do not report a recovered write as a first-pass success. Reads may accumulate
CRC-valid sectors across up to three captures; all attempts are printed.

## Raw flux replay

```sh
python bench.py replay --input clean.flux24 --cylinder 5 --head 0 --output replay-native --destructive
python bench.py replay --input clean.scp --cylinder 5 --head 0 --output replay-scp --destructive
```

SCP import needs Greaseweazle's Python package on `PYTHONPATH`. Its maintained
reader handles the file format; this script does not decode/re-encode sectors.
It converts intervals to the native writer's 24 MHz clock with carried rounding
error, retaining recorded timing variation. This bench does not adjust for a
different drive RPM. Only INDEX-cued tracks spliced at INDEX are supported.

Native `.flux24` stores one unsigned 24 MHz interval per byte. `--packed` selects
the library's `greasepack` interval encoding, **not** a complete Greaseweazle USB
command stream. SCP automatically uses this packed form. The firmware accepts
20..65534-tick intervals, at most 300,000 bytes, with 210 ms to 1 s of input. The
overlap accommodates one 300 RPM revolution; writing ends at the next INDEX.
Timed, unaligned captures must not be replayed as indexed tracks.

Every replay saves a new flux capture, decoded sector bytes, validity mask, and
hashes. `RAW WRITE PASS` means the writer completed; it does **not** certify the
sector contents. Compare the returned IMG against the fixture's sector oracle.
For deliberately damaged flux, check the expected missing sectors instead.
No raw replay can reproduce the analog noise or probabilistic behaviour of weak
media exactly. Saturated native capture intervals are reported in JSON.

## Failure checks and restoration

`erase --cylinder C --head H --destructive` creates a blank track for rejection
tests. `protect` reports the physical disk tab. To test write rejection, stop the
motor, open that tab, and try a destructive command: expect
`REJECT WRITE_PROTECTED` before any data transfer. Close the tab afterward.
The board WRITE EN switch is not a software-readable replacement for this test.

`capture --timed` bypasses waiting for INDEX during the capture. It also permits
startup's missing-INDEX flag, leaving the physical pin configured. This is the
only command allowed to continue without INDEX; writes remain blocked. A missing
pulse alone does not establish disk presence or distinguish an indexless drive
from an empty one.

Restore a known FAT12 IMG after destructive tests, read it back, compare it, and
inspect its files using an independent tool such as 7-Zip. For an independent
flux check, install the Greaseweazle example, capture SCP with `gw read --raw`,
then decode it with `gw convert --format ibm.1440`. Finally reinstall the regular
hardware self-test; its read/check operations do not write to the floppy.

To create your own source image with mtools, use `mformat -C -f 1440 -i source.img ::`
and `mcopy -i source.img file.bin ::`. Keep hashes of the original files and
compare extracted readback files as well as the complete IMG. A FAT12 data disk
with a batch file is not a bootable DOS operating-system installation.
