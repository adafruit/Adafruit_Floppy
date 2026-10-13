# Floppsy write/read verification

This destructive engineering test is separate from the read-only production
self-test and standalone dumper. Use a disposable 3.5-inch 1.44 MB disk, close
its write-protect hole, enable Floppsy's WRITE switch, and connect 12 V power.
It selects DS1 for a straight cable to the prototype's Sony drive.

Build for **Adafruit Floppsy RP2350**, **240 MHz**, **Adafruit TinyUSB**, with
the 80 MHz PSRAM board definition. It requires Adafruit INA3221 and TinyUSB.
Open Serial at 115200 baud. Nothing is written on startup.

- `W`: write cylinder 1, head 0 with five patterns: 00, FF, 55, AA, and an
  address-dependent pattern. Capture fresh flux after each write; require all
  18 sector CRCs and all 9,216 data bytes to match.
- `B`: repeat those tests on both heads at cylinders 1 and 79.
- `F`: write a host-provided 1,474,560-byte IMG, verifying each track immediately.
  After each `IMAGE Cxx Hx READY` line, send exactly 9,216 binary bytes for that
  cylinder/head, without a newline. Tracks arrive in cylinder/head order. A
  host-transfer timeout, missing sector, or byte mismatch stops the test.

BOOT or USB disconnection cancels between track operations. Each low-level
write is bounded to one second; normal indexed writes last one revolution.
Every failure stops the motor and isolates the cable. Reset to restart after
a failure. Return to the read-only dumper after testing.

For a filesystem check, create a standard FAT12 image with `mformat -C -f 1440`,
add known files with `mcopy`, and transmit it using `F`. Independently recapture
the physical disk with the dumper as SCP, decode with Greaseweazle's `ibm.1440`
format, and compare the complete decoded IMG with the source. Windows 7-Zip
can list/extract the recovered FAT12 filesystem; compare each extracted file
with its original too. This is a DOS-compatible data disk, not a bootable DOS
operating-system installation.
