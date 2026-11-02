// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyImage.h"
#include <stdio.h>
#include <string.h>

// Format references: ADFlib's ADF FAQ, Commodore's 1541 disk format, and
// CiderPress II DOS/ProDOS notes. All reads are bounded by the supplied image.
// On-disk byte orders and packed flags require the masks below.
namespace {
uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
uint32_t le32(const uint8_t *p) {
  return le16(p) | ((uint32_t)le16(p + 2) << 16);
}
uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
bool amigaSum(const uint8_t *p) {
  uint32_t sum = 0;
  for (size_t i = 0; i < 512; i += 4) {
    sum += be32(p + i);
  }
  return sum == 0;
}
void text(char *dst, const uint8_t *src, size_t count, bool petscii = false) {
  for (size_t i = 0; i < count; i++) {
    uint8_t c = src[i];
    if (petscii && c >= 0xc1 && c <= 0xda) {
      c -= 0x80;
    } else {
      c &= 0x7f;
    }
    dst[i] = c >= 32 && c <= 126 ? c : '?';
  }
  while (count && dst[count - 1] == ' ') {
    count--;
  }
  dst[count] = 0;
}
uint8_t cbmSectors(uint8_t track) {
  if (track <= 17) {
    return 21;
  }
  if (track <= 24) {
    return 19;
  }
  if (track <= 30) {
    return 18;
  }
  return 17;
}
uint32_t cbmBlock(uint8_t track, uint8_t sector) {
  uint32_t block = sector;
  for (uint8_t t = 1; t < track; t++) {
    block += cbmSectors(t);
  }
  return block;
}
bool seen(uint8_t *visited, uint16_t block) {
  uint8_t mask = 1u << (block % 8);
  if (visited[block / 8] & mask) {
    return true;
  }
  visited[block / 8] |= mask;
  return false;
}
} // namespace

/** @brief Bind a reader without reading or modifying its image.
    @param reader Callback for exact byte reads.
    @param context Opaque callback context, owned by the caller.
    @param imageBytes Total accessible image bytes. */
Adafruit_FloppyImage::Adafruit_FloppyImage(Read reader, void *context,
                                           uint32_t imageBytes)
    : _reader(reader), _context(context), _size(imageBytes) {}

bool Adafruit_FloppyImage::fail(const char *message) {
  _error = message;
  return false;
}
bool Adafruit_FloppyImage::readAt(uint32_t offset, size_t length) {
  if (!_reader || length > sizeof(_buffer) || offset > _size ||
      length > _size - offset) {
    return fail("Read outside image");
  }
  if (!_reader(_context, offset, _buffer, length)) {
    return fail("Image read failed");
  }
  return true;
}

/** @brief Identify a supported filesystem from validated on-disk structures.
    @return True when a supported filesystem and root directory are found. */
bool Adafruit_FloppyImage::identify() {
  _format = UNKNOWN;
  _volume[0] = 0;
  _error = "";
  if (!_reader || _size < 512) {
    return fail("Image too small or no reader");
  }
  if (identifyFAT() || identifyAmiga() || identifyCBM() || identifyDOS33() ||
      identifyProDOS()) {
    _error = "";
    return true;
  }
  _format = UNKNOWN;
  _volume[0] = 0;
  return fail("Unknown or unsupported filesystem");
}

bool Adafruit_FloppyImage::identifyFAT() {
  if (!readAt(0)) {
    return false;
  }
  if (le16(_buffer + 11) != 512) {
    return false;
  }
  uint8_t cluster = _buffer[13], fats = _buffer[16], media = _buffer[21];
  uint16_t reserved = le16(_buffer + 14), rootEntries = le16(_buffer + 17);
  uint16_t fatSize = le16(_buffer + 22);
  uint32_t total = le16(_buffer + 19);
  if (!total) {
    total = le32(_buffer + 32);
  }
  if (!cluster || (cluster & (cluster - 1)) || !reserved || !fats || fats > 2 ||
      !rootEntries || !fatSize || media < 0xf0 || !total ||
      total > _size / 512) {
    return false;
  }
  uint32_t root = reserved + (uint32_t)fats * fatSize;
  uint32_t data = root + ((uint32_t)rootEntries * 32 + 511) / 512;
  if (data >= total || (total - data) / cluster == 0 ||
      (total - data) / cluster >= 4085 ||
      (total - data) / cluster + 2 > (uint32_t)fatSize * 512 * 2 / 3) {
    return false;
  }
  if (!readAt((uint32_t)reserved * 512) || _buffer[0] != media ||
      (_buffer[1] & 15) != 15 || _buffer[2] != 255) {
    return false;
  }
  _root = root * 512;
  _entries = rootEntries;
  _format = FAT12;
  // Prefer the directory's volume label, which is authoritative on old disks.
  for (uint32_t i = 0; i < _entries; i += 16) {
    if (!readAt(_root + (uint32_t)i * 32)) {
      return false;
    }
    for (uint8_t j = 0; j < 16 && i + j < _entries; j++) {
      const uint8_t *e = _buffer + j * 32;
      if (!e[0]) {
        return true;
      }
      if (e[0] != 0xe5 && e[11] == 8) {
        text(_volume, e, 11);
        return true;
      }
    }
  }
  return true;
}

