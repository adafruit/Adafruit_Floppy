#include "mfm_bpb.h"
#include <Adafruit_Floppy.h>

/// @cond false
static const uint16_t flux_rates[] = {2000, 1000, 833, 1667};

struct adafruit_floppy_format_info_t {
  uint8_t cylinders, sectors;
  uint16_t bit_time_ns;
};

// must match the order of adafruit_floppy_disk_t
static const adafruit_floppy_format_info_t _format_info[] = {
    /* IBMPC360K */
    {40, 9, 2000},
    /* IBMPC720K */
    {80, 9, 2000},
    /* IBMPC720K_360RPM */
    {80, 9, 1667},
    /* IBMPC1200K: 500 kbit/s in a 360 RPM drive. */
    {80, 15, 1000},
    /* IBMPC1440K */
    {80, 18, 1000},
    /* IBMPC1440K_360RPM */
    {80, 18, 833},
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

  // The same profile supplies geometry and timing before and after insertion.
  // AUTODETECT remains empty until a valid boot sector has been read.
  switch (_format) {
  case IBMPC360K:
  case IBMPC720K:
  case IBMPC720K_360RPM:
  case IBMPC1200K:
  case IBMPC1440K:
  case IBMPC1440K_360RPM: {
    const auto &info = _format_info[_format];
    _sectors_per_track = info.sectors;
    _tracks_per_side = info.cylinders;
    _bit_time_ns = info.bit_time_ns;
    break;
  }
  case AUTODETECT:
    break;
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
  } else {
    return false;
  }

  return true;
}

/**************************************************************************/
/*!
    @brief  Spin down and deselect the motor and drive
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
int32_t Adafruit_MFM_Floppy::readTrack(int logical_track, bool head) {
  // A failed flush must retain the pending track, not replace it with a read.
  if (!syncDevice()) {
    return -1;
  }

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
  // Hardware writes are synchronous; a dirty cache is not an active write.
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
  if (block >= sectorCount()) {
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
    @brief  Update a 512-byte block in the track cache; syncDevice() writes it
    @param  block Block number, will be split into head and track based on
    expected formatting
    @param  src Source buffer
    @returns True if cached, false if protected or the track could not be read
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::writeSector(uint32_t block, const uint8_t *src) {
  if (block >= sectorCount()) {
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
    @brief  Update consecutive 512-byte blocks, flushing when changing tracks
    @param  block Starting lock number, will be split into head and track based
   on expected formatting
    @param  src Source buffer
    @param  nb Number of consecutive blocks to write
    @returns True if all blocks were accepted, false on a read/write failure
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
    @brief  Write the complete dirty track, retaining it if the write fails
    @returns True if clean or written; false leaves pending data available
*/
/**************************************************************************/
bool Adafruit_MFM_Floppy::syncDevice() {
  if (!_dirty || _last_track_read == NO_TRACK) {
    return true;
  }
  // The disk's write-protect tab may have changed since writeSector().
  if (_floppy->get_write_protect()) {
    return false;
  }

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
                                      sizeof(_flux), _bit_time_ns / 1000.f,
                                      logical_track);

  if (!_n_flux || !_floppy->write_track(_flux, _n_flux, false)) {
    Serial.println("failed to write track");
    return false;
  }

  _dirty = false;
  return true;
}

void Adafruit_MFM_Floppy::removed() {
  noInterrupts();
  _tracks_per_side = 0;
  _last_track_read = NO_TRACK;
  _dirty = false;
  interrupts();
}

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
      mfm_bpb_geometry_t geometry;
      if (!mfm_bpb_geometry(track_data, &geometry)) {
        Serial.print("Invalid or unsupported DOS geometry\n\r");
        continue;
      }

      _bit_time_ns = flux_rate_ns;
      _sectors_per_track = geometry.sectors;
      _tracks_per_side = geometry.cylinders;
      _last_track_read = NO_TRACK;
      _dirty = false;

      if (_tracks_per_side <= 40) {
        _floppy->goto_track(2);
        _n_flux = _floppy->capture_track(_flux, sizeof(_flux) / 16,
                                         &index_offset, false, 220);
        uint8_t track_number = 0;
        auto captured_sectors = _floppy->decode_track_mfm(
            track_data, 1, track_validity, _flux, _n_flux,
            flux_rate_ns / 1000.f, true, &track_number);
        if (!captured_sectors) {
          Serial.printf("failed to read on physical track 2\r\n");
        }
        _double_step = captured_sectors && (track_number == 1);
        Serial.printf(
            "on physical track 2, track_number=%d. _double_step <- %d\r\n",
            track_number, _double_step);
      } else {
        _double_step = false;
      }
      Serial.printf("Detected flux rate %dns/bit\r\n%d/%d/%d C/H/S\r\n",
                    flux_rate_ns, _tracks_per_side, FLOPPY_HEADS,
                    _sectors_per_track);
      return true;
    }
  }
  Serial.printf("failed autodetect\r\n");
  _sectors_per_track = 0;
  return false;
}

bool Adafruit_MFM_Floppy::inserted(adafruit_floppy_disk_t floppy_type) {
  if (!_floppy || floppy_type < IBMPC360K || floppy_type > AUTODETECT) {
    return false;
  }
  _floppy->goto_track(0);
  _floppy->side(0);

  if (floppy_type == AUTODETECT) {
    return autodetect();
  }
  const auto &info = _format_info[floppy_type];

  noInterrupts();
  _tracks_per_side = info.cylinders;
  _sectors_per_track = info.sectors;
  _bit_time_ns = info.bit_time_ns;
  _double_step = false;
  _last_track_read = NO_TRACK;
  _dirty = false;
  interrupts();

  return true;
  // TODO: set up double stepping on HD 5.25 drives with 360kB media inserted
}
