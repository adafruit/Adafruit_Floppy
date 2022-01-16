// this example makes a lot of assumptions: MFM floppy which is already inserted
// and only reading is supported - no write yet!

#include <Adafruit_Floppy.h>
#include "Adafruit_TinyUSB.h"

Adafruit_USBD_MSC usb_msc;

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
Adafruit_MFM_Floppy mfm_floppy(&floppy, IBMPC1440K);


constexpr size_t SECTOR_SIZE = 512UL;
int8_t last_track_read = -1;  // last cached track

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);      

#if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
  // Manual begin() is required on core without built-in support for TinyUSB such as
  // - mbed rp2040
  TinyUSB_Device_Init(0);
#endif

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Adafruit", "Floppy Mass Storage", "1.0");

    // Set disk size
  usb_msc.setCapacity(mfm_floppy.sectors_per_track() * mfm_floppy.tracks_per_side() * FLOPPY_HEADS, SECTOR_SIZE);

  // Set callback
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);

  floppy.debug_serial = &Serial;
  floppy.begin();
  // Set Lun ready
  usb_msc.setUnitReady(true);
  Serial.println("Ready!");

  usb_msc.begin();

  if (! mfm_floppy.begin()) {
    Serial.println("Failed to spin up motor & find index pulse");
    while (1) yield();
  }
}

void loop() {
  delay(1000);
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_callback (uint32_t lba, void* buffer, uint32_t bufsize)
{
  Serial.printf("read call back block %d size %d\n", lba, bufsize);

  uint8_t track = lba / (2 * mfm_floppy.sectors_per_track());
  uint8_t head = (lba / mfm_floppy.sectors_per_track()) % 2;
  uint8_t subsector = lba % mfm_floppy.sectors_per_track();

  uint8_t retries = 5;

  for (int retry=0; retry<retries; retry++) {
    if (((track * 2 + head) == last_track_read) && mfm_floppy.track_validity[subsector]) {
      // aah we've got it and its valid!
      Serial.println("OK!");
      memcpy(buffer, mfm_floppy.track_data+(subsector * SECTOR_SIZE), SECTOR_SIZE);
      return SECTOR_SIZE;
    }
    // ok so either its not valid, or we didn't read this track yet...
    int32_t tracks_read = mfm_floppy.readTrack(track, head);
    if (tracks_read < 0) {
      Serial.println("Failed to seek to track");
      return 0;
    }
    last_track_read = track * 2 + head;
    // we'll go again on the next round
  }
  Serial.println("subsector invalid CRC :(");
  return 0;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_callback (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  Serial.printf("write call back block %d size %d\n", lba, bufsize);
  // we dont actually write yet
  return bufsize;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_callback (void)
{
  Serial.println("flush\n");
  // nothing to do
}