bool Adafruit_FloppyImage::identifyAmiga() {
  if (_size != 901120 && _size != 1802240) {
    return false;
  }
  if (!readAt(0) || memcmp(_buffer, "DOS", 3) || _buffer[3] > 5) {
    return false;
  }
  uint8_t type = _buffer[3];
  _blocks = _size / 512;
  _root = _blocks / 2;
  if (!readAt(_root * 512) || !amigaSum(_buffer) || be32(_buffer) != 2 ||
      be32(_buffer + 12) != 72 || be32(_buffer + 508) != 1 ||
      _buffer[432] > 30) {
    return false;
  }
  text(_volume, _buffer + 433, _buffer[432]);
  _format = (type & 1) ? AMIGA_FFS : AMIGA_OFS;
  return true;
}

bool Adafruit_FloppyImage::identifyCBM() {
  if (_size == 174848 || _size == 175531) {
    _tracks = 35;
  } else if (_size == 196608 || _size == 197376) {
    _tracks = 40;
  } else if (_size == 205312 || _size == 206114) {
    _tracks = 42;
  } else {
    return false;
  }
  if (!readAt(cbmBlock(18, 0) * 256, 256) || _buffer[0] != 18 ||
      _buffer[1] >= 19 || _buffer[2] != 'A' || _buffer[3] != 0 ||
      _buffer[0xa5] != '2' || _buffer[0xa6] != 'A') {
    return false;
  }
  _root = _buffer[1];
  text(_volume, _buffer + 0x90, 16, true);
  _format = CBM_DOS;
  return true;
}

bool Adafruit_FloppyImage::identifyDOS33() {
  if (_size != 143360 && _size != 163840) {
    return false;
  }
  if (!readAt(17 * 4096, 256) || _buffer[3] != 3 || _buffer[0x27] != 122 ||
      _buffer[0x35] != 16 || le16(_buffer + 0x36) != 256 ||
      _buffer[0x34] != _size / 4096 || !_buffer[1] ||
      _buffer[1] >= _buffer[0x34] || _buffer[2] >= 16) {
    return false;
  }
  _tracks = _buffer[0x34];
  _catalogTrack = _buffer[1];
  _catalogSector = _buffer[2];
  snprintf(_volume, sizeof(_volume), "VOLUME %u", _buffer[6]);
  _format = APPLE_DOS33;
  return true;
}

bool Adafruit_FloppyImage::identifyProDOS() {
  if (_size % 512 || _size / 512 > 4096 || !readAt(1024)) {
    return false;
  }
  const uint8_t *header = _buffer + 4;
  uint8_t length = header[0] & 15;
  uint16_t total = le16(header + 0x25), bitmap = le16(header + 0x23);
  if (header[0] / 16 != 15 || !length || header[0x1f] != 39 ||
      header[0x20] != 13 || le16(_buffer) != 0 || total < 7 ||
      total > _size / 512 || bitmap < 3 || bitmap >= total) {
    return false;
  }
  text(_volume, header + 1, length);
  _blocks = total;
  _format = APPLE_PRODOS;
  return true;
}

/** @brief Enumerate the root directory with bounded traversal and read checks.
    @param visitor Called once per live entry; false requests an early stop.
    @param context Opaque visitor context.
    @return True on completion/visitor stop, false for invalid data or I/O. */
