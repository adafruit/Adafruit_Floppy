#include <Adafruit_Floppy.h>


const adafruit_floppy_format_info_t _format_info[] = {
    /* IBMPC360K */
    { 40, 9, 2000, 167 },
    /* IBMPC1200K */
    { 80, 18, 1000, 200 },

    /* IBMPC720K */
    { 80, 9, 1000, 200 },
    /* IBMPC720K_360RPM */
    { 80, 9, 1667, 167 },

    /* IBMPC1440K */
    { 80, 18, 1000, 200 },
    /* IBMPC1440K_360RPM */
    { 80, 18, 867, 167 },
};

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

  return _floppy->spin_motor(true);
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
    @param  track track number, 0 to whatever is the  max tracks for the given
    @param  head which side to read, false for side 1, true for side 2
    format during instantiation (e.g. 40 for DD, 80 for HD)
    @returns Number of sectors captured, or -1 if we couldn't seek
*/
/**************************************************************************/
int32_t Adafruit_MFM_Floppy::readTrack(uint8_t track, bool head) {
  syncDevice();

  // Serial.printf("\tSeeking track %d head %d...", track, head);
  if (!_floppy->goto_track(track)) {
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
    captured_sectors = _floppy->decode_track_mfm(
        track_data, _sectors_per_track, track_validity, _flux, _n_flux,
        _high_density ? 1.f : 2.f, i == 0);
  }

  _track_has_errors = (captured_sectors != _sectors_per_track);
  if (_track_has_errors) {
    Serial.printf("Track has errors (%d != %d)\n", captured_sectors,
                  _sectors_per_track);
  }
  _last_track_read = track * FLOPPY_HEADS + head;
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
  if (block > sectorCount()) { return false; }

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
  if (block > sectorCount()) { return false; }

  // promptly fail if disk is protected
  // might also fail if WGATE is masked by HW (e.g., by physical switch on floppsy)
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

  int track = _last_track_read / FLOPPY_HEADS;
  int head = _last_track_read % FLOPPY_HEADS;
  Serial.printf("Flushing track %d side %d\r\n", track, head);

  // should be a no-op
  if (!_floppy->goto_track(track)) {
    Serial.println("failed to seek to track");
    return false;
  }

  if (!_floppy->side(head)) {
    Serial.println("failed to select head");
    return false;
  }

  bool has_errors = false;
  for(size_t i=0; !has_errors && i<_sectors_per_track; i++) {
      has_errors = !track_validity[i];
  }

  if (has_errors) {
    Serial.printf("Can't do a non-full track write to track with read errors\n");
    return false;
  }
  _n_flux = _floppy->encode_track_mfm(track_data, _sectors_per_track, _flux,
                                      sizeof(_flux), _high_density ? 1.f : 2.f);

  if (!_floppy->write_track(_flux, _n_flux, false)) {
    Serial.println("failed to write track");
    return false;
  }

  return true;
}

void Adafruit_MFM_Floppy::removed() {
    _tracks_per_side = 0;
}

void Adafruit_MFM_Floppy::inserted() {
    if (_floppy->track() <= 0) { _floppy->goto_track(1); }
    _floppy->goto_track(0);
    _tracks_per_side = 80;
}

const adafruit_floppy_format_info_t *Adafruit_MFM_Floppy::format_info() const {
    if (_format < 0 || _format >= std::size(_format_info)) {
        return nullptr;
    }
    return &_format_info[_format];
}

