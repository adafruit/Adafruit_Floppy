// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Exercise argument/buffer rejection and emit flux for the independent oracle.
#include "Adafruit_FloppyCodec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 7)
    return 2;
  unsigned sectors = atoi(argv[3]), cylinder = atoi(argv[4]),
           head = atoi(argv[5]);
  unsigned period = atoi(argv[6]);
  std::vector<uint8_t> source(sectors * 512), output(210002, 0x5a);
  FILE *input = fopen(argv[1], "rb");
  if (!input)
    return 2;
  bool read = fread(source.data(), 1, source.size(), input) == source.size() &&
              fgetc(input) == EOF;
  fclose(input);
  if (!read)
    return 2;
  auto encode = [&](const uint8_t *data, unsigned count, unsigned c, unsigned h,
                    size_t capacity, unsigned us) {
    return Adafruit_FloppyCodec::encodeAmiga(data, count, c, h,
                                             output.data() + 1, capacity, us);
  };
  assert(!encode(nullptr, sectors, cylinder, head, 210000, period));
  assert(!encode(source.data(), 12, cylinder, head, 210000, period));
  assert(!encode(source.data(), sectors, 80, head, 210000, period));
  assert(!encode(source.data(), sectors, cylinder, 2, 210000, period));
  assert(!encode(source.data(), sectors, cylinder, head, 210000, 179999));
  assert(!encode(source.data(), sectors, cylinder, head, 210000, 220001));
  assert(!encode(source.data(), sectors, cylinder, head, 0, period));
  assert(output[0] == 0x5a && output[1] == 0x5a && output.back() == 0x5a);
  size_t count = encode(source.data(), sectors, cylinder, head, 210000, period);
  assert(count && output.front() == 0x5a && output.back() == 0x5a);
  FILE *file = fopen(argv[2], "wb");
  if (!file)
    return 2;
  bool saved = fwrite(output.data() + 1, 1, count, file) == count;
  fclose(file);
  assert(!encode(source.data(), sectors, cylinder, head, count - 1, period));
  return saved ? 0 : 2;
}
