// SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(DEBUG_PRINTF)
#define DEBUG_PRINTF(...) ((void)0)
#endif

#if !defined(DEBUG_ASSERT)
#define DEBUG_ASSERT(x) assert(x)
#endif

/// @cond false

#define MFM_MAYBE_UNUSED __attribute__((unused))

typedef struct mfm_io mfm_io_t;

MFM_MAYBE_UNUSED
static void mfm_io_encode_raw_mfm(mfm_io_t *io, uint8_t b);

MFM_MAYBE_UNUSED
static void mfm_io_encode_raw_fm(mfm_io_t *io, uint8_t b);

typedef struct mfm_io_settings {
  uint16_t gap_1, gap_2;
  uint16_t gap_3[8]; // indexed by 'n', the sector size control
  uint16_t gap_4a;
  uint16_t gap_presync;
  uint8_t gap_byte;
  bool is_fm;
} mfm_io_settings_t;

static const mfm_io_settings_t standard_mfm = {
    50, 22, {32, 54, 84, 116, 255, 255, 255, 255}, 80, 12, 0x4e, false,
};

static const mfm_io_settings_t standard_fm = {
    26, 11, {27, 42, 58, 138, 255, 255, 255, 255}, 40, 6, 0xff, true,
};

struct mfm_io {
  bool encode_compact; ///< When writing flux, use compact form
  uint16_t T2_max;     ///< MFM decoder max length of 2us pulse
  uint16_t T3_max;     ///< MFM decoder max length of 3us pulse
  uint16_t T1_nom;     ///< MFM nominal 1us pulse value

  size_t n_valid; ///< Count of valid sectors decoded

  uint8_t *pulses; ///< Encoded track data
  size_t n_pulses; ///< Total size of encoded track data
  size_t pos;      ///< Position within encoded track data
  size_t time;     ///< Total track time in flux units (set by encoder)

  uint8_t *sectors; ///< Pointer to decoded data
  size_t n_sectors; ///< Number of sectors on track

  uint8_t *sector_validity; ///< Which sectors decoded successfully
  uint8_t
      *cylinder_ptr; ///< When decoding, the cylinder number read is stored here
  uint8_t head, cylinder; ///< Location of the track on disk
  uint8_t pulse_len;      ///< bookkeeping value used by MFM decoder
  uint8_t y;              ///< bookkeeping value used by MFM encoder
  uint8_t n; ///< Sector size value. Sector is (128<<n) bytes big. Valid values
             ///< are 0..7

  uint16_t crc; ///< bookkeeping value used by encoder & decoder
  const mfm_io_settings_t *settings; ///< various settings, used by encoder
  void (*flux_byte)(
      struct mfm_io *,
      uint8_t); ///< can be mfm_io_flux_put or mfm_io_flux_put_compact
  void (*encode_raw)(
      mfm_io_t *io,
      uint8_t b); ///< can be mfm_io_encode_raw_fm or mfm_io_encode_raw_mfm
};

typedef enum {
  mfm_io_pulse_10,
  mfm_io_pulse_100,
  mfm_io_pulse_1000
} mfm_io_symbol_t;

typedef enum { mfm_io_odd = 0, mfm_io_even = 1 } mfm_state_t;

// FM and MFM use the same marker nubmers
enum { MFM_IO_IAM = 0xfc, MFM_IO_IDAM = 0xfe, MFM_IO_DAM = 0xfb };

enum {
  mfm_io_idam_size = 4,
  mfm_io_crc_size = 2,
};

// static const char sync_bytes[] = "\x44\x89\x44\x89\x44\x89";
// a1 a1 a1 but with special timing bits
static const uint8_t mfm_io_sync_bytes_mfm[] = {0x44, 0x89, 0x44,
                                                0x89, 0x44, 0x89};
static const uint8_t mfm_io_iam_sync_bytes_mfm[] = {0x52, 0x24, 0x52,
                                                    0x24, 0x52, 0x24};

static const uint8_t mfm_io_sync_bytes_fm[] = {};
static const uint8_t mfm_io_iam_sync_bytes_fm[] = {0xaa, 0xaa, 0xf7, 0x7a};

