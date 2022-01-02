#include <Adafruit_Floppy.h>

// If using SAMD51, turn on TINYUSB USB stack
#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
  #define DENSITY_PIN  5     // IDC 2
  #define INDEX_PIN    6     // IDC 8
  #define SELECT_PIN   A5    // IDC 12
  #define MOTOR_PIN    9     // IDC 16
  #define DIR_PIN     10     // IDC 18
  #define STEP_PIN    11     // IDC 20
  #define WRDATA_PIN  -1     // IDC 22 (not used during read)
  #define WRGATE_PIN  -1     // IDC 24 (not used during read)
  #define TRK0_PIN    A4     // IDC 26
  #define PROT_PIN    A3     // IDC 28
  #define READ_PIN    12     // IDC 30
  #define SIDE_PIN    A1     // IDC 32
  #define READY_PIN   A0     // IDC 34
#elif defined (ARDUINO_ADAFRUIT_FEATHER_RP2040)
  #define DENSITY_PIN  7     // IDC 2
  #define INDEX_PIN    8     // IDC 8
  #define SELECT_PIN  25     // IDC 12
  #define MOTOR_PIN    9     // IDC 16
  #define DIR_PIN     10     // IDC 18
  #define STEP_PIN    11     // IDC 20
  #define WRDATA_PIN  -1     // IDC 22 (not used during read)
  #define WRGATE_PIN  -1     // IDC 24 (not used during read)
  #define TRK0_PIN    24     // IDC 26
  #define PROT_PIN    A3     // IDC 28
  #define READ_PIN    12     // IDC 30
  #define SIDE_PIN    A1     // IDC 32
  #define READY_PIN   A0     // IDC 34
#else
#error "Please set up pin definitions!"
#endif

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

// WARNING! there are 150K max flux pulses per track!
uint8_t flux_transitions[MAX_FLUX_PULSE_PER_TRACK];

uint32_t time_stamp = 0;


void setup() {
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
  uint32_t captured_flux = floppy.capture_track(flux_transitions, sizeof(flux_transitions));
 
  Serial.print("Captured ");
  Serial.print(captured_flux);
  Serial.println(" flux transitions");

  //floppy.print_pulses(flux_transitions, captured_flux);
  floppy.print_pulse_bins(flux_transitions, captured_flux, 255);
  
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