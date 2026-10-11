// MIT license. Copyright (c) 2026 Adafruit Industries.
#pragma once
#include <stddef.h>
#include <stdint.h>

// SCP uses 25 ns ticks, three index-cued revolutions and 168 track offsets.
const size_t SCP_HEADER_SIZE = 688; ///< File header plus 168 track offsets.
const size_t SCP_TRACK_HEADER_SIZE = 40;   ///< Three-revolution track header.
const size_t SCP_TRACK_CAPACITY = 1204096; ///< Example track encoding capacity.

void scpPut32(uint8_t *destination, uint32_t value);
uint32_t scpChecksum(const uint8_t *data, size_t size);
void scpHeader(uint8_t *header);
// Input timestamps are descending 40 MHz counters from synchronized PIO SMs.
// Returns zero on invalid timing or insufficient output space.
size_t scpTrack(uint8_t track, const uint32_t *samples, size_t count,
                const uint32_t index[4], uint8_t *output, size_t capacity,
                size_t *badPulse = nullptr);