enum { fm_default_sync_clk = 0xc7 };

static int mfm_io_eof(mfm_io_t *io) { return io->pos >= io->n_pulses; }

static mfm_io_symbol_t mfm_io_read_symbol(mfm_io_t *io) {
  if (mfm_io_eof(io)) {
    return mfm_io_pulse_10;
  }
  uint8_t pulse_len = io->pulses[io->pos++];
  if (pulse_len > io->T3_max)
    return mfm_io_pulse_1000;
  if (pulse_len > io->T2_max)
    return mfm_io_pulse_100;
  return mfm_io_pulse_10;
}

// Automatically generated CRC function
// polynomial: 0x11021
static const uint16_t mfm_io_crc16_table[256] = {
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

static uint16_t mfm_io_crc16(const uint8_t *data, int len, uint16_t crc) {
  while (len > 0) {
    crc = mfm_io_crc16_table[*data ^ (uint8_t)(crc >> 8)] ^ (crc << 8);
    data++;
    len--;
  }
  return crc;
}

enum {
  mfm_io_triple_mark_magic = 0x09926499,
  mfm_io_triple_mark_mask = 0x0fffffff
};

static bool skip_triple_sync_mark(mfm_io_t *io) {
  uint32_t state = 0;
  while (!mfm_io_eof(io) && state != mfm_io_triple_mark_magic) {
    state = ((state << 2) | mfm_io_read_symbol(io)) & mfm_io_triple_mark_mask;
  }
  DEBUG_PRINTF("mark @ %zd ? %d\n", io->pos, state == mfm_io_triple_mark_magic);
  return state == mfm_io_triple_mark_magic;
}

// The MFM crc initialization value, _excluding the three 0xa1 sync bytes_
enum { mfm_io_crc_preload_value = 0xcdb4 };

// Copy data into a series of buffers, returning the CRC.
// This must be called right after sync_triple_sync_mark, because an assumption
// is made about the code that's about to be read.
//
// The "..." arguments must be pairs of (uint8_t *buf, size_t n), ending with a
// NULL buf.
__attribute__((sentinel)) static uint16_t receive_crc(mfm_io_t *io, ...) {
  // `tmp` holds up to 9 bits of data, in bits 6..15.
  unsigned tmp = 0, weight = 0x8000;
  uint16_t crc = mfm_io_crc_preload_value;

#define PUT_BIT(x)                                                             \
  do {                                                                         \
    if (x)                                                                     \
      tmp |= weight;                                                           \
    weight >>= 1;                                                              \
  } while (0)

  // In MFM, flux marks can be 2, 3, or 4 "T" apart. These three signals
  // stand for the bit sequences 10, 100, and 1000.  However, half of the
  // bits are data bits, and half are 'clock' bits.  We have to keep track of
  // whether [in the next symbol] we want the "mfm_io_even" bit(s) or the
  // "mfm_io_odd" bit(s):
  //
  // 10     - leaves mfm_io_even/mfm_io_odd (parity) unchanged
  // 100    - inverts mfm_io_even/mfm_io_odd (parity)
  // 1000   - leaves mfm_io_even/mfm_io_odd (parity) unchanged
  // ^ ^  data bits if state is mfm_io_even
  //  ^ ^ data bits if state is mfm_io_odd

  // We do this by knowing that when we arrive, we are waiting to parse the
  // final '1' data bit of the MFM sync mark. This means we apply a special rule
  // to the first word, starting as though in the 'mfm_io_even' state but not
  // recording the '1' bit.
  mfm_io_symbol_t s = mfm_io_read_symbol(io);
  mfm_state_t state = mfm_io_even;
  switch (s) {
  case mfm_io_pulse_100: // first data bit is a 0, and we start in the ODD state
    state = mfm_io_odd;
    /* fallthrough */
  case mfm_io_pulse_1000: // first data bit is a 0, and we start in EVEN state
    PUT_BIT(0);
    break;
  default: // this flux doesn't represent a data bit, and we start in the EVEN
           // state
    break;
  }

  va_list ap;
  va_start(ap, io);
  uint8_t *buf;
  while ((buf = va_arg(ap, uint8_t *)) != NULL) {
    size_t n = va_arg(ap, size_t);
    while (n) {
      s = mfm_io_read_symbol(io);
      PUT_BIT(
          state); // 'mfm_io_even' is 1, so record a '1' or '0' as appropriate
      if (s == mfm_io_pulse_1000) {
        PUT_BIT(0); // the other bit recorded for a 1000 is always a '0'
      }
      if (s == mfm_io_pulse_100) {
        if (state) {
          PUT_BIT(0);
        } // If 'mfm_io_even', record an additional '0'
        state = (mfm_state_t)!state; // the next symbol has opposite parity
      }

      if (weight <= 0x80) {
        *buf = tmp >> 8;
        crc = mfm_io_crc16_table[*buf ^ (uint8_t)(crc >> 8)] ^ (crc << 8);
        tmp <<= 8;
        weight <<= 8;
        buf++;
        n--;
      }
    }
  }
  va_end(ap);
  return crc;
}

// Read a whole track, setting validity[] for each sector actually read, up to
// n_sectors indexing of validity & data is 0-based, mfm_io_even though
// MFM_IO_IDAMs store sectors as 1-based
MFM_MAYBE_UNUSED
static size_t decode_track_mfm(mfm_io_t *io) {
  io->pos = 0;

  // count previous valid sectors, so we can early-terminate if we're just
  // picking up some errored sectors on a 2nd pass
  io->n_valid = 0;
  for (size_t i = 0; i < io->n_sectors; i++)
    if (io->sector_validity[i])
      io->n_valid += 1;

  uint8_t mark;
  uint8_t idam_buf[mfm_io_idam_size];
  uint8_t crc_buf[mfm_io_crc_size];

  // IDAM structure is:
  //  * buf[0]: cylinder
  //  * buf[1]: head
  //  * buf[2]: sector
  //  * buf[3]: "n" (sector size shift) -- must be 2 for 512 bytes
  // Only the sector number is validated. In theory, the other values should be
  // validated and we are only interested in working with DOS/Windows MFM
  // floppies which always use 512 byte sectors
  while (!mfm_io_eof(io) && io->n_valid < io->n_sectors) {
    if (!skip_triple_sync_mark(io)) {
      continue;
    }

    uint16_t crc = receive_crc(io, &mark, 1, idam_buf, sizeof(idam_buf),
                               crc_buf, sizeof(crc_buf), NULL);

    DEBUG_PRINTF("mark=%02x [expecting IDAM=%02x]\n", mark, MFM_IO_IDAM);
    DEBUG_PRINTF("idam=%02x %02x %02x %02x\n", idam_buf[0], idam_buf[1],
                 idam_buf[2], idam_buf[3]);
    DEBUG_PRINTF("crc_buf=%02x %02x\n", crc_buf[0], crc_buf[1]);
    DEBUG_PRINTF("crc=%04x [expecting 0]\n", crc);
    if (mark != MFM_IO_IDAM) {
      continue;
    }
    if (crc != 0) {
      continue;
    }

    // TODO: verify track & side numbers in IDAM
    size_t r = (uint8_t)idam_buf[2] - 1; // sectors are 1-based
    if (r >= io->n_sectors) {
      continue;
    }

    if (io->sector_validity[r]) {
      continue;
    }

    if (!skip_triple_sync_mark(io)) {
      continue;
    }
    size_t io_block_size = 128 << io->n;
    crc = receive_crc(io, &mark, 1, io->sectors + io_block_size * r,
                      io_block_size, crc_buf, sizeof(crc_buf), NULL);
    DEBUG_PRINTF("mark=%02x [expecting DAM=%02x]\n", mark, MFM_IO_DAM);
    DEBUG_PRINTF("crc_buf=%02x %02x\n", crc_buf[0], crc_buf[1]);
    DEBUG_PRINTF("crc=%04x [expecting 0]\n", crc);
    if (mark != MFM_IO_DAM) {
      continue;
    }
    if (crc != 0) {
      continue;
    }

    if (io->cylinder_ptr)
      *io->cylinder_ptr = idam_buf[0];
    io->sector_validity[r] = 1;
    io->n_valid++;
  }
  return io->n_valid;
}

static void mfm_io_flux_put(mfm_io_t *io, uint8_t len) {
  if (mfm_io_eof(io))
    return;
  io->pulses[io->pos++] = len;
}

static void mfm_io_flux_byte_compact(mfm_io_t *io, uint8_t b) {
  if (mfm_io_eof(io))
    return;
  io->pulses[io->pos++] = b;
}

static void mfm_io_flux_byte(mfm_io_t *io, uint8_t b) {
  for (int i = 8; i-- > 0;) {
    if (b & (1 << i)) {
      io->time += io->pulse_len + 1;
      mfm_io_flux_put(io, (1 + io->pulse_len) * io->T1_nom);
      io->pulse_len = 0;
    } else {
      io->pulse_len += 1;
    }
  }
}

static void mfm_io_encode_raw_fm(mfm_io_t *io, uint8_t b) {
  if ((b & 0xaa) == 0) {
    b |= 0xaa;
  }
  io->flux_byte(io, b);
}

static void mfm_io_encode_raw_mfm(mfm_io_t *io, uint8_t b) {
  uint16_t y = (io->y << 8) | b;
  if ((b & 0xaa) == 0) {
    // if there are no clocks, synthesize them
    y |= ~((y >> 1) | (y << 1)) & 0xaaaa;
    y &= 0xff;
  }
  io->flux_byte(io, y);
  io->y = y;
}

static const uint16_t mfm_encode_list[] = {
    // taken from greaseweazle
    0x00,   0x01,   0x04,   0x05,   0x10,   0x11,   0x14,   0x15,   0x40,
    0x41,   0x44,   0x45,   0x50,   0x51,   0x54,   0x55,   0x100,  0x101,
    0x104,  0x105,  0x110,  0x111,  0x114,  0x115,  0x140,  0x141,  0x144,
    0x145,  0x150,  0x151,  0x154,  0x155,  0x400,  0x401,  0x404,  0x405,
    0x410,  0x411,  0x414,  0x415,  0x440,  0x441,  0x444,  0x445,  0x450,
    0x451,  0x454,  0x455,  0x500,  0x501,  0x504,  0x505,  0x510,  0x511,
    0x514,  0x515,  0x540,  0x541,  0x544,  0x545,  0x550,  0x551,  0x554,
    0x555,  0x1000, 0x1001, 0x1004, 0x1005, 0x1010, 0x1011, 0x1014, 0x1015,
    0x1040, 0x1041, 0x1044, 0x1045, 0x1050, 0x1051, 0x1054, 0x1055, 0x1100,
    0x1101, 0x1104, 0x1105, 0x1110, 0x1111, 0x1114, 0x1115, 0x1140, 0x1141,
    0x1144, 0x1145, 0x1150, 0x1151, 0x1154, 0x1155, 0x1400, 0x1401, 0x1404,
    0x1405, 0x1410, 0x1411, 0x1414, 0x1415, 0x1440, 0x1441, 0x1444, 0x1445,
    0x1450, 0x1451, 0x1454, 0x1455, 0x1500, 0x1501, 0x1504, 0x1505, 0x1510,
    0x1511, 0x1514, 0x1515, 0x1540, 0x1541, 0x1544, 0x1545, 0x1550, 0x1551,
    0x1554, 0x1555, 0x4000, 0x4001, 0x4004, 0x4005, 0x4010, 0x4011, 0x4014,
    0x4015, 0x4040, 0x4041, 0x4044, 0x4045, 0x4050, 0x4051, 0x4054, 0x4055,
    0x4100, 0x4101, 0x4104, 0x4105, 0x4110, 0x4111, 0x4114, 0x4115, 0x4140,
    0x4141, 0x4144, 0x4145, 0x4150, 0x4151, 0x4154, 0x4155, 0x4400, 0x4401,
    0x4404, 0x4405, 0x4410, 0x4411, 0x4414, 0x4415, 0x4440, 0x4441, 0x4444,
    0x4445, 0x4450, 0x4451, 0x4454, 0x4455, 0x4500, 0x4501, 0x4504, 0x4505,
    0x4510, 0x4511, 0x4514, 0x4515, 0x4540, 0x4541, 0x4544, 0x4545, 0x4550,
    0x4551, 0x4554, 0x4555, 0x5000, 0x5001, 0x5004, 0x5005, 0x5010, 0x5011,
    0x5014, 0x5015, 0x5040, 0x5041, 0x5044, 0x5045, 0x5050, 0x5051, 0x5054,
    0x5055, 0x5100, 0x5101, 0x5104, 0x5105, 0x5110, 0x5111, 0x5114, 0x5115,
    0x5140, 0x5141, 0x5144, 0x5145, 0x5150, 0x5151, 0x5154, 0x5155, 0x5400,
    0x5401, 0x5404, 0x5405, 0x5410, 0x5411, 0x5414, 0x5415, 0x5440, 0x5441,
    0x5444, 0x5445, 0x5450, 0x5451, 0x5454, 0x5455, 0x5500, 0x5501, 0x5504,
    0x5505, 0x5510, 0x5511, 0x5514, 0x5515, 0x5540, 0x5541, 0x5544, 0x5545,
    0x5550, 0x5551, 0x5554, 0x5555};

static void mfm_io_encode_fm_sync(mfm_io_t *io, uint8_t data, uint8_t clock) {
  uint16_t encoded = 0;
  // can this be done with two lookups in encoded[] ?
  for (size_t i = 0; i < 8; i++) {
    encoded <<= 1;
    encoded |= (clock >> (7 - i)) & 1;
    encoded <<= 1;
    encoded |= (data >> (7 - i)) & 1;
  }
  io->encode_raw(io, encoded >> 8);
  io->encode_raw(io, encoded & 0xff);
}

static void mfm_io_encode_fm_sync_crc(mfm_io_t *io, uint8_t data, uint8_t clock) {
    mfm_io_encode_fm_sync(io, data, clock);
  io->crc = mfm_io_crc16(&data, 1, io->crc);
}

static void mfm_io_encode_byte(mfm_io_t *io, uint8_t b) {
  uint16_t encoded = mfm_encode_list[b];
  io->encode_raw(io, encoded >> 8);
  io->encode_raw(io, encoded & 0xff);
}

static void mfm_io_encode_raw_buf(mfm_io_t *io, const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    io->encode_raw(io, buf[i]);
  }
}

