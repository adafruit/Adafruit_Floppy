// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Check rejection/buffer boundaries, then emit flux for the independent oracle.
#include "Adafruit_FloppyCodec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 6)
    return 2;
  unsigned cylinder = atoi(argv[3]), period = atoi(argv[4]);
  unsigned diskID = strtoul(argv[5], nullptr, 0);
  unsigned sectors = cylinder < 17   ? 21
                     : cylinder < 24 ? 19
                     : cylinder < 30 ? 18
                                     : 17;
  std::vector<uint8_t> source(sectors * 256), output(210002, 0x5a);
  FILE *input = fopen(argv[1], "rb");
  if (!input)
    return 2;
  bool read = fread(source.data(), 1, source.size(), input) == source.size() &&
              fgetc(input) == EOF;
  fclose(input);
  if (!read)
    return 2;
  auto encode = [&](const uint8_t *data, unsigned c, size_t capacity,
                    unsigned us) {
    return Adafruit_FloppyCodec::encodeC64(data, c, diskID, output.data() + 1,
                                           capacity, us);
  };
  assert(!encode(nullptr, cylinder, 210000, period));
  assert(!Adafruit_FloppyCodec::encodeC64(source.data(), cylinder, diskID,
                                          nullptr, 210000, period));
  assert(!encode(source.data(), 35, 210000, period));
  assert(!encode(source.data(), cylinder, 210000, 179999));
  assert(!encode(source.data(), cylinder, 210000, 220001));
  assert(!encode(source.data(), cylinder, 0, period));
  assert(output[0] == 0x5a && output[1] == 0x5a && output.back() == 0x5a);
  size_t count = encode(source.data(), cylinder, 210000, period);
  assert(count && output.front() == 0x5a && output.back() == 0x5a);
  FILE *file = fopen(argv[2], "wb");
  if (!file)
    return 2;
  bool saved = fwrite(output.data() + 1, 1, count, file) == count;
  fclose(file);
  assert(!encode(source.data(), cylinder, count - 1, period));
  assert(output.front() == 0x5a && output.back() == 0x5a);
  return saved ? 0 : 2;
}
