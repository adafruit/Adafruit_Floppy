#include <Adafruit_Floppy.h>

/// @cond false
static const uint16_t flux_rates[] = {2000, 1000, 867, 1667};

struct adafruit_floppy_format_info_t {
  uint8_t cylinders, sectors;
  uint16_t bit_time_ns;
  uint16_t track_time_ms;
};

// must match the order of adafruit_floppy_disk_t
static const adafruit_floppy_format_info_t _format_info[] = {
    /* IBMPC360K */
    {40, 9, 2000, 167},
    /* IBMPC1200K */
    {80, 18, 1000, 200},

    /* IBMPC720K */
    {80, 9, 1000, 200},
    /* IBMPC720K_360RPM */
    {80, 9, 1667, 167},

    /* IBMPC1440K */
    {80, 18, 1000, 200},
    /* IBMPC1440K_360RPM */
    {80, 18, 867, 167},
};
/// @endcond

static_assert(sizeof(_format_info) / sizeof(_format_info[0]) == AUTODETECT);

/**************************************************************************/
/*!
    @brief  Instantiate an MFM-formatted floppy
    @param  floppy An Adafruit_Floppy object that has the pins defined
    @param  format What kind of format we will be parsing out - we DO NOT
   autodetect!
*/
/**************************************************************************/
Adafruit_MFM_Floppy::Adafruit_MFM_Floppy(Adafruit_Floppy *floppy,
                                         adafruit_floppy_disk_t format) {
  _floppy = floppy;
  _format = format;

  // different formats have different 'hardcoded' sectors and tracks
  if (_format == IBMPC1440K) {
    _sectors_per_track = MFM_IBMPC1440K_SECTORS_PER_TRACK;
    _tracks_per_side = FLOPPY_IBMPC_HD_TRACKS;
    _high_density = true;
  } else if (_format == IBMPC360K) {
    _sectors_per_track = MFM_IBMPC360K_SECTORS_PER_TRACK;
    _tracks_per_side = FLOPPY_IBMPC_DD_TRACKS;
    _high_density = false;
  }
}

/**************************************************************************/
/*!
    @brief  Initialize and spin up the floppy drive
    @returns True if we were able to spin up and detect an index track
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::begin(void) {
  if (!_floppy)
    return false;
  _floppy->begin();

  // now's the time to tweak settings
  if (_format == IBMPC360K) {
    _floppy->step_delay_us = 65000UL; // lets make it max 65ms not 10ms?
    _floppy->settle_delay_ms = 50;    // 50ms not 15
  }

  _floppy->select(true);

  if (_floppy->spin_motor(true)) {
    return inserted(_format);
  }

  return true;
}

/**************************************************************************/
/*!
    @brief  Spin down and deselect the motor and drive
    @returns True always
*/
/**************************************************************************/
void Adafruit_MFM_Floppy::end(void) {
  _floppy->spin_motor(false);
  _floppy->select(false);
}

/**************************************************************************/
/*!
    @brief   Quick calculator for expected max capacity
    @returns Size of the drive in bytes
*/
/**************************************************************************/
uint32_t Adafruit_MFM_Floppy::size(void) const {
  return (uint32_t)_tracks_per_side * FLOPPY_HEADS * _sectors_per_track *
         MFM_BYTES_PER_SECTOR;
}

/**************************************************************************/
/*!
    @brief  Read one track's worth of data and MFM decode it
    @param  logical_track the logical track number, 0 to whatever is the  max
   tracks for the given format during instantiation (e.g. 40 for DD, 80 for HD)
    @param  head which side to read, false for side 1, true for side 2
    @returns Number of sectors captured, or -1 if we couldn't seek
*/
/**************************************************************************/
int32_t Adafruit_MFM_Floppy::readTrack(uint8_t logical_track, bool head) {
  syncDevice();

  uint8_t physical_track = _double_step ? 2 * logical_track : logical_track;

  Serial.printf("\t[readTrack] Seeking track %d [phys=%d] head %d...\r\n",
                logical_track, physical_track, head);
  if (!_floppy->goto_track(physical_track)) {
    // Serial.println("failed to seek to track");
    return -1;
  }
  _floppy->side(head);
  // Serial.println("done!");
  // flux not decoding from a 3.5" floppy? Maybe it's rotating at 360RPM instead
  // of 300RPM see e.g.,
  // https://www.retrotechnology.com/herbs_stuff/drive.html#rotate2
  // and change nominal bit time to 0.833 ~= 300/360
  // would be good to auto-detect!
  uint32_t captured_sectors = 0;
  for (int i = 0; i < 5 && captured_sectors < _sectors_per_track; i++) {
    int32_t index_offset;
    _n_flux =
        _floppy->capture_track(_flux, sizeof(_flux), &index_offset, false, 220);
    captured_sectors = _floppy->decode_track_mfm(track_data, _sectors_per_track,
                                                 track_validity, _flux, _n_flux,
                                                 _bit_time_ns / 1000.f, i == 0);
  }

  _track_has_errors = (captured_sectors != _sectors_per_track);
  if (_track_has_errors) {
    Serial.printf("Track %d/%d has errors (%d != %d)\n", logical_track, head,
                  captured_sectors, _sectors_per_track);
  }
  _last_track_read = logical_track * FLOPPY_HEADS + head;
  return captured_sectors;
}

