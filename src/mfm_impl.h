// SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma GCC push_options
#pragma GCC optimize("-O3")
typedef struct mfm_io mfm_io_t;

#ifndef MFM_IO_MMIO
#define MFM_IO_MMIO (0)
#endif

// If you have a memory mapped peripheral, define MFM_IO_MMIO to get an
// implementation of the mfm_io functions. then, just populate the fields with
// the actual registers to use and define T2_5 and T3_5 to the empirical values
// dividing between T2/3 and T3/4 pulses.
#if MFM_IO_MMIO
struct mfm_io {
  const volatile uint32_t *index_port;
  uint32_t index_mask;
  const volatile uint32_t *data_port;
  uint32_t data_mask;
  unsigned index_state;
  unsigned index_count;
};
#endif

typedef enum { pulse_10, pulse_100, pulse_1000 } mfm_io_symbol_t;

typedef enum { odd = 0, even = 1 } mfm_state_t;

enum { IDAM = 0xfe, DAM = 0xfb };

enum { blocksize = 512, overhead = 3, metadata_size = 7 };
__attribute__((always_inline)) static inline mfm_io_symbol_t
mfm_io_read_symbol(mfm_io_t *io);
static void mfm_io_reset_sync_count(mfm_io_t *io);
__attribute__((always_inline)) static int mfm_io_get_sync_count(mfm_io_t *io);

// Automatically generated CRC function
// polynomial: 0x11021
static uint16_t crc16(uint8_t *data, int len, uint16_t crc) {
  static const uint16_t table[256] = {
      0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
      0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
      0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
      0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
      0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
      0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
      0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
      0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
      0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
      0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
      0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
      0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
      0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
      0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
      0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
      0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
      0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
      0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
      0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
      0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
      0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
      0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
      0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
      0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
      0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
      0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
      0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU,
      0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
      0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
      0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
      0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
      0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
  };

  while (len > 0) {
    crc = table[*data ^ (uint8_t)(crc >> 8)] ^ (crc << 8);
    data++;
    len--;
  }
  return crc;
}

enum { triple_mark_magic = 0x09926499, triple_mark_mask = 0x0fffffff };

__attribute__((always_inline)) inline static bool
wait_triple_sync_mark(mfm_io_t *io) {
  uint32_t state = 0;
  while (mfm_io_get_sync_count(io) < 3 && state != triple_mark_magic) {
    state = ((state << 2) | mfm_io_read_symbol(io)) & triple_mark_mask;
  }
  return state == triple_mark_magic;
}

// Compute the MFM CRC of the data, _assuming it was preceded by three 0xa1 sync
// bytes
static int crc16_preloaded(unsigned char *buf, size_t n) {
  return crc16((uint8_t *)buf, n, 0xcdb4);
}

// Copy 'n' bytes of data into 'buf'
__attribute__((always_inline)) inline static void
receive(mfm_io_t *io, unsigned char *buf, size_t n) {
  // `tmp` holds up to 9 bits of data, in bits 6..15.
  unsigned tmp = 0, weight = 0x8000;

#define PUT_BIT(x)                                                             \
  do {                                                                         \
    if (x)                                                                     \
      tmp |= weight;                                                           \
    weight >>= 1;                                                              \
  } while (0)

  // In MFM, flux marks can be 2, 3, or 4 "T" apart. These three signals
  // stand for the bit sequences 10, 100, and 1000.  However, half of the
  // bits are data bits, and half are 'clock' bits.  We have to keep track of
  // whether [in the next symbol] we want the "even" bit(s) or the "odd" bit(s):
  //
  // 10     - leaves even/odd (parity) unchanged
  // 100    - inverts even/odd (parity)
  // 1000   - leaves even/odd (parity) unchanged
  // ^ ^  data bits if state is even
  //  ^ ^ data bits if state is odd

  // We do this by knowing that when we arrive, we are waiting to parse the
  // final '1' data bit of the MFM sync mark. This means we apply a special rule
  // to the first word, starting as though in the 'even' state but not recording
  // the '1' bit.
  mfm_io_symbol_t s = mfm_io_read_symbol(io);
  mfm_state_t state = even;
  switch (s) {
  case pulse_100: // first data bit is a 0, and we start in the ODD state
    state = odd;
    /* fallthrough */
  case pulse_1000: // first data bit is a 0, and we start in EVEN state
    PUT_BIT(0);
    break;
  default:
    break;
  }

  while (n) {
    s = mfm_io_read_symbol(io);
    PUT_BIT(state); // 'even' is 1, so record a '1' or '0' as appropriate
    if (s == pulse_1000) {
      PUT_BIT(0); // the other bit recorded for a 1000 is always a '0'
    }
    if (s == pulse_100) {
      if (state) {
        PUT_BIT(0);
      }                            // If 'even', record an additional '0'
      state = (mfm_state_t)!state; // the next symbol has opposite parity
    }

    *buf = tmp >> 8; // store every time to make timing more even
    if (weight <= 0x80) {
      tmp <<= 8;
      weight <<= 8;
      buf++;
      n--;
    }
  }
}

