#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#include "mfm_impl.h"

uint8_t flux[] = {
#include "test_flux.h"
};

enum { sector_count = 18 };

uint8_t track_buf[sector_count * mfm_io_block_size];
uint8_t validity[sector_count];

mfm_io_t io = {
    .T1_nom = 2,
    .T2_max = 5,
    .T3_max = 7,
    .pulses = flux,
    .n_pulses = sizeof(flux),
    .sectors = track_buf,
    .sector_validity = validity,
    .n_sectors = sizeof(track_buf) / mfm_io_block_size,
};

static void flux_bins(mfm_io_t *io) {
  io->pos = 0;
  int bins[3] = {};
  while (!mfm_io_eof(io)) {
    bins[mfm_io_read_symbol(io)]++;
  }
  printf("Flux bins: %d %d %d\n", bins[0], bins[1], bins[2]);
}

static void dump_flux_compact(const char *filename, mfm_io_t *io) {
  FILE *f = fopen(filename, "w");
  io->pos = 0;
  while (!mfm_io_eof(io)) {
    int b = io->pulses[io->pos++];
    for (int i = 8; i-- > 0;) {
        fputc('0' + ((b >> i) & 1), f); };
    fputc(io->pos % 8 == 0 ? '\n' : ' ', f);
  }
  fclose(f);
}
static void dump_flux(const char *filename, mfm_io_t *io) {
  FILE *f = fopen(filename, "w");
  io->pos = 0;
  uint32_t state = 0;
  while (!mfm_io_eof(io)) {
    int s = mfm_io_read_symbol(io);
    state = ((state << 2) | s) & mfm_io_triple_mark_mask;
    fprintf(f, "10");
    if (s > mfm_io_pulse_10) {
      fprintf(f, "0");
    }
    if (s > mfm_io_pulse_100) {
      fprintf(f, "0");
    }
    if (state == mfm_io_triple_mark_magic) {
      DEBUG_PRINTF("triple mark @%zd\n", io->pos);
      fprintf(f, "\n");
    }
  }
  fclose(f);
}

int main() {
  flux_bins(&io);
  printf("Decoded %zd sectors\n", decode_track_mfm(&io));

  dump_flux("flux0", &io);

  memset(flux, 0, sizeof(flux));

#if 0
  for (size_t i = 0; i < sizeof(track_buf); i++)
    track_buf[i] = i & 0xff;
#endif

  printf("Create new flux data\n");
  encode_track_mfm(&io);
  dump_flux("flux1", &io);

  memset(track_buf, 0, sizeof(track_buf));

  io.n_valid = 0;
  memset(validity, 0, sizeof(validity));
  flux_bins(&io);
  size_t decoded = decode_track_mfm(&io);
  printf("Decoded %zd sectors\n", decoded);

  io.encode_compact = true;
  encode_track_mfm(&io);
  dump_flux_compact("flux2", &io);
  
  return decoded != 18;
}
