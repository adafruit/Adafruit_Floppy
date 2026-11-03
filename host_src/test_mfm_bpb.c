// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "mfm_bpb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t boot[512], original[512];

int main(void) {
  FILE *file = fopen("fixtures/boot-1440.img", "rb");
  assert(file &&
         fread(original, 1, sizeof(original), file) == sizeof(original));
  fclose(file);
  mfm_bpb_geometry_t geometry;
  assert(mfm_bpb_geometry(original, &geometry));
  assert(geometry.cylinders == 80 && geometry.sectors == 18);

  file = fopen("fixtures/boot-zero-heads.img", "rb");
  assert(file && fread(boot, 1, sizeof(boot), file) == sizeof(boot));
  fclose(file);
  assert(!mfm_bpb_geometry(boot, &geometry));

  // Values are little endian. These formerly allowed division by zero,
  // narrowing overflow, or a geometry larger than the driver's sector buffer.
  const struct {
    unsigned offset, value;
  } bad[] = {{11, 1024}, {19, 0},   {19, 2881}, {19, 65535}, {24, 0},
             {24, 19},   {24, 256}, {26, 0},    {26, 1},     {26, 256}};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    memcpy(boot, original, sizeof(boot));
    boot[bad[i].offset] = bad[i].value;
    boot[bad[i].offset + 1] = bad[i].value / 256;
    assert(!mfm_bpb_geometry(boot, &geometry));
  }
  memcpy(boot, original, sizeof(boot));
  boot[0] = 0;
  assert(!mfm_bpb_geometry(boot, &geometry));
  printf("PASS DOS geometry: valid 1.44 MB and malformed boot sectors\n\r");
  return 0;
}