// Perform all the steps of receiving the next IDAM, DAM (or DDAM, but we don't
// use them)
__attribute__((always_inline)) inline static bool
wait_triple_sync_mark_receive_crc(mfm_io_t *io, void *buf, size_t n) {
  if (!wait_triple_sync_mark(io)) {
    return false;
  }
  receive(io, (uint8_t *)buf, n);
  unsigned crc = crc16_preloaded((uint8_t *)buf, n);
  return crc == 0;
}

// Read a whole track, setting validity[] for each sector actually read, up to
// n_sectors indexing of validity & data is 0-based, even though IDAMs store
// sectors as 1-based
static int read_track(mfm_io_t io, int n_sectors, void *data,
                      uint8_t *validity) {
  memset(validity, 0, n_sectors);

  int n_valid = 0;

  mfm_io_reset_sync_count(&io);

  unsigned char buf[512 + 3];
  while (mfm_io_get_sync_count(&io) < 3 && n_valid < n_sectors) {
    if (!wait_triple_sync_mark_receive_crc(&io, buf, metadata_size)) {
      continue;
    }
    if (buf[0] != IDAM) {
      continue;
    }

    int r = (uint8_t)buf[3] - 1;
    if (r >= n_sectors) {
      continue;
    }

    if (validity[r]) {
      continue;
    }

    if (!wait_triple_sync_mark_receive_crc(&io, buf, sizeof(buf))) {
      continue;
    }
    if (buf[0] != DAM) {
      continue;
    }

    memcpy((char *)data + blocksize * r, buf + 1, blocksize);
    validity[r] = 1;
    n_valid++;
  }
  return n_valid;
}

#if MFM_IO_MMIO
#define READ_DATA() (!!(*io->data_port & io->data_mask))
#define READ_INDEX() (!!(*io->index_port & io->index_mask))
__attribute__((optimize("O3"), always_inline)) static inline mfm_io_symbol_t
mfm_io_read_symbol(mfm_io_t *io) {
  unsigned pulse_count = 3;
  while (!READ_DATA()) {
    pulse_count++;
  }

  unsigned index_state = (io->index_state << 1) | READ_INDEX();
  if ((index_state & 3) == 2) { // a zero-to-one transition
    io->index_count++;
  }
  io->index_state = index_state;

  while (READ_DATA()) {
    pulse_count++;
  }

  int result = pulse_10;
  if (pulse_count > T2_5) {
    result++;
  }
  if (pulse_count > T3_5) {
    result++;
  }

  return (mfm_io_symbol_t)result;
}

static void mfm_io_reset_sync_count(mfm_io_t *io) { io->index_count = 0; }

__attribute__((optimize("O3"), always_inline)) inline static int
mfm_io_get_sync_count(mfm_io_t *io) {
  return io->index_count;
}
#endif

#pragma GCC pop_options
