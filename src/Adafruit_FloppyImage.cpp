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
uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
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
bool seen(uint8_t *visited, uint16_t block) {
  uint8_t mask = 1u << (block % 8);
  if (visited[block / 8] & mask) {
    return true;
  }
  visited[block / 8] |= mask;
  return false;
}
} // namespace

uint8_t Adafruit_FloppyImage::cbmSectors(uint8_t track) const {
  // D81 has 80 logical tracks, each containing both physical heads as
  // forty 256-byte blocks. D64 retains its original four density zones.
  if (_tracks == 80) {
    return 40;
  }
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
uint32_t Adafruit_FloppyImage::cbmBlock(uint8_t track, uint8_t sector) const {
  uint32_t block = sector;
  for (uint8_t t = 1; t < track; t++) {
    block += cbmSectors(t);
  }
  return block;
}

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
      identifyProDOS() || identifyHFS()) {
    _error = "";
    return true;
  }
  _format = UNKNOWN;
  _volume[0] = 0;
  return fail("Unknown or unsupported filesystem");
}

// Classic HFS layouts: Apple's hfs_format.h (HFSMasterDirectoryBlock,
// HFSPlus's shared BTNodeDescriptor/BTHeaderRec, HFSCatalogKey/File/Folder).
// https://github.com/apple-oss-distributions/hfs/blob/main/core/hfs_format.h
// HFS uses big-endian fields and 512-byte B-tree nodes. This reader accepts a
// raw volume, not a Disk Copy container, partition map, MFS or HFS+ volume.
bool Adafruit_FloppyImage::identifyHFS() {
  if (!readAt(1024) || be16(_buffer) != 0x4244 || !_buffer[36] ||
      _buffer[36] > 27 || be16(_buffer + 124) == 0x482b ||
      be16(_buffer + 124) == 0x4858)
    return false;
  _blocks = be16(_buffer + 18);
  _hfsBlockSize = be32(_buffer + 20);
  _hfsStart = (uint32_t)be16(_buffer + 28) * 512;
  if (!_blocks || !_hfsBlockSize || _hfsBlockSize % 512 || _hfsStart < 1536 ||
      _hfsStart > _size || _blocks > (_size - _hfsStart) / _hfsBlockSize)
    return false;
  for (uint8_t i = 0; i < _buffer[36]; i++) {
    uint8_t c = _buffer[37 + i];
    _volume[i] = c >= 32 && c < 127 ? c : '?';
  }
  _volume[_buffer[36]] = 0;
  _hfsExtentsSize = be32(_buffer + 130);
  _hfsCatalogSize = be32(_buffer + 146);
  for (uint8_t i = 0; i < 6; i++) {
    _hfsExtents[i] = be16(_buffer + 134 + i * 2);
    _hfsCatalog[i] = be16(_buffer + 150 + i * 2);
  }
  for (uint8_t tree = 0; tree < 2; tree++) {
    uint32_t bytes = tree ? _hfsCatalogSize : _hfsExtentsSize;
    if (bytes < 512 || bytes > _size || bytes % 512 || !hfsNode(tree, 0) ||
        _buffer[8] != 1 || be16(_buffer + 10) != 3)
      return false;
    uint16_t start, end;
    if (!hfsRecord(0, start, end) || start != 14 || end - start < 106 ||
        be16(_buffer + start + 18) != 512 || !be32(_buffer + start + 22) ||
        be32(_buffer + start + 22) > bytes / 512 ||
        be32(_buffer + start + 10) >= bytes / 512 ||
        be32(_buffer + start + 14) >= bytes / 512)
      return false;
    if (tree && (!be32(_buffer + start + 10) || !be32(_buffer + start + 14)))
      return false;
  }
  _format = MAC_HFS;
  return true;
}

