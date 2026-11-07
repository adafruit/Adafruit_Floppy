// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Emit the library's packed Mac flux for the independent Greaseweazle oracle.
#include "Adafruit_FloppyBitstream.h"
#include "Adafruit_FloppyCodec.h"
#include "greasepack.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 7 && argc != 8)
    return 2;
  unsigned cylinder = atoi(argv[3]), head = atoi(argv[4]),
           heads = atoi(argv[5]);
  unsigned period = atoi(argv[6]);
  std::vector<uint8_t> source((12 - cylinder / 16) * 512), output(210002, 0x5a);
  std::vector<uint8_t> tags((12 - cylinder / 16) * 12);
  if (argc == 8)
    for (size_t i = 0; i < tags.size(); i++)
      tags[i] = i * 13 + 7;
  FILE *input = fopen(argv[1], "rb");
  if (!input)
    return 2;
  bool read = fread(source.data(), 1, source.size(), input) == source.size() &&
              fgetc(input) == EOF;
  fclose(input);
  if (!read)
    return 2;
  auto encode = [&](const uint8_t *data, unsigned c, unsigned h, unsigned sides,
                    size_t capacity, unsigned us) {
    return Adafruit_FloppyCodec::encodeMac(data, c, h, sides, output.data() + 1,
                                           capacity, us, tags.data());
  };
  assert(!encode(nullptr, cylinder, head, heads, 210000, period));
  assert(!encode(source.data(), 80, head, heads, 210000, period));
  assert(!encode(source.data(), cylinder, 2, heads, 210000, period));
  assert(!encode(source.data(), cylinder, 1, 1, 210000, period));
  assert(!encode(source.data(), cylinder, head, 0, 210000, period));
  assert(!encode(source.data(), cylinder, head, heads, 210000, 179999));
  assert(!encode(source.data(), cylinder, head, heads, 210000, 220001));
  assert(!encode(source.data(), cylinder, head, heads, 0, period));
  assert(output[0] == 0x5a && output[1] == 0x5a && output.back() == 0x5a);
  size_t count = encode(source.data(), cylinder, head, heads, 210000, period);
  assert(count && output.front() == 0x5a && output.back() == 0x5a);
  // Check the callback's separate tag bytes and early-stop behavior too.
  std::vector<uint32_t> intervals;
  uint8_t *next = output.data() + 1, *end = next + count;
  while (next < end)
    intervals.push_back(greaseunpack(&next, end, true));
  std::vector<uint8_t> bits(100000);
  size_t bitCount = Adafruit_FloppyBitstream::recover(
      intervals.data(), intervals.size(), 24000000,
      Adafruit_FloppyCodec::macBitCellMicroseconds(cylinder), bits.data(),
      bits.size());
  struct Check {
    const uint8_t *data;
    const uint8_t *tags;
  } check = {source.data(), tags.data()};
  auto visit = [](void *context, const Adafruit_FloppyCodec::Sector &sector) {
    Check &expected = *(Check *)context;
    assert(sector.size == 512 && sector.tags);
    assert(!memcmp(sector.data, expected.data + sector.number * 512, 512));
    assert(!memcmp(sector.tags, expected.tags + sector.number * 12, 12));
    return false;
  };
  auto result = Adafruit_FloppyCodec::decode(
      Adafruit_FloppyCodec::MAC_GCR, bits.data(), bitCount, visit, &check);
  assert(result.sectors == 1);
  FILE *file = fopen(argv[2], "wb");
  if (!file)
    return 2;
  bool saved = fwrite(output.data() + 1, 1, count, file) == count;
  fclose(file);
  assert(!encode(source.data(), cylinder, head, heads, count - 1, period));
  return saved ? 0 : 2;
}
