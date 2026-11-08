// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyWOZ.h"
#include "Adafruit_FloppyArchive.h"
#include "greasepack.h"
#include <string.h>

static uint32_t wozRead32(const uint8_t *p) {
  return p[0] + 256u * p[1] + 65536u * p[2] + 16777216u * p[3];
}

/** @brief Validate the entire container and every mapped track before writing.
 * @param image Complete WOZ2 file, retained unchanged until the reader is done.
 * @param bytes File length in bytes.
 * @return True for a supported, structurally valid file with a valid CRC (when
 * supplied). Does not validate the filesystem, sectors or copy protection.
 */
bool Adafruit_FloppyWOZ::begin(const uint8_t *image, size_t bytes) {
  _image = image;
  _reader = nullptr;
  _bytes = bytes <= UINT32_MAX ? bytes : 0;
  return validate();
}

/** @brief Validate a file using bounded reads and no whole-image allocation.
 * @param reader Exact-read callback; the caller handles filesystem ownership.
 * @param context Borrowed callback context, retained for subsequent flux calls.
 * @param bytes Source file size.
 * @return True on complete structural/CRC validation. Keep the source
 * unchanged.
 */
bool Adafruit_FloppyWOZ::begin(Read reader, void *context, uint32_t bytes) {
  _image = nullptr;
  _reader = reader;
  _context = context;
  _bytes = bytes;
  return validate();
}

/** @brief Read a bounded range from the selected source.
 * @param offset File offset. @param data Destination. @param bytes Byte count.
 * @return True on complete read.
 */
bool Adafruit_FloppyWOZ::read(uint32_t offset, uint8_t *data, size_t bytes) {
  if (offset > _bytes || bytes > _bytes - offset)
    return false;
  if (_image) {
    memcpy(data, _image + offset, bytes);
    return true;
  }
  return _reader && _reader(_context, offset, data, bytes);
}

/** @brief Check chunks, CRC and mapped descriptors. @return True on success. */
bool Adafruit_FloppyWOZ::validate() {
  _valid = false;
  uint8_t buffer[512], info[60] = {};
  _error = "Invalid WOZ2 header";
  if (!read(0, buffer, 12) || memcmp(buffer, "WOZ2\xff\x0a\x0d\x0a", 8))
    return false;
  uint32_t crc = wozRead32(buffer + 8), calculated = 0;
  for (uint32_t offset = 12; offset < _bytes;) {
    size_t length = _bytes - offset;
    if (length > sizeof(buffer))
      length = sizeof(buffer);
    _error = "WOZ source read failed";
    if (!read(offset, buffer, length))
      return false;
    calculated = Adafruit_FloppyArchive::crc32(buffer, length, calculated);
    offset += length;
  }
  if (crc && crc != calculated) {
    _error = "WOZ CRC mismatch";
    return false;
  }
  bool haveInfo = false, haveMap = false, haveTracks = false;
  uint32_t trackBytes = 0, trackStart = 0;
  for (uint32_t offset = 12; offset < _bytes;) {
    _error = "Truncated WOZ chunk";
    if (!read(offset, buffer, 8))
      return false;
    const uint8_t *chunk = buffer;
    size_t size = wozRead32(chunk + 4);
    offset += 8;
    if (size > _bytes - offset)
      return false;
    _error = "Invalid or duplicate WOZ chunk";
    if (!memcmp(chunk, "INFO", 4)) {
      if (haveInfo || size != 60 || !read(offset, info, 60))
        return false;
      haveInfo = true;
    } else if (!memcmp(chunk, "TMAP", 4)) {
      if (haveMap || size != 160 || !read(offset, _map, 160))
        return false;
      haveMap = true;
    } else if (!memcmp(chunk, "TRKS", 4)) {
      if (haveTracks || size < 1280 || !read(offset, _tracks, 1280))
        return false;
      haveTracks = true;
      trackStart = offset;
      trackBytes = size;
    } else if (!memcmp(chunk, "FLUX", 4) || !memcmp(chunk, "WRIT", 4)) {
      _error = "WOZ FLUX/WRIT not supported";
      return false;
    }
    offset += size;
  }
  _error = "Need WOZ2 single-sided 5.25-inch bitstreams";
  if (!haveInfo || !haveMap || !haveTracks || info[0] < 2 || info[0] > 3 ||
      info[1] != 1 || info[37] != 1 ||
      (info[0] >= 3 && (info[46] || info[47] || info[48] || info[49])))
    return false;
  for (size_t q = 0; q < 160; q++) {
    if (_map[q] == 255)
      continue;
    _error = "Invalid WOZ track map";
    if (_map[q] >= 160)
      return false;
    const uint8_t *entry = _tracks + 8 * _map[q];
    size_t start = (entry[0] + 256u * entry[1]) * 512u;
    size_t length = (entry[2] + 256u * entry[3]) * 512u;
    uint32_t bits = wozRead32(entry + 4);
    _error = "Invalid WOZ track bounds";
    if (!length || !bits || start < trackStart + 1280 ||
        start > trackStart + trackBytes ||
        length > trackStart + trackBytes - start || bits > (uint64_t)length * 8)
      return false;
  }
  _valid = true;
  _error = "";
  return true;
}

