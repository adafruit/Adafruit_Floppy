// MIT license. Copyright (c) 2026 Adafruit Industries.
#pragma once
#include <stddef.h>
#include <stdint.h>

// SCP uses 25 ns ticks, three revolution windows and 168 track offsets.
const size_t SCP_HEADER_SIZE = 688; ///< File header plus 168 track offsets.
const size_t SCP_TRACK_HEADER_SIZE = 40;   ///< Three-revolution track header.
const size_t SCP_TRACK_CAPACITY = 1204096; ///< Example track encoding capacity.

void scpPut32(uint8_t *destination, uint32_t value);
uint32_t scpChecksum(const uint8_t *data, size_t size);
void scpHeader(uint8_t *header, bool indexCued = true);
// Input timestamps are descending 40 MHz counters from synchronized PIO SMs.
// Returns zero on invalid timing or insufficient output space.
size_t scpTrack(uint8_t track, const uint32_t *samples, size_t count,
                const uint32_t index[4], uint8_t *output, size_t capacity,
                size_t *badPulse = nullptr);

/** @brief Checked SCP input for index-cued 300 RPM floppy replay.
 * Converts the first revolution to packed 24 MHz intervals, with 10 percent
 * circular overlap. It never decodes or regenerates sectors. Call begin() and
 * preflight every intended track before enabling a drive for writing.
 */
class Adafruit_FloppySCPReader {
public:
  /** @brief Read exactly length bytes from an immutable source image. */
  typedef bool (*Read)(void *context, uint32_t offset, uint8_t *data,
                       size_t length);
  Adafruit_FloppySCPReader(Read reader, void *context, uint32_t bytes);
  bool begin();
  bool hasTrack(uint8_t track) const;
  size_t trackFlux(uint8_t track, uint32_t revolutionMicroseconds,
                   uint8_t *output, size_t capacity);
  const char *error() const;

private:
  Read _reader;                          ///< Exact-read callback.
  void *_context;                        ///< Caller-owned source context.
  uint32_t _size;                        ///< Accessible source bytes.
  uint8_t _header[SCP_HEADER_SIZE] = {}; ///< Validated standard header/table.
  uint8_t _buffer[1024];                 ///< Bounded file I/O scratch buffer.
  bool _ready = false;                   ///< begin() completed successfully.
  const char *_error = "";               ///< Static error description.
  bool readAt(uint32_t offset, size_t length);
  bool fail(const char *message);
};
