// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// @cond false
// Internal DOS geometry parser. Keep it independent of Arduino so malformed
// boot sectors can be regression-tested without spinning a drive.
typedef struct {
  uint8_t cylinders;
  uint8_t sectors;
} mfm_bpb_geometry_t;

static inline uint16_t mfm_bpb_le16(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}

static inline bool mfm_bpb_geometry(const uint8_t *sector,
                                    mfm_bpb_geometry_t *geometry) {
  if (sector[0] != 0xeb || sector[1] < 0x1e || sector[2] != 0x90)
    return false;

  uint16_t bytes = mfm_bpb_le16(sector + 11);
  uint16_t total = mfm_bpb_le16(sector + 19);
  uint16_t sectors = mfm_bpb_le16(sector + 24);
  uint16_t heads = mfm_bpb_le16(sector + 26);

  // This block driver allocates 18 x 512 bytes and assumes two heads. Reject
  // unsupported or damaged geometry before division, narrowing, or buffer use.
  if (bytes != 512 || heads != 2 || !sectors || sectors > 18 || !total)
    return false;
  uint16_t per_cylinder = heads * sectors;
  if (total % per_cylinder)
    return false;
  uint16_t cylinders = total / per_cylinder;
  if (!cylinders || cylinders > 80)
    return false;

  geometry->cylinders = cylinders;
  geometry->sectors = sectors;
  return true;
}
/// @endcond
