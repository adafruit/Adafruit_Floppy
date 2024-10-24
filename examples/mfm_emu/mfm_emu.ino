volatile int trackno;
int cached_trackno = -1;

#define DIRECTION_PIN (D8)
#define STEP_PIN (D9)
#define TRK0_PIN (D10)
#define INDEX_PIN (D11)
#define STEP_IN (HIGH)

enum { track_max_bits = 200000 };

uint32_t track_data[track_max_bits / 32 + 2];

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool appUsing = false;

#if 0
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
#endif

void stepped() {
    auto direction = digitalRead(DIRECTION_PIN);
    int new_track = trackno;
    if(direction == STEP_IN) {
        if (new_track > 0) new_track--;
    } else {
        if (new_track < 79) new_track++;
    }
    trackno = new_track;
    digitalWrite(TRK0_PIN, trackno != 0); // active LOW
}

void setup() {
  pinMode(DIRECTION_PIN, INPUT_PULLUP);
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(TRK0_PIN, OUTPUT);
  pinMode(INDEX_PIN, OUTPUT);

  
  Serial.begin(115200);                                                                      
  while (!Serial) {                                                                          
    delay(1);                                                                                
  }                                                                                          
  delay(5000);                                                                               
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), stepped, FALLING);
        Serial.println("set digital interrupt or tried anyway");
#if 0
                                                                                             
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
#endif

}

void loop() {
    auto tmp = trackno;
    if(tmp != cached_trackno) {
        Serial.printf("Stepped to track %d\n", tmp);
        cached_trackno = tmp;
    }    
// Serial.printf("%2d\r", (int)digitalRead(STEP_PIN));
}
