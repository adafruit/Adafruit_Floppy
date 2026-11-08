// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Synthetic channel bits: no commercial disk data. Independently check timing,
// the non-byte-aligned revolution seam and rejection before hardware access.
#include "Adafruit_FloppyArchive.h"
#include "Adafruit_FloppyWOZ.h"
#include "greasepack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static void put32(uint8_t *p, uint32_t value) {
  for (int i = 0; i < 4; i++) {
    p[i] = value % 256;
    value /= 256;
  }
}

int main() {
  std::vector<uint8_t> image(1536 + 6656), output(210002, 0x5a);
  const uint32_t bits = 50003;
  Adafruit_FloppyArchive::wozHeader(image.data(), false);
  assert(Adafruit_FloppyArchive::wozTrack(image.data(), 0, 1536, bits));
  // Unequal transition spacings and a partial final byte catch phase/seam bugs.
  for (uint32_t i = 0; i < bits; i++)
    if (i % 7 == 1 || i % 7 == 4 || i % 7 == 6)
      image[1536 + i / 8] |= 0x80 >> (i % 8);
  Adafruit_FloppyWOZ reader;
  assert(!reader.hasTrack(0));
  assert(!reader.begin(nullptr, image.size()));
  assert(reader.begin(image.data(), image.size()));
  assert(reader.hasTrack(0) && !reader.hasTrack(1) && !reader.hasTrack(160));
  Adafruit_FloppyWOZ streamed;
  auto read = [](void *context, uint32_t offset, uint8_t *data, size_t bytes) {
    auto &file = *(std::vector<uint8_t> *)context;
    if (offset > file.size() || bytes > file.size() - offset)
      return false;
    memcpy(data, file.data() + offset, bytes);
    return true;
  };
  assert(streamed.begin(read, &image, image.size()));
  std::vector<uint8_t> scratch(8192), streamedOutput(210000);
  for (uint32_t period : {180000u, 200000u, 202325u, 220000u}) {
    size_t bytes = reader.flux(0, period, output.data() + 1, 210000);
    assert(streamed.flux(0, period, streamedOutput.data(),
                         streamedOutput.size(), scratch.data(),
                         scratch.size()) == bytes);
    assert(!memcmp(streamedOutput.data(), output.data() + 1, bytes));
    assert(bytes && output.front() == 0x5a && output.back() == 0x5a);
    uint8_t *cursor = output.data() + 1, *end = cursor + bytes;
    uint32_t elapsed = 0;
    for (uint32_t i = 0; i < bits * 2; i++) {
      uint32_t bit = i % bits;
      if (bit % 7 != 1 && bit % 7 != 4 && bit % 7 != 6)
        continue;
      assert(cursor < end);
      elapsed += greaseunpack(&cursor, end, true);
      // Absolute rounding prevents accumulated drift across 100,006 cells.
      assert(elapsed == ((uint64_t)(i + 1) * period * 24 + bits / 2) / bits);
    }
    assert(cursor == end);
    assert(!reader.flux(0, period, output.data() + 1, bytes - 1));
  }
  assert(!reader.flux(1, 200000, output.data(), output.size()));
  assert(!reader.flux(0, 179999, output.data(), output.size()));
  assert(!reader.flux(0, 220001, output.data(), output.size()));
  assert(!reader.flux(0, 200000, nullptr, output.size()));
  assert(!reader.flux(0, 200000, output.data(), 0));
  assert(!streamed.flux(0, 200000, output.data(), output.size()));
  assert(!streamed.flux(0, 200000, output.data(), output.size(), scratch.data(),
                        1));
  auto shortFile = image;
  assert(streamed.begin(read, &shortFile, shortFile.size()));
  shortFile.resize(1536);
  assert(!streamed.flux(0, 200000, output.data(), output.size(), scratch.data(),
                        scratch.size()));
  assert(!streamed.begin(read, &shortFile, image.size()));

  auto bad = image;
  bad[0] = 'X';
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  bad[21] = 2;
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  bad[88] = 160;
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  bad[256] = 1;
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  put32(bad.data() + 260, 100000);
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  put32(bad.data() + 252, UINT32_MAX);
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  bad[20] = 3;
  bad[66] = 1;
  assert(!reader.begin(bad.data(), bad.size()));
  for (const char *chunk : {"FLUX", "WRIT", "INFO", "TMAP", "TRKS"}) {
    bad = image;
    bad.resize(bad.size() + 8);
    memcpy(bad.data() + image.size(), chunk, 4);
    assert(!reader.begin(bad.data(), bad.size()));
  }
  for (size_t bytes = 0; bytes < image.size(); bytes++)
    assert(!reader.begin(image.data(), bytes));

  // CRC zero is allowed by the spec; a supplied CRC must match every byte.
  put32(image.data() + 8,
        Adafruit_FloppyArchive::crc32(image.data() + 12, image.size() - 12));
  assert(reader.begin(image.data(), image.size()));
  bad = image;
  bad.back() ^= 1;
  assert(!reader.begin(bad.data(), bad.size()));
  bad = image;
  memset(bad.data() + 8, 0, 4);
  memset(bad.data() + 1536, 0, bad.size() - 1536);
  assert(reader.begin(bad.data(), bad.size()));
  assert(!reader.flux(0, 200000, output.data(), output.size()));
  // A long no-transition interval must not silently wrap the 16-bit PIO delay.
  bad[1536 + 400] = 0x80;
  assert(!reader.flux(0, 200000, output.data(), output.size()));
  puts("WOZ2 bounds, CRC, timing, cyclic seam and unsupported-input tests "
       "passed");
}
