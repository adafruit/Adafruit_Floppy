# Read-only image catalogs

Run `make check-catalog` with a C++11 compiler and Python 3. To keep the generated
good and bad images for inspection, run
`python3 test_image_catalog.py /tmp/floppsy-image-catalog --fixtures NEW_DIRECTORY`.
These are minimal catalog fixtures, not bootable operating-system disks.

The 26 cases cover FAT12, Amiga OFS/FFS, Commodore 1541 DOS, Apple DOS 3.3,
and ProDOS; invalid geometry, checksums, pointers, loops, truncated images, and
unformatted media. The same tests also pass with GCC AddressSanitizer and
UndefinedBehaviorSanitizer. The host CLI uses the library implementation directly.

The catalog API lists root entries only. It does not validate allocation maps,
file chains, executable contents, or optional D64 sector-error tables. Apple
images must use DOS order for DOS 3.3 and block order for ProDOS. Character names
are rendered as printable ASCII approximations; custom PETSCII glyphs and
international characters are not preserved as Unicode. Amiga directory caches
are ignored in favor of the authoritative hash table.

## External images checked separately

The following Internet Archive images were checked locally on 2026-11-01.
They are not bundled or downloaded by the regression test. Hashes pin the exact
inputs; availability and rights remain with their respective sources.

| Source | SHA-256 | Root result |
| --- | --- | --- |
| [Fred Fish 857 ADF](https://archive.org/details/amiga-floppy-fredfish-857) | `11efe117ff36a97f92051b94d26873aed6aef15fe1ba28d8cda39527151e6e19` | Amiga OFS, AmigaLibDisk857, 24 entries |
| [Public Domain 64 Collection Number 5 D64](https://archive.org/details/loadstar-side-1) | `8c4923cd5baecd5919d65e6b247ab52df0d7967ec90d5dcd6379505229c4872a` | Commodore 1541 DOS, 64 COLLECTION #5, 41 entries |
| [DOS 3.3 Sample Programs](https://archive.org/details/woodbridge_DOS_3.3_Sample_Programs) | `2202d2ec9d255a3b1a3bdb692b370c73885f92449b12ad70439c59e810247b18` | Apple II DOS 3.3, volume 1, 26 entries |

The local FAT12 scratch-disk image also lists README.TXT, HELLO.BAT, PATTERN.BIN
and TAIL.BIN with the same names and lengths as 7-Zip's independent FAT reader.
ProDOS currently has generated-fixture coverage, not an archived-image result.

Format references:

- [ADFlib format documentation](https://adflib.github.io/FAQ/adf_info.html)
- [CiderPress II DOS notes](https://ciderpress2.com/formatdoc/DOS-notes.html)
- [CiderPress II ProDOS notes](https://ciderpress2.com/formatdoc/ProDOS-notes.html)
- [Commodore 1541 manual](https://project64.c64.org/Hardware/Peripherals/vic1541.pdf)

Filesystem identification is separate from magnetic encoding. These catalog
results do not establish C64, Amiga, or Disk II drive-interface support.
