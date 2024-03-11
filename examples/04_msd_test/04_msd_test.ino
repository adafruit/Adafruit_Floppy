// this example makes a lot of assumptions: MFM floppy which is already inserted
// and only reading is supported - no write yet!

#include <Adafruit_Floppy.h>
#include "Adafruit_TinyUSB.h"

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
#define USE_GFX (1)
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RESET);
#define TFT_INIT() do { \
  tft.init(240, 240); \
  pinMode(TFT_BACKLIGHT, OUTPUT); \
  digitalWrite(TFT_BACKLIGHT, 1); \
  tft.fillScreen(0); \
  tft.setTextSize(3); \
  tft.setTextColor(~0, 0); \
  tft.setCursor(3, 3); \
  tft.println("Floppsy MSD"); \
  Serial.println("TFTinit!"); \
} while(0)
#else
#error "Please set up pin definitions!"
#endif

#ifndef USE_TINYUSB
#error "Please set Adafruit TinyUSB under Tools > USB Stack"
#endif

#if USE_GFX
#include "Adafruit_GFX.h"
#define IF_GFX(...) do { __VA_ARGS__ } while(0)
#else
#define IF_GFX(...) ((void)0)
#endif

Adafruit_USBD_MSC usb_msc;

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);

// You can select IBMPC1440K or IBMPC360K (check adafruit_floppy_disk_t options!)
auto FLOPPY_TYPE = AUTODETECT;
Adafruit_MFM_Floppy mfm_floppy(&floppy, FLOPPY_TYPE);


constexpr size_t SECTOR_SIZE = 512UL;

void setup() {
  TFT_INIT();
  Serial.begin(115200);

#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, HIGH);
#endif

#if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
  // Manual begin() is required on core without built-in support for TinyUSB such as
  // - mbed rp2040
  TinyUSB_Device_Init(0);
#endif

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Adafruit", "Floppy Mass Storage", "1.0");

  // Set disk size
  usb_msc.setCapacity(0, SECTOR_SIZE);
  // Set callbacks
  usb_msc.setReadyCallback(0, msc_ready_callback);
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);

  // floppy.debug_serial = &Serial;
  // Set Lun ready
  usb_msc.setUnitReady(false);
  Serial.println("Ready!");
  usb_msc.begin();

  Serial.println("serial Ready!");

  floppy.begin();
  if (! mfm_floppy.begin()) {
    Serial.println("Failed to spin up motor & find index pulse");
    mfm_floppy.removed();
  }

  IF_GFX(tft.fillScreen(0););
}

uint32_t index_time, last_update_time;
bool index_ui_state, index_ui_state_delayed;
volatile bool update_queued;

#if USE_GFX
const char spinner[4] = {' ', '.', 'o', 'O'};
size_t i_spin;

void update_display() {
  noInterrupts();
  auto trk0 = floppy.get_track0_sense();
  auto wp = floppy.get_write_protect();
  auto ind = digitalRead(INDEX_PIN);
  auto rdy = digitalRead(READY_PIN);
  auto dirty = mfm_floppy.dirty();
  int x = 3;
  int y = 3;
  tft.setCursor(x, y);
  tft.printf("%s %s %s",
      trk0 ? "TRK0" : "    ",
      wp ? "R/O" : "   ",
      rdy ? "RDY" : index_ui_state ? "IDX" : "   "
  );

  y += 24;
  tft.setCursor(x, y);
  if (mfm_floppy.sectorCount() == 0) {
      tft.printf("NO MEDIA ");
      y += 24;
      tft.setCursor(x, y);
      tft.printf("         ");
  } else {
      tft.printf("%4d KiB ", mfm_floppy.sectorCount()/2);
      y += 24;
      tft.setCursor(x, y);
      if (floppy.track() == -1) {
          tft.printf("T:?? S:? ");
      } else {
          tft.printf("T:%02d S:%d ",
            floppy.track(),
            floppy.get_side()
          );
      }
  }
  y += 24;
  tft.setCursor(x, y);
  tft.printf("%s %c",
    dirty ? "dirty" : "     ", spinner[i_spin++ % std::size(spinner)]);
  interrupts();
}
void maybe_update_display(uint32_t now) {
    noInterrupts();
    auto do_update = update_queued || (now - last_update_time) > 500;
    update_queued = false;
    interrupts();

    if (do_update) {
        update_display();
        last_update_time = now;
    } else {
        yield();
    }
}

void queue_update_display() {
}

#else
void maybe_update_display(uint32_t now) {
}

void queue_update_display() {}

#endif

bool index_delayed, ready_delayed;
void loop() {
  uint32_t now = millis();
  bool index = !digitalRead(INDEX_PIN);
  bool ready = digitalRead(READY_PIN);

  // ready pin fell: media ejected
  if (!ready && ready_delayed) {
    Serial.println("removed");
    mfm_floppy.removed();
    queue_update_display();
  }
  if (index && !index_delayed) {
    index_time = now;
    if (mfm_floppy.sectorCount() == 0) {
        Serial.println("inserted");
        mfm_floppy.inserted(FLOPPY_TYPE);
        queue_update_display();
    }
  }

  index_ui_state = (now - index_time) < 400;
  if (index_ui_state != index_ui_state_delayed) {
        queue_update_display();
  }

  index_ui_state_delayed = index_ui_state;
  index_delayed = index;
  ready_delayed = ready;

  maybe_update_display(now);
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_callback (uint32_t lba, void* buffer, uint32_t bufsize)
{
  //Serial.printf("read call back block %d size %d\r\n", lba, bufsize);
  auto result = mfm_floppy.readSectors(lba, reinterpret_cast<uint8_t*>(buffer), bufsize / MFM_BYTES_PER_SECTOR);
  queue_update_display();
  return result ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_callback (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  //Serial.printf("write call back block %d size %d\r\n", lba, bufsize);
  auto sectors =  bufsize / MFM_BYTES_PER_SECTOR;
  auto result = mfm_floppy.writeSectors(lba, buffer, sectors);
  if (result) {
    if (lba == 0 || (lba + sectors) == mfm_floppy.sectorCount()) {
      // If writing the first or last sector,
      mfm_floppy.syncDevice();
    }
  }
  queue_update_display();
  return result ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_callback (void)
{
  Serial.print("flush\r\n");
  mfm_floppy.syncDevice();
  queue_update_display();
  // nothing to do
}

bool msc_ready_callback (void)
{
//Serial.printf("ready callback -> %d\r\n", mfm_floppy.sectorCount());
auto sectors = mfm_floppy.sectorCount();
  usb_msc.setCapacity(sectors, SECTOR_SIZE);
  return sectors != 0;
}
