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
  #define SIDE_PIN      6    // IDC 32
  #define READY_PIN     5    // IDC 34
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

  if (!floppy.begin()) {
    Serial.println("Failed to initialize floppy interface");
    while (1) yield();
  }

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
  uint32_t index_pulse_offset;
  uint32_t captured_flux = floppy.capture_track(flux_transitions, sizeof(flux_transitions), &index_pulse_offset, true);
 
  Serial.print("Captured ");
  Serial.print(captured_flux);
  Serial.println(" flux transitions");

  //floppy.print_pulses(flux_transitions, captured_flux);
  floppy.print_pulse_bins(flux_transitions, captured_flux, 255, true);
  
  if ((millis() - time_stamp) > 1000) {
    Serial.print("Ready? ");
    Serial.println(digitalRead(READY_PIN) ? "No" : "Yes");
    Serial.print("Write Protected? "); 
    Serial.println(digitalRead(PROT_PIN) ? "No" : "Yes");
    Serial.print("Track 0? ");
    Serial.println(digitalRead(TRK0_PIN) ? "No" : "Yes");
    time_stamp = millis();
  }

  unsigned T_2 = floppy.getSampleFrequency() * 2 / 1000000;
  unsigned T_3 = floppy.getSampleFrequency() * 3 / 1000000;
  unsigned T_4 = floppy.getSampleFrequency() * 4 / 1000000;

  for(size_t i=0; i<sizeof(flux_transitions); i+=3) {
    flux_transitions[i] = T_2;
  }
  for(size_t i=1; i<sizeof(flux_transitions); i+=3) {
    flux_transitions[i] = T_3;
  }
  for(size_t i=2; i<sizeof(flux_transitions); i+=3) {
    flux_transitions[i] = T_4;
  }

  floppy.print_pulse_bins(flux_transitions, captured_flux, 255, true);

  Serial.println("Writing track with T234234...");
  Serial.printf("T2 = %d T3 = %d T4 = %d\n", T_2, T_3, T_4);
  floppy.write_track(flux_transitions, sizeof(flux_transitions), true);

  yield();
}
