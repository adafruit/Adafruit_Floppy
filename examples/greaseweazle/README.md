# Greaseweazle USB firmware

This example implements the Greaseweazle USB serial protocol using Adafruit
Floppy. Feather M4, Feather RP2040, Pico and both Floppsy versions share the
command parser and handlers in `greaseweazle.ino`. The RP2350B board setup and
continuous PIO/DMA capture adapter live in `floppsy_rp2350.h`; `Adafruit_Floppsy`
handles its drive GPIO, level shifters, BOOT input and power monitor.

## Floppsy RP2350

Select **Adafruit Floppsy RP2350**, **240 MHz**, **Adafruit TinyUSB**, with the
board definition's **8 MB PSRAM / 80 MHz** setting. Install Adafruit INA3221,
ILI9341, GFX and their dependencies in addition to Adafruit Floppy and TinyUSB.
Connect the 12 V supply and keep the hardware WRITE switch **OFF**.

This firmware is **read-only**: write and erase commands return Write Protected,
and SET PIN cannot change WRITE GATE. Both transceivers remain disabled until
drive selection passes the power and inactive-write checks. BOOT cancels, USB
disconnect stops the motor, and the default idle watchdog is ten seconds.

Use the Floppsy computer USB serial port explicitly. A straight cable to the
DS1 Sony drive used for development needs `--drive B` (or Shugart `--drive 1`).
DS0 is `--drive A` / `--drive 0`; confirm the drive jumper and cable wiring.

```sh
gw info --device COM51
gw read --device COM51 --drive B --format ibm.1440 --revs 3 disk.img
gw read --device COM51 --drive B --tracks c=0-79:h=0-1 --revs 3 --raw disk.scp
```

Replace `COM51` with your board's port. Other Greaseweazle formats and output
containers are decoded/generated on the computer from the same raw flux.
The transport accepts one through five complete revolutions, with actual index
timing at 25 ns resolution. It handles long no-flux gaps without inventing edges.
Capture overflow, bad timestamps and missing index during an indexed read
are returned as errors.

For a 300 RPM drive whose INDEX pin remains connected but does not pulse:

```sh
gw read --device COM51 --drive B --fake-index 300 --revs 3 --raw disk.scp
```

These reads use an explicit time limit, up to one second, without waiting for
INDEX. Firmware streams the measured flux intervals and no INDEX events;
Greaseweazle supplies the nominal boundaries requested by `--fake-index`.
The specified RPM is an assumption, not a spindle-speed measurement. Use the
same option with `--format ibm.1440` for sector decoding. Motor startup accepts
a silent INDEX pin, but ordinary indexed reads still return No Index.

The first Floppsy implementation supports one connected PC/Shugart or FPC drive,
DS0/DS1, cylinders 0-83, two heads and 150-250 ms index periods.
Apple drive control, hard sectors, combined tick/index limits, writing, erase, bandwidth test
commands and Greaseweazle firmware updates are not implemented. Unsupported
commands receive an error rather than a success acknowledgement.

The device reports the existing **Adafruity** hardware family and protocol 1.0.
`gw info` may advertise a newer official Greaseweazle firmware. **Do not run
`gw update` on Floppsy**; update this Arduino sketch/UF2 instead.

For computer-free capture to microSD, use [Floppsy Dumper](../floppsy_dumper).
