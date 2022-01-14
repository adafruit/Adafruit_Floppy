#include <Adafruit_Floppy.h>

Adafruit_MFM_Floppy::Adafruit_MFM_Floppy(Adafruit_Floppy *floppy) {
  _floppy = floppy;
}

bool Adafruit_MFM_Floppy::begin(void) {
  if (!_floppy) return false;
  _floppy->begin();
  _floppy->select(true);
  return _floppy->spin_motor(true);
}

uint32_t Adafruit_MFM_Floppy::size(void) { 
  return (uint32_t)FLOPPY_MAX_TRACKS * FLOPPY_HEADS * MFM_SECTORS_PER_TRACK * MFM_BYTES_PER_SECTOR; 
}

//--------------------------------------------------------------------+
// SdFat BaseBlockDriver API
// A block is 512 bytes
//--------------------------------------------------------------------+
bool Adafruit_MFM_Floppy::readBlock(uint32_t block, uint8_t *dst) {
  uint8_t track = block / (FLOPPY_HEADS * MFM_SECTORS_PER_TRACK);
  uint8_t head = (block / MFM_SECTORS_PER_TRACK) % FLOPPY_HEADS;
  uint8_t subsector = block % MFM_SECTORS_PER_TRACK;
  
  //Serial.printf("\tRead request block %d\n", block);
  if ((track * FLOPPY_HEADS + head) != last_track_read) {
    // oof it is not cached!

    //Serial.printf("\tSeeking track %d head %d...", track, head);
    if (! _floppy->goto_track(track)) {
      //Serial.println("failed to seek to track");
      return false;
    }
    _floppy->side(head);
    //Serial.println("done!");
    uint32_t captured_sectors =_floppy->read_track_mfm(track_data, MFM_SECTORS_PER_TRACK, track_validity);
    (void)captured_sectors;

    /*
      Serial.print("Captured %d sectors", captured_sectors);

      Serial.print("Validity: ");
      for(size_t i=0; i<MFM_SECTORS_PER_TRACK; i++) {
        Serial.print(track_validity[i] ? "V" : "?");
      }
    */
    last_track_read = track * FLOPPY_HEADS + head;
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