// Map a fork's first three extents. Overflow-tree storage itself must fit in
// these extents; ordinary catalog/file overflow is resolved separately below.
bool Adafruit_FloppyImage::hfsRead(const uint16_t *extents, uint32_t offset,
                                   uint8_t *data, size_t length) {
  for (uint8_t i = 0; i < 6 && length; i += 2) {
    uint32_t start = extents[i], count = extents[i + 1];
    if (!count || start >= _blocks || count > _blocks - start)
      return fail("Invalid HFS extent");
    uint32_t bytes = count * _hfsBlockSize;
    if (offset >= bytes) {
      offset -= bytes;
      continue;
    }
    size_t n = length < bytes - offset ? length : bytes - offset;
    uint32_t physical = _hfsStart + start * _hfsBlockSize + offset;
    if (!_reader(_context, physical, data, n))
      return fail("HFS extent read failed");
    data += n;
    length -= n;
    offset = 0;
  }
  return !length || fail("HFS overflow-tree storage unsupported");
}

bool Adafruit_FloppyImage::hfsFork(uint32_t fileID, const uint16_t *initial,
                                   uint32_t offset, uint8_t *data,
                                   size_t length) {
  uint16_t extents[6];
  memcpy(extents, initial, sizeof(extents));
  uint32_t first = 0;
  while (length) {
    uint32_t count = 0;
    bool ended = false;
    for (uint8_t i = 0; i < 6; i += 2) {
      if (!extents[i + 1]) {
        ended = true;
        continue;
      }
      if (ended || extents[i] >= _blocks ||
          extents[i + 1] > _blocks - extents[i])
        return fail("Invalid HFS fork extent");
      count += extents[i + 1];
    }
    if (!count || count > _blocks - first)
      return fail("Invalid HFS fork allocation");
    uint32_t bytes = count * _hfsBlockSize;
    if (offset < bytes) {
      size_t n = length < bytes - offset ? length : bytes - offset;
      if (!hfsRead(extents, offset, data, n))
        return false;
      data += n;
      length -= n;
      offset = 0;
    } else
      offset -= bytes;
    first += count;
    if (length && !hfsOverflow(fileID, first, extents))
      return false;
  }
  return true;
}

bool Adafruit_FloppyImage::hfsNode(bool catalog, uint32_t node) {
  uint32_t bytes = catalog ? _hfsCatalogSize : _hfsExtentsSize;
  if (node >= bytes / 512)
    return fail("HFS node outside tree");
  if (catalog)
    return hfsFork(4, _hfsCatalog, node * 512, _buffer, 512);
  return hfsRead(_hfsExtents, node * 512, _buffer, 512);
}

bool Adafruit_FloppyImage::hfsRecord(uint16_t index, uint16_t &start,
                                     uint16_t &end) {
  uint16_t records = be16(_buffer + 10);
  if (records > 248 || index >= records)
    return fail("Invalid HFS record count");
  start = be16(_buffer + 510 - index * 2);
  end = be16(_buffer + 508 - index * 2);
  if (start < 14 || start % 2 || end <= start || end > 510 - records * 2)
    return fail("Invalid HFS record bounds");
  return true;
}

bool Adafruit_FloppyImage::hfsOverflow(uint32_t fileID, uint16_t firstBlock,
                                       uint16_t *extents) {
  if (!hfsNode(false, 0))
    return false;
  uint32_t node = be32(_buffer + 24), previous = 0;
  uint32_t limit = _hfsExtentsSize / 512;
  for (uint32_t step = 0; node && step < limit; step++) {
    if (!hfsNode(false, node) || _buffer[8] != 255 || _buffer[9] != 1 ||
        be32(_buffer + 4) != previous)
      return fail("Invalid HFS overflow chain");
    uint32_t next = be32(_buffer);
    uint16_t records = be16(_buffer + 10);
    for (uint16_t i = 0; i < records; i++) {
      uint16_t start, end;
      if (!hfsRecord(i, start, end) || end - start < 20 || _buffer[start] != 7)
        return fail("Invalid HFS overflow record");
      // Fork type zero denotes a data fork (catalog is file ID 4).
      const uint8_t *p = _buffer + start;
      if (p[1] == 0 && be32(p + 2) == fileID && be16(p + 6) == firstBlock) {
        for (uint8_t j = 0; j < 6; j++)
          extents[j] = be16(p + 8 + j * 2);
        return true;
      }
    }
    previous = node;
    node = next;
  }
  return fail("Missing HFS overflow extent");
}

