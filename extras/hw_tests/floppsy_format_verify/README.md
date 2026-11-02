# Multi-format capture and raw replay bench

Engineering test for Floppsy RP2350 at 240 MHz with Adafruit TinyUSB. This is
separate from the normal hardware self-test. Nothing writes on startup. A write
requires an explicit command, a disposable disk, physical INDEX pulses, and
writable media. Hold BOOT to cancel between operations. The motor stops before
USB transfer and decoding. Failure stops the motor and isolates the cable.

The standard PC connector uses DS1. Select the drive/media arrangement with
`--hd`; this controls the density pin, not the decoder. Decoding tries common
rates independently. An HD disk should keep the drive in HD mode even when
experimenting with slower encodings. That experiment does not qualify DD media.
Writes end at the next physical INDEX and cannot reproduce weak magnetic bits.

Install `pyserial`, build/upload `floppsy_format_verify.ino`, then:

```sh
python format_bench.py read --port COM52 --cylinder 0 --head 0 --hd --output new-read
python format_bench.py timed --port COM52 --cylinder 0 --head 0 --hd --output new-timed
```

`timed` captures 0.6 seconds without waiting for INDEX. Ordinary reads also use
timed capture when motor startup found no INDEX pulses. A missing pulse does
not prove media presence. Index positions are meaningful only when `timed` is
false; timed captures must not be relabeled as physically indexed tracks.

For reproducible writes, generate the deterministic oracles using
`host_src/make_codec_fixtures.py` as described in `host_src/codec_fixtures/README.md`.
The generator retains `.fluxpack` raw replay files in its work directory:

```sh
python format_bench.py write --port COM52 --cylinder 40 --head 1 --hd \
  --input generated/mfm720.fluxpack --fixture mfm720 \
  --destructive --output new-write-check
```

`--fixture` compares every observed payload against the deterministic source
pattern and fails on missing sectors, wrong layout, or any conflicting payload.
An acknowledged write or a valid on-disk checksum alone is not a byte comparison.
The optional fixture check works on reads too. Each output directory must be new.

For a known source format, add `--format ibm.720`, `ibm.1440`,
`amiga.amigados`, or `amiga.amigados_hd`. This runs one PLL/decoder pass
instead of searching all encodings and rates on every track. The log and JSON
call this a format hint, not detection. Sector checksums and payload comparisons
still apply. Omit the option for a separate autodetection check. Media density
remains independently selected with `--hd`.

On the Sony drive, a 720 KB track read with all nine payloads verified took
3.6 seconds with the hint versus 9.8 seconds with autodetection. These times
include motor startup, capture, transfer and decoding, not just disk rotation.

Files retained:

- `timestamps.u32`: original descending 40 MHz counter samples, little-endian.
- `capture.flux32`: positive transition intervals, little-endian uint32. It is
  not an SCP container and does not invent index pulses.
- One `.bin` per checksum-valid sector observation, including repeated IDs.
- `results.json`: timestamps/index mode, encoding, IDs, hashes, and counts.
- `comparison.json`: optional fixture comparison, including wrong observations.
- `serial.log`: commands/results; partial binary transfers are retained on error.

Write input is the library's packed 24 MHz interval representation, not a
Greaseweazle USB command stream. Inputs must cover 210 ms to 1 s, fit in 300 KB,
and use 20..65534-tick intervals. Raw replay preserves supplied timing; it does
not normalize drive RPM or decode and regenerate sectors.

## Bench evidence and limits

On the available Sony 3.5-inch drive and disposable HD disk, exact sector payloads
were recovered after writes of IBM MFM HD/DD, IBM FM, and Amiga MFM HD/DD tracks.
With the separate IBM encoder-layout fix and a replacement HD disk in the same
drive, all 2,880 sectors passed write/CRC/byte verification on the first read.
A separate full-image readback matched every byte without retries or rewrites.
The previous disk's failures followed physical disk angle; retain those failed
captures as negative cases, not as evidence that the replacement disk failed.
Normal-rate
C64/Apple tracks had unreliable physical readback; internal MCU write-pin
captures recovered every expected sector. Running their streams at twice the
normal rate recovered all expected payloads from disk using an explicitly
adjusted host PLL. That is a transport experiment, not a standard C64/Apple disk
and not validation of the Disk II power, phase stepping, or connector interface.

Restore a known FAT12 image after destructive testing, verify its recovered bytes,
and reinstall the normal read-only hardware self-test. Do not report a partial
or retry-recovered disk as a clean first-pass full-disk success.
