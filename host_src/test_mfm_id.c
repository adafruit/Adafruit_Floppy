// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Run: gcc -Wall -Werror -I../src test_mfm_id.c -o /tmp/test_mfm_id &&
// /tmp/test_mfm_id
#include "mfm_impl.h"
#include <stdio.h>

static uint8_t flux[300000];
static uint8_t original[36 * 512];
static uint8_t decoded[sizeof(original)];
static uint8_t valid[36];
static uint8_t sector_count = 18;
static uint16_t nominal = 24;

static size_t decode(uint8_t cylinder, uint8_t head, uint8_t size,
                     bool strict) {
  memset(valid, 0, sizeof(valid));
  memset(decoded, 0, sizeof(decoded));
  mfm_io_t io = {
      .T1_nom = nominal,
      .T2_max = nominal * 5 / 2,
      .T3_max = nominal * 7 / 2,
      .pulses = flux,
      .n_pulses = sizeof(flux),
      .sectors = decoded,
      .n_sectors = sector_count,
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
      .T1_nom = nominal,
      .T2_max = nominal * 5 / 2,
      .T3_max = nominal * 7 / 2,
      .pulses = flux,
      .n_pulses = sizeof(flux),
      .sectors = original,
      .n_sectors = sector_count,
      .cylinder = 37,
      .head = 1,
      .n = 2,
      .settings = &standard_mfm,
  };
  encode_track_mfm(&io);
  assert(decode(37, 1, 2, true) == 18);
  assert(memcmp(original, decoded, sector_count * 512) == 0);
  assert(decode(36, 1, 2, true) == 0);
  assert(decode(37, 0, 2, true) == 0);
  assert(decode(37, 1, 1, true) == 0);
  assert(decode(0, 0, 2, false) == 18);
  puts("PASS: matching sectors, wrong cylinder/head/size, legacy decoding");
  const uint8_t geometries[] = {9, 10, 11, 18, 21, 22, 36};
  for (size_t geometry = 0; geometry < sizeof(geometries); geometry++) {
    sector_count = geometries[geometry];
    nominal = sector_count <= 11 ? 48 : sector_count == 36 ? 12 : 24;
    io.T1_nom = nominal;
    io.T2_max = nominal * 5 / 2;
    io.T3_max = nominal * 7 / 2;
    io.n_sectors = sector_count;
    memset(flux, 0, sizeof(flux));
    encode_track_mfm(&io);
    assert(decode(37, 1, 2, true) == sector_count);
    assert(memcmp(original, decoded, sector_count * 512) == 0);
  }
  puts("PASS: 9/10/11/18/21/22/36 sectors at 250/500/1000 kbit/s");
  return 0;
}
