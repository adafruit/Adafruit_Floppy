// this example makes a lot of assumptions: MFM floppy which is already inserted
// and only reading is supported - no write yet!

#include "Adafruit_TinyUSB.h"
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
#elif defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2040)
// Yay built in pin definitions!
// enable the display, though!
#include "display_floppsy.h"
#else
#error "Please set up pin definitions!"
#endif

#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif

Adafruit_USBD_MSC usb_msc;

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN, MOTOR_PIN, DIR_PIN,
                       STEP_PIN, WRDATA_PIN, WRGATE_PIN, TRK0_PIN, PROT_PIN,
                       READ_PIN, SIDE_PIN, READY_PIN);

// You can select IBMPC1440K or IBMPC360K (check adafruit_floppy_disk_t
// options!)
auto FLOPPY_TYPE = AUTODETECT;
Adafruit_MFM_Floppy mfm_floppy(&floppy, FLOPPY_TYPE);

// To make a display on another board, check out "display_floppsy.h"; adapt it
// to your board & include it
#if defined(HAVE_DISPLAY)
#include "display_common.h"
#else
#include "display_none.h"
#endif

constexpr size_t SECTOR_SIZE = 512UL;

void setup() {
  Serial.begin(115200);

#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, HIGH);
#endif

#if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
  // Manual begin() is required on core without built-in support for TinyUSB
  // such as
  // - mbed rp2040
  TinyUSB_Device_Init(0);
#endif

  // Set disk vendor id, product id and revision with string up to 8, 16, 4
  // characters respectively
  usb_msc.setID("Adafruit", "Floppy Mass Storage", "1.0");

  // Set disk size
  usb_msc.setCapacity(0, SECTOR_SIZE);
  // Set callbacks
  usb_msc.setReadyCallback(0, msc_ready_callback);
  usb_msc.setWritableCallback(0, msc_writable_callback);
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback,
                               msc_flush_callback);

  // floppy.debug_serial = &Serial;
  // Set Lun ready
  usb_msc.setUnitReady(false);
  Serial.println("Ready!");
  usb_msc.begin();

  Serial.println("serial Ready!");

  init_display();

  floppy.begin();
  attachInterrupt(digitalPinToInterrupt(INDEX_PIN), count_index, FALLING);
  if (!mfm_floppy.begin()) {
    Serial.println("Failed to spin up motor & find index pulse");
    mfm_floppy.removed();
  }
}

volatile uint32_t flush_time;

volatile uint32_t index_count;
void count_index() { index_count += 1; }

bool index_delayed, ready_delayed;
uint32_t old_index_count;
void loop() {
  uint32_t now = millis();
  bool index = !digitalRead(INDEX_PIN);
  bool ready = digitalRead(READY_PIN);

  noInterrupts();
  auto new_index_count = index_count;
  interrupts();

  if (mfm_floppy.dirty() && now > flush_time) {
    noInterrupts();
    mfm_floppy.syncDevice();
    interrupts();
  }

  // ready pin fell: media ejected
  if (!ready && ready_delayed) {
    Serial.println("removed");
    mfm_floppy.removed();
  }
  if (new_index_count != old_index_count) {
    if (mfm_floppy.sectorCount() == 0) {
      Serial.println("inserted");
      mfm_floppy.inserted(FLOPPY_TYPE);
    }
  }

  maybe_update_display(false, new_index_count != old_index_count);
  ready_delayed = ready;
  old_index_count = new_index_count;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_callback(uint32_t lba, void *buffer, uint32_t bufsize) {
  // Serial.printf("read call back block %d size %d\r\n", lba, bufsize);
  auto result = mfm_floppy.readSectors(lba, reinterpret_cast<uint8_t *>(buffer),
                                       bufsize / MFM_BYTES_PER_SECTOR);
  return result ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_callback(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  // Serial.printf("write call back block %d size %d\r\n", lba, bufsize);
  auto sectors = bufsize / MFM_BYTES_PER_SECTOR;
  auto result = mfm_floppy.writeSectors(lba, buffer, sectors);
  if (result) {
    flush_time = millis() + 200;
    if (lba == 0 || (lba + sectors) == mfm_floppy.sectorCount()) {
      // If writing the first or last sector,
      mfm_floppy.syncDevice();
    }
  }
  return result ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and
// accepted by host). used to flush any pending cache.
void msc_flush_callback(void) {
  Serial.print("flush\r\n");
  mfm_floppy.syncDevice();
  // nothing to do
}

bool msc_ready_callback(void) {
  // Serial.printf("ready callback -> %d\r\n", mfm_floppy.sectorCount());
  auto sectors = mfm_floppy.sectorCount();
  usb_msc.setCapacity(sectors, SECTOR_SIZE);
  return sectors != 0;
}

bool msc_writable_callback(void) { return !floppy.get_write_protect(); }
