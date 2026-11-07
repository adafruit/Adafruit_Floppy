// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyBitstream.h"
#include <math.h>
#include <string.h>

// PLL update rule follows Greaseweazle's public-domain flux_to_bitcells,
// written by Keir Fraser: phase correction, bounded period tracking, and
// return toward the nominal period after a long gap. No index is synthesized.

/** @brief Reset the PLL and clear the caller's packed-bit output buffer.
    @param buffer Output bytes, owned by caller.
    @param capacity Output size in bytes.
    @param sampleFrequency Flux timestamp frequency in Hz.
    @param bitCellMicroseconds Encoded bit-cell period in microseconds; e.g.
           1 for IBM HD MFM, 2 for DD MFM, or about 4 for Apple II GCR.
    @param phaseAdjust Phase-error correction fraction, from 0 through 1.
    @param periodAdjust Period-error correction fraction, from 0 through 1.
    @return True for valid storage and timing settings. */
bool Adafruit_FloppyBitstream::begin(uint8_t *buffer, size_t capacity,
                                     float sampleFrequency,
                                     float bitCellMicroseconds,
                                     float phaseAdjust, float periodAdjust) {
  _buffer = nullptr;
  _count = 0;
  float nominal = sampleFrequency * bitCellMicroseconds / 1000000.0f;
  if (!buffer || !capacity || capacity > SIZE_MAX / 8 ||
      !isfinite(sampleFrequency) || sampleFrequency <= 0 ||
      !isfinite(bitCellMicroseconds) || bitCellMicroseconds <= 0 ||
      !isfinite(nominal) || nominal < 1 || nominal > 1000000 ||
      !isfinite(phaseAdjust) || phaseAdjust < 0 || phaseAdjust > 1 ||
      !isfinite(periodAdjust) || periodAdjust < 0 || periodAdjust > 1) {
    return false;
  }
  _buffer = buffer;
  _capacity = capacity * 8;
  _nominal = _clock = nominal;
  _phase = phaseAdjust;
  _period = periodAdjust;
  _residual = 0;
  memset(buffer, 0, capacity);
  return true;
}

/** @brief Append one flux interval, measured with the begin() sample frequency.
    @param ticks Positive time from the preceding transition to this transition.
    @return False for invalid input or a full buffer. After failure, begin()
            must be called again; never decode a failed/truncated capture. */
bool Adafruit_FloppyBitstream::addInterval(uint32_t ticks) {
  if (!_buffer || !ticks) {
    _buffer = nullptr;
    return false;
  }
  float elapsed = _residual + ticks;
  if (elapsed < _clock / 2) {
    _residual = elapsed;
    return true;
  }
  double cells = floor((double)elapsed / _clock + 0.5);
  if (cells > _capacity - _count) {
    _buffer = nullptr;
    return false;
  }
  size_t count = (size_t)cells;
  _count += count;
  _buffer[(_count - 1) / 8] |= 0x80 >> ((_count - 1) % 8);
  float error = elapsed - count * _clock;
  _residual = error * (1 - _phase);
  if (count <= 4) {
    _clock += error * _period;
  } else {
    _clock += (_nominal - _clock) * _period;
  }
  if (_clock < _nominal * 0.9f) {
    _clock = _nominal * 0.9f;
  }
  if (_clock > _nominal * 1.1f) {
    _clock = _nominal * 1.1f;
  }
  return true;
}

/** @brief Return the number of recovered bits.
    @return Zero after failure; otherwise the count, including a partial byte.
 */
size_t Adafruit_FloppyBitstream::bitCount() const {
  return _buffer ? _count : 0;
}

/** @brief Recover a complete interval array, optionally removing short
   glitches.
    @param intervals Positive time intervals in capture ticks.
    @param count Number of intervals.
    @param sampleFrequency Capture timebase in Hz.
    @param bitCellMicroseconds Nominal encoded cell period in microseconds.
    @param buffer Caller-owned packed-bit output storage.
    @param capacity Output capacity in bytes.
    @param lowpassMicroseconds Merge short transition pairs below this duration;
           zero disables filtering. Keep below the shortest genuine transition.
    @return Recovered bit count, or zero on invalid/truncated input/output. */
size_t Adafruit_FloppyBitstream::recover(const uint32_t *intervals,
                                         size_t count, uint32_t sampleFrequency,
                                         float bitCellMicroseconds,
                                         uint8_t *buffer, size_t capacity,
                                         float lowpassMicroseconds) {
  Adafruit_FloppyBitstream pll;
  if (!intervals || !count || !isfinite(lowpassMicroseconds) ||
      lowpassMicroseconds < 0 ||
      !pll.begin(buffer, capacity, sampleFrequency, bitCellMicroseconds)) {
    return 0;
  }
  double threshold = (double)sampleFrequency * lowpassMicroseconds / 1000000;
  uint64_t pending = 0;
  // Greaseweazle's low-pass rule merges Long/Short/Long into their sum. Hold
  // the preceding interval until we know whether it participates in a glitch.
  // For consecutive short intervals, merge around the shorter middle one.
  auto emit = [&]() {
    if (!pending) {
      return true;
    }
    return pending <= UINT32_MAX && pll.addInterval((uint32_t)pending);
  };
  for (size_t i = 0; i < count;) {
    uint32_t x = intervals[i++];
    if (!x) {
      return 0;
    }
    if (x <= threshold) {
      uint32_t y = 0;
      if (i < count) {
        y = intervals[i++];
        if (!y) {
          return 0;
        }
      }
      if (y <= x) {
        if (!emit()) {
          return 0;
        }
        pending = (uint64_t)x + y;
        if (i < count) {
          uint32_t next = intervals[i++];
          if (!next) {
            return 0;
          }
          pending += next;
        }
      } else {
        pending += (uint64_t)x + y;
      }
    } else {
      if (!emit()) {
        return 0;
      }
      pending = x;
    }
  }
  return emit() ? pll.bitCount() : 0;
}
