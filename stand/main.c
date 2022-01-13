// SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <stdio.h>

struct mfm_io {};

#include "mfm_impl.h"

void hexdump(void *buf_in, size_t n) {
  unsigned char *buf = buf_in;
  size_t i = 0;
  for (; i < n; i++) {
    if (i % 16 == 0) {
      printf("%04zx ", i);
    }
    printf("%02x%c", buf[i], (i % 16 == 15) ? '\n' : ' ');
  }
  if (i % 16 != 0) {
    putchar('\n');
  }
}

static inline mfm_io_symbol_t mfm_io_read_symbol(mfm_io_t *io) {
  int c = getchar();
  return (mfm_io_symbol_t)(c - '0');
}

static void mfm_io_reset_sync_count(mfm_io_t *io) {}

static inline int mfm_io_get_sync_count(mfm_io_t *io) {
  return feof(stdin) ? 2 : 0;
}

int main() {
  enum { n_sectors = 18 };
  char data[n_sectors * blocksize];
  uint8_t validity[n_sectors];
  struct mfm_io io;
  read_track(io, n_sectors, data, validity);
  for (int i = 0; i < n_sectors; i++) {
    printf("validity[% 2d] = %d\n", i, validity[i]);
    if (validity[i]) {
      hexdump(data + i * blocksize, blocksize);
      printf("\n");
    }
  }
}
