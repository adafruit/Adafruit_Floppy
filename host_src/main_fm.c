#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#include "mfm_impl.h"

enum { sector_count = 26 };
enum { block_size = 128 };

uint8_t flux[10000];
uint8_t track_buf[sector_count * block_size];
uint8_t validity[sector_count];

mfm_io_t io = {
    .T1_nom = 2,
    .T2_max = 5,
    .T3_max = 7,
    .pulses = flux,
    .n_pulses = sizeof(flux),
    .sectors = track_buf,
    .sector_validity = validity,
    .n_sectors = sector_count,
    .n = 0,
    .settings = &standard_fm,
    .encode_raw = mfm_io_encode_raw_fm,
    .encode_compact = true,
};

static void dump_flux_compact(const char *filename, mfm_io_t *io) {
  FILE *f = fopen(filename, "w");
  io->pos = 0;
  while (!mfm_io_eof(io)) {
    int b = io->pulses[io->pos++];
    for (int i = 8; i-- > 0;) {
      fputc('0' + ((b >> i) & 1), f);
    };
    if (io->pos % 2 == 0) {
      fputc('\n', f);
    }
  }
  fclose(f);
}

int main() {
  for (size_t i = 0; i < sector_count; i++) {
    memset(track_buf + i * block_size, block_size, 'A' + i);
  }

  size_t r = encode_track_mfm(&io);
  printf("Used flux %zd\n", r);
  dump_flux_compact("fluxfm", &io);
}
