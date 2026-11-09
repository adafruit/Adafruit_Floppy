// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Synthetic channel bits: no commercial disk data. Independently check timing,
// the non-byte-aligned revolution seam and rejection before hardware access.
#include "Adafruit_FloppyArchive.h"
#include "Adafruit_FloppyCodec.h"
#include "Adafruit_FloppyWOZ.h"
#include "greasepack.h"
#include <assert.h>
#include <fstream>
#include <iterator>
#include <stdio.h>
#include <string.h>
#include <vector>

static void put32(uint8_t *p, uint32_t value) {
  for (int i = 0; i < 4; i++) {
    p[i] = value % 256;
    value /= 256;
  }
}

// Optional external fixtures stay outside Git. This checks the actual parser
// and encoder, not emulator behavior or magnetic protection reproduction.
static void checkFile(const char *path) {
  std::ifstream file(path, std::ios::binary);
  assert(file.good());
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), {});
  uint32_t original = Adafruit_FloppyArchive::crc32(image.data(), image.size());
  Adafruit_FloppyWOZ reader, streamed;
  if (!reader.begin(image.data(), image.size())) {
    printf("REJECT %s: %s\n\r", path, reader.error());
    return;
  }
  auto read = [](void *context, uint32_t offset, uint8_t *data, size_t bytes) {
    auto &image = *(std::vector<uint8_t> *)context;
    if (offset > image.size() || bytes > image.size() - offset)
      return false;
    memcpy(data, image.data() + offset, bytes);
    return true;
  };
  assert(streamed.begin(read, &image, image.size()));
  std::vector<uint8_t> output(210000), fromFile(210000), scratch(8192);
  unsigned converted = 0, rejected = 0, splices = 0, wholeTracks = 0;
  for (unsigned q = 0; q < 160; q++) {
    if (!reader.hasTrack(q))
      continue;
    for (uint32_t period : {180000u, 200000u, 202325u, 220000u}) {
      size_t bytes = reader.flux(q, period, output.data(), output.size());
      size_t other = streamed.flux(q, period, fromFile.data(), fromFile.size(),
                                   scratch.data(), scratch.size());
      assert(bytes == other);
      if (!bytes) {
        printf("TRACK REJECT %s Q%u: %s\n\r", path, q, reader.error());
        rejected++;
        break;
      }
      assert(!memcmp(output.data(), fromFile.data(), bytes));
      if (period == 180000 && q % 4 == 0) {
        wholeTracks++;
        if (Adafruit_FloppyCodec::findAppleWriteSplice(scratch.data(),
                                                       reader.bitCount(q)))
          splices++;
      }
      // A valid packet stream must consume exactly the output and never emit
      // an interval outside the hardware writer's supported delay range.
      uint8_t *next = output.data(), *end = next + bytes;
      uint32_t elapsed = 0;
      while (next < end) {
        uint32_t ticks = greaseunpack(&next, end, true);
        assert(ticks > 20 && ticks <= 65535);
        elapsed += ticks;
      }
      assert(next == end && elapsed <= period * 48);
      assert(elapsed > period * 48 - 65535);
      converted++;
    }
  }
  assert(original == Adafruit_FloppyArchive::crc32(image.data(), image.size()));
  printf("CONVERT %s: %u position/period cases, %u rejected positions\n\r",
         path, converted, rejected);
  printf("SPLICE %s: %u/%u whole tracks have a verified sector gap\n\r", path,
         splices, wholeTracks);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    for (int i = 1; i < argc; i++)
      checkFile(argv[i]);
    return 0;
  }
  // Independent minimal field: 6-and-2 zero maps to $96, including checksum.
  // A valid following four-and-four address encloses a 256-bit inter-sector
  // gap. Bad checksums and weak data must not be treated as safe space.
  std::vector<uint8_t> field(512, 0xff);
  const uint8_t dataMark[] = {0xd5, 0xaa, 0xad};
  memcpy(field.data() + 40, dataMark, 3);
  memset(field.data() + 43, 0x96, 343);
  const uint8_t epilogue[] = {0xde, 0xaa, 0xeb};
  memcpy(field.data() + 386, epilogue, 3);
  const uint8_t address[] = {0xd5, 0xaa, 0x96, 0xaa, 0xaa, 0xaa,
                             0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
  memcpy(field.data() + 421, address, sizeof(address));
  assert(Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 4096) ==
         405 * 8);
  field[385] = 0x97;
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 4096));
  field[385] = 0x96;
  field[431] = 0xab;
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 4096));
  field[431] = 0xaa;
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 425 * 8));
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(nullptr, 4096));
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 65537));
  memset(field.data(), 0, field.size());
  assert(!Adafruit_FloppyCodec::findAppleWriteSplice(field.data(), 4096));
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
  assert(reader.bitCount(0) == bits && reader.bitCount(1) == 0);
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
  for (uint32_t first : {1u, 723u, bits - 1}) {
    size_t bytes =
        reader.flux(0, 200000, output.data(), output.size(), nullptr, 0, first);
    assert(bytes &&
           bytes == streamed.flux(0, 200000, streamedOutput.data(),
                                  streamedOutput.size(), scratch.data(),
                                  scratch.size(), first));
    assert(!memcmp(output.data(), streamedOutput.data(), bytes));
    uint8_t *next = output.data(), *end = next + bytes;
    uint32_t elapsed = 0;
    for (uint32_t i = 0; i < bits * 2; i++) {
      uint32_t bit = (i + first) % bits;
      if (bit % 7 != 1 && bit % 7 != 4 && bit % 7 != 6)
        continue;
      elapsed += greaseunpack(&next, end, true);
      assert(elapsed == ((uint64_t)(i + 1) * 4800000 + bits / 2) / bits);
    }
    assert(next == end);
  }
  assert(
      !reader.flux(0, 200000, output.data(), output.size(), nullptr, 0, bits));
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