/** @brief Test whether a quarter-track has a mapped bitstream.
 * @param quarterTrack Physical position 0..159, in quarter-track steps.
 * @return True if begin succeeded and the requested position is mapped.
 */
bool Adafruit_FloppyWOZ::hasTrack(uint8_t quarterTrack) const {
  return _valid && quarterTrack < 160 && _map[quarterTrack] != 255;
}

/** @brief Convert two cyclic revolutions to packed 24 MHz transition intervals.
 * Scale the complete bitstream to the measured rotation period. The second
 * revolution keeps the writer fed until hardware INDEX stops it. Long zero
 * runs remain transition-free; no random weak bits or sector repairs are added.
 * @param quarterTrack Physical position 0..159.
 * @param revolutionMicroseconds Measured rotation period, 180000..220000 us.
 * @param output Caller-owned buffer; use SRAM when feeding a hardware writer.
 * @param capacity Output buffer capacity in bytes.
 * @param trackBuffer SRAM scratch space for callback-based reads; ignored for
 * an in-memory image. @param trackCapacity Scratch capacity, 8192 is
 * sufficient.
 * @return Packed byte count, or zero on unsupported timing/capacity. On failure
 * discard the partial output. Intervals fit the RP2 writer's 16-bit delay.
 */
size_t Adafruit_FloppyWOZ::flux(uint8_t quarterTrack,
                                uint32_t revolutionMicroseconds,
                                uint8_t *output, size_t capacity,
                                uint8_t *trackBuffer, size_t trackCapacity) {
  _error = "Missing WOZ track / invalid rotation period";
  if (!hasTrack(quarterTrack) || !output || !capacity ||
      revolutionMicroseconds < 180000 || revolutionMicroseconds > 220000)
    return 0;
  const uint8_t *entry = _tracks + 8 * _map[quarterTrack];
  uint32_t offset = (entry[0] + 256u * entry[1]) * 512u;
  uint32_t bits = wozRead32(entry + 4);
  _error = "Unsupported WOZ bit timing";
  if (bits < 32000 || bits > 65536)
    return 0;
  const uint8_t *data = _image ? _image + offset : trackBuffer;
  if (!_image) {
    _error = "WOZ track read / scratch capacity";
    if (!trackBuffer || trackCapacity < (bits + 7) / 8 ||
        !read(offset, trackBuffer, (bits + 7) / 8))
      return 0;
  }
  uint8_t *next = output, *end = output + capacity;
  uint32_t previous = 0;
  uint32_t period = revolutionMicroseconds * 24;
  for (uint32_t i = 0; i < bits * 2; i++) {
    uint32_t bit = i % bits;
    // WOZ channel bits are MSB-first; a one is a physical transition.
    if (!(data[bit / 8] & (0x80 >> (bit % 8))))
      continue;
    uint32_t tick = ((uint64_t)(i + 1) * period + bits / 2) / bits;
    uint32_t delta = tick - previous;
    _error = "WOZ transition interval out of range";
    if (delta <= 20 || delta > 65535)
      return 0;
    next = greasepack(next, end, delta);
    _error = "WOZ flux buffer too small";
    if (next == end)
      return 0;
    previous = tick;
  }
  _error = "WOZ track has no transitions";
  if (next == output)
    return 0;
  _error = "";
  return next - output;
}

/** @brief Describe the most recent parse/conversion failure.
 * @return Borrowed message, or an empty string after success.
 */
const char *Adafruit_FloppyWOZ::error() const { return _error; }
