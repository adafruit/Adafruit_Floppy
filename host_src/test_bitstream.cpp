// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// API boundary tests, run with ASan/UBSan alongside the independent fixtures.
#include "Adafruit_FloppyBitstream.h"
#include "Adafruit_FloppyCodec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main() {
  Adafruit_FloppyBitstream pll;
  uint8_t storage[32];
  assert(!pll.begin(nullptr, sizeof(storage), 40000000, 1));
  assert(!pll.begin(storage, 0, 40000000, 1));
  assert(!pll.begin(storage, sizeof(storage), 0, 1));
  assert(!pll.begin(storage, sizeof(storage), 40000000, NAN));
  assert(!pll.begin(storage, sizeof(storage), INFINITY, 1));
  assert(!pll.begin(storage, sizeof(storage), 40000000, 1, -1));
  assert(!pll.begin(storage, sizeof(storage), 40000000, 1, 0.6, 2));
  assert(!pll.addInterval(80));
  assert(pll.bitCount() == 0);

  assert(pll.begin(storage, 1, 40000000, 1));
  assert(pll.addInterval(80));
  assert(pll.addInterval(120));
  assert(pll.bitCount() == 5 && storage[0] == 0x48);
  assert(!pll.addInterval(160)); // It would cross the one-byte capacity.
  assert(pll.bitCount() == 0);
  assert(!pll.addInterval(40)); // Failure remains latched until begin().
  assert(pll.begin(storage, sizeof(storage), 40000000, 1));
  assert(!pll.addInterval(0));
  assert(pll.bitCount() == 0);
  assert(pll.begin(storage, sizeof(storage), 40000000, 1));
  assert(!pll.addInterval(UINT32_MAX));

  // A zero interval must fail even when it is consumed inside a glitch pair.
  const uint32_t invalid[][4] = {
      {80, 0, 80, 80}, {80, 2, 0, 80}, {80, 2, 1, 0}, {0, 80, 80, 80}};
  for (const auto &intervals : invalid) {
    assert(!Adafruit_FloppyBitstream::recover(intervals, 4, 40000000, 1,
                                              storage, sizeof(storage), 1));
  }
  const uint32_t good[] = {80, 120, 160};
  assert(Adafruit_FloppyBitstream::recover(good, 3, 40000000, 1, storage,
                                           sizeof(storage)) == 9);
  assert(!Adafruit_FloppyBitstream::recover(good, 3, 40000000, 1, storage,
                                            sizeof(storage), NAN));
  assert(!Adafruit_FloppyBitstream::recover(nullptr, 3, 40000000, 1, storage,
                                            sizeof(storage)));

  // Random malformed captures exercise field bounds, sync searches, and every
  // partial-byte endpoint under the sanitizers. They are never valid fixtures.
  uint32_t random = 0x2350;
  uint8_t noise[4096];
  for (auto &byte : noise) {
    random = random * 1664525 + 1013904223;
    byte = random >> 24;
  }
  for (int encoding = 0; encoding <= 4; encoding++) {
    auto format = (Adafruit_FloppyCodec::Encoding)encoding;
    assert(Adafruit_FloppyCodec::decode(format, nullptr, 1).sectors == 0);
    for (size_t length = 0; length < sizeof(noise) * 8; length += 1031) {
      Adafruit_FloppyCodec::decode(format, noise, length);
    }
  }
  puts("PASS bitstream boundaries and malformed input\n\r");
}
