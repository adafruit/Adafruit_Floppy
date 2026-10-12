# Floppsy Dumper

Separate, read-only firmware for the RP2350B Floppsy. It saves IBM sector
images and genuine multi-revolution flux captures to microSD. The existing
[self-test](https://github.com/adafruit/Adafruit_Arduino_Tester_Code/tree/master/DevBoards/Floppsy/floppsy_selftest) stays focused on hardware checks.

## Build and connect

Select **Adafruit Floppsy RP2350**, **240 MHz**, **Adafruit TinyUSB**. The board
definition provides 16 MB flash and 8 MB PSRAM. Tested with Arduino-Pico 6.1.1,
Adafruit GFX 1.12.6, ILI9341 1.6.3, INA3221 1.0.3, TinyUSB 3.7.7 and
SdFat - Adafruit Fork 2.3.103. Use Adafruit Floppy's
[`fix-rp2350-capture-validation`](https://github.com/ladyada-eagleclaw/Adafruit_Floppy/tree/fix-rp2350-capture-validation)
branch; stock 0.6.1 lacks the RP2350 fixes and strict sector-ID argument.

```sh
arduino-cli compile --fqbn rp2040:rp2040:adafruit_floppsy_rp2350:freq=240,usbstack=tinyusb --library /path/to/SdFat_-_Adafruit_Fork --library /path/to/Adafruit_Floppy floppsy_dumper
```

Connect one PC/Shugart or laptop FPC drive, a FAT/exFAT microSD, and the 12 V
barrel supply. Computer USB alone browned out this prototype on motor start;
the dumper requires measured 5 V and 12 V before enabling the drive. It does
not renegotiate USB PD. Keep the hardware **WRITE switch OFF**. No floppy
write/format operation exists in this sketch. Apple Disk II drive control
and the USB-A camera are not implemented here.

The plain TFT interface uses UP/DOWN for mode, LEFT/RIGHT for DS0/DS1 and SELECT
to start. DS1 is the default for the connected PC drive. BOOT or serial `!`
cancels. The motor, drive selects and both cable drivers turn off on exit.
File writes never overwrite existing captures or format the card.

## Modes and serial commands

| Command | DS1 operation |
| --- | --- |
| `a` | AUTO IBM: detect supported 80-cylinder IBM MFM geometries, then save IMG |
| `d` | Save 1.44 MB IMG: 80 cylinders, 2 heads, 18 × 512-byte sectors |
| `j` | Save 720 KB IMG: 80 cylinders, 2 heads, 9 × 512-byte sectors |
| `g` | Save raw SCP: 80 cylinders, 2 heads, 3 complete revolutions per side |
| `r` | Brief 1.44 MB track-zero sector read |
| `A/D/J/G/R` | The corresponding operation using DS0 |
| `x` | Download the last capture over computer USB serial |
| `m` | Download the last IMG's sector map |
| `!` | Cancel an active capture |
| `?` | Help |

AUTO tries 250, 500 and 1000 kbit/s on both heads at cylinders 0 and 2.
It recognizes 9/10/11, 18/21/22, or 36 consecutive 512-byte sectors per side:
720/800/880, 1440/1680/1760, or 2880 KiB. The 1680 KiB layout is used by DMF.
The 800/880 KiB choices here are IBM MFM layouts, **not** Apple or Amiga formats.
An ED-capable drive is required for 2880 KiB. It requires
complete CRC-valid sector sets on multiple sides, or a matching boot-sector
BPB plus a complete side. If the boot sector is damaged, it explicitly reports
that standard 80-cylinder geometry was inferred. This is not universal format
detection: other IBM geometries, FM, Amiga and Apple sector decoders are
not included. An unknown format is a reason to choose raw SCP, not to fabricate
an IMG geometry. Manual IMG modes remain available for badly damaged media.

## IMG results

IMG contains sectors in cylinder/head/sector order, without a header. Each side
gets up to three captures; good sectors survive retries. Both header and data
CRC must pass, and cylinder/head/sector size must match the requested location.

- `FDnnn.IMG`: every expected sector passed CRC.
- `FDnnn.BAD.IMG`: the full-sized image has missing sectors. Those sectors are
  zero-filled; the companion `FDnnn.MAP` identifies every good/bad CHS location.
- `FDnnn.PART`: interrupted capture or SD failure; never presented as complete.

Every written image block is read back from SD and compared before accepting
it. A finished file with bad sectors is not a lossless recovery. Keep the map
and capture raw flux as well when preservation matters.

## Raw SCP results

`FXnnn.SCP` is a
[Greaseweazle-supported SuperCard Pro flux image](https://github.com/keirf/greaseweazle/wiki/Supported-Image-Types),
not regenerated MFM data. Two synchronized PIO state machines timestamp
READ DATA and INDEX falling edges. DMA records four index marks, giving three
consecutive complete revolutions. Captures keep original timing at 25 ns
resolution; no speed normalization, filtering or sector decoding is applied.
Long intervals use SCP's zero-word extensions, including quiet regions across
index boundaries. An interval exactly divisible by 65,536 ticks has its edge
rounded one tick early to avoid SCP's reserved zero value; the following
interval compensates, so timing error does not accumulate.

SCP headers use little-endian fields, big-endian 16-bit flux words, 168 track
offsets and the additive file checksum. Each completed side is saved and
readback-verified, then the offset table/checksum is updated. Cancelled captures
retain `FXnnn.PART`; after completed sides, its SCP header describes those sides.
A power failure during an SD/header write is not guaranteed recoverable.

The capture reserves about 4.8 MB of PSRAM and uses bounded DMA/PIO resources.
FIFO overruns, full buffers, missing index and nonmonotonic timestamps reject
the capture. At 240 MHz the program uses **80 MHz PSRAM timing** with matching
receive delay, below the fitted APS6404L-3SQR's 84 MHz linear-burst limit. This
setting remains active until reset. SD writes use an SRAM staging buffer so
PSRAM latency cannot starve the PIO SDIO writer.

This first raw mode assumes a double-sided 80-cylinder 3.5-inch drive and uses
the HD density-select setting; some drives select density from the media hole.
It preserves non-IBM flux but is not yet a configurable archival reader for
arbitrary drives, track counts, density-select conventions or Apple mechanisms.

## USB download framing

Use a binary-capable serial client at 115200 (USB CDC ignores the physical baud
rate). Close other serial monitors first. After `x` or `m`, discard status lines
until `FILE filename bytecount\n`, receive exactly `bytecount` binary bytes, then
expect `\nEND\r\n`. Do not treat embedded newlines as delimiters inside the file.
The sender stops if the host disconnects/stops accepting data for three seconds,
or BOOT is pressed. Last-file names are remembered until firmware reset; files
remain on the microSD after reset. SD removal is another way to retrieve them.

## Host regression checks

`host_src/test_scp.cpp` exercises index boundaries, counter wrap, long gaps,
quiet tracks, output capacity and malformed timestamps using the production
encoder. Run from the library root. It optionally writes a synthetic SCP for validation with Greaseweazle.

```sh
g++ -std=c++11 -fsanitize=address,undefined host_src/test_scp.cpp src/Adafruit_FloppySCP.cpp -o /tmp/test_scp
/tmp/test_scp /tmp/synthetic.scp
```
