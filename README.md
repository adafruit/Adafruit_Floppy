<!--
SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries

SPDX-License-Identifier: MIT
-->

# libmfm - decode flux data according to the MFM standard

## Using in a microcontroller
In your source, define `struct mfm_io` and the necessary routines:
 * `static mfm_io_symbol_t mfm_io_read_symbol(mfm_io_t *io);`
 * `static void mfm_io_reset_sync_count(mfm_io_t *io);`
 * `static int mfm_io_get_sync_count(mfm_io_t *io);`

and then `#include "mfm_impl.h"` (it must be included in just one file in your project)

Generally, you will need to use compiler flags that ensure these are always fully inlined.

`mfm_io_read_symbol` returns one of the `pulse_` symbol constants, also sometimes
called 2T, 3T, and 4T. It also tracks the number of index pulses, if necessary.

`mfm_io_reset_sync_count` resets the count of sync pulses to zero, and
`mfm_io_get_sync_count` returns the number of sync pulses seen.

Client code simply prepares an `mfm_io_t` structure and calls `read_track` with it.

## Theory
For a primer on MFM encoding, see [the Wikipedia article](https://en.wikipedia.org/wiki/Modified_frequency_modulation).

All code is written to be "constant-ish time"; this is lousy in theory, but works in practice on a 120MHz Cortex M4 (SAM D51) with the right compiler flags, though you do have to have a good estimate of just how fast the flux counting loop runs. Because you get to define `mfm_io_read_symbol` yourself, you could also make it use fancy pulse capture hardware, work from previously read flux timing data, etc.

Broadly, the library parses an entire track at once, in about 1 revolution of the floppy.

Reception is divided into several steps:
 * Finding a SYNC mark (the data byte 0xA1, but written with non-standard clock bits)
 * Reading the IDAM and its payload, which indicate the sector number
 * Reading the DAM and its payload, which is the content of the sector.

The metadata and the block data both have CRCs as a check of data integrity.

Reading a SYNC mark is done by tracking the the recent history of raw MFM symbols (`pulse_10` / `pulse_100` / `pulse_1000`), 16 of them in a 32-bit register. When the low 28 of those bits match a special magic number, that indicates that a proper A1 sync has occurred.

If the mark is an IDAM, then the CRC and sector number are checked (real FDCs check the track & cylinder number against expected values too, we don't); if we haven't read that sector yet, then we wait for another SYNC mark. If it is a DAM, then we just found the block data. If its CRC passes, then we can count it as a successfully received sector.

Once all sectors in the track are received, or two index pulses have been seen, the track is as received as it's going to get, and `read_track` returns.

## "Testing"
On a host computer, you can test the MFM decoding logic with `make test`. It will display a single sector,
a boot sector created by linux `mkfs.fat` for a 1.44MB floppy disk.

## Room for improvement
This library is extremely minimal, and has very limited testing. There's a lot that _could_ be added.

It's my hope that we integrate the library with CircuitPython and with [Adafruit Floppy for Arduino](https://github.com/adafruit/Adafruit_Floppy), and make it work on at least RP2040 microcontrollers as well.

Adding support for writing (likely track-at-a-time) is another possibility.
