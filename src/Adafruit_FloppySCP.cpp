// MIT license. Copyright (c) 2026 Adafruit Industries.
#include "Adafruit_FloppySCP.h"
#include "greasepack.h"
#include <string.h>

namespace {
uint32_t scpGet32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
} // namespace

/** @brief Store a little-endian SCP field.
    @param destination Four writable bytes.
    @param value Field value. */
void scpPut32(uint8_t *destination, uint32_t value) {
  for (uint8_t byte = 0; byte < 4; byte++)
    destination[byte] = value >> (8 * byte);
}

/** @brief Calculate the additive SCP checksum for a block.
    @param data Bytes to include (file checksum excludes the first 16 bytes).
    @param size Number of bytes.
    @return Sum, modulo 2^32. */
uint32_t scpChecksum(const uint8_t *data, size_t size) {
  uint32_t sum = 0;
  for (size_t byte = 0; byte < size; byte++)
    sum += data[byte];
  return sum;
}

/** @brief Initialize a three-revolution, 80-cylinder, two-head SCP header.
    @param header At least SCP_HEADER_SIZE writable bytes.
    @param indexCued True for real INDEX boundaries; false for timed windows. */
void scpHeader(uint8_t *header, bool indexCued) {
  memset(header, 0, SCP_HEADER_SIZE);
  memcpy(header, "SCP", 3);
  header[3] = 0x24;
  header[4] =
      0x85; // Other 3.5-inch HD: raw capture does not assume IBM sectors.
  header[5] = 3;
  header[7] = 159;
  header[8] =
      0x82 | (indexCued ? 1 : 0); // 96 TPI, non-SCP creator; NOT normalized.
}

/** @brief Encode three unnormalized revolutions with 25 ns resolution.
    @param track Cylinder * 2 + head, 0 through 159.
    @param samples Descending 40 MHz timestamps, including optional lead-in.
    @param count Number of timestamps.
    @param index Four descending timestamps bounding three revolution windows.
    For nominal timed boundaries, use scpHeader() with indexCued set to false.
    @param output Destination for the complete track record.
    @param capacity Available output bytes.
    @param badPulse Optional index of a malformed input timestamp.
    @return Encoded byte count, or zero for invalid timing or insufficient
   space. */
size_t scpTrack(uint8_t track, const uint32_t *samples, size_t count,
                const uint32_t index[4], uint8_t *output, size_t capacity,
                size_t *badPulse) {
  if (capacity < SCP_TRACK_HEADER_SIZE || track >= 160)
    return 0;
  memcpy(output, "TRK", 3);
  output[3] = track;
  size_t position = SCP_TRACK_HEADER_SIZE;
  size_t pulse = 0;
  while (pulse < count && (int32_t)(index[0] - samples[pulse]) <= 0)
    pulse++;
  uint32_t accounted = 0;
  uint32_t previous = 0;
  for (uint8_t rev = 0; rev < 3; rev++) {
    uint32_t duration = index[rev] - index[rev + 1];
    if (duration < 6000000 || duration > 10000000)
      return 0;
    uint32_t end = index[0] - index[rev + 1];
    size_t start = position;
    while (pulse < count) {
      uint32_t time = index[0] - samples[pulse];
      if (time > end)
        break;
      if (time <= previous || time <= accounted) {
        if (badPulse)
          *badPulse = pulse;
        return 0;
      }
      previous = time;
      uint32_t interval = time - accounted;
      // A zero word extends an interval; it must never fabricate a transition.
      // An exact 65536-tick edge is rounded one tick early, then compensated
      // by the following interval. Error never accumulates across the track.
      if ((interval & 65535) == 0)
        interval--;
      while (interval >= 65536) {
        if (position + 2 > capacity)
          return 0;
        output[position++] = 0;
        output[position++] = 0;
        interval -= 65536;
        accounted += 65536;
      }
      if (position + 2 > capacity)
        return 0;
      output[position++] = interval >> 8;
      output[position++] = interval;
      accounted += interval;
      pulse++;
    }
    // Preserve long quiet regions, including an entirely unformatted side.
    // Carry the unfinished interval across index marks without inventing edges.
    while (end - accounted >= 65536) {
      if (position + 2 > capacity)
        return 0;
      output[position++] = 0;
      output[position++] = 0;
      accounted += 65536;
    }
    scpPut32(output + 4 + rev * 12, duration);
    scpPut32(output + 8 + rev * 12, (position - start) / 2);
    scpPut32(output + 12 + rev * 12, start);
  }
  return position;
}