// Entry IDs for files are record offsets in the catalog fork; directory IDs
// are HFS CNIDs. Decode only bounded records from a validated leaf node.
bool Adafruit_FloppyImage::hfsEntry(uint32_t id, uint16_t start, uint16_t end,
                                    Entry &entry) {
  const uint8_t *key = _buffer + start;
  // Some writers include the key's even-alignment padding in keyLength.
  if (end - start < 8 || key[6] > 31 || key[0] < 6 + key[6] ||
      key[0] > 7 + key[6])
    return fail("Invalid HFS catalog key");
  uint16_t body = start + (key[0] + 2) / 2 * 2;
  if (body + 2 > end)
    return fail("Short HFS catalog record");
  const uint8_t *p = _buffer + body;
  uint16_t kind = be16(p);
  entry = {};
  if ((kind != 0x0100 && kind != 0x0200) ||
      end - body < (kind == 0x0100 ? 70 : 102) || !key[6])
    return fail("Unsupported HFS catalog record");
  entry.directory = kind == 0x0100;
  entry.id = entry.directory ? be32(p + 6) : id;
  for (uint8_t i = 0; i < key[6]; i++) {
    uint8_t c = key[7 + i];
    entry.name[i] = c >= 32 && c < 127 ? c : '?';
  }
  strcpy(entry.kind, entry.directory ? "DIR" : "DATA");
  if (!entry.directory) {
    entry.bytes = be32(p + 26);
    uint32_t allocated = be32(p + 30);
    if (entry.bytes > allocated || allocated > _size ||
        allocated % _hfsBlockSize)
      return fail("Invalid HFS data fork size");
    entry.blocks = allocated / _hfsBlockSize;
    entry.exactBytes = true;
    entry.locked = (p[2] & 1) != 0;
  }
  return true;
}

bool Adafruit_FloppyImage::hfsFile(uint32_t id, Entry &entry, uint32_t &fileID,
                                   uint16_t *extents) {
  if (!hfsNode(true, id / 512) || _buffer[8] != 255 || _buffer[9] != 1)
    return fail("Invalid HFS file node");
  uint16_t records = be16(_buffer + 10);
  for (uint16_t i = 0; i < records; i++) {
    uint16_t start, end;
    if (!hfsRecord(i, start, end))
      return false;
    if (start != id % 512)
      continue;
    if (!hfsEntry(id, start, end, entry) || entry.directory)
      return fail("Invalid HFS file entry");
    const uint8_t *p = _buffer + start + (_buffer[start] + 2) / 2 * 2;
    fileID = be32(p + 20);
    if (fileID < 16)
      return fail("Invalid HFS file ID");
    for (uint8_t j = 0; j < 6; j++)
      extents[j] = be16(p + 74 + j * 2);
    return true;
  }
  return fail("Missing HFS file record");
}

