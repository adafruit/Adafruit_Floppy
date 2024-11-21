#include <Adafruit_Floppy.h>

#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
#define DENSITY_PIN A1 // IDC 2
#define INDEX_PIN A5   // IDC 8
#define SELECT_PIN A0  // IDC 12
#define MOTOR_PIN A2   // IDC 16
#define DIR_PIN A3     // IDC 18
#define STEP_PIN A4    // IDC 20
#define WRDATA_PIN 13  // IDC 22
#define WRGATE_PIN 12  // IDC 24
#define TRK0_PIN 10    // IDC 26
#define PROT_PIN 11    // IDC 28
#define READ_PIN 9     // IDC 30
#define SIDE_PIN 6     // IDC 32
#define READY_PIN 5    // IDC 34
#elif defined(ARDUINO_ADAFRUIT_FEATHER_RP2040)
#define DENSITY_PIN A1 // IDC 2
#define INDEX_PIN 25   // IDC 8
#define SELECT_PIN A0  // IDC 12
#define MOTOR_PIN A2   // IDC 16
#define DIR_PIN A3     // IDC 18
#define STEP_PIN 24    // IDC 20
#define WRDATA_PIN 13  // IDC 22
#define WRGATE_PIN 12  // IDC 24
#define TRK0_PIN 10    // IDC 26
#define PROT_PIN 11    // IDC 28
#define READ_PIN 9     // IDC 30
#define SIDE_PIN 8     // IDC 32
#define READY_PIN 7    // IDC 34
#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
#define DENSITY_PIN 2 // IDC 2
#define INDEX_PIN 3   // IDC 8
#define SELECT_PIN 4  // IDC 12
#define MOTOR_PIN 5   // IDC 16
#define DIR_PIN 6     // IDC 18
#define STEP_PIN 7    // IDC 20
#define WRDATA_PIN 8  // IDC 22 (not used during read)
#define WRGATE_PIN 9  // IDC 24 (not used during read)
#define TRK0_PIN 10   // IDC 26
#define PROT_PIN 11   // IDC 28
#define READ_PIN 12   // IDC 30
#define SIDE_PIN 13   // IDC 32
#define READY_PIN 14  // IDC 34
#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif
#elif defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2040)
// Yay built in pin definitions!
#else
#error "Please set up pin definitions!"
#endif


Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

// You can select IBMPC1440K or IBMPC360K (check adafruit_floppy_disk_t options!)
Adafruit_MFM_Floppy mfm_floppy(&floppy, IBMPC1440K);


uint32_t time_stamp = 0;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  while (!Serial) delay(100);

#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, HIGH);
#endif
#if defined(FLOPPY_ENABLE_PIN)
  pinMode(FLOPPY_ENABLE_PIN, OUTPUT);
  digitalWrite(FLOPPY_ENABLE_PIN, LOW); // do second after setting direction
#endif

  delay(500); // wait for serial to open
  Serial.println("its time for a nice floppy transfer!");

  floppy.debug_serial = &Serial;

  if (! mfm_floppy.begin()) {
    Serial.println("Failed to spin up motor & find index pulse");
    while (1) yield();
  }
}

void hexdump(size_t offset, const uint8_t *data, size_t n) {
  for (size_t i = 0; i < n; i += 16) {
    size_t addr = offset + i;
    Serial.printf("%08x", addr);
    for (size_t j = 0; j < 16; j++) {
      if(i+j > n) Serial.printf("   ");else
      Serial.printf(" %02x", mfm_floppy.track_data[addr + j]);
    }
    Serial.print(" | ");
    for (size_t j = 0; j < 16; j++) {
      if(i+j > n) break;
      uint8_t d = mfm_floppy.track_data[addr + j];
      if (! isprint(d)) {
        d = ' ';
      }
      Serial.write(d);
    }
    Serial.print("\n");
  }
}

uint8_t track = 0;
bool head = 0;
int i = 0;
void loop() {
  int32_t captured_sectors;

  uint8_t sector[512];
  int lba = (i++ % 2 == 0) ? 0 : 18;
  if (!mfm_floppy.readSector(lba, sector)) {
    Serial.println("Failed to read sector");
    return;
  }

  hexdump(lba * 512, sector, 512);

  memset(sector, 0, 512);
  snprintf(reinterpret_cast<char*>(sector), sizeof(sector), "Hello from iteration %zd of Adafruit Floppy MFM writing\n", i);

  if (!mfm_floppy.writeSector(lba, sector)) {
    Serial.println("Failed to write sectorn");
    return;
  }
  if (!mfm_floppy.syncDevice()) {
    Serial.println("Failed to sync device");
    return;
  }

  delay(1000);
}
