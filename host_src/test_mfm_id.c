// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Host-side regression test for strict MFM sector identification. This runs on
// a computer using synthetic flux in memory; no board or floppy drive is
// needed. It belongs with the host tools, rather than the hardware tests, and
// is not included in Arduino firmware. Run manually from host_src (not part of
// CI): gcc -Wall -Werror -I../src test_mfm_id.c -o /tmp/test_mfm_id &&
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
  // Clear both data and validity so a previous successful decode cannot make
  // a later rejection test pass with stale results.
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
  // Use repeatable data that varies by byte and sector so an incorrect payload
  // or sector order is visible in the byte-for-byte comparison below.
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
  // The encoded track is cylinder 37, head 1, with 18 sectors of 512 bytes.
  // MFM stores sector size as an exponent: size code 2 means 128 << 2 bytes.
  assert(decode(37, 1, 2, true) == 18);
  assert(memcmp(original, decoded, sector_count * 512) == 0);
  // A valid CRC is not enough: strict mode must reject sector IDs from a
  // different cylinder, head, or sector size.
  assert(decode(36, 1, 2, true) == 0);
  assert(decode(37, 0, 2, true) == 0);
  assert(decode(37, 1, 1, true) == 0);
  // Callers opting out of strict validation retain the old permissive behavior.
  assert(decode(0, 0, 2, false) == 18);
  puts("PASS: matching sectors, wrong cylinder/head/size, legacy decoding");
  // Exercise several track capacities at DD, HD, and ED data rates. These are
  // encoder/decoder round trips, not a substitute for tests on real media.
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
