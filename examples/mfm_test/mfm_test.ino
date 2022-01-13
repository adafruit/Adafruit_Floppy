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

constexpr size_t N_SECTORS = 18;
uint8_t track_data[512*N_SECTORS];

uint32_t time_stamp = 0;

void setup() {
pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);      
  while (!Serial) delay(100);

  Serial.println("its time for a nice floppy transfer!");

  floppy.debug_serial = &Serial;
  floppy.begin();

  floppy.select(true);
  if (! floppy.spin_motor(true)) {
    Serial.println("Failed to spin up motor & find index pulse");
    while (1) yield();
  }

  Serial.print("Seeking track...");
  if (! floppy.goto_track(0)) {
    Serial.println("Failed to seek to track");
    while (1) yield();
  }
  Serial.println("done!");
}

void loop() {
    uint8_t validity[N_SECTORS];
  uint32_t captured_sectors = floppy.read_track_mfm(track_data, N_SECTORS, validity);
 
  Serial.print("Captured ");
  Serial.print(captured_sectors);
  Serial.println(" sectors");

  Serial.print("Validity: ");
  for(int i=0; i<N_SECTORS; i++) {
    Serial.print(validity[i] ? "V" : "?");
  }
  Serial.print("\n");

  if(validity[0]) {
      for(int i=0; i<512; i+=32) {
        Serial.printf("%04x", i);
        for(int j=0; j<32; j++) {
           Serial.printf(" %02x", track_data[i+j]);
        }
        Serial.print("\n");
    }
  }

  if ((millis() - time_stamp) > 1000) {
    Serial.print("Ready? ");
    Serial.println(digitalRead(READY_PIN) ? "No" : "Yes");
    Serial.print("Write Protected? "); 
    Serial.println(digitalRead(PROT_PIN) ? "No" : "Yes");
    Serial.print("Track 0? ");
    Serial.println(digitalRead(TRK0_PIN) ? "No" : "Yes");
    time_stamp = millis();
  }
  yield();
}