bool Adafruit_FloppyImage::listRoot(Visit visitor, void *context) {
  _error = "";
  if (!visitor) {
    return fail("No directory visitor");
  }
  switch (_format) {
  case FAT12:
    return listFAT(visitor, context);
  case AMIGA_OFS:
  case AMIGA_FFS:
    return listAmiga(visitor, context);
  case CBM_DOS:
    return listCBM(visitor, context);
  case APPLE_DOS33:
    return listDOS33(visitor, context);
  case APPLE_PRODOS:
    return listProDOS(visitor, context);
  default:
    return fail("Identify the image first");
  }
}

bool Adafruit_FloppyImage::listFAT(Visit visitor, void *context) {
  for (uint32_t i = 0; i < _entries; i += 16) {
    if (!readAt(_root + (uint32_t)i * 32)) {
      return false;
    }
    for (uint8_t j = 0; j < 16 && i + j < _entries; j++) {
      const uint8_t *p = _buffer + j * 32;
      if (!p[0]) {
        return true;
      }
      if (p[0] == 0xe5 || p[11] == 0x0f || (p[11] & 8)) {
        continue;
      }
      Entry entry = {};
      text(entry.name, p, 8);
      char extension[4];
      text(extension, p + 8, 3);
      if (extension[0]) {
        strcat(entry.name, ".");
        strcat(entry.name, extension);
      }
      entry.bytes = le32(p + 28);
      entry.exactBytes = (p[11] & 16) == 0;
      entry.directory = (p[11] & 16) != 0;
      entry.locked = (p[11] & 1) != 0;
      strcpy(entry.kind, entry.directory ? "DIR" : "FILE");
      if (!visitor(context, entry)) {
        return true;
      }
    }
  }
  return true;
}

bool Adafruit_FloppyImage::listAmiga(Visit visitor, void *context) {
  uint8_t visited[512] = {};
  uint32_t buckets[72];
  if (!readAt(_root * 512) || !amigaSum(_buffer)) {
    return fail("Invalid Amiga root");
  }
  for (uint8_t i = 0; i < 72; i++) {
    buckets[i] = be32(_buffer + 24 + i * 4);
  }
  for (uint8_t bucket = 0; bucket < 72; bucket++) {
    uint32_t block = buckets[bucket];
    while (block) {
      if (block >= _blocks || block < 2 || seen(visited, block)) {
        return fail("Invalid Amiga directory chain");
      }
      if (!readAt(block * 512)) {
        return false;
      }
      int32_t kind = (int32_t)be32(_buffer + 508);
      if (!amigaSum(_buffer) || be32(_buffer) != 2 ||
          be32(_buffer + 4) != block || be32(_buffer + 500) != _root ||
          !_buffer[432] || _buffer[432] > 30) {
        return fail("Invalid Amiga entry");
      }
      Entry entry = {};
      text(entry.name, _buffer + 433, _buffer[432]);
      entry.bytes = be32(_buffer + 324);
      entry.directory = kind == 2;
      entry.exactBytes = kind == -3;
      entry.locked = (be32(_buffer + 320) & 4) != 0;
      if (entry.directory) {
        strcpy(entry.kind, "DIR");
      } else if (kind == -3) {
        strcpy(entry.kind, "FILE");
      } else {
        strcpy(entry.kind, "LINK");
      }
      block = be32(_buffer + 496);
      if (!visitor(context, entry)) {
        return true;
      }
    }
  }
  return true;
}

bool Adafruit_FloppyImage::listCBM(Visit visitor, void *context) {
  uint8_t visited[101] = {}, track = 18, sector = _root;
  const char *types[] = {"DEL", "SEQ", "PRG",     "USR",
                         "REL", "CBM", "UNKNOWN", "UNKNOWN"};
  while (track) {
    if (track > _tracks || sector >= cbmSectors(track)) {
      return fail("Invalid CBM directory pointer");
    }
    uint32_t block = cbmBlock(track, sector);
    if (seen(visited, block)) {
      return fail("Loop in CBM directory");
    }
    if (!readAt(block * 256, 256)) {
      return false;
    }
    track = _buffer[0];
    sector = _buffer[1];
    for (uint8_t i = 0; i < 8; i++) {
      const uint8_t *p = _buffer + 2 + i * 32;
      if (!p[0]) {
        continue;
      }
      Entry entry = {};
      text(entry.name, p + 3, 16, true);
      strcpy(entry.kind, types[p[0] & 7]);
      entry.blocks = le16(p + 28);
      entry.locked = (p[0] & 0x40) != 0;
      if (!visitor(context, entry)) {
        return true;
      }
    }
  }
  return true;
}

