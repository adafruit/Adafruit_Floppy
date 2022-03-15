#include <Adafruit_Floppy.h>

#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
#define ENABLE_PIN (6)
#define PHASE1_PIN (A2)
#define PHASE2_PIN (13)
#define PHASE3_PIN (12)
#define PHASE4_PIN (11)
#define RDDATA_PIN (5)
#define INDEX_PIN  (A3)
#define APPLE2_PROTECT_PIN (21) // "SDA"
#elif defined (ARDUINO_ADAFRUIT_FEATHER_RP2040)
#define ENABLE_PIN (8)  // D6
#define PHASE1_PIN (A2)
#define PHASE2_PIN (13)
#define PHASE3_PIN (12)
#define PHASE4_PIN (11)
#define RDDATA_PIN (7)  // D5
#define INDEX_PIN  (A3)
#define APPLE2_PROTECT_PIN (2) // "SDA"
#else
#error "Please set up pin definitions!"
#endif

Adafruit_Apple2Floppy floppy(INDEX_PIN, ENABLE_PIN,
                             PHASE1_PIN, PHASE2_PIN, PHASE3_PIN, PHASE4_PIN,
                             -1, -1, APPLE2_PROTECT_PIN, RDDATA_PIN);

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
  int32_t index_pulse_offset;
  uint32_t captured_flux = floppy.capture_track(flux_transitions, sizeof(flux_transitions), &index_pulse_offset, true);

  Serial.print("Captured ");
  Serial.print(captured_flux);
  Serial.println(" flux transitions");

  //floppy.print_pulses(flux_transitions, captured_flux);
  floppy.print_pulse_bins(flux_transitions, captured_flux, 255, true);

  Serial.printf("Write protect: %s\n", floppy.get_write_protect() ? "ON" : "off");

  delay(100);
}
