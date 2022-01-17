#include <Adafruit_Floppy.h>



// If using SAMD51, turn on TINYUSB USB stack
#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
  #define DENSITY_PIN  A0    // IDC 2
  #define INDEX_PIN    A1    // IDC 8
  #define SELECT_PIN   A2    // IDC 12
  #define MOTOR_PIN    A3    // IDC 16
  #define DIR_PIN      A4    // IDC 18
  #define STEP_PIN     A5    // IDC 20
  #define WRDATA_PIN   13    // IDC 22 (not used during read)
  #define WRGATE_PIN   12    // IDC 24 (not used during read)
  #define TRK0_PIN     11    // IDC 26
  #define PROT_PIN     10    // IDC 28
  #define READ_PIN      9    // IDC 30
  #define SIDE_PIN      6    // IDC 32
  #define READY_PIN     5    // IDC 34
#if F_CPU != 180000000L
  #warning "please set CPU speed to 180MHz overclock"
#endif
#elif defined (ARDUINO_ADAFRUIT_FEATHER_RP2040)
  #define DENSITY_PIN  A0    // IDC 2
  #define INDEX_PIN    A1    // IDC 8
  #define SELECT_PIN   A2    // IDC 12
  #define MOTOR_PIN    A3    // IDC 16
  #define DIR_PIN      24    // IDC 18
  #define STEP_PIN     25    // IDC 20
  #define WRDATA_PIN   13    // IDC 22 (not used during read)
  #define WRGATE_PIN   12    // IDC 24 (not used during read)
  #define TRK0_PIN     11    // IDC 26
  #define PROT_PIN     10    // IDC 28
  #define READ_PIN      9    // IDC 30
  #define SIDE_PIN      8    // IDC 32
  #define READY_PIN     7    // IDC 34
#if F_CPU != 200000000L
  #warning "please set CPU speed to 200MHz overclock"
#endif
#elif defined (ARDUINO_RASPBERRY_PI_PICO)
  #define DENSITY_PIN  2     // IDC 2
  #define INDEX_PIN    3     // IDC 8
  #define SELECT_PIN   4     // IDC 12
  #define MOTOR_PIN    5     // IDC 16
  #define DIR_PIN      6     // IDC 18
  #define STEP_PIN     7     // IDC 20
  #define WRDATA_PIN   8     // IDC 22 (not used during read)
  #define WRGATE_PIN   9     // IDC 24 (not used during read)
  #define TRK0_PIN    10     // IDC 26
  #define PROT_PIN    11     // IDC 28
  #define READ_PIN    12     // IDC 30
  #define SIDE_PIN    13     // IDC 32
  #define READY_PIN   14     // IDC 34
#if F_CPU != 200000000L
  #warning "please set CPU speed to 200MHz overclock"
#endif
#else
#error "Please set up pin definitions!"
#endif

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

// You can select IBMPC1440K or IBMPC360K (check adafruit_floppy_disk_t options!)
Adafruit_MFM_Floppy mfm_floppy(&floppy, IBMPC360K);


uint32_t time_stamp = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);      
  while (!Serial) delay(100);

  delay(500); // wait for serial to open
  Serial.println("its time for a nice floppy transfer!");

  floppy.debug_serial = &Serial;

  if (! mfm_floppy.begin()) {
    Serial.println("Failed to spin up motor & find index pulse");
    while (1) yield();
  }
}

uint8_t track = 0;
bool head = 0;
void loop() {
  int32_t captured_sectors;
  
  Serial.printf("Seeking track %d head %d\n", track, head);
  captured_sectors = mfm_floppy.readTrack(track, head);
  if (captured_sectors < 0) {
    Serial.println("Failed to seek to track");
    while (1) yield();
  }
 
  Serial.printf("Captured %d sectors\n", captured_sectors);

  Serial.print("Validity: ");
  for(size_t i=0; i < mfm_floppy.sectors_per_track(); i++) {
    Serial.print(mfm_floppy.track_validity[i] ? "V" : "?");
  }
  Serial.print("\n");
  for(size_t sector=0; sector < mfm_floppy.sectors_per_track(); sector++) {
    if (!mfm_floppy.track_validity[sector]) {
      continue; // skip it, not valid
    }
    for(size_t i=0; i<512; i+=16) {
      size_t addr = sector * 512 + i;
      Serial.printf("%08x", addr);
      for(size_t j=0; j<16; j++) {
         Serial.printf(" %02x", mfm_floppy.track_data[addr+j]);
      }
      Serial.print(" | ");
      for(size_t j=0; j<16; j++) {
        uint8_t d = mfm_floppy.track_data[addr+j];
        if (! isprint(d)) {
          d = ' ';
        }
        Serial.write(d);
      }
      Serial.print("\n");
    }
  }

  // advance to next track
  if (!head) {  // we were on side 0
    head = 1;   // go to side 1
  } else {      // we were on side 1?
    track = (track + 1) % mfm_floppy.tracks_per_side(); // next track!
    head = 0;   // and side 0
  }
  
  delay(1000);
}