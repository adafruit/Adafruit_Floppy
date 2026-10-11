// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Run: gcc -Wall -Werror -I../src test_mfm_id.c -o /tmp/test_mfm_id &&
// /tmp/test_mfm_id
#include "mfm_impl.h"
#include <stdio.h>

static uint8_t flux[150000];
static uint8_t original[18 * 512];
static uint8_t decoded[sizeof(original)];
static uint8_t valid[18];

static size_t decode(uint8_t cylinder, uint8_t head, uint8_t size,
                     bool strict) {
  memset(valid, 0, sizeof(valid));
  memset(decoded, 0, sizeof(decoded));
  mfm_io_t io = {
      .T1_nom = 24,
      .T2_max = 60,
      .T3_max = 84,
      .pulses = flux,
      .n_pulses = sizeof(flux),
      .sectors = decoded,
      .n_sectors = 18,
      .sector_validity = valid,
      .cylinder = cylinder,
      .head = head,
      .n = size,
      .validate_id = strict,
  };
  return decode_track_mfm(&io);
}

int main(void) {
  for (size_t i = 0; i < sizeof(original); i++)
    original[i] = i * 17 + i / 512;
  mfm_io_t io = {
      .T1_nom = 24,
      .T2_max = 60,
      .T3_max = 84,
      .pulses = flux,
      .n_pulses = sizeof(flux),
      .sectors = original,
      .n_sectors = 18,
      .cylinder = 37,
      .head = 1,
      .n = 2,
      .settings = &standard_mfm,
  };
  encode_track_mfm(&io);
  assert(decode(37, 1, 2, true) == 18);
  assert(memcmp(original, decoded, sizeof(original)) == 0);
  assert(decode(36, 1, 2, true) == 0);
  assert(decode(37, 0, 2, true) == 0);
  assert(decode(37, 1, 1, true) == 0);
  assert(decode(0, 0, 2, false) == 18);
  puts("PASS: matching sectors, wrong cylinder/head/size, legacy decoding");
  return 0;
}
