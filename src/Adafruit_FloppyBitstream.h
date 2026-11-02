// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

/** @brief Recover packed, MSB-first bit cells from timestamped flux intervals.
 * The caller owns the output buffer and supplies positive interval lengths.
 * INDEX is not needed. Capture more than one revolution to include sectors
 * crossing the capture boundary. No drive, allocation, or filesystem access.
 */
class Adafruit_FloppyBitstream {
public:
  bool begin(uint8_t *buffer, size_t capacity, float sampleFrequency,
             float bitCellMicroseconds, float phaseAdjust = 0.6f,
             float periodAdjust = 0.05f);
  bool addInterval(uint32_t ticks);
  size_t bitCount() const;
  static size_t recover(const uint32_t *intervals, size_t count,
                        uint32_t sampleFrequency, float bitCellMicroseconds,
                        uint8_t *buffer, size_t capacity,
                        float lowpassMicroseconds = 0);

private:
  uint8_t *_buffer = nullptr; ///< Caller-owned packed bit cells.
  size_t _capacity = 0;       ///< Capacity in bits.
  size_t _count = 0;          ///< Number of recovered bit cells.
  float _nominal = 0;         ///< Nominal cell period in capture ticks.
  float _clock = 0;           ///< Tracked cell period in capture ticks.
  float _residual = 0;        ///< Fractional ticks retained across transitions.
  float _phase = 0.6f;        ///< Fraction of phase error corrected per edge.
  float _period = 0.05f; ///< Fraction of phase error applied to the period.
};
