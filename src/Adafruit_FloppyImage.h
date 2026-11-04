// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

/** @brief Read-only catalogs and file access for decoded floppy images.
 * The caller supplies bounded byte reads from an image file or sector cache.
 * This class does not control a drive, decode flux, or write filesystem data.
 */
class Adafruit_FloppyImage {
public:
  /** @brief Filesystems identified by their structures, not file extensions. */
  enum Format {
    UNKNOWN,      ///< Unknown, unsupported, or invalid filesystem.
    FAT12,        ///< FAT12, shared by PC DOS and many Atari ST disks.
    AMIGA_OFS,    ///< Amiga original filesystem, including international mode.
    AMIGA_FFS,    ///< Amiga fast filesystem, including international mode.
    CBM_DOS,      ///< Commodore 1541 DOS on a D64 sector image.
    APPLE_DOS33,  ///< Apple II DOS 3.3 in DOS-ordered sector image form.
    APPLE_PRODOS, ///< Apple ProDOS in block-ordered image form.
    CBM_1581      ///< Commodore 1581 DOS on a D81 sector image.
  };
  /** @brief One root-directory entry; valid only during the visitor call. */
  struct Entry {
    uint32_t id;     ///< Opaque entry identifier, valid only in this image.
    char name[64];   ///< Printable name, terminated with NUL.
    char kind[12];   ///< File type such as FILE, DIR, PRG, or BASIC.
    uint32_t bytes;  ///< Exact file length when exactBytes is true.
    uint16_t blocks; ///< Filesystem-reported allocation, when available.
    bool exactBytes; ///< False for formats that list only allocated blocks.
    bool directory;  ///< True for a subdirectory; listing does not recurse.
    bool locked;     ///< Filesystem write/delete protection flag.
  };
  /** @brief Read exactly length bytes; return false for missing/bad sectors. */
  typedef bool (*Read)(void *context, uint32_t offset, uint8_t *data,
                       size_t length);
  /** @brief Visit an entry; return false to stop a successful listing early. */
  typedef bool (*Visit)(void *context, const Entry &entry);

  Adafruit_FloppyImage(Read reader, void *context, uint32_t imageBytes);
  bool identify();
  bool listRoot(Visit visitor, void *context);
  bool listDirectory(uint32_t directory, Visit visitor, void *context);
  bool openFile(uint32_t id, Entry &entry);
  int32_t readFile(const Entry &entry, uint32_t offset, uint8_t *data,
                   size_t length);
  Format format() const;
  const char *formatName() const;
  const char *volumeName() const;
  const char *error() const;

private:
  Read _reader;               ///< Caller-owned reader.
  void *_context;             ///< Reader context.
  uint32_t _size;             ///< Image length in bytes.
  Format _format = UNKNOWN;   ///< Last identified filesystem.
  char _volume[32] = {};      ///< Printable volume label.
  const char *_error = "";    ///< Static failure description.
  uint32_t _root = 0;         ///< Root byte offset or first directory block.
  uint16_t _entries = 0;      ///< FAT root-entry limit.
  uint16_t _blocks = 0;       ///< Logical volume block count.
  uint8_t _tracks = 0;        ///< DOS 3.3 / Commodore logical track count.
  uint8_t _catalogTrack = 0;  ///< DOS 3.3 / Commodore first catalog track.
  uint8_t _catalogSector = 0; ///< DOS 3.3 / Commodore first catalog sector.
  uint8_t _buffer[512];       ///< One sector/block of scratch space.

  bool readAt(uint32_t offset, size_t length = 512);
  bool fail(const char *message);
  bool identifyFAT();
  bool identifyAmiga();
  bool identifyCBM();
  bool identifyDOS33();
  bool identifyProDOS();
  bool listFAT(Visit visitor, void *context);
  bool listAmiga(uint32_t directory, Visit visitor, void *context);
  bool listCBM(Visit visitor, void *context);
  bool readAmiga(const Entry &entry, uint32_t offset, uint8_t *data,
                 size_t length);
  bool readCBM(Entry &entry, uint32_t offset, uint8_t *data, size_t length);
  bool readCBMBlock(uint32_t block);
  uint8_t cbmSectors(uint8_t track) const;
  uint32_t cbmBlock(uint8_t track, uint8_t sector) const;
  bool listDOS33(Visit visitor, void *context);
  bool listProDOS(Visit visitor, void *context);
};
