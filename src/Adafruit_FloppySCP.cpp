// MIT license. Copyright (c) 2026 Adafruit Industries.
#include "Adafruit_FloppySCP.h"
#include <string.h>

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
