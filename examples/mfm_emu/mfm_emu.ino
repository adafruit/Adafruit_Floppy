volatile int trackno;
int cached_trackno;

enum { track_max_bits = 200000 };

uint32_t track_data[track_max_bits / 32 + 2];

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool appUsing = false;

// Called by FatFSUSB when the drive is released.  We note this, restart FatFS, and tell the main loop to rescan.
void unplug(uint32_t i) {
  (void) i;
  driveConnected = false;
  updated = true;
  FatFS.begin();
}

// Called by FatFSUSB when the drive is mounted by the PC.  Have to stop FatFS, since the drive data can change, note it, and continue.
void plug(uint32_t i) {
  (void) i;
  driveConnected = true;
  FatFS.end();
}

// Called by FatFSUSB to determine if it is safe to let the PC mount the USB drive.  If we're accessing the FS in any way, have any Files open, etc. then it's not safe to let the PC mount the drive.
bool mountable(uint32_t i) {
  (void) i;
  return !appUsing;
}

void stepped() {
    int direction = digitalRead(DIRECTION_PIN);
    int new_track = trackno;
    if(direction == STEP_IN) {
        if (new_track > 0) new_track--;
    } else {
        if (new_track < 79) new_track++;
    }
    trackno = new_track;
    digitalWrite(INDEX_PIN, trackno != 0); // active LOW
}

void setup() {
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), step_interrupt, FALLING);

  Serial.begin(115200);                                                                      
  while (!Serial) {                                                                          
    delay(1);                                                                                
  }                                                                                          
  delay(5000);                                                                               
                                                                                             
  if (!FatFS.begin()) {                                                                      
    Serial.println("FatFS initialization failed!");                                          
    while (1) {                                                                              
      delay(1);                                                                              
    }                                                                                        
  }
  Serial.println("FatFS initialization done.");

  FatFSUSB.onUnplug(unplug);
  FatFSUSB.onPlug(plug);
  FatFSUSB.driveReady(mountable);

  Serial.println("FatFSUSB started.");
  Serial.println("Connect drive via USB to upload/erase files and re-display");

}

void loop() {
}