/** @brief Bind an immutable SCP source.
    @param reader Exact byte-read callback.
    @param context Caller-owned callback context.
    @param bytes Total source length. */
Adafruit_FloppySCPReader::Adafruit_FloppySCPReader(Read reader, void *context,
                                                   uint32_t bytes)
    : _reader(reader), _context(context), _size(bytes) {}

bool Adafruit_FloppySCPReader::fail(const char *message) {
  _error = message;
  return false;
}

bool Adafruit_FloppySCPReader::readAt(uint32_t offset, size_t length) {
  if (!_reader || length > sizeof(_buffer) || offset > _size ||
      length > _size - offset) {
    return fail("SCP read outside file");
  }
  return _reader(_context, offset, _buffer, length) ||
         fail("SCP source read failed");
}

/** @brief Validate the SCP header, complete checksum and track descriptors.
    @return True for indexed, 25 ns, 16-bit, 300 RPM images within 80 cylinders.
    @note Timed/index-less captures, extended media and other resolutions are
    rejected explicitly. Validate each track's pulses with trackFlux() too. */
bool Adafruit_FloppySCPReader::begin() {
  _ready = false;
  _error = "";
  if (!readAt(0, sizeof(_header))) {
    return false;
  }
  memcpy(_header, _buffer, sizeof(_header));
  if (memcmp(_header, "SCP", 3) || !_header[5] || _header[5] > 16 ||
      _header[6] > _header[7] || _header[7] >= 160 || !(_header[8] & 1) ||
      (_header[8] & (4 | 64)) || (_header[9] != 0 && _header[9] != 16) ||
      _header[10] > 2 || _header[11]) {
    return fail("Need indexed 300 RPM / 25 ns SCP");
  }
  uint32_t checksum = 0;
  for (uint32_t offset = 16; offset < _size;) {
    size_t count = _size - offset;
    if (count > sizeof(_buffer)) {
      count = sizeof(_buffer);
    }
    if (!readAt(offset, count)) {
      return false;
    }
    checksum += scpChecksum(_buffer, count);
    offset += count;
  }
  if (checksum != scpGet32(_header + 12)) {
    return fail("SCP checksum mismatch");
  }
  unsigned tracks = 0;
  for (unsigned track = 0; track < 168; track++) {
    uint32_t start = scpGet32(_header + 16 + track * 4);
    if (!start) {
      continue;
    }
    if (track < _header[6] || track > _header[7] ||
        (_header[10] && track % 2 != (unsigned)_header[10] - 1) ||
        start < sizeof(_header) || start > _size ||
        4u + 12u * _header[5] > _size - start || !readAt(start, 4) ||
        memcmp(_buffer, "TRK", 3) || _buffer[3] != track) {
      return fail("Invalid SCP track table");
    }
    tracks++;
    for (uint8_t rev = 0; rev < _header[5]; rev++) {
      if (!readAt(start + 4 + rev * 12, 12)) {
        return false;
      }
      uint32_t period = scpGet32(_buffer), words = scpGet32(_buffer + 4);
      uint32_t relative = scpGet32(_buffer + 8);
      if (period < 7200000 || period > 8800000 || !words ||
          relative < 4u + 12u * _header[5] || relative > _size - start ||
          words > (_size - start - relative) / 2) {
        return fail("Invalid SCP revolution bounds");
      }
    }
  }
  if (!tracks) {
    return fail("SCP contains no tracks");
  }
  _ready = true;
  return true;
}