bool Adafruit_FloppyImage::listDOS33(Visit visitor, void *context) {
  uint8_t visited[80] = {}, track = _catalogTrack, sector = _catalogSector;
  while (track) {
    if (track >= _tracks || sector >= 16) {
      return fail("Invalid DOS catalog pointer");
    }
    uint16_t block = track * 16 + sector;
    if (seen(visited, block)) {
      return fail("Loop in DOS catalog");
    }
    if (!readAt((uint32_t)block * 256, 256)) {
      return false;
    }
    track = _buffer[1];
    sector = _buffer[2];
    for (uint8_t i = 0; i < 7; i++) {
      const uint8_t *p = _buffer + 11 + i * 35;
      if (!p[0]) {
        return true;
      }
      if (p[0] == 255) {
        continue;
      }
      if (p[0] >= _tracks || p[1] >= 16) {
        return fail("Invalid DOS file pointer");
      }
      Entry entry = {};
      text(entry.name, p + 3, 30);
      entry.blocks = le16(p + 33);
      entry.locked = (p[2] & 0x80) != 0;
      switch (p[2] & 0x7f) {
      case 0:
        strcpy(entry.kind, "TEXT");
        break;
      case 1:
        strcpy(entry.kind, "INTEGER");
        break;
      case 2:
        strcpy(entry.kind, "BASIC");
        break;
      case 4:
        strcpy(entry.kind, "BINARY");
        break;
      default:
        strcpy(entry.kind, "FILE");
        break;
      }
      if (!visitor(context, entry)) {
        return true;
      }
    }
  }
  return true;
}

bool Adafruit_FloppyImage::listProDOS(Visit visitor, void *context) {
  uint8_t visited[512] = {};
  uint16_t block = 2, previous = 0;
  while (block) {
    if (block >= _blocks || seen(visited, block)) {
      return fail("Invalid ProDOS directory chain");
    }
    if (!readAt((uint32_t)block * 512)) {
      return false;
    }
    if (le16(_buffer) != previous) {
      return fail("Broken ProDOS back pointer");
    }
    uint16_t next = le16(_buffer + 2);
    for (uint8_t i = block == 2 ? 1 : 0; i < 13; i++) {
      const uint8_t *p = _buffer + 4 + i * 39;
      uint8_t kind = p[0] / 16, length = p[0] & 15;
      if (!kind) {
        continue;
      }
      if (!length || (kind > 5 && kind != 13) || le16(p + 17) >= _blocks) {
        return fail("Invalid ProDOS entry");
      }
      Entry entry = {};
      text(entry.name, p + 1, length);
      entry.blocks = le16(p + 19);
      entry.bytes = le16(p + 21) | ((uint32_t)p[23] << 16);
      entry.exactBytes = kind <= 3;
      entry.directory = kind == 13;
      entry.locked = (p[30] & 2) == 0;
      strcpy(entry.kind, entry.directory ? "DIR" : "FILE");
      if (!visitor(context, entry)) {
        return true;
      }
    }
    previous = block;
    block = next;
  }
  return true;
}

/** @brief Return the last successfully identified format.
    @return Format, or UNKNOWN before identification/after failure. */
Adafruit_FloppyImage::Format Adafruit_FloppyImage::format() const {
  return _format;
}
/** @brief Return a human-readable filesystem name without guessing the host OS.
    @return Static name string. */
const char *Adafruit_FloppyImage::formatName() const {
  switch (_format) {
  case FAT12:
    return "FAT12 (PC / Atari ST compatible)";
  case AMIGA_OFS:
    return "Amiga OFS";
  case AMIGA_FFS:
    return "Amiga FFS";
  case CBM_DOS:
    return "Commodore 1541 DOS";
  case APPLE_DOS33:
    return "Apple II DOS 3.3";
  case APPLE_PRODOS:
    return "Apple ProDOS";
  default:
    return "Unknown";
  }
}
/** @brief Return the volume label, or an empty string when none is present.
    @return Object-owned NUL-terminated label. */
const char *Adafruit_FloppyImage::volumeName() const { return _volume; }
/** @brief Explain the last failed operation.
    @return Static error string, empty after a successful operation. */
const char *Adafruit_FloppyImage::error() const { return _error; }