static void mfm_io_encode_gap(mfm_io_t *io, size_t n_gap) {
  for (size_t i = 0; i < n_gap; i++) {
    mfm_io_encode_byte(io, io->settings->gap_byte);
  }
}

static void mfm_io_encode_gap_and_presync(mfm_io_t *io, size_t n_gap) {
  mfm_io_encode_gap(io, n_gap);
  for (size_t i = 0; i < io->settings->gap_presync; i++) {
    mfm_io_encode_byte(io, 0);
  }
}

static void mfm_io_encode_gap_and_sync(mfm_io_t *io, size_t n_gap) {
  mfm_io_encode_gap_and_presync(io, n_gap);
  if (io->settings->is_fm) {
    mfm_io_encode_raw_buf(io, mfm_io_sync_bytes_fm,
                          sizeof(mfm_io_sync_bytes_fm));
  } else {
    mfm_io_encode_raw_buf(io, mfm_io_sync_bytes_mfm,
                          sizeof(mfm_io_sync_bytes_mfm));
  }
}

static void mfm_io_encode_iam(mfm_io_t *io) {
  mfm_io_encode_gap_and_presync(io, io->settings->gap_4a);
  if (io->settings->is_fm) {
    mfm_io_encode_raw_buf(io, mfm_io_iam_sync_bytes_fm,
                          sizeof(mfm_io_iam_sync_bytes_fm));
  } else {
    mfm_io_encode_raw_buf(io, mfm_io_iam_sync_bytes_mfm,
                          sizeof(mfm_io_iam_sync_bytes_mfm));
  }
  mfm_io_encode_byte(io, MFM_IO_IAM);
}

