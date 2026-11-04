// Host regression: long quiet gaps, index boundaries, counter wrap, malformed
// capture rejection, and a fixture readable by the real Greaseweazle parser.
#include "../src/Adafruit_FloppySCP.h"
#include "../src/greasepack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static bool readSource(void *context, uint32_t offset, uint8_t *data,
                       size_t size) {
  auto &source = *(std::vector<uint8_t> *)context;
  if (offset > source.size() || size > source.size() - offset)
    return false;
  memcpy(data, source.data() + offset, size);
  return true;
}

static void updateChecksum(std::vector<uint8_t> &source) {
  scpPut32(source.data() + 12,
           scpChecksum(source.data() + 16, source.size() - 16));
}

static void testReplay() {
  // A regular 2.5 us flux stream tests absolute-time rescaling to a slightly
  // slower target drive. No sector encoder participates in this comparison.
  std::vector<uint32_t> samples;
  uint32_t index[] = {30000000, 22000000, 14000000, 6000000};
  for (uint32_t time = 100; time <= 24000000; time += 100)
    samples.push_back(index[0] - time);
  std::vector<uint8_t> source(SCP_HEADER_SIZE + SCP_TRACK_CAPACITY);
  scpHeader(source.data());
  source[7] = 0;
  scpPut32(source.data() + 16, SCP_HEADER_SIZE);
  size_t length = scpTrack(0, samples.data(), samples.size(), index,
                           source.data() + SCP_HEADER_SIZE, SCP_TRACK_CAPACITY);
  assert(length);
  source.resize(SCP_HEADER_SIZE + length);
  updateChecksum(source);
  Adafruit_FloppySCPReader reader(readSource, &source, source.size());
  assert(reader.begin() && reader.hasTrack(0) && !reader.hasTrack(1));
  std::vector<uint8_t> output(300000);
  size_t bytes = reader.trackFlux(0, 201000, output.data(), output.size());
  assert(bytes);
  uint8_t *next = output.data(), *end = next + bytes;
  uint64_t total = 0, edges = 0;
  while (next < end) {
    total += greaseunpack(&next, end, true);
    edges++;
    uint64_t expected = (edges * 100 * 201000 * 24 + 4000000) / 8000000;
    assert(total == expected);
  }
  assert(total >= 201000 * 24 * 11 / 10);
  assert(!reader.trackFlux(0, 201000, output.data(), 10));
  assert(!reader.trackFlux(0, 100000, output.data(), output.size()));
  for (unsigned field : {5u, 7u, 8u, 9u, 11u, 16u}) {
    auto bad = source;
    bad[field] = field == 8 ? 0 : 255;
    updateChecksum(bad);
    Adafruit_FloppySCPReader rejected(readSource, &bad, bad.size());
    assert(!rejected.begin());
  }
  auto bad = source;
  bad.back() ^= 1;
  Adafruit_FloppySCPReader checksum(readSource, &bad, bad.size());
  assert(!checksum.begin());
  bad = source;
  scpPut32(bad.data() + SCP_HEADER_SIZE + 8, UINT32_MAX);
  updateChecksum(bad);
  Adafruit_FloppySCPReader bounds(readSource, &bad, bad.size());
  assert(!bounds.begin());
  bad = source;
  scpPut32(bad.data() + 16, UINT32_MAX - 3);
  updateChecksum(bad);
  Adafruit_FloppySCPReader overflow(readSource, &bad, bad.size());
  assert(!overflow.begin());
  bad = source;
  bad[SCP_HEADER_SIZE + SCP_TRACK_HEADER_SIZE + 3] =
      1; // Impossible second interval.
  updateChecksum(bad);
  Adafruit_FloppySCPReader spacing(readSource, &bad, bad.size());
  assert(spacing.begin());
  assert(!spacing.trackFlux(0, 200000, output.data(), output.size()));
  puts("SCP replay scaling, seam, checksum, bounds and pulse rejection PASS");
}

static uint32_t get32(const uint8_t *p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}

int main(int argc, char **argv) {
  testReplay();
  uint8_t flags[SCP_HEADER_SIZE];
  scpHeader(flags);
  assert(flags[8] == 0x83);
  scpHeader(flags, false);
  assert(flags[8] ==
         0x82); // Timed capture is neither index-cued nor normalized.
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
