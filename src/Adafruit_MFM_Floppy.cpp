#include <Adafruit_Floppy.h>

extern uint32_t T2_5, T3_5;

Adafruit_MFM_Floppy::Adafruit_MFM_Floppy(Adafruit_Floppy *floppy, adafruit_floppy_disk_t format) {
  _floppy = floppy;
  _format = format;
  if (_format == IBMPC1440K) {
    _sectors_per_track = MFM_IBMPC1440K_SECTORS_PER_TRACK;
    _tracks_per_side = FLOPPY_IBMPC_HD_TRACKS;
    T2_5 = T2_5_IBMPC_HD;
    T3_5 = T3_5_IBMPC_HD;
  } else if (_format == IBMPC360K) {
    _sectors_per_track = MFM_IBMPC360K_SECTORS_PER_TRACK;
    _tracks_per_side = FLOPPY_IBMPC_DD_TRACKS;
    T2_5 = T2_5_IBMPC_DD;
    T3_5 = T3_5_IBMPC_DD;
  }
}

bool Adafruit_MFM_Floppy::begin(void) {
  if (!_floppy) return false;
  _floppy->begin();
  _floppy->select(true);
  return _floppy->spin_motor(true);
}

uint32_t Adafruit_MFM_Floppy::size(void) { 
  return (uint32_t)_tracks_per_side * FLOPPY_HEADS * _sectors_per_track * MFM_BYTES_PER_SECTOR; 
}


int32_t Adafruit_MFM_Floppy::readTrack(uint8_t track, bool head) {

    //Serial.printf("\tSeeking track %d head %d...", track, head);
    if (! _floppy->goto_track(track)) {
      //Serial.println("failed to seek to track");
      return -1;
    }
    _floppy->side(head);
    //Serial.println("done!");
    uint32_t captured_sectors =_floppy->read_track_mfm(track_data, _sectors_per_track, track_validity);
    /*
      Serial.print("Captured %d sectors", captured_sectors);

      Serial.print("Validity: ");
      for(size_t i=0; i<MFM_SECTORS_PER_TRACK; i++) {
        Serial.print(track_validity[i] ? "V" : "?");
      }
    */
    return captured_sectors;
}


//--------------------------------------------------------------------+
// SdFat BaseBlockDriver API
// A block is 512 bytes
//--------------------------------------------------------------------+
bool Adafruit_MFM_Floppy::readBlock(uint32_t block, uint8_t *dst) {
  uint8_t track = block / (FLOPPY_HEADS * _sectors_per_track);
  uint8_t head = (block / _sectors_per_track) % FLOPPY_HEADS;
  uint8_t subsector = block % _sectors_per_track;
  
  //Serial.printf("\tRead request block %d\n", block);
  if ((track * FLOPPY_HEADS + head) != _last_track_read) {
    // oof it is not cached!

    if (readTrack(track, head) == -1) {
        return false;
      }

    _last_track_read = track * FLOPPY_HEADS + head;
  }

  if (! track_validity[subsector]) {
    //Serial.println("subsector invalid");
    return false;
  }
  //Serial.println("OK!");
  memcpy(dst, track_data+(subsector * MFM_BYTES_PER_SECTOR), MFM_BYTES_PER_SECTOR);

  return true;
}

bool Adafruit_MFM_Floppy::writeBlock(uint32_t block, const uint8_t *src) {
  Serial.printf("Writing block %d\n", block);
  (void *)src;
  return false;
}

bool Adafruit_MFM_Floppy::syncBlocks() {
  return false;
}

bool Adafruit_MFM_Floppy::readBlocks(uint32_t block, uint8_t *dst,
                                        size_t nb) {
  // read each block one by one
  for (size_t blocknum=0; blocknum<nb; blocknum++) {
    if (!readBlock(block+blocknum, dst + (blocknum*MFM_BYTES_PER_SECTOR)))
      return false;
  }
  return true;
}

bool Adafruit_MFM_Floppy::writeBlocks(uint32_t block, const uint8_t *src,
                                         size_t nb) {
  Serial.printf("Writing %d blocks %d\n", nb, block);
  (void *)src;
  return false;
}
