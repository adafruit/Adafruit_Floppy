// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

/** @brief Checked, allocation-free WOZ2 bitstream reader for 5.25-inch disks.
 * The caller owns the immutable image and output buffers. This does not decide
 * which physical head positions to write or reproduce analog copy protection.
 * WOZ1, FLUX and WRIT chunks are deliberately unsupported.
 */
class Adafruit_FloppyWOZ {
public:
  /** @brief Read exactly length bytes from a bounded, read-only file. */
  typedef bool (*Read)(void *context, uint32_t offset, uint8_t *data,
                       size_t length);
  bool begin(const uint8_t *image, size_t bytes);
  bool begin(Read reader, void *context, uint32_t bytes);
  bool hasTrack(uint8_t quarterTrack) const;
  uint32_t bitCount(uint8_t quarterTrack) const;
  size_t flux(uint8_t quarterTrack, uint32_t revolutionMicroseconds,
              uint8_t *output, size_t capacity, uint8_t *trackBuffer = nullptr,
              size_t trackCapacity = 0, uint32_t startBit = 0);
  const char *error() const;

private:
  const uint8_t *_image = nullptr; ///< Borrowed, validated file bytes.
  Read _reader = nullptr;     ///< Optional file reader instead of memory image.
  void *_context = nullptr;   ///< Borrowed reader context.
  uint32_t _bytes = 0;        ///< Source file size for bounded reads.
  bool _valid = false;        ///< Complete preflight succeeded.
  uint8_t _map[160] = {};     ///< Quarter-track to descriptor mapping.
  uint8_t _tracks[1280] = {}; ///< Validated TRKS descriptor table.
  const char *_error = "Not initialized"; ///< Last failure; empty on success.
  bool read(uint32_t offset, uint8_t *data, size_t bytes);
  bool validate();
};
