// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Replay real drive captures on the host, including recoverable timing jitter
// and unreadable data. Run from host_src with `make check-files`.
#include "mfm_impl.h"
#include <stdio.h>

static uint8_t flux[300000], decoded[18 * 512], expected[18 * 512];
static uint8_t valid[18];

static size_t load(const char *name, uint8_t *buffer, size_t capacity) {
  FILE *file = fopen(name, "rb");
  assert(file);
  size_t count = fread(buffer, 1, capacity, file);
  assert(!ferror(file) && fgetc(file) == EOF);
  fclose(file);
  return count;
}

static size_t decode(size_t length, uint8_t cylinder) {
  // Clear both buffers: a previous good decode must not conceal a bad capture.
  memset(decoded, 0, sizeof(decoded));
  memset(valid, 0, sizeof(valid));
  mfm_io_t io = {.T1_nom = 24,
                 .T2_max = 60,
                 .T3_max = 84,
                 .pulses = flux,
                 .n_pulses = length,
                 .sectors = decoded,
                 .n_sectors = 18,
                 .sector_validity = valid,
                 .cylinder = cylinder,
                 .head = 0,
                 .n = 2,
                 .validate_id = true};
  return decode_track_mfm(&io);
}

int main(void) {
  assert(load("fixtures/c05h0.img", expected, sizeof(expected)) ==
         sizeof(expected));
  const char *good[] = {"fixtures/clean.flux24", "fixtures/borderline.flux24"};
  for (size_t i = 0; i < 2; i++) {
    size_t length = load(good[i], flux, sizeof(flux));
    assert(decode(length, 5) == 18);
    assert(memcmp(decoded, expected, sizeof(expected)) == 0);
    printf("PASS %s: 18 sectors, every byte matches\n\r", good[i]);
  }

  // A sector image has no CRC metadata. Only comparison against an independent
  // oracle can detect the deliberately flipped byte in this otherwise valid
  // IMG.
  assert(load("fixtures/wrong-byte.img", expected, sizeof(expected)) ==
         sizeof(expected));
  assert(memcmp(decoded, expected, sizeof(expected)) != 0);

  size_t length = load("fixtures/truncated.flux24", flux, sizeof(flux));
  size_t count = decode(length, 5);
  assert(count > 0 && count < 18);
  printf("PASS truncated capture: only %zu/18 sectors accepted\n\r", count);

  assert(load("fixtures/c05h0.img", expected, sizeof(expected)) ==
         sizeof(expected));
  length = load("fixtures/bad-crc.flux24", flux, sizeof(flux));
  assert(decode(length, 5) == 17);
  for (size_t i = 0; i < 18; i++) {
    assert(valid[i] == (i != 7));
    if (valid[i])
      assert(memcmp(decoded + i * 512, expected + i * 512, 512) == 0);
  }
  printf("PASS corrupted flux: sector 8 CRC rejected; 17 neighbours match\n\r");

  assert(load("fixtures/c01h0.img", expected, sizeof(expected)) ==
         sizeof(expected));
  length = load("fixtures/missing-dam.flux24", flux, sizeof(flux));
  assert(decode(length, 1) == 17);
  for (size_t i = 0; i < 18; i++) {
    assert(valid[i] == (i != 9));
    if (valid[i])
      assert(memcmp(decoded + i * 512, expected + i * 512, 512) == 0);
  }
  printf("PASS damaged capture: sector 10 rejected; 17 neighbours match\n\r");

  // Rule out a data-dependent encoder failure using the random-data track
  // that produced the damaged recording. No drive participates in this test.
  mfm_io_t io = {.T1_nom = 24,
                 .pulses = flux,
                 .n_pulses = sizeof(flux),
                 .sectors = expected,
                 .n_sectors = 18,
                 .cylinder = 1,
                 .head = 0,
                 .n = 2,
                 .settings = &standard_mfm};
  encode_track_mfm(&io);
  assert(decode(io.pos, 1) == 18);
  assert(memcmp(decoded, expected, sizeof(expected)) == 0);
  printf("PASS random-data software encode/decode\n\r");
  return 0;
}
