// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Host adapter for the portable Arduino codec, used by independent fixtures.
#include "Adafruit_FloppyBitstream.h"
#include "Adafruit_FloppyCodec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static bool saveSector(void *context, const Adafruit_FloppyCodec::Sector &s) {
  FILE *file = (FILE *)context;
  uint8_t header[] = {s.cylinder,
                      s.head,
                      s.number,
                      (uint8_t)s.size,
                      (uint8_t)(s.size >> 8),
                      (uint8_t)s.volume,
                      (uint8_t)(s.volume >> 8),
                      (uint8_t)s.deleted};
  return fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
         fwrite(s.data, 1, s.size, file) == s.size;
}

int main(int argc, char **argv) {
  if (argc != 7) {
    fprintf(stderr, "Usage: track_codec encoding bits|flux input bit-count|Hz "
                    "cell-us output\n\r");
    return 2;
  }
  int encoding = atoi(argv[1]);
  if (encoding < -1 || encoding > 4) {
    return 2;
  }
  FILE *input = fopen(argv[3], "rb");
  if (!input) {
    return 2;
  }
  fseek(input, 0, SEEK_END);
  long length = ftell(input);
  if (length < 0 || length > 32 * 1024 * 1024) {
    fclose(input);
    return 2;
  }
  rewind(input);
  std::vector<uint8_t> bytes(length);
  if (fread(bytes.data(), 1, length, input) != (size_t)length) {
    fclose(input);
    return 2;
  }
  fclose(input);
  std::vector<uint8_t> recovered;
  const uint8_t *bits = bytes.data();
  size_t count = strtoul(argv[4], nullptr, 10);
  if (!strcmp(argv[2], "flux") || !strcmp(argv[2], "filtered")) {
    if (bytes.size() % 4) {
      return 2;
    }
    recovered.resize(1024 * 1024);
    std::vector<uint32_t> intervals;
    for (size_t i = 0; i < bytes.size(); i += 4) {
      uint32_t ticks = (uint32_t)bytes[i] | ((uint32_t)bytes[i + 1] << 8) |
                       ((uint32_t)bytes[i + 2] << 16) |
                       ((uint32_t)bytes[i + 3] << 24);
      intervals.push_back(ticks);
    }
    if (encoding == -1) {
      auto detection = Adafruit_FloppyCodec::detect(
          intervals.data(), intervals.size(), strtoul(argv[4], nullptr, 10),
          recovered.data(), recovered.size());
      encoding = detection.encoding;
      count = detection.bitCount;
      printf("DETECT %d ENCODING %d UNIQUE %u CLOCK %.6f\n\r", detection.found,
             encoding, detection.uniqueSectors, detection.bitCellMicroseconds);
    } else if (!strcmp(argv[2], "filtered")) {
      float clock = atof(argv[5]);
      float cutoff = encoding >= 3   ? 2.5f
                     : encoding == 1 ? clock * 0.5f
                                     : clock;
      count = Adafruit_FloppyBitstream::recover(
          intervals.data(), intervals.size(), strtoul(argv[4], nullptr, 10),
          clock, recovered.data(), recovered.size(), cutoff);
    } else {
      Adafruit_FloppyBitstream pll;
      if (!pll.begin(recovered.data(), recovered.size(), atof(argv[4]),
                     atof(argv[5]))) {
        return 2;
      }
      for (uint32_t ticks : intervals) {
        if (!pll.addInterval(ticks)) {
          fprintf(stderr, "Invalid interval or bit buffer overflow\n\r");
          return 2;
        }
      }
      count = pll.bitCount();
    }
    bits = recovered.data();
  } else if (strcmp(argv[2], "bits") || count > bytes.size() * 8) {
    return 2;
  }
  FILE *output = fopen(argv[6], "wb");
  if (!output) {
    return 2;
  }
  auto result =
      Adafruit_FloppyCodec::decode((Adafruit_FloppyCodec::Encoding)encoding,
                                   bits, count, saveSector, output);
  bool failed = ferror(output) != 0;
  fclose(output);
  printf("SECTORS %zu BADHEAD %zu BADDATA %zu BITS %zu\n\r", result.sectors,
         result.badHeaders, result.badData, count);
  return failed ? 2 : result.sectors ? 0 : 1;
}
