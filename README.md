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

Generally, you will need to use compiler flags that ensure these are always inlined.

`mfm_io_read_symbol` returns one of the `pulse_` symbol constants, also sometimes
called 2T, 3T, and 4T. It also tracks the number of index pulses, if necessary.

`mfm_io_reset_sync_count` resets the count of sync pulses to zero, and
`mfm_io_get_sync_count` returns the number of sync pulses seen.

Client code simply prepares an `mfm_io_t` structure and calls `read_track` with it.

## "Testing"
On a host computer, you can test the MFM decoding logic with `make test`. It will display a single sector,
a boot sector created by linux `mkfs.fat` for a 1.44MB floppy disk.
