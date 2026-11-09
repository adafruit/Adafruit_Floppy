// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Synthetic flux only: exercise INDEX slicing and the two file containers
// without putting captured commercial disks in the repository.
#include "Adafruit_FloppyArchive.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static uint32_t get32(const uint8_t *p) {
  return p[0] + 256u * p[1] + 65536u * p[2] + 16777216u * p[3];
}

int main() {
  using Archive = Adafruit_FloppyArchive;
  assert(Archive::crc32((const uint8_t *)"123456789", 9) == 0xcbf43926);
  uint32_t crc = Archive::crc32((const uint8_t *)"1234", 4);
  assert(Archive::crc32((const uint8_t *)"56789", 5, crc) == 0xcbf43926);
  std::vector<uint32_t> flux(100001);
  for (size_t i = 0; i < flux.size(); i++)
    flux[i] = UINT32_MAX - i * 320;
  uint32_t index[4];
  for (int i = 0; i < 4; i++)
    index[i] = UINT32_MAX - 2000000 - i * 8000000;
  std::vector<uint8_t> raw(250000), work(65536), bits(8192);
  size_t count =
      Archive::indexedBits(flux.data(), flux.size(), index[0], index[1], 4,
                           work.data(), work.size(), bits.data(), bits.size());
  assert(count == 50000);
  for (size_t i = 0; i < count / 8; i++)
    assert(bits[i] == 0x55);
  // Repetitive data cannot prove a correction, even with a jittered edge.
  assert(Archive::indexedBits(flux.data(), flux.size(), index[0],
                              index[1] - 7 * 160, 4, work.data(), work.size(),
                              bits.data(), bits.size()) == 50007);
  assert(!Archive::indexedBits(flux.data(), 2, index[0], index[1], 4,
                               work.data(), work.size(), bits.data(),
                               bits.size()));
  assert(!Archive::indexedBits(flux.data(), flux.size(), index[0], index[1], 4,
                               work.data(), work.size(), bits.data(), 5));
  assert(!Archive::indexedBits(nullptr, flux.size(), index[0], index[1], 4,
                               work.data(), work.size(), bits.data(),
                               bits.size()));

  uint8_t header[Archive::A2R_HEADER_SIZE];
  Archive::a2rHeader(header, true);
  assert(!memcmp(header, "A2R3", 4) && get32(header + 12) == 37);
  assert(header[49] == 1 && header[50] == 1 && header[51] == 1 &&
         header[52] == 0);
  size_t bytes = Archive::a2rCapture(flux.data(), flux.size(), index, 3, 23,
                                     raw.data(), raw.size());
  assert(bytes && !memcmp(raw.data(), "RWCP", 4));
  assert(get32(raw.data() + 4) == bytes - 8 && get32(raw.data() + 9) == 25000);
  assert(raw[24] == 'C' && raw[25] == 3 && raw[26] == 23 && raw[28] == 3);
  for (int i = 0; i < 3; i++)
    assert(get32(raw.data() + 29 + 4 * i) == 8000000u * (i + 1));
  assert(get32(raw.data() + 41) == bytes - 46 && raw[bytes - 1] == 'X');
  // Independently unpack 255 extension bytes and recover every original
  // transition after the first INDEX, including a transition on INDEX itself.
  uint32_t ticks = 0, elapsed = 0;
  size_t sample = 6250;
  for (size_t i = 45; i < bytes - 1; i++) {
    ticks += raw[i];
    if (raw[i] == 255)
      continue;
    elapsed += ticks;
    ticks = 0;
    assert(sample < flux.size() && elapsed == index[0] - flux[sample++]);
  }
  assert(sample == flux.size());
  // Stopping on the last INDEX can legitimately precede the next transition.
  // Keep the final index timestamp without manufacturing a pulse there.
  assert(Archive::a2rCapture(flux.data(), 81250, index, 3, 0, raw.data(),
                             raw.size()));
  assert(!Archive::a2rCapture(flux.data(), flux.size(), index, 3, 160,
                              raw.data(), raw.size()));
  assert(!Archive::a2rCapture(flux.data(), flux.size(), index, 3, 0, raw.data(),
                              45));
  assert(!Archive::a2rCapture(flux.data(), 100, index, 3, 0, raw.data(),
                              raw.size()));
  assert(!Archive::a2rCapture(flux.data(), flux.size(), index, 6, 0, raw.data(),
                              raw.size()));
  flux[40000] = flux[39999];
  assert(!Archive::a2rCapture(flux.data(), flux.size(), index, 3, 0, raw.data(),
                              raw.size()));

  uint8_t woz[Archive::WOZ_HEADER_SIZE];
  Archive::wozHeader(woz, true);
  assert(!memcmp(woz, "WOZ2", 4) && woz[20] == 2 && woz[23] == 1 &&
         woz[24] == 0);
  assert(Archive::wozTrack(woz, 23, sizeof(woz), 50001));
  assert(woz[88 + 23] == 23 && woz[64] == 13);
  assert(get32(woz + 256 + 23 * 8 + 4) == 50001);
  assert(get32(woz + 252) == 1536 + 13 * 512 - 256);
  assert(!Archive::wozTrack(woz, 23, sizeof(woz), 50000));
  assert(!Archive::wozTrack(woz, 24, 1537, 50000));
  assert(!Archive::wozTrack(woz, 160, 1536, 50000));
  assert(!Archive::wozTrack(woz, 24, 1536, UINT32_MAX));

  // A deterministic random-looking channel stream makes the join unique.
  // Early or late INDEX edges must not drop or duplicate the channel bits
  // inside the sector spanning the join.
  std::vector<uint8_t> cycle(6250);
  uint32_t random = 12345;
  for (size_t i = 0; i < cycle.size(); i++) {
    random = random * 1664525u + 1013904223u;
    cycle[i] = random >> 24;
  }
  flux.clear();
  flux.push_back(UINT32_MAX);
  for (size_t i = 0; i < 150000; i++) {
    size_t bit = i % 50000;
    if (cycle[bit / 8] & (0x80 >> (bit % 8)))
      flux.push_back(UINT32_MAX - (i + 1) * 160);
  }
  for (int jitter : {-73, 7, 73}) {
    count = Archive::indexedBits(flux.data(), flux.size(), UINT32_MAX - 8000000,
                                 UINT32_MAX - 16000000 - jitter * 160, 4,
                                 work.data(), work.size(), bits.data(),
                                 bits.size());
    assert(count == 50000 && !memcmp(bits.data(), cycle.data(), cycle.size()));
  }
  assert(Archive::sameTrack(cycle.data(), 50000, bits.data(), 50000));
  // A changed gap after the next INDEX must not override a unique match of
  // the data immediately preceding it. The selected middle revolution stays
  // byte-for-byte intact; nothing is borrowed from the noisy next revolution.
  flux.clear();
  flux.push_back(UINT32_MAX);
  for (size_t i = 0; i < 150000; i++) {
    size_t bit = i % 50000;
    bool transition = cycle[bit / 8] & (0x80 >> (bit % 8));
    if (i >= 100064 && i < 100080)
      transition = !transition;
    if (transition)
      flux.push_back(UINT32_MAX - (i + 1) * 160);
  }
  count = Archive::indexedBits(flux.data(), flux.size(), UINT32_MAX - 8000000,
                               UINT32_MAX - 16000000 - 2 * 160, 4, work.data(),
                               work.size(), bits.data(), bits.size());
  assert(count == 50000 && !memcmp(bits.data(), cycle.data(), cycle.size()));
  for (uint8_t &byte : cycle)
    byte = ~byte;
  assert(!Archive::sameTrack(cycle.data(), 50000, bits.data(), 50000));
  memset(cycle.data(), 0xff, cycle.size());
  assert(!Archive::sameTrack(cycle.data(), 50000, cycle.data(), 50000));
  assert(!Archive::sameTrack(nullptr, 50000, bits.data(), 50000));
  assert(!Archive::sameTrack(bits.data(), 50000, bits.data(), 51000));
  puts("PASS indexed Apple bit boundaries, A2R3 flux roundtrip, WOZ2 bounds "
       "and CRC");
}
