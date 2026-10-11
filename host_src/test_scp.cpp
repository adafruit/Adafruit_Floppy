// Host regression: long quiet gaps, index boundaries, counter wrap, malformed
// capture rejection, and a fixture readable by the real Greaseweazle parser.
#include "../src/Adafruit_FloppySCP.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

static uint32_t get32(const uint8_t *p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}

int main(int argc, char **argv) {
  uint32_t origin =
      10000000; // Three revs deliberately wrap the 32-bit counter.
  uint32_t index[4] = {origin, origin - 8000000, origin - 16000000,
                       origin - 24000000};
  std::vector<uint32_t> times = {100,     65636,    70000,
                                 8000100, 16000100, 23999000};
  std::vector<uint32_t> samples = {origin + 100}; // Discard the lead-in sample.
  for (auto time : times)
    samples.push_back(origin - time);
  samples.push_back(origin - 24000100); // Discard the trailing partial rev.
  std::vector<uint8_t> track(SCP_TRACK_CAPACITY);
  size_t size = scpTrack(0, samples.data(), samples.size(), index, track.data(),
                         track.size());
  assert(size > SCP_TRACK_HEADER_SIZE);
  uint32_t absolute = 0;
  size_t edge = 0;
  for (size_t i = SCP_TRACK_HEADER_SIZE; i < size; i += 2) {
    uint16_t word = uint16_t(track[i]) << 8 | track[i + 1];
    absolute += word ? word : 65536;
    if (word) {
      assert(edge < times.size());
      assert(absolute == times[edge] || absolute + 1 == times[edge]);
      edge++;
    }
  }
  assert(edge == times.size());
  size_t offset = SCP_TRACK_HEADER_SIZE;
  for (uint8_t rev = 0; rev < 3; rev++) {
    assert(get32(track.data() + 4 + 12 * rev) == 8000000);
    assert(get32(track.data() + 12 + 12 * rev) == offset);
    offset += 2 * get32(track.data() + 8 + 12 * rev);
  }
  assert(offset == size);
  if (argc == 2) {
    uint8_t header[SCP_HEADER_SIZE];
    scpHeader(header);
    header[7] = 0;
    scpPut32(header + 16, sizeof(header));
    scpPut32(header + 12, scpChecksum(header + 16, sizeof(header) - 16) +
                              scpChecksum(track.data(), size));
    FILE *file = fopen(argv[1], "wb");
    assert(file);
    assert(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    assert(fwrite(track.data(), 1, size, file) == size);
    fclose(file);
  }
  assert(scpTrack(0, samples.data(), samples.size(), index, track.data(), 40) ==
         0);
  assert(scpTrack(160, samples.data(), samples.size(), index, track.data(),
                  track.size()) == 0);
  samples[2] = samples[1]; // Duplicate/nonmonotonic timestamp must fail.
  assert(scpTrack(0, samples.data(), samples.size(), index, track.data(),
                  track.size()) == 0);
  // No transitions: represent duration with extensions, without inventing
  // edges.
  size = scpTrack(0, nullptr, 0, index, track.data(), track.size());
  assert(size > 40);
  for (size_t i = 40; i < size; i++)
    assert(track[i] == 0);
  index[1] = origin - 100;
  assert(scpTrack(0, nullptr, 0, index, track.data(), track.size()) == 0);
  puts("SCP boundary, long-gap, capacity and malformed-input tests PASS");
}
