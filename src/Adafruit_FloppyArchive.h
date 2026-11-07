// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

/** @brief Filesystem-independent Apple raw-flux and WOZ2 image builders.
 * All buffers belong to the caller. No heap allocation or drive access.
 * A2R preserves measured flux; WOZ stores recovered bits and is not a
 * substitute for the raw capture when protection/weak bits are unresolved.
 */
class Adafruit_FloppyArchive {
public:
  static const size_t A2R_HEADER_SIZE = 53;   ///< A2R3 header and INFO chunk.
  static const size_t WOZ_HEADER_SIZE = 1536; ///< WOZ2 header and track table.
  static void a2rHeader(uint8_t *output, bool writeProtected);
  static size_t a2rCapture(const uint32_t *samples, size_t count,
                           const uint32_t *indexes, uint8_t revolutions,
                           uint16_t quarterTrack, uint8_t *output,
                           size_t capacity);
  static size_t indexedBits(const uint32_t *samples, size_t count,
                            uint32_t firstIndex, uint32_t lastIndex,
                            float cellMicroseconds, uint8_t *work,
                            size_t workCapacity, uint8_t *output,
                            size_t outputCapacity);
  static void wozHeader(uint8_t *output, bool writeProtected);
  static bool sameTrack(const uint8_t *a, size_t aBits, const uint8_t *b,
                        size_t bBits);
  static bool wozTrack(uint8_t *header, uint8_t quarterTrack, uint32_t offset,
                       uint32_t bitCount);
  static uint32_t crc32(const uint8_t *data, size_t bytes,
                        uint32_t previous = 0);
};