//--------------------------------------------------------------------+
// SdFat BaseBlockDriver API
// A block is 512 bytes
//--------------------------------------------------------------------+

/**************************************************************************/
/*!
    @brief   Max capacity in sector block
    @returns Size of the drive in sector (512 bytes)
*/
/**************************************************************************/
uint32_t Adafruit_MFM_Floppy::sectorCount() {
  return size() / MFM_BYTES_PER_SECTOR;
}

/**************************************************************************/
/*!
    @brief   Check if device busy
    @returns true if busy
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::isBusy() {
  // since writing is not supported yet
  return false;
}

/**************************************************************************/
/*!
    @brief  Read a 512 byte block of data, may used cached data
    @param  block Block number, will be split into head and track based on
    expected formatting
    @param  dst Destination buffer
    @returns True on success
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::readSector(uint32_t block, uint8_t *dst) {
  if (block > sectorCount()) {
    return false;
  }

  uint8_t track = block / (FLOPPY_HEADS * _sectors_per_track);
  uint8_t head = (block / _sectors_per_track) % FLOPPY_HEADS;
  uint8_t subsector = block % _sectors_per_track;

  // Serial.printf("\tRead request block %d\n", block);
  if ((track * FLOPPY_HEADS + head) != _last_track_read) {
    // oof it is not cached!

    if (readTrack(track, head) == -1) {
      return false;
    }
  }

  if (!track_validity[subsector]) {
    // Serial.println("subsector invalid");
    return false;
  }
  // Serial.println("OK!");
  memcpy(dst, track_data + (subsector * MFM_BYTES_PER_SECTOR),
         MFM_BYTES_PER_SECTOR);

  return true;
}

/**************************************************************************/
/*!
    @brief  Read multiple 512 byte block of data, may used cached data
    @param  block Starting block number, will be split into head and track based
   on expected formatting
    @param  dst Destination buffer
    @param  nb Number of blocks to read
    @returns True on success
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::readSectors(uint32_t block, uint8_t *dst, size_t nb) {
  // read each block one by one
  for (size_t blocknum = 0; blocknum < nb; blocknum++) {
    if (!readSector(block + blocknum, dst + (blocknum * MFM_BYTES_PER_SECTOR)))
      return false;
  }
  return true;
}

/**************************************************************************/
/*!
    @brief  Write a 512 byte block of data NOT IMPLEMENTED YET
    @param  block Block number, will be split into head and track based on
    expected formatting
    @param  src Source buffer
    @returns True on success, false if failed or unimplemented
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::writeSector(uint32_t block, const uint8_t *src) {
  if (block > sectorCount()) {
    return false;
  }

  // promptly fail if disk is protected
  // might also fail if WGATE is masked by HW (e.g., by physical switch on
  // floppsy)
  if (_floppy->get_write_protect()) {
    return false;
  }

  uint8_t track = block / (FLOPPY_HEADS * _sectors_per_track);
  uint8_t head = (block / _sectors_per_track) % FLOPPY_HEADS;
  uint8_t subsector = block % _sectors_per_track;

  if ((track * FLOPPY_HEADS + head) != _last_track_read) {
    // oof it is not cached!

    if (readTrack(track, head) == -1) {
      return false;
    }

    _last_track_read = track * FLOPPY_HEADS + head;
  }
  Serial.printf("Writing block %d\r\n", block);
  track_validity[subsector] = 1;
  memcpy(track_data + (subsector * MFM_BYTES_PER_SECTOR), src,
         MFM_BYTES_PER_SECTOR);
  _dirty = true;
  return true;
}

/**************************************************************************/
/*!
    @brief  Write multiple 512 byte blocks of data NOT IMPLEMENTED YET
    @param  block Starting lock number, will be split into head and track based
   on expected formatting
    @param  src Source buffer
    @param  nb Number of consecutive blocks to write
    @returns True on success, false if failed or unimplemented
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::writeSectors(uint32_t block, const uint8_t *src,
                                       size_t nb) {
  // write each block one by one
  for (size_t blocknum = 0; blocknum < nb; blocknum++) {
    if (!writeSector(block + blocknum, src + (blocknum * MFM_BYTES_PER_SECTOR)))
      return false;
  }
  return true;
}

/**************************************************************************/
/*!
    @brief  Sync written blocks NOT IMPLEMENTED YET
    @returns True on success, false if failed or unimplemented
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::syncDevice() {
  if (!_dirty || _last_track_read == NO_TRACK) {
    return true;
  }
  _dirty = false;

  int logical_track = _last_track_read / FLOPPY_HEADS;
  int head = _last_track_read % FLOPPY_HEADS;

  uint8_t physical_track = _double_step ? 2 * logical_track : logical_track;
  Serial.printf("Flushing track %d [phys %d] side %d\r\n", logical_track,
                physical_track, head);
  // should be a no-op
  if (!_floppy->goto_track(physical_track)) {
    Serial.println("failed to seek to track");
    return false;
  }

  if (!_floppy->side(head)) {
    Serial.println("failed to select head");
    return false;
  }

  bool has_errors = false;
  for (size_t i = 0; !has_errors && i < _sectors_per_track; i++) {
    has_errors = !track_validity[i];
  }

  if (has_errors) {
    Serial.printf(
        "Can't do a non-full track write to track with read errors\n");
    return false;
  }
  _n_flux = _floppy->encode_track_mfm(track_data, _sectors_per_track, _flux,
                                      sizeof(_flux), _high_density ? 1.f : 2.f,
                                      logical_track);

  if (!_floppy->write_track(_flux, _n_flux, false)) {
    Serial.println("failed to write track");
    return false;
  }

  return true;
}

void Adafruit_MFM_Floppy::removed() {
  noInterrupts();
  _tracks_per_side = 0;
  _last_track_read = NO_TRACK;
  _dirty = false;
  interrupts();
}

static uint16_t le16_at(uint8_t *ptr) { return ptr[0] | (ptr[1] << 8); }

bool Adafruit_MFM_Floppy::autodetect() {
  Serial.printf("autodetecting\r\n");
  int32_t index_offset;
  _n_flux = _floppy->capture_track(_flux, sizeof(_flux) / 16, &index_offset,
                                   false, 220);
  for (auto flux_rate_ns : flux_rates) {
    Serial.printf("flux rate %d\r\n", flux_rate_ns);
    auto captured_sectors =
        _floppy->decode_track_mfm(track_data, 1, track_validity, _flux, _n_flux,
                                  flux_rate_ns / 1000.f, true);
    if (captured_sectors) {
      auto valid_signature =
          track_data[0] == 0xeb && // short jump
          track_data[1] >= 0x1e && // minimum BPB size (DOS 3.0 BPB)
          track_data[2] == 0x90;   // NOP
      if (!valid_signature) {
        Serial.printf("Invalid signature %02x %02x %02x\r\n", track_data[0],
                      track_data[1], track_data[2]);
        continue;
      }
      auto heads = le16_at(track_data + 0x1A);
      auto total_logical_sectors = le16_at(track_data + 0x13);

      _bit_time_ns = flux_rate_ns;
      _sectors_per_track = le16_at(track_data + 0x18);
      _tracks_per_side = total_logical_sectors / heads / _sectors_per_track;
      _last_track_read = NO_TRACK;
      _dirty = false;

      if (_tracks_per_side <= 40) {
        _floppy->goto_track(2);
        _n_flux = _floppy->capture_track(_flux, sizeof(_flux) / 16,
                                         &index_offset, false, 220);
        uint8_t track_number;
        auto captured_sectors = _floppy->decode_track_mfm(
            track_data, 1, track_validity, _flux, _n_flux,
            flux_rate_ns / 1000.f, true, &track_number);
        if (!captured_sectors) {
          Serial.printf("failed to read on physical track 2\r\n");
        }
        _double_step = (track_number == 1);
        Serial.printf(
            "on physical track 2, track_number=%d. _double_step <- %d\r\n",
            track_number, _double_step);
      } else {
        _double_step = false;
      }
      Serial.printf("Detected flux rate %dns/bit\r\n%d/%d/%d C/H/S\r\n",
                    flux_rate_ns, _tracks_per_side, heads, _sectors_per_track);
      return true;
    }
  }
  Serial.printf("failed autodetect\r\n");
  _sectors_per_track = 0;
  return false;
}

bool Adafruit_MFM_Floppy::inserted(adafruit_floppy_disk_t floppy_type) {
  _floppy->goto_track(0);
  _floppy->side(0);

  if (floppy_type == AUTODETECT) {
    return autodetect();
  } else if (floppy_type < 0 || floppy_type > AUTODETECT) {
    return false;
  }
  const auto &info = _format_info[floppy_type];

  noInterrupts();
  _tracks_per_side = info.cylinders;
  _sectors_per_track = info.sectors;
  _bit_time_ns = info.bit_time_ns;
  _last_track_read = NO_TRACK;
  _dirty = false;
  interrupts();

  return true;
  // TODO: set up double stepping on HD 5.25 drives with 360kB media inserted
}