bool Adafruit_FloppyImage::listHFS(uint32_t directory, Visit visitor,
                                   void *context) {
  if (!hfsNode(true, 0))
    return false;
  uint32_t node = be32(_buffer + 24), last = be32(_buffer + 28), previous = 0;
  uint32_t limit = _hfsCatalogSize / 512;
  for (uint32_t step = 0; node && step < limit; step++) {
    if (!hfsNode(true, node) || _buffer[8] != 255 || _buffer[9] != 1 ||
        be32(_buffer + 4) != previous)
      return fail("Invalid HFS catalog chain");
    uint32_t next = be32(_buffer);
    uint16_t records = be16(_buffer + 10);
    for (uint16_t i = 0; i < records; i++) {
      uint16_t start, end;
      if (!hfsRecord(i, start, end) || end - start < 8)
        return fail("Invalid HFS catalog record");
      if (be32(_buffer + start + 2) != directory)
        continue;
      // Empty keys are file/directory thread records, not visible entries.
      if (!_buffer[start + 6])
        continue;
      Entry entry;
      if (!hfsEntry(node * 512 + start, start, end, entry))
        return false;
      if (!visitor(context, entry))
        return true;
    }
    previous = node;
    node = next;
  }
  return (!node && previous == last) || fail("Loop or truncated HFS catalog");
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
  if (_size == 819200 || _size == 822400) {
    _tracks = 80;
    // Standard 1581 root header: logical track 40, block 0. Its directory
    // starts after the header and two BAM blocks. Partitions are not followed.
    if (!readAt(cbmBlock(40, 0) * 256, 256) || _buffer[0] != 40 ||
        _buffer[1] < 3 || _buffer[1] >= 40 || _buffer[2] != 'D' ||
        _buffer[3] != 0 || _buffer[25] != '3' || _buffer[26] != 'D') {
      return false;
    }
    _catalogTrack = 40;
    _catalogSector = _buffer[1];
    text(_volume, _buffer + 4, 16, true);
    _format = CBM_1581;
    return true;
  }
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
  _catalogTrack = 18;
  _catalogSector = _buffer[1];
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
  return listDirectory(0, visitor, context);
}

/** @brief List a directory in an identified image.
    @param directory Zero for root, or an Amiga/HFS directory Entry::id.
    @param visitor Callback for each live entry.
    @param context Caller-owned visitor context.
    @return True on completion or visitor stop; false on I/O or invalid data.
    @note Subdirectories are supported for Amiga OFS/FFS and classic HFS. */