static void mfm_io_encode_buf(mfm_io_t *io, const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    mfm_io_encode_byte(io, buf[i]);
  }
}

static void mfm_io_crc_preload(mfm_io_t *io) {
  if (io->settings->is_fm) {
      io->crc = 0xffff;
  } else {
      io->crc = mfm_io_crc_preload_value;
  }
}

static void mfm_io_encode_buf_crc(mfm_io_t *io, const uint8_t *buf, size_t n) {
  mfm_io_encode_buf(io, buf, n);
  io->crc = mfm_io_crc16(buf, n, io->crc);
}

static void mfm_io_encode_byte_crc(mfm_io_t *io, uint8_t b) {
  mfm_io_encode_buf_crc(io, &b, 1);
}

static void mfm_io_encode_crc(mfm_io_t *io) {
  unsigned crc = io->crc;
  mfm_io_encode_byte_crc(io, crc >> 8);
  mfm_io_encode_byte_crc(io, crc & 0xff);
  DEBUG_ASSERT(io->crc == 0);
}

// Convert a whole track into flux, up to n_sectors. indexing of data is
// 0-based, mfm_io_even though MFM_IO_IDAMs store sectors as 1-based
MFM_MAYBE_UNUSED
static size_t encode_track_mfm(mfm_io_t *io) {
  io->pos = 0;
  io->pulse_len = 0;
  io->y = 0;
  io->time = 0;
  io->flux_byte =
      io->encode_compact ? mfm_io_flux_byte_compact : mfm_io_flux_byte;
  io->encode_raw =
      io->settings->is_fm ? mfm_io_encode_raw_fm : mfm_io_encode_raw_mfm;

  // sector_validity might end up reused for interleave?
  // memset(io->sector_validity, 0, io->n_sectors);

  unsigned char buf[mfm_io_idam_size + 1];

  mfm_io_encode_iam(io);

  mfm_io_encode_gap_and_sync(io, io->settings->gap_1);
  for (size_t i = 0; i < io->n_sectors; i++) {
    buf[0] = MFM_IO_IDAM;
    buf[1] = io->cylinder;
    buf[2] = io->head;
    buf[3] = i + 1; // sectors are 1-based
    buf[4] = io->n;

    mfm_io_crc_preload(io);
    printf("crc=%04x\n", io->crc);
    if (io->settings->is_fm) {
        mfm_io_encode_fm_sync_crc(io, buf[0], fm_default_sync_clk);
        printf("crc=%04x\n", io->crc);
        mfm_io_encode_buf_crc(io, buf + 1, sizeof(buf) - 1);
        printf("crc=%04x\n", io->crc);
    } else {
      mfm_io_encode_buf_crc(io, buf, sizeof(buf));
    }
    mfm_io_encode_crc(io);
    printf("crc=%04x\n\n", io->crc);

    mfm_io_encode_gap_and_sync(io, io->settings->gap_2);
    mfm_io_crc_preload(io);
    if(io->settings->is_fm) {
      mfm_io_encode_fm_sync_crc(io, MFM_IO_DAM, fm_default_sync_clk);
    } else {
      mfm_io_encode_byte_crc(io, MFM_IO_DAM);
    }
    size_t io_block_size = 128 << io->n;
    mfm_io_encode_buf_crc(io, &io->sectors[io_block_size * i], io_block_size);
    mfm_io_encode_crc(io);

    mfm_io_encode_gap_and_sync(io, io->settings->gap_3[io->n]);
  }
  size_t result = io->pos;
  DEBUG_ASSERT(!mfm_io_eof(io));

  while (!mfm_io_eof(io)) {
    mfm_io_encode_byte(io, io->settings->gap_byte);
  }
  return result;
}

// Encoding sectors in MFM:
//  * Each sector is preceded by "gap" bytes with value "gapbyte"
//  * Then "gap_presync" '\0' bytes
//  * Then "sync_bytes" pattern
//  * Then the MFM_IO_IDAM & header data & CRC
//  * Then "gap_presync" '\0' bytes
//  * Then "sync_bytes" pattern
//  * Then the MFM_IO_DAM & sector data & CRC
// The track is filled out to the set length with "gapbyte"s
// ref:
// https://github.com/keirf/greaseweazle/blob/2484a089d6a50bdbc9fb9a2117ca3968ab3aa2a8/scripts/greaseweazle/codec/ibm/mfm.py
// https://retrocmp.de/hardware/kryoflux/track-mfm-format.htm
/// @endcond
