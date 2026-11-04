// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Compare complete encoded bitstreams with independently generated Greaseweazle
// tracks, including gaps, CRCs, clock bits, and the final pre-index padding.
// Run from host_src. This test needs neither a board nor the Python package.
#include "mfm_impl.h"
#include <stdio.h>

static uint8_t sectors[18 * 512], encoded[25000], reference[25000];

static void check(const char *name, size_t count, uint8_t size, bool fm) {
  char path[80];
  snprintf(path, sizeof(path), "layout_fixtures/%s.bits", name);
  FILE *file = fopen(path, "rb");
  assert(file);
  size_t bytes = fread(reference, 1, sizeof(reference), file);
  assert(bytes && fgetc(file) == EOF);
  fclose(file);

  size_t sector_bytes = 128u << size;
  for (size_t i = 0; i < count * sector_bytes; i++)
    sectors[i] = i * 17 + i / sector_bytes;
  memset(encoded, 0, sizeof(encoded));
  mfm_io_t io = {.encode_compact = true,
                 .pulses = encoded,
                 .n_pulses = bytes,
                 .sectors = sectors,
                 .n_sectors = count,
                 .cylinder = 43,
                 .head = 0,
                 .n = size,
                 .settings = mfm_io_standard_settings(count, size, fm)};
  if (!strcmp(name, "1581")) {
    io.settings = &standard_mfm_1581;
    io.head = 1; // Physical head 0 stores logical head 1 on a 1581.
  }
  encode_track_mfm(&io);
  assert(io.pos == bytes);
  for (size_t i = 0; i < bytes; i++) {
    if (encoded[i] != reference[i]) {
      fprintf(stderr, "%s: mismatch at byte %zu: %02x != %02x\n", name, i,
              encoded[i], reference[i]);
      assert(false);
    }
  }
  printf("PASS %s: %zu encoded bytes match Greaseweazle\n\r", name, bytes);
}

int main(void) {
  check("1440", 18, 2, false);
  check("720", 9, 2, false);
  check("fm26", 26, 0, true);
  check("1581", 10, 2, false);
  return 0;
}