/** @brief Test whether the validated image supplies a track side.
    @param track Cylinder times two plus head.
    @return True if this side has a descriptor. Missing sides are left alone. */
bool Adafruit_FloppySCPReader::hasTrack(uint8_t track) const {
  return _ready && track < 160 && scpGet32(_header + 16 + track * 4) != 0;
}

/** @brief Convert one captured revolution into a bounded replay buffer.
    @param track Cylinder times two plus head.
    @param revolutionMicroseconds Measured target-drive period, 180000..220000.
    @param output Destination for greasepack-formatted 24 MHz pulse intervals.
    @param capacity Output capacity in bytes.
    @return Bytes produced, or zero on invalid data/insufficient space.
    @note Absolute-time rounding avoids accumulated quantization error. Replay
    scales to the target RPM and repeats the start for 10 percent overlap.
    The first edge is delayed to at least the RP2 writer's 20-clock minimum;
    later edges retain their absolute positions. Other unrepresentable pulse
    spacings are rejected, never silently clipped or filled with fake edges. */
size_t Adafruit_FloppySCPReader::trackFlux(uint8_t track,
                                           uint32_t revolutionMicroseconds,
                                           uint8_t *output, size_t capacity) {
  _error = "";
  if (!hasTrack(track) || !output || capacity < 7 ||
      revolutionMicroseconds < 180000 || revolutionMicroseconds > 220000) {
    fail("Invalid SCP replay arguments");
    return 0;
  }
  uint32_t start = scpGet32(_header + 16 + track * 4);
  if (!readAt(start + 4, 12)) {
    return 0;
  }
  uint32_t period = scpGet32(_buffer), words = scpGet32(_buffer + 4);
  uint32_t offset = start + scpGet32(_buffer + 8);
  uint32_t target = revolutionMicroseconds * 24;
  uint64_t previous = 0;
  uint8_t *next = output;
  for (uint8_t cycle = 0; cycle < 2; cycle++) {
    uint64_t elapsed = 0;
    unsigned transitions = 0;
    for (uint32_t position = 0; position < words;) {
      size_t count = words - position;
      if (count > sizeof(_buffer) / 2) {
        count = sizeof(_buffer) / 2;
      }
      if (!readAt(offset + position * 2, count * 2)) {
        return 0;
      }
      for (size_t i = 0; i < count; i++) {
        uint16_t word = ((uint16_t)_buffer[i * 2] << 8) | _buffer[i * 2 + 1];
        elapsed += word ? word : 65536;
        if (elapsed > (uint64_t)period + 65536) {
          fail("SCP flux exceeds revolution");
          return 0;
        }
        if (!word || elapsed > period) {
          continue;
        }
        transitions++;
        uint64_t time =
            ((cycle * (uint64_t)period + elapsed) * target + period / 2) /
            period;
        if (!previous && time < 20) {
          time = 20;
        }
        uint64_t delta = time - previous;
        // The RP2 writer has 20 clocks of fixed overhead and a 16-bit delay.
        if (time <= previous || delta < 20 || delta > 65555) {
          fail("Flux spacing outside RP2 writer range");
          return 0;
        }
        size_t needed = delta < 250 ? 1 : delta < 1525 ? 2 : 6;
        if ((size_t)(output + capacity - next) < needed) {
          fail("SCP replay buffer too small");
          return 0;
        }
        next = greasepack(next, output + capacity, delta);
        previous = time;
        if (cycle && time >= (uint64_t)target * 11 / 10) {
          return next - output;
        }
      }
      position += count;
    }
    if (transitions < 1000) {
      fail("SCP track blank or too sparse for replay");
      return 0;
    }
  }
  fail("SCP has insufficient replay overlap");
  return 0;
}

/** @brief Explain the last failed operation.
    @return Static error string, empty after a successful operation. */
const char *Adafruit_FloppySCPReader::error() const { return _error; }