bool Adafruit_FloppyImage::listDirectory(uint32_t directory, Visit visitor,
                                         void *context) {
  _error = "";
  if (!visitor) {
    return fail("No directory visitor");
  }
  if (directory && _format != AMIGA_OFS && _format != AMIGA_FFS &&
      _format != MAC_HFS) {
    return fail("Subdirectories unsupported for this format");
  }
  switch (_format) {
  case FAT12:
    return listFAT(visitor, context);
  case AMIGA_OFS:
  case AMIGA_FFS:
    return listAmiga(directory ? directory : _root, visitor, context);
  case CBM_DOS:
  case CBM_1581:
    return listCBM(visitor, context);
  case APPLE_DOS33:
    return listDOS33(visitor, context);
  case APPLE_PRODOS:
    return listProDOS(visitor, context);
  case MAC_HFS:
    return listHFS(directory ? directory : 2, visitor, context);
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

bool Adafruit_FloppyImage::listAmiga(uint32_t directory, Visit visitor,
                                     void *context) {
  uint8_t visited[512] = {};
  uint32_t buckets[72];
  if (directory < 2 || directory >= _blocks || !readAt(directory * 512) ||
      !amigaSum(_buffer) || be32(_buffer) != 2 ||
      be32(_buffer + 508) != (directory == _root ? 1u : 2u) ||
      (directory != _root && be32(_buffer + 4) != directory)) {
    return fail("Invalid Amiga directory");
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
          be32(_buffer + 4) != block || be32(_buffer + 500) != directory ||
          !_buffer[432] || _buffer[432] > 30) {
        return fail("Invalid Amiga entry");
      }
      Entry entry = {};
      entry.id = block;
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
  uint8_t visited[400] = {}, track = _catalogTrack, sector = _catalogSector;
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
    if (!readCBMBlock(block)) {
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
      entry.id = block * 256 + 2 + i * 32;
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

bool Adafruit_FloppyImage::readCBMBlock(uint32_t block) {
  uint32_t blocks = cbmBlock(_tracks, cbmSectors(_tracks));
  if (block >= blocks) {
    return fail("CBM sector outside image");
  }
  // Some D64/D81 images append one drive-error byte per logical block. Do not
  // silently turn a sector explicitly marked unreadable into successful file
  // contents.
  if (_size == blocks * 257) {
    if (!readAt(blocks * 256 + block, 1)) {
      return false;
    }
    if (_buffer[0] != 1) {
      return fail("CBM sector has recorded read error");
    }
  }
  return readAt(block * 256, 256);
}

/** @brief Open an Amiga, Commodore or classic HFS file without changing it.
    @param id Identifier returned in a directory Entry.
    @param entry Receives validated metadata and exact byte length.
    @return True for a readable regular file; false for links, directories,
    unsupported files, or corrupt data. Commodore PRG load-address bytes are
   retained.
    @note Commodore REL data is exposed as its linear byte stream, not records.
    HFS exposes the data fork; resource forks remain in the whole image.
 */
bool Adafruit_FloppyImage::openFile(uint32_t id, Entry &entry) {
  _error = "";
  entry = {};
  entry.id = id;
  if (_format == MAC_HFS) {
    uint32_t fileID;
    uint16_t extents[6];
    return hfsFile(id, entry, fileID, extents);
  }
  if (_format == AMIGA_OFS || _format == AMIGA_FFS) {
    if (id < 2 || id >= _blocks || !readAt(id * 512) || !amigaSum(_buffer) ||
        be32(_buffer) != 2 || be32(_buffer + 4) != id ||
        (int32_t)be32(_buffer + 508) != -3 || !_buffer[432] ||
        _buffer[432] > 30) {
      return fail("Invalid Amiga file header");
    }
    text(entry.name, _buffer + 433, _buffer[432]);
    strcpy(entry.kind, "FILE");
    entry.bytes = be32(_buffer + 324);
    if (entry.bytes > _size) {
      return fail("Amiga file exceeds image");
    }
  } else if (_format == CBM_DOS || _format == CBM_1581) {
    if (id % 32 != 2 || !readCBMBlock(id / 256)) {
      return fail("Invalid CBM file entry");
    }
    const uint8_t *p = _buffer + id % 256;
    uint8_t kind = p[0] & 7;
    if (!(p[0] & 0x80) || kind < 1 || kind > 4) {
      return fail("CBM file is unclosed or unsupported");
    }
    text(entry.name, p + 3, 16, true);
    const char *kinds[] = {"", "SEQ", "PRG", "USR", "REL"};
    strcpy(entry.kind, kinds[kind]);
    entry.blocks = le16(p + 28);
    if (!readCBM(entry, 0, nullptr, 0)) {
      return false;
    }
  } else {
    return fail("File bytes unsupported for this format");
  }
  entry.exactBytes = true;
  return true;
}

/** @brief Read raw file bytes, retaining original character encoding.
    @param entry File metadata returned by openFile(), for this same image.
    @param offset Byte offset within the file.
    @param data Destination for up to length bytes.
    @param length Capacity of data; reads stop at end of file.
    @return Bytes read, zero at EOF, or -1 on failure. Discard data on failure.
    @note Supports Amiga OFS/FFS, D64/D81 and classic HFS data forks. */
int32_t Adafruit_FloppyImage::readFile(const Entry &entry, uint32_t offset,
                                       uint8_t *data, size_t length) {
  _error = "";
  if (!entry.exactBytes || entry.directory || offset > entry.bytes ||
      (!data && length)) {
    fail("Invalid file read");
    return -1;
  }
  if (length > entry.bytes - offset) {
    length = entry.bytes - offset;
  }
  bool valid = false;
  if (_format == MAC_HFS) {
    Entry checked;
    uint32_t fileID;
    uint16_t extents[6];
    valid = hfsFile(entry.id, checked, fileID, extents) &&
            checked.bytes == entry.bytes &&
            hfsFork(fileID, extents, offset, data, length);
  } else if (_format == AMIGA_OFS || _format == AMIGA_FFS) {
    valid = readAmiga(entry, offset, data, length);
  } else if (_format == CBM_DOS || _format == CBM_1581) {
    Entry checked = entry;
    valid =
        readCBM(checked, offset, data, length) && checked.bytes == entry.bytes;
    if (!valid && !_error[0]) {
      fail("CBM file length changed");
    }
  } else {
    fail("File bytes unsupported for this format");
  }
  return valid ? (int32_t)length : -1;
}

bool Adafruit_FloppyImage::readCBM(Entry &entry, uint32_t offset, uint8_t *data,
                                   size_t length) {
  if (entry.id % 32 != 2 || !readCBMBlock(entry.id / 256)) {
    return fail("Invalid CBM file entry");
  }
  uint8_t track = _buffer[entry.id % 256 + 1];
  uint8_t sector = _buffer[entry.id % 256 + 2];
  uint8_t visited[400] = {};
  uint32_t position = 0;
  while (track) {
    if (track > _tracks || sector >= cbmSectors(track)) {
      return fail("Invalid CBM file pointer");
    }
    uint32_t block = cbmBlock(track, sector);
    if (seen(visited, block)) {
      return fail("Loop in CBM file");
    }
    if (!readCBMBlock(block)) {
      return false;
    }
    track = _buffer[0];
    sector = _buffer[1];
    if (!track && !sector) {
      return fail("Invalid CBM final byte count");
    }
    uint16_t bytes = track ? 254 : sector - 1;
    if (length && offset < position + bytes) {
      uint16_t skip = offset > position ? offset - position : 0;
      size_t count = bytes - skip;
      if (count > length) {
        count = length;
      }
      memcpy(data, _buffer + 2 + skip, count);
      data += count;
      offset += count;
      length -= count;
    }
    position += bytes;
  }
  entry.bytes = position;
  return length == 0 || fail("Truncated CBM file");
}

bool Adafruit_FloppyImage::readAmiga(const Entry &entry, uint32_t offset,
                                     uint8_t *data, size_t length) {
  uint8_t visited[512] = {};
  uint32_t table = entry.id, position = 0, sequence = 0;
  const uint16_t payload = _format == AMIGA_OFS ? 488 : 512;
  do {
    if (table < 2 || table >= _blocks || seen(visited, table) ||
        !readAt(table * 512) || !amigaSum(_buffer) ||
        be32(_buffer) != (table == entry.id ? 2u : 16u) ||
        be32(_buffer + 4) != table || (int32_t)be32(_buffer + 508) != -3 ||
        (table != entry.id && be32(_buffer + 500) != entry.id)) {
      return fail("Invalid Amiga file block list");
    }
    if (table == entry.id && be32(_buffer + 324) != entry.bytes) {
      return fail("Amiga file length changed");
    }
    uint32_t count = be32(_buffer + 8), next = be32(_buffer + 504);
    if (count > 72 || (!count && position < entry.bytes)) {
      return fail("Invalid Amiga data pointer count");
    }
    uint32_t blocks[72];
    for (uint32_t i = 0; i < count; i++) {
      blocks[i] = be32(_buffer + 308 - i * 4);
    }
    for (uint32_t i = 0; i < count; i++) {
      uint32_t block = blocks[i];
      if (position >= entry.bytes || block < 2 || block >= _blocks ||
          seen(visited, block)) {
        return fail("Invalid Amiga data pointer");
      }
      if (!readAt(block * 512)) {
        return false;
      }
      uint32_t bytes = entry.bytes - position;
      if (bytes > payload) {
        bytes = payload;
      }
      sequence++;
      if (_format == AMIGA_OFS &&
          (!amigaSum(_buffer) || be32(_buffer) != 8 ||
           be32(_buffer + 4) != entry.id || be32(_buffer + 8) != sequence ||
           be32(_buffer + 12) != bytes)) {
        return fail("Invalid Amiga OFS data block");
      }
      if (length && offset < position + bytes) {
        uint16_t skip = offset > position ? offset - position : 0;
        size_t amount = bytes - skip;
        if (amount > length) {
          amount = length;
        }
        memcpy(data, _buffer + (512 - payload) + skip, amount);
        data += amount;
        offset += amount;
        length -= amount;
      }
      position += bytes;
    }
    table = next;
  } while (table);
  return (position == entry.bytes && length == 0) ||
         fail("Truncated Amiga file");
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
  case CBM_1581:
    return "Commodore 1581 DOS";
  case MAC_HFS:
    return "Macintosh HFS";
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
