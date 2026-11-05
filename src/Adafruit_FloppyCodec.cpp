// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyCodec.h"
#include "greasepack.h"
#include <string.h>

// Layout references: IBM address marks/CRC-CCITT, ADFlib's Amiga format FAQ,
// Commodore's 1541 format, and Beneath Apple DOS/CiderPress II format notes.
// Greaseweazle (Keir Fraser, public domain) is the independent fixture oracle.
namespace {
using Codec = Adafruit_FloppyCodec;

// Every field read is checked against the capture's exact bit length. A partial
// final byte is allowed; fields straddling the capture boundary are discarded.
class Bits {
public:
  const uint8_t *data;
  size_t count;
  uint8_t bit(size_t offset) const {
    return (data[offset / 8] >> (7 - offset % 8)) & 1;
  }
  bool word(size_t offset, unsigned length, uint32_t &value) const {
    if (length > 32 || offset > count || length > count - offset) {
      return false;
    }
    value = 0;
    for (unsigned i = 0; i < length; i++) {
      value = (value << 1) | bit(offset + i);
    }
    return true;
  }
  bool byte(size_t offset, uint8_t &value) const {
    uint32_t wordValue;
    if (!word(offset, 8, wordValue)) {
      return false;
    }
    value = wordValue;
    return true;
  }
  bool mfmByte(size_t offset, uint8_t &value) const {
    if (offset > count || 16 > count - offset) {
      return false;
    }
    value = 0;
    for (unsigned i = 0; i < 8; i++) {
      value = (value << 1) | bit(offset + i * 2 + 1);
    }
    return true;
  }
};

uint16_t crcByte(uint16_t crc, uint8_t value) {
  crc ^= (uint16_t)value << 8;
  for (uint8_t i = 0; i < 8; i++) {
    if (crc & 0x8000) {
      crc = (crc << 1) ^ 0x1021;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

bool deliver(Codec::Result &result, Codec::Visit visitor, void *context,
             const Codec::Sector &sector) {
  result.sectors++;
  return !visitor || visitor(context, sector);
}

Codec::Result decodeIBM(const Bits &bits, bool fm, Codec::Visit visitor,
                        void *context) {
  Codec::Result result;
  Codec::Sector sector = {};
  uint8_t payload[1024];
  uint64_t shift = 0;
  bool haveHeader = false;
  size_t headerEnd = 0;
  // MFM's CRC includes three A1 sync bytes. FM starts at the address mark.
  uint16_t initialCRC = 0xffff;
  if (!fm) {
    for (uint8_t i = 0; i < 3; i++) {
      initialCRC = crcByte(initialCRC, 0xa1);
    }
  }
  for (size_t offset = 0; offset < bits.count; offset++) {
    shift = (shift << 1) | bits.bit(offset);
    uint8_t mark;
    size_t field;
    if (fm) {
      // C7 clock patterns, interleaved with FE / FB / F8 data bits.
      uint16_t sync = shift & 0xffff;
      if (offset < 15 || (sync != 0xf57e && sync != 0xf56f && sync != 0xf56a)) {
        continue;
      }
      mark = sync == 0xf57e ? 0xfe : sync == 0xf56f ? 0xfb : 0xf8;
      field = offset + 1;
    } else {
      if (offset < 47 || (shift & 0xffffffffffffULL) != 0x448944894489ULL ||
          !bits.mfmByte(offset + 1, mark)) {
        continue;
      }
      field = offset + 17;
    }
    if (mark == 0xfe) {
      haveHeader = false;
      uint8_t header[6];
      uint16_t crc = crcByte(initialCRC, mark);
      bool complete = true;
      for (uint8_t i = 0; i < sizeof(header); i++) {
        if (!bits.mfmByte(field + i * 16, header[i])) {
          complete = false;
          break;
        }
        crc = crcByte(crc, header[i]);
      }
      if (!complete || crc || header[1] > 1 || header[3] > 3) {
        result.badHeaders++;
        continue;
      }
      sector.cylinder = header[0];
      sector.head = header[1];
      sector.number = header[2];
      sector.size = 128u << header[3];
      headerEnd = field + sizeof(header) * 16;
      haveHeader = true;
    } else if ((mark == 0xfb || mark == 0xf8) && haveHeader) {
      haveHeader = false;
      // Do not attach a distant data field to a stale header after damage.
      if (field < headerEnd || field - headerEnd > 1600) {
        continue;
      }
      uint16_t crc = crcByte(initialCRC, mark);
      bool complete = true;
      for (uint16_t i = 0; i < sector.size + 2; i++) {
        uint8_t value;
        if (!bits.mfmByte(field + (size_t)i * 16, value)) {
          complete = false;
          break;
        }
        crc = crcByte(crc, value);
        if (i < sector.size) {
          payload[i] = value;
        }
      }
      if (!complete || crc) {
        result.badData++;
        continue;
      }
      sector.deleted = mark == 0xf8;
      sector.data = payload;
      if (!deliver(result, visitor, context, sector)) {
        break;
      }
      offset = field + (sector.size + 2) * 16 - 1;
      shift = 0;
    }
  }
  return result;
}

// Amiga stores all odd bits of a field followed by all even bits. This is not
// IBM byte-interleaved MFM, even though the on-disk bit cells have the same
// rate.
bool amigaField(const Bits &bits, size_t offset, uint8_t *output, size_t size) {
  if (offset > bits.count || size > (bits.count - offset) / 16) {
    return false;
  }
  for (size_t i = 0; i < size; i++) {
    uint8_t odd, even;
    if (!bits.byte(offset + i * 8, odd) ||
        !bits.byte(offset + (size + i) * 8, even))
      return false;
    output[i] = ((odd << 1) & 0xaa) | (even & 0x55);
  }
  return true;
}

uint32_t bigEndian(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

uint32_t amigaChecksum(const uint8_t *data, size_t size) {
  uint32_t sum = 0;
  for (size_t i = 0; i < size; i += 4) {
    sum ^= bigEndian(data + i);
  }
  return (sum ^ (sum >> 1)) & 0x55555555;
}

Codec::Result decodeAmiga(const Bits &bits, Codec::Visit visitor,
                          void *context) {
  Codec::Result result;
  uint8_t payload[512], header[20], headerCRC[4], dataCRC[4];
  uint32_t shift = 0;
  for (size_t offset = 0; offset < bits.count; offset++) {
    shift = (shift << 1) | bits.bit(offset);
    if (offset < 31 || shift != 0x44894489) {
      continue;
    }
    size_t field = offset + 1;
    if (!amigaField(bits, field, header, 4) || header[0] != 0xff) {
      continue;
    }
    if (!amigaField(bits, field + 64, header + 4, 16) ||
        !amigaField(bits, field + 320, headerCRC, 4) || header[2] >= 22 ||
        !header[3] || header[3] > 22 ||
        bigEndian(headerCRC) != amigaChecksum(header, sizeof(header))) {
      result.badHeaders++;
      continue;
    }
    if (!amigaField(bits, field + 384, dataCRC, 4) ||
        !amigaField(bits, field + 448, payload, sizeof(payload)) ||
        bigEndian(dataCRC) != amigaChecksum(payload, sizeof(payload))) {
      result.badData++;
      continue;
    }
    Codec::Sector sector = {};
    sector.cylinder = header[1] / 2;
    sector.head = header[1] % 2;
    sector.number = header[2];
    sector.size = sizeof(payload);
    sector.data = payload;
    if (!deliver(result, visitor, context, sector)) {
      break;
    }
    offset = field + 448 + sizeof(payload) * 16 - 1;
    shift = 0;
  }
  return result;
}

int gcrNibble(uint8_t encoded) {
  // Commodore 1541's published four-data-bit to five-recorded-bit code.
  static const uint8_t code[16] = {0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f,
                                   0x16, 0x17, 0x09, 0x19, 0x1a, 0x1b,
                                   0x0d, 0x1d, 0x1e, 0x15};
  for (uint8_t i = 0; i < 16; i++) {
    if (code[i] == encoded) {
      return i;
    }
  }
  return -1;
}

bool cbmBytes(const Bits &bits, size_t offset, uint8_t *output, size_t size) {
  if (offset > bits.count || size > (bits.count - offset) / 10) {
    return false;
  }
  for (size_t i = 0; i < size; i++) {
    uint32_t value;
    if (!bits.word(offset + i * 10, 10, value))
      return false;
    int high = gcrNibble(value >> 5), low = gcrNibble(value & 31);
    if (high < 0 || low < 0) {
      return false;
    }
    output[i] = (high << 4) | low;
  }
  return true;
}

Codec::Result decodeCBM(const Bits &bits, Codec::Visit visitor, void *context) {
  Codec::Result result;
  Codec::Sector sector = {};
  uint8_t payload[260];
  unsigned ones = 0;
  bool haveHeader = false;
  size_t headerEnd = 0;
  for (size_t offset = 0; offset < bits.count; offset++) {
    if (bits.bit(offset)) {
      if (ones < 10) {
        ones++;
      }
      continue;
    }
    bool sync = ones >= 10;
    ones = 0;
    uint8_t mark;
    if (!sync || !cbmBytes(bits, offset, &mark, 1)) {
      continue;
    }
    if (mark == 8) {
      haveHeader = false;
      uint8_t header[8];
      if (!cbmBytes(bits, offset, header, sizeof(header))) {
        result.badHeaders++;
        continue;
      }
      uint8_t sum = 0;
      for (uint8_t i = 1; i <= 5; i++) {
        sum ^= header[i];
      }
      if (sum || !header[3] || header[3] > 42 || header[2] >= 21) {
        result.badHeaders++;
        continue;
      }
      sector.cylinder = header[3] - 1;
      sector.number = header[2];
      sector.volume = ((uint16_t)header[4] << 8) | header[5];
      headerEnd = offset + sizeof(header) * 10;
      haveHeader = true;
    } else if (mark == 7 && haveHeader) {
      haveHeader = false;
      if (offset < headerEnd || offset - headerEnd > 800) {
        continue;
      }
      if (!cbmBytes(bits, offset, payload, sizeof(payload))) {
        result.badData++;
        continue;
      }
      uint8_t sum = 0;
      for (uint16_t i = 1; i <= 257; i++) {
        sum ^= payload[i];
      }
      if (sum) {
        result.badData++;
        continue;
      }
      sector.size = 256;
      sector.data = payload + 1;
      if (!deliver(result, visitor, context, sector)) {
        break;
      }
      offset += sizeof(payload) * 10 - 1;
    }
  }
  return result;
}

uint8_t appleCode(uint8_t value) {
  // Apple II DOS 3.3 six-and-two GCR translation table.
  static const uint8_t code[64] = {
      0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac,
      0xad, 0xae, 0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba,
      0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3, 0xd6,
      0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7,
      0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5,
      0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
  return code[value & 63];
}

int appleValue(uint8_t encoded) {
  for (uint8_t i = 0; i < 64; i++) {
    if (appleCode(i) == encoded) {
      return i;
    }
  }
  return -1;
}

// Disk II makes a byte available when a 1 reaches the high bit. Permit padding
// zero bits between bytes, but bound the whole field to avoid scanning forever.
bool appleByte(const Bits &bits, size_t &offset, size_t end, uint8_t &value) {
  value = 0;
  for (uint8_t n = 0; n < 16 && offset < end; n++) {
    value = (value << 1) | bits.bit(offset++);
    if (value & 0x80) {
      return true;
    }
  }
  return false;
}

Codec::Result decodeApple(const Bits &bits, Codec::Visit visitor,
                          void *context) {
  Codec::Result result;
  Codec::Sector sector = {};
  uint8_t payload[256];
  uint32_t shift = 0;
  bool haveHeader = false;
  size_t headerEnd = 0;
  for (size_t offset = 0; offset < bits.count; offset++) {
    shift = ((shift << 1) | bits.bit(offset)) & 0xffffff;
    if (offset < 23) {
      continue;
    }
    if (shift == 0xd5aa96) {
      haveHeader = false;
      uint8_t header[4];
      bool complete = true;
      for (uint8_t i = 0; i < 4; i++) {
        uint8_t odd, even;
        if (!bits.byte(offset + 1 + i * 16, odd) ||
            !bits.byte(offset + 9 + i * 16, even) || (odd & 0xaa) != 0xaa ||
            (even & 0xaa) != 0xaa) {
          complete = false;
          break;
        }
        header[i] = ((odd << 1) | 1) & even;
      }
      if (!complete || (header[0] ^ header[1] ^ header[2]) != header[3] ||
          header[1] >= 40 || header[2] >= 16) {
        result.badHeaders++;
        continue;
      }
      sector.cylinder = header[1];
      sector.number = header[2];
      sector.volume = header[0];
      headerEnd = offset + 65;
      haveHeader = true;
    } else if (shift == 0xd5aaad && haveHeader) {
      haveHeader = false;
      if (offset < headerEnd || offset - headerEnd > 800) {
        continue;
      }
      size_t position = offset + 1;
      size_t end = bits.count;
      if (bits.count - position > 400 * 8) {
        end = position + 400 * 8;
      }
      uint8_t checksum = 0;
      bool complete = true;
      memset(payload, 0, sizeof(payload));
      for (uint16_t i = 0; i < 343; i++) {
        uint8_t encoded;
        if (!appleByte(bits, position, end, encoded)) {
          complete = false;
          break;
        }
        int value = appleValue(encoded);
        if (value < 0) {
          complete = false;
          break;
        }
        if (i == 342) {
          complete = checksum == value;
          break;
        }
        checksum ^= value;
        if (i < 86) {
          // The first 86 values each carry the low two bits of up to three
          // payload bytes. Within each pair, the bit order is reversed.
          for (uint8_t group = 0; group < 3; group++) {
            size_t byte = i + group * 86;
            if (byte < sizeof(payload)) {
              uint8_t pair = (checksum >> (group * 2)) & 3;
              payload[byte] = ((pair & 1) << 1) | (pair >> 1);
            }
          }
        } else {
          payload[i - 86] |= checksum << 2;
        }
      }
      if (!complete) {
        result.badData++;
        continue;
      }
      sector.size = sizeof(payload);
      sector.data = payload;
      if (!deliver(result, visitor, context, sector)) {
        break;
      }
      offset = position - 1;
      shift = 0;
    }
  }
  return result;
}
// Macintosh checksum/6-and-2 transform, adapted from Greaseweazle mac.c.
// That implementation incorporates FluxEngine (David Given), inspired by
// MAME/MESS ap_dsk35.cpp (Nathan Woods and R. Belmont). Unlike Apple II,
// each Mac sector has 12 tag bytes followed by 512 data bytes.
bool macPayload(uint8_t *payload, uint8_t *encoded, bool encode) {
  uint32_t c1 = 0, c2 = 0, c3 = 0;
  size_t position = 0, out = 0;
  while (position < 524) {
    uint8_t b[3] = {};
    unsigned bytes = position == 522 ? 2 : 3;
    if (!encode) {
      uint8_t high = encoded[out++];
      for (unsigned i = 0; i < bytes; i++) {
        b[i] = encoded[out++] | ((high << (2 + i * 2)) & 0xc0);
      }
    }

    // Rotate the first checksum byte, then propagate carries between the
    // three accumulators before mixing each successive payload byte.
    c1 = (c1 & 255) << 1;
    if (c1 & 256)
      c1++;
    uint8_t value = encode ? payload[position] : b[0] ^ c1;
    c3 += value;
    if (c1 & 256) {
      c3++;
      c1 &= 255;
    }
    b[0] = value ^ c1;
    payload[position++] = value;
    value = encode ? payload[position] : b[1] ^ c3;
    c2 += value;
    if (c3 > 255) {
      c2++;
      c3 &= 255;
    }
    b[1] = value ^ c3;
    payload[position++] = value;
    if (bytes == 3) {
      value = encode ? payload[position] : b[2] ^ c2;
      c1 += value;
      if (c2 > 255) {
        c1++;
        c2 &= 255;
      }
      b[2] = value ^ c2;
      payload[position++] = value;
    }
    if (encode) {
      encoded[out++] = (b[0] >> 6) * 16 + (b[1] >> 6) * 4 + (b[2] >> 6);
      for (unsigned i = 0; i < bytes; i++)
        encoded[out++] = b[i] & 63;
    }
  }
  uint8_t checksum[] = {
      (uint8_t)(((c1 & 0xc0) >> 6) | ((c2 & 0xc0) >> 4) | ((c3 & 0xc0) >> 2)),
      (uint8_t)(c3 & 63), (uint8_t)(c2 & 63), (uint8_t)(c1 & 63)};
  if (encode) {
    memcpy(encoded + out, checksum, sizeof(checksum));
    return true;
  }
  return memcmp(encoded + out, checksum, sizeof(checksum)) == 0;
}

bool macBytes(const Bits &bits, size_t offset, uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; i++) {
    uint8_t byte;
    if (!bits.byte(offset + i * 8, byte))
      return false;
    int value = appleValue(byte);
    if (value < 0)
      return false;
    data[i] = value;
  }
  return true;
}

Codec::Result decodeMac(const Bits &bits, Codec::Visit visitor, void *context) {
  Codec::Result result;
  Codec::Sector sector = {};
  uint8_t payload[524], encoded[703];
  uint32_t shift = 0, epilogue;
  bool haveHeader = false;
  size_t headerEnd = 0;
  for (size_t offset = 0; offset < bits.count; offset++) {
    shift = ((shift << 1) | bits.bit(offset)) & 0xffffff;
    if (offset < 23)
      continue;
    if (shift == 0xd5aa96) {
      haveHeader = false;
      uint8_t header[5];
      if (!macBytes(bits, offset + 1, header, sizeof(header)) ||
          (header[0] ^ header[1] ^ header[2] ^ header[3]) != header[4] ||
          (header[2] & ~0x21) || (header[3] != 0x02 && header[3] != 0x22) ||
          !bits.word(offset + 41, 16, epilogue) || epilogue != 0xdeaa) {
        result.badHeaders++;
        continue;
      }
      sector.cylinder = header[0] | ((header[2] & 1) << 6);
      sector.head = header[2] >> 5;
      sector.number = header[1];
      sector.volume = header[3];
      if (sector.cylinder >= 80 || sector.number >= 12 - sector.cylinder / 16 ||
          (sector.head && sector.volume == 0x02)) {
        result.badHeaders++;
        continue;
      }
      headerEnd = offset + 41;
      haveHeader = true;
    } else if (shift == 0xd5aaad && haveHeader) {
      haveHeader = false;
      if (offset - headerEnd > 800)
        continue;
      uint8_t number;
      if (!macBytes(bits, offset + 1, &number, 1) || number != sector.number ||
          !macBytes(bits, offset + 9, encoded, sizeof(encoded)) ||
          !macPayload(payload, encoded, false) ||
          !bits.word(offset + 9 + sizeof(encoded) * 8, 16, epilogue) ||
          epilogue != 0xdeaa) {
        result.badData++;
        continue;
      }
      sector.size = 512;
      sector.data = payload + 12;
      sector.tags = payload;
      if (!deliver(result, visitor, context, sector))
        break;
      offset += 8 + sizeof(encoded) * 8 + 16;
      shift = 0;
    }
  }
  return result;
}
} // namespace

/** @brief Encode a standard Apple II 16-sector track as packed 24 MHz flux.
    @param sectors Exactly 4096 bytes in physical sector-ID order, 0 through 15.
           Callers must reorder DOS/ProDOS sector images before encoding.
    @param cylinder Zero-based cylinder, 0 through 34.
    @param volume Address-field volume number; 254 is the conventional default.
    @param pulses Caller-owned Greaseweazle-packed transition intervals.
    @param capacity Output capacity in bytes; 210000 accommodates a track.
    @param revolutionMicroseconds Measured rotation period, 180000 to 220000.
    @return Bytes produced, or zero on failure; discard partial output.
    Emits two revolutions for an INDEX-bounded Shugart write, packing enabled.
    Uses standard six-and-two data and four-and-four addresses, with 10-cell
    sync bytes. Layout follows Greaseweazle apple2_gcr.py (Keir Fraser, public
    domain). No allocation or drive access; no 13-sector or protected formats.
 */
size_t Adafruit_FloppyCodec::encodeApple(const uint8_t *sectors,
                                         uint8_t cylinder, uint8_t volume,
                                         uint8_t *pulses, size_t capacity,
                                         uint32_t revolutionMicroseconds) {
  if (!sectors || !pulses || cylinder >= 35 ||
      revolutionMicroseconds < 180000 || revolutionMicroseconds > 220000)
    return 0;

  const uint32_t trackBits =
      20000000 / 392; // Nominal 3.92 us cells at 300 RPM.
  uint32_t periodTicks = revolutionMicroseconds * 24;
  uint32_t cells = 0, previousEdge = 0;
  uint8_t *next = pulses;
  // Bit-level construction preserves the two zero cells after each sync byte.
  // Round absolute edge times so fractional ticks cannot accumulate drift.
  auto bits = [&](uint32_t value, unsigned width) {
    for (int bit = width - 1; bit >= 0; bit--) {
      cells++;
      if (!(value & (1u << bit)))
        continue;
      uint32_t edge =
          ((uint64_t)cells * periodTicks + trackBits / 2) / trackBits;
      unsigned interval = edge - previousEdge;
      size_t needed = interval < 250 ? 1 : interval < 1525 ? 2 : 6;
      if (interval < 20 || interval > 65555 ||
          needed > capacity - (next - pulses))
        return false;
      next = greasepack(next, pulses + capacity, interval);
      previousEdge = edge;
    }
    return true;
  };
  auto sync = [&](unsigned count) {
    for (unsigned i = 0; i < count; i++)
      if (!bits(0x3fc, 10)) // Eight ones followed by two zeros.
        return false;
    return true;
  };

  for (unsigned revolution = 0; revolution < 2; revolution++) {
    for (unsigned i = 0; i < 300; i++)
      if (!bits(1, 1))
        return 0;
    for (uint8_t id = 0; id < 16; id++) {
      uint8_t header[] = {volume, cylinder, id,
                          (uint8_t)(volume ^ cylinder ^ id)};
      if (!sync(18) || !bits(0xff, 8) || !bits(0xd5aa96, 24))
        return 0;
      for (uint8_t value : header)
        if (!bits((value >> 1) | 0xaa, 8) || !bits(value | 0xaa, 8))
          return 0;
      if (!bits(0xdeaaeb, 24) || !sync(6) || !bits(0xd5aaad, 24))
        return 0;

      const uint8_t *data = sectors + id * 256;
      uint8_t previous = 0;
      for (unsigned i = 0; i < 342; i++) {
        uint8_t value = 0;
        if (i < 86) {
          // Pack the low two bits of up to three payload bytes, reversing
          // each pair. The last two auxiliary bytes have only two pairs.
          for (unsigned group = 0; group < 3; group++) {
            unsigned index = i + group * 86;
            if (index < 256) {
              uint8_t pair =
                  ((data[index] & 1) << 1) | ((data[index] & 2) >> 1);
              value |= pair << (group * 2);
            }
          }
        } else {
          value = data[i - 86] >> 2;
        }
        if (!bits(appleCode(value ^ previous), 8))
          return 0;
        previous = value;
      }
      if (!bits(appleCode(previous), 8) || !bits(0xdeaaeb, 24))
        return 0;
    }
    while (cells < (revolution + 1) * trackBits)
      if (!bits(1, 1))
        return 0;
  }
  return next - pulses;
}

/** @brief Encode a standard 35-track C64/1541 track as packed 24 MHz flux.
    @param sectors Payloads in sector-ID order, 256 bytes each. Cylinders 0-16
           have 21 sectors, 17-23 have 19, 24-29 have 18, and 30-34 have 17.
    @param cylinder Zero-based cylinder, 0 through 34.
    @param diskID Disk ID in sector-header byte order (BAM bytes 163 then 162).
    @param pulses Caller-owned Greaseweazle-packed transition intervals.
    @param capacity Output capacity in bytes; 210000 accommodates every zone.
    @param revolutionMicroseconds Measured rotation period, 180000 to 220000.
    @return Bytes produced, or zero on failure; discard partial output.
    Emits two revolutions for an INDEX-bounded write with packing enabled.
    No allocation or drive access. The standard sync/gap layout follows
    Greaseweazle c64_gcr.py (Keir Fraser, public domain). Custom protection,
    half-tracks and D64 error tables are not reproduced. */
size_t Adafruit_FloppyCodec::encodeC64(const uint8_t *sectors, uint8_t cylinder,
                                       uint16_t diskID, uint8_t *pulses,
                                       size_t capacity,
                                       uint32_t revolutionMicroseconds) {
  if (!sectors || !pulses || cylinder >= 35 ||
      revolutionMicroseconds < 180000 || revolutionMicroseconds > 220000)
    return 0;

  unsigned zone = cylinder < 17 ? 0 : cylinder < 24 ? 1 : cylinder < 30 ? 2 : 3;
  const unsigned clocks[] = {325, 350, 375,
                             400}; // Hundredths of a microsecond.
  const uint8_t sectorCounts[] = {21, 19, 18, 17};
  uint32_t trackBits = (20000000 / clocks[zone]) & ~31u;
  uint32_t periodTicks = revolutionMicroseconds * 24;
  uint32_t cells = 0, previousEdge = 0;
  uint8_t *next = pulses;

  // Emit physical cells, rounding absolute edge times to prevent accumulated
  // drift. GCR's 4-to-5 code and sync fields require bit-level construction.
  auto bits = [&](uint16_t value, unsigned width) {
    for (int bit = width - 1; bit >= 0; bit--) {
      cells++;
      if (!(value & (1u << bit)))
        continue;
      uint32_t edge =
          ((uint64_t)cells * periodTicks + trackBits / 2) / trackBits;
      unsigned interval = edge - previousEdge;
      size_t needed = interval < 250 ? 1 : interval < 1525 ? 2 : 6;
      if (interval < 20 || interval > 65555 ||
          needed > capacity - (next - pulses))
        return false;
      next = greasepack(next, pulses + capacity, interval);
      previousEdge = edge;
    }
    return true;
  };
  auto gcr = [&](uint8_t value) {
    static const uint8_t code[] = {0x0a, 0x0b, 0x12, 0x13, 0x0e, 0x0f,
                                   0x16, 0x17, 0x09, 0x19, 0x1a, 0x1b,
                                   0x0d, 0x1d, 0x1e, 0x15};
    return bits(code[value >> 4], 5) && bits(code[value & 15], 5);
  };
  auto repeat = [&](uint8_t value, unsigned count) {
    for (unsigned i = 0; i < count; i++)
      if (!bits(value, 8))
        return false;
    return true;
  };

  for (unsigned revolution = 0; revolution < 2; revolution++) {
    if (!repeat(0x55, 10))
      return 0;
    for (uint8_t id = 0; id < sectorCounts[zone]; id++) {
      uint8_t header[] = {8,
                          0,
                          id,
                          (uint8_t)(cylinder + 1),
                          (uint8_t)(diskID >> 8),
                          (uint8_t)diskID,
                          15,
                          15};
      for (unsigned i = 2; i <= 5; i++)
        header[1] ^= header[i];
      if (!repeat(0xff, 5))
        return 0;
      for (uint8_t value : header)
        if (!gcr(value))
          return 0;
      if (!repeat(0x55, 9) || !repeat(0xff, 5) || !gcr(7))
        return 0;

      uint8_t checksum = 0;
      for (unsigned i = 0; i < 256; i++) {
        uint8_t value = sectors[id * 256 + i];
        checksum ^= value;
        if (!gcr(value))
          return 0;
      }
      if (!gcr(checksum) || !gcr(15) || !gcr(15) || !repeat(0x55, 9))
        return 0;
    }
    while (cells < (revolution + 1) * trackBits)
      if (!bits(0x55, 8))
        return 0;
  }
  return next - pulses;
}

/** @brief Nominal Mac GCR cell period adapted to a 300 RPM drive.
    @param cylinder Cylinder 0 through 79.
    @return Microseconds per cell; zero for an invalid cylinder. */
float Adafruit_FloppyCodec::macBitCellMicroseconds(uint8_t cylinder) {
  static const float clocks[] = {2.63f, 2.89f, 3.20f, 3.57f, 3.98f};
  return cylinder < 80 ? clocks[cylinder / 16] : 0;
}

/** @brief Encode a standard Macintosh GCR track as packed 24 MHz flux.
    @param sectors Exactly (12 - cylinder/16) * 512 bytes, in sector-ID order.
    @param cylinder Cylinder 0 through 79.
    @param head Head 0 or 1, less than heads.
    @param heads One for 400K or two for 800K.
    @param pulses Caller-owned Greaseweazle-packed transition intervals.
    @param capacity Output capacity in bytes; 210000 accommodates every zone.
    @param revolutionMicroseconds Measured rotation period, 180000 to 220000.
    @param tags Optional 12 bytes per sector, in sector-ID order; null writes
           zero tags, as in standard raw sector images.
    @return Bytes produced, or zero on failure; discard partial output.
    Emits two revolutions for an INDEX-bounded write. Enable packing when
    calling write_track(). No allocation or drive access. The track layout
    follows Greaseweazle mac_gcr.py (Keir Fraser, public domain). */
size_t Adafruit_FloppyCodec::encodeMac(const uint8_t *sectors, uint8_t cylinder,
                                       uint8_t head, uint8_t heads,
                                       uint8_t *pulses, size_t capacity,
                                       uint32_t revolutionMicroseconds,
                                       const uint8_t *tags) {
  if (!sectors || !pulses || cylinder >= 80 || (heads != 1 && heads != 2) ||
      head >= heads || revolutionMicroseconds < 180000 ||
      revolutionMicroseconds > 220000)
    return 0;
  // Integer hundredths of a microsecond avoid a float rounding difference
  // when choosing the whole 32-cell track length.
  static const unsigned clocks[] = {263, 289, 320, 357, 398};
  uint32_t trackBits = (20000000 / clocks[cylinder / 16]) & ~31u;
  // Some inner zones need slightly more room for this canonical gap layout.
  // Greaseweazle retains the whole layout and scales it to one revolution.
  uint32_t layoutBits = (188 + 764 * (12 - cylinder / 16)) * 8;
  if (trackBits < layoutBits)
    trackBits = layoutBits;
  uint32_t cells = 0, previousEdge = 0;
  uint32_t periodTicks = revolutionMicroseconds * 24;
  uint8_t *next = pulses;
  auto byte = [&](uint8_t value) {
    for (int bit = 7; bit >= 0; bit--) {
      cells++;
      if (!(value & (1u << bit)))
        continue;
      uint32_t edge =
          ((uint64_t)cells * periodTicks + trackBits / 2) / trackBits;
      unsigned interval = edge - previousEdge;
      size_t needed = interval < 250 ? 1 : interval < 1525 ? 2 : 6;
      if (interval < 20 || interval > 65555 ||
          needed > capacity - (next - pulses))
        return false;
      next = greasepack(next, pulses + capacity, interval);
      previousEdge = edge;
    }
    return true;
  };
  auto sync = [&](unsigned count) {
    const uint8_t pattern[] = {0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff};
    for (unsigned i = 0; i < count; i++)
      for (uint8_t value : pattern)
        if (!byte(value))
          return false;
    return true;
  };
  uint8_t sectorsPerTrack = 12 - cylinder / 16;
  uint8_t order[12];
  memset(order, 0xff, sizeof(order));
  unsigned position = 0;
  for (uint8_t id = 0; id < sectorsPerTrack; id++) {
    while (order[position] != 0xff)
      position = (position + 1) % sectorsPerTrack;
    order[position] = id;
    position = (position + 2) % sectorsPerTrack;
  }
  for (unsigned revolution = 0; revolution < 2; revolution++) {
    for (unsigned i = 0; i < 64; i++)
      if (!byte(0x96))
        return 0;
    if (!sync(20))
      return 0;
    for (uint8_t i = 0; i < sectorsPerTrack; i++) {
      uint8_t id = order[i];
      uint8_t header[] = {(uint8_t)(cylinder & 63), id,
                          (uint8_t)((head << 5) | (cylinder >> 6)),
                          (uint8_t)(heads == 1 ? 0x02 : 0x22), 0};
      for (unsigned j = 0; j < 4; j++)
        header[4] ^= header[j];
      if (!sync(6) || !byte(0xd5) || !byte(0xaa) || !byte(0x96))
        return 0;
      for (uint8_t value : header)
        if (!byte(appleCode(value)))
          return 0;
      if (!byte(0xde) || !byte(0xaa) || !byte(0xff) || !byte(0xff) ||
          !sync(1) || !byte(0xd5) || !byte(0xaa) || !byte(0xad) ||
          !byte(appleCode(id)))
        return 0;
      uint8_t payload[524] = {}, encoded[703];
      if (tags)
        memcpy(payload, tags + id * 12, 12);
      memcpy(payload + 12, sectors + id * 512, 512);
      macPayload(payload, encoded, true);
      for (uint8_t value : encoded)
        if (!byte(appleCode(value)))
          return 0;
      if (!byte(0xde) || !byte(0xaa) || !byte(0xff))
        return 0;
    }
    for (unsigned i = 0; i < 4; i++)
      if (!byte(0xff))
        return 0;
    while (cells < (revolution + 1) * trackBits)
      if (!byte(0x96))
        return 0;
  }
  return next - pulses;
}

/** @brief Encode a standard Amiga track as 24 MHz flux intervals.
    @param sectors Exactly sectorsPerTrack * 512 bytes, in sector-ID order.
    @param sectorsPerTrack 11 for DD or 22 for HD.
    @param cylinder Cylinder 0 through 79.
    @param head Head 0 or 1.
    @param pulses Caller-owned output, one byte per transition interval.
    @param capacity Output capacity in bytes; 210000 accommodates HD.
    @param revolutionMicroseconds Measured rotation period, 180000 to 220000.
    @return Bytes produced, or zero for invalid arguments/capacity. Discard
    partial output on failure. Emits two revolutions to provide overlap for
    an INDEX-bounded write; use write_track with packing disabled and INDEX
    enabled. Sector labels are zero. No allocation or drive access. */
size_t Adafruit_FloppyCodec::encodeAmiga(const uint8_t *sectors,
                                         uint8_t sectorsPerTrack,
                                         uint8_t cylinder, uint8_t head,
                                         uint8_t *pulses, size_t capacity,
                                         uint32_t revolutionMicroseconds) {
  if (!sectors || !pulses || (sectorsPerTrack != 11 && sectorsPerTrack != 22) ||
      cylinder >= 80 || head > 1 || revolutionMicroseconds < 180000 ||
      revolutionMicroseconds > 220000)
    return 0;

  // Amiga PAL cell clock: 14/7093790 seconds (DD), half that for HD.
  // Round the nominal 200 ms track up to a whole 32-cell word, as in
  // Greaseweazle's AmigaDOS encoder (Keir Fraser, public domain).
  uint32_t trackBits =
      ((7093790 / (sectorsPerTrack == 11 ? 70 : 35) + 31) / 32) * 32;
  uint32_t periodTicks = revolutionMicroseconds * 24;
  uint32_t cells = 0, previousEdge = 0;
  size_t count = 0;
  uint8_t previousByte = 0;
  auto byte = [&](uint8_t value) {
    // Alternating bit positions carry data (0x55) and clocks (0xaa).
    // Sync bytes already contain their deliberately missing clock bits.
    uint16_t pair = (uint16_t)previousByte * 256 + value;
    if ((value & 0xaa) == 0)
      pair |= ~((pair >> 1) | (pair << 1)) & 0xaaaa;
    previousByte = pair;
    for (int bit = 7; bit >= 0; bit--) {
      cells++;
      if (!(previousByte & (1u << bit)))
        continue;
      uint32_t edge =
          ((uint64_t)cells * periodTicks + trackBits / 2) / trackBits;
      uint32_t interval = edge - previousEdge;
      // The RP2 writer needs at least 20 ticks. A standard Amiga track has
      // no long gaps requiring Greaseweazle escape-byte packing.
      if (interval < 20 || interval >= 250 || count == capacity)
        return false;
      pulses[count++] = interval;
      previousEdge = edge;
    }
    return true;
  };
  auto field = [&](const uint8_t *data, size_t size) {
    // Each field stores all odd data bits, then all even data bits.
    for (uint8_t odd = 1;; odd--) {
      for (size_t i = 0; i < size; i++) {
        if (!byte((data[i] >> odd) & 0x55))
          return false;
      }
      if (!odd)
        break;
    }
    return true;
  };
  auto checksum = [&](const uint8_t *data, size_t size) {
    uint32_t sum = amigaChecksum(data, size);
    uint8_t encoded[] = {(uint8_t)(sum >> 24), (uint8_t)(sum >> 16),
                         (uint8_t)(sum >> 8), (uint8_t)sum};
    return field(encoded, sizeof(encoded));
  };
  for (uint8_t revolution = 0; revolution < 2; revolution++) {
    // Post-INDEX zero-data gap, doubled for HD.
    for (unsigned i = 0; i < 256u * (sectorsPerTrack / 11); i++) {
      if (!byte(0))
        return 0;
    }
    for (uint8_t sector = 0; sector < sectorsPerTrack; sector++) {
      uint8_t header[20] = {0xff, (uint8_t)(cylinder * 2 + head), sector,
                            (uint8_t)(sectorsPerTrack - sector)};
      const uint8_t *data = sectors + sector * 512;
      // Two 0x4489 sync words, ID, 16 zero label bytes, checksums and payload.
      if (!byte(0x44) || !byte(0x89) || !byte(0x44) || !byte(0x89) ||
          !field(header, 4) || !field(header + 4, 16) ||
          !checksum(header, sizeof(header)) || !checksum(data, 512) ||
          !field(data, 512))
        return 0;
      for (uint8_t i = 0; i < 4; i++)
        if (!byte(0))
          return 0;
    }
    // Fill to INDEX without changing the measured revolution duration.
    while (cells < (revolution + 1) * trackBits)
      if (!byte(0))
        return 0;
  }
  return count;
}

/** @brief Decode checksum-verified sectors from a bounded packed bitstream.
    @param encoding Standard sector layout to attempt.
    @param bits Packed bytes, with the earliest bit in bit 7 of byte 0.
    @param bitCount Exact number of accessible bits.
    @param visitor Optional sector callback; false stops decoding early.
    @param context Opaque caller-owned visitor context.
    @return Counts of good sectors and rejected recognized fields. */
Adafruit_FloppyCodec::Result
Adafruit_FloppyCodec::decode(Encoding encoding, const uint8_t *bits,
                             size_t bitCount, Visit visitor, void *context) {
  if (!bits || !bitCount) {
    return {};
  }
  Bits source = {bits, bitCount};
  switch (encoding) {
  case IBM_MFM:
    return decodeIBM(source, false, visitor, context);
  case IBM_FM:
    return decodeIBM(source, true, visitor, context);
  case AMIGA_MFM:
    return decodeAmiga(source, visitor, context);
  case COMMODORE_GCR:
    return decodeCBM(source, visitor, context);
  case APPLE_6_AND_2:
    return decodeApple(source, visitor, context);
  case MAC_GCR:
    return decodeMac(source, visitor, context);
  }
  return {};
}

/** @brief Name the magnetic layout without guessing the filesystem.
    @param encoding Standard sector layout.
    @return Static human-readable string. */
const char *Adafruit_FloppyCodec::name(Encoding encoding) {
  switch (encoding) {
  case IBM_MFM:
    return "IBM MFM";
  case IBM_FM:
    return "IBM FM";
  case AMIGA_MFM:
    return "Amiga MFM";
  case COMMODORE_GCR:
    return "Commodore 1541 GCR";
  case APPLE_6_AND_2:
    return "Apple II 6-and-2 GCR";
  case MAC_GCR:
    return "Macintosh GCR";
  }
  return "Unknown";
}
