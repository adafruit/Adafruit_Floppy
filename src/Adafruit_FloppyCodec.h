// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

/** @brief Decode standard floppy sectors and encode standard Amiga tracks.
 * Decoders consume recovered MSB-first bit cells.
 * Callbacks receive only sectors with valid header and data checksums. Multiple
 * revolutions can repeat sectors; the caller decides how to merge them. These
 * formats do not cover custom copy protection or identify a filesystem.
 */
class Adafruit_FloppyCodec {
public:
  /** @brief Magnetic encoding and standard sector layout. */
  enum Encoding {
    IBM_MFM,       ///< IBM-style MFM, 128/256/512/1024-byte sectors.
    IBM_FM,        ///< IBM-style FM, 128/256/512/1024-byte sectors.
    AMIGA_MFM,     ///< AmigaDOS odd/even MFM, 512-byte sectors.
    COMMODORE_GCR, ///< Commodore 1541 GCR, 256-byte sectors.
    APPLE_6_AND_2  ///< Apple II 16-sector 6-and-2 GCR, 256-byte sectors.
  };
  /** @brief A checksum-verified sector, valid only during the callback. */
  struct Sector {
    uint8_t cylinder; ///< Zero-based cylinder (C64 track number minus one).
    uint8_t head;     ///< Head number; zero for the single-sided GCR formats.
    uint8_t number;   ///< On-disk sector ID (IBM normally 1-based; others 0).
    uint16_t size;    ///< Payload size in bytes.
    uint16_t volume;  ///< Apple volume or C64 disk ID; zero for IBM/Amiga.
    bool deleted;     ///< IBM deleted-data address mark.
    const uint8_t *data; ///< Payload, owned by the decoder during the callback.
  };
  /** @brief Results include repeats from overlapping revolutions. */
  struct Result {
    size_t sectors = 0;    ///< Checksum-verified sectors delivered.
    size_t badHeaders = 0; ///< Recognized headers with invalid fields/checksum.
    size_t badData = 0; ///< Recognized data fields rejected by checksum/coding.
  };
  /** @brief Return false to stop decoding after this sector. */
  typedef bool (*Visit)(void *context, const Sector &sector);

  /** @brief Best checksum-supported interpretation of a flux capture. */
  struct Detection {
    bool found = false;          ///< At least two distinct, consistent sectors.
    Encoding encoding = IBM_MFM; ///< Meaningful only when found is true.
    float bitCellMicroseconds =
        0;               ///< Successful PLL setting, not an RPM measurement.
    size_t bitCount = 0; ///< Recovered bits left in the caller's buffer.
    uint16_t uniqueSectors =
        0; ///< Distinct sector IDs, excluding repeated revolutions.
    uint16_t sectorBytes = 0; ///< Common payload size, or zero for mixed sizes.
    uint8_t cylinder = 0;     ///< Cylinder agreed by all accepted headers.
    uint8_t head = 0;         ///< Head agreed by all accepted headers.
  };

  static Result decode(Encoding encoding, const uint8_t *bits, size_t bitCount,
                       Visit visitor = nullptr, void *context = nullptr);
  static Detection detect(const uint32_t *intervals, size_t count,
                          uint32_t sampleFrequency, uint8_t *workBuffer,
                          size_t workCapacity);
  static const char *name(Encoding encoding);
  static size_t encodeAmiga(const uint8_t *sectors, uint8_t sectorsPerTrack,
                            uint8_t cylinder, uint8_t head, uint8_t *pulses,
                            size_t capacity,
                            uint32_t revolutionMicroseconds = 200000);
};
