

#define WAIT_SERIAL
#define XEROX_820
// #define USE_CUSTOM_PINOUT

#if defined(USE_CUSTOM_PINOUT) && __has_include("custom_pinout.h")
#warning Using custom pinout
#include "custom_pinout.h"
#elif defined(ADAFRUIT_FEATHER_M4_EXPRESS)
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
#define NEOPIXEL_PIN PIN_NEOPIXEL
#else
#error "Please set up pin definitions!"
#endif

#if defined(NEOPIXEL_PIN)
#include <Adafruit_NeoPixel.h>

#ifndef NEOPIXEL_COUNT
#define NEOPIXEL_COUNT (1)
#endif

#ifndef NEOPIXEL_FORMAT
#define NEOPIXEL_FORMAT NEO_GRB + NEO_KHZ800
#endif

Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEOPIXEL_FORMAT);

#define STATUS_RGB(r, g, b)                                                    \
  do {                                                                         \
    strip.fill(strip.Color(r, g, b));                                          \
    strip.show();                                                              \
  } while (0)
#else
#define STATUS_RGB(r, g, b)                                                    \
  do {                                                                         \
  } while (0)
#endif

#include "drive.pio.h"
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DEBUG_ASSERT(x)                                                        \
  do {                                                                         \
    if (!(x)) {                                                                \
      Serial.printf(__FILE__ ":%d: Assert fail: " #x "\n", __LINE__);          \
    }                                                                          \
  } while (0)
#include "mfm_impl.h"

enum {
  max_flux_bits = 200000 // 300RPM (200ms rotational time), 1us bit times
};
enum { max_flux_count_long = (max_flux_bits + 31) / 32 };

// Data shared between the two CPU cores
volatile int fluxout; // side number 0/1 or -1 if no flux should be generated
volatile size_t flux_count_long =
    max_flux_count_long; // in units of uint32_ts (longs)
volatile uint32_t flux_data[2]
                           [max_flux_count_long]; // one track of flux data for
                                                  // both sides of the disk

////////////////////////////////
// Code & data for core 1
// Generate index pulses & flux
////////////////////////////////
#define FLUX_OUT_PIN (READ_PIN) // "read pin" is named from the controller's POV

PIO pio = pio0;
uint sm_fluxout, offset_fluxout;
uint sm_index_pulse, offset_index_pulse;

volatile bool early_setup_done;

void setup1() {
  while (!early_setup_done) {
  }
}

void __not_in_flash_func(loop1)() {
  static bool once;
  if (fluxout >= 0) {
    pio_sm_put_blocking(pio, sm_index_pulse,
                        4000); // ??? put index high for 4ms (out of 200ms)
    for (size_t i = 0; i < flux_count_long; i++) {
      int f = fluxout;
      if (f < 0)
        break;
      auto d = flux_data[fluxout][i];
      pio_sm_put_blocking(pio, sm_fluxout, __builtin_bswap32(d));
    }
    // terminate index pulse if ongoing
    pio_sm_exec(pio, sm_index_pulse,
                0 | offset_index_pulse); // JMP to the first instruction
  }
}

////////////////////////////////////////////////
// Code & data for core 0
// "UI", control signal handling & MFM encoding
////////////////////////////////////////////////

// Set via IRQ so must be volatile
volatile int trackno;

enum {
  max_sector_count = 18,
  mfm_io_block_size = 512,
  track_max_bytes = max_sector_count * mfm_io_block_size
};

uint8_t track_data[track_max_bytes];

void onStep() {
  auto enabled =
      !digitalRead(SELECT_PIN); // motor need not be enabled to seek tracks
  auto direction = digitalRead(DIR_PIN);
  int new_track = trackno;
  if (direction) {
    if (new_track > 0)
      new_track--;
  } else {
    if (new_track < 79)
      new_track++;
  }
  if (!enabled) {
    return;
  }
  trackno = new_track;
  digitalWrite(TRK0_PIN, trackno != 0); // active LOW
}

#if defined(PIN_CARD_CS)
#define USE_SDFAT (1)
#include "SdFat.h"
SdFat SD;
FsFile dir;
FsFile file;

struct floppy_format_info_t {
  uint8_t cylinders, sectors, sides; // number of sides may be 1 or 2
  uint16_t bit_time_ns;
  size_t flux_count_bit;
  uint8_t n; // sector size is 128<<n
  bool is_fm;
};

const struct floppy_format_info_t format_info[] = {
    {80, 18, 2, 1000, 200000, 2, false}, // 3.5" 1440kB, 300RPM
    {80, 9, 2, 2000, 100000, 2, false},  // 3.5" 720kB, 300RPM

    {80, 15, 2, 1000, 166667, 2, false}, // 5.25" 1200kB, 360RPM
    {40, 9, 2, 2000, 100000, 2, false},  // 5.25" 360kB, 300RPM

    {77, 26, 1, 2000, 80000, 0, true}, // 8" 256kB, 360RPM
};

const floppy_format_info_t *cur_format = &format_info[0];

void pio_sm_set_clk_ns(PIO pio, uint sm, uint time_ns) {
  Serial.printf("set_clk_ns %u\n", time_ns);
  float f = clock_get_hz(clk_sys) * 1e-9 * time_ns;
  int scaled_clkdiv = (int)roundf(f * 256);
  pio_sm_set_clkdiv_int_frac(pio, sm, scaled_clkdiv / 256, scaled_clkdiv % 256);
}

bool setFormat(size_t size) {
  cur_format = NULL;
  for (const auto &i : format_info) {
    auto img_size = (size_t)i.sectors * i.cylinders * i.sides * (128 << i.n);
    if (size != img_size)
      continue;
    cur_format = &i;
    if (cur_format->is_fm) {
      pio_sm_set_wrap(pio, sm_fluxout, offset_fluxout, offset_fluxout + 1);
      pio_sm_set_clk_ns(pio, sm_fluxout, i.bit_time_ns / 4);
      gpio_set_outover(FLUX_OUT_PIN, GPIO_OVERRIDE_INVERT);
    } else {
      pio_sm_set_wrap(pio, sm_fluxout, offset_fluxout, offset_fluxout + 0);
      pio_sm_set_clk_ns(pio, sm_fluxout, i.bit_time_ns);
      gpio_set_outover(FLUX_OUT_PIN, GPIO_OVERRIDE_NORMAL);
    }
    flux_count_long = (i.flux_count_bit + 31) / 32;
    return true;
  }
  return false;
}

void openNextImage() {
  bool rewound = false;
  while (true) {
    auto res = file.openNext(&dir, O_RDONLY);
    if (!res) {
      if (rewound) {
        Serial.println("No image found");
        return;
      }
      dir.rewind();
      rewound = true;
      continue;
    }
    file.printFileSize(&Serial);
    Serial.write(' ');
    file.printModifyDateTime(&Serial);
    Serial.write(' ');
    file.printName(&Serial);
    if (file.isDir()) {
      // Indicate a directory.
      Serial.println("/");
      continue;
    }
    if (setFormat(file.fileSize())) {
      Serial.printf(": Valid floppy image\n");
      return;
    } else {
      Serial.println(": Unrecognized file length\n");
    }
  }
}
#endif

void setup() {
#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, LOW); // we are emulating a floppy
#endif
#if defined(FLOPPY_ENABLE_PIN)
  pinMode(FLOPPY_ENABLE_PIN, OUTPUT);
  digitalWrite(FLOPPY_ENABLE_PIN, LOW); // do second after setting direction
#endif

  offset_fluxout = pio_add_program(pio, &fluxout_compact_program);
  sm_fluxout = pio_claim_unused_sm(pio, true);
  fluxout_compact_program_init(pio, sm_fluxout, offset_fluxout, FLUX_OUT_PIN,
                               1000);

  offset_index_pulse = pio_add_program(pio, &index_pulse_program);
  sm_index_pulse = pio_claim_unused_sm(pio, true);
  index_pulse_program_init(pio, sm_index_pulse, offset_index_pulse, INDEX_PIN,
                           1000);
  early_setup_done = true;

  pinMode(DIR_PIN, INPUT_PULLUP);
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(SIDE_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, INPUT_PULLUP);
  pinMode(SELECT_PIN, INPUT_PULLUP);
  pinMode(TRK0_PIN, OUTPUT);
  pinMode(READY_PIN, OUTPUT);
  digitalWrite(READY_PIN, HIGH); // active low
#if defined(PROT_PIN)
  pinMode(PROT_PIN, OUTPUT);
  digitalWrite(PROT_PIN, LOW); // always write-protected, no write support
#endif
#if defined(DISKCHANGE_PIN)
  pinMode(DISKCHANGE_PIN, INPUT_PULLUP);
#endif

  Serial.begin(115200);
#if defined(WAIT_SERIAL)
  while (!Serial) {
  }
  Serial.println("Serial connected");
#endif

#if defined(XEROX_820)
  pinMode(DENSITY_PIN, OUTPUT);
  digitalWrite(DENSITY_PIN,
               HIGH); // Xerox 820 density select HIGH means 8" floppy
  pinMode(READY_PIN, OUTPUT);
  digitalWrite(READY_PIN, LOW); // Drive always reports readiness
  Serial.println("Configured for Xerox 820 8\" floppy emulation");
#endif

  attachInterrupt(digitalPinToInterrupt(STEP_PIN), onStep, FALLING);

#if defined(NEOPIXEL_PIN)
  strip.begin();
#endif

#if USE_SDFAT
  if (!SD.begin(PIN_CARD_CS)) {
    Serial.println("SD card initialization failed");
    STATUS_RGB(255, 0, 0);
    delay(2000);
  } else if (!dir.open("/")) {
    Serial.println("SD card directory could not be read");
    STATUS_RGB(255, 255, 0);
    delay(2000);
  } else {
    STATUS_RGB(0, 0, 255);
    openNextImage();
  }
#endif
}

static void encode_track(uint8_t head, uint8_t cylinder) {

  mfm_io_t io = {
      .encode_compact = true,
      .pulses = (uint8_t *)flux_data[head],
      .n_pulses = flux_count_long * sizeof(long),
      .sectors = track_data,
      .n_sectors = cur_format->sectors,
      .head = head,
      .cylinder = cylinder,
      .n = cur_format->n,
      .settings = cur_format->is_fm ? &standard_fm : &standard_mfm,
  };

  size_t pos = encode_track_mfm(&io);
  Serial.printf("Encoded to %zu flux\n", pos);
}

// As an easter egg, the dummy disk image embeds the boot sector Tetris
// implementation from https://github.com/daniel-e/tetros (source available
// under MIT license)
const uint8_t tetros[] = {
#include "tetros.h"
};

static void make_dummy_data(uint8_t head, uint8_t cylinder, size_t n_bytes) {
  uint8_t dummy_byte = head * 2 + cylinder;
  std::fill(track_data, track_data + n_bytes, dummy_byte);
  if (head == 0 && cylinder == 0 && n_bytes >= 512) {
    Serial.println("Injecting tetros in boot sector");
    std::copy(tetros, std::end(tetros), track_data);
  }
}

void loop() {
  static int cached_trackno = -1;
  auto new_trackno = trackno;
  int motor_pin = !digitalRead(MOTOR_PIN);
  int select_pin = !digitalRead(SELECT_PIN);
  int side = !digitalRead(SIDE_PIN);

#if defined(XEROX_820)
  // no separate motor pin on this baby
  motor_pin = true;
  // only one side
  side = 0;
#endif

  auto enabled = motor_pin && select_pin;
  static bool old_enabled = false, old_select_pin = false,
              old_motor_pin = false;

  if (motor_pin != old_motor_pin) {
    Serial.printf("motor_pin -> %s\n", motor_pin ? "true" : "false");
    old_motor_pin = motor_pin;
  }
  if (select_pin != old_select_pin) {
    Serial.printf("select_pin -> %s\n", select_pin ? "true" : "false");
    old_select_pin = select_pin;
  }

  if (enabled != old_enabled) {
    Serial.printf("enabled -> %s\n", enabled ? "true" : "false");
    old_enabled = enabled;
  }
#if defined(DISKCHANGE_PIN) && USE_SDFAT
  int diskchange_pin = digitalRead(DISKCHANGE_PIN);
  static int diskchange_pin_delayed = false;
  auto diskchange = diskchange_pin_delayed && !diskchange_pin;
  diskchange_pin_delayed = diskchange_pin;
  if (diskchange) {
    delay(20);
    while (!digitalRead(DISKCHANGE_PIN)) { /* NOTHING */
    }
    fluxout = -1;
    cached_trackno = -1;
    openNextImage();
  }
#endif

  if (cur_format && new_trackno != cached_trackno) {
    STATUS_RGB(0, 255, 255);
    fluxout = -1;
    Serial.printf("Preparing flux data for track %d\n", new_trackno);
    int sector_count = cur_format->sectors;
    int side_count = cur_format->sides;
    int sector_size = 128 << cur_format->n;
    size_t offset = sector_size * sector_count * side_count * new_trackno;
    size_t count = sector_size * sector_count;
    int dummy_byte = new_trackno * side_count;
#if USE_SDFAT
    file.seek(offset);
    for (auto side = 0; side < side_count; side++) {
      int n = file.read(track_data, count);
      if (n != count) {
        Serial.println("Read failed -- using dummy data");
        make_dummy_data(side, new_trackno, count);
      }
      encode_track(side, new_trackno);
    }
#else
    Serial.println("No filesystem - using dummy data");
    for (auto side = 0; side < side_count; side++) {
      make_dummy_data(side, new_trackno, count);
      encode_track(side, new_trackno);
    }
#endif

    Serial.println("flux data prepared");
    cached_trackno = new_trackno;
  }
  fluxout =
      (cur_format != NULL && enabled && cached_trackno == trackno) ? side : -1;
#if defined(NEOPIXEL_PIN)
  if (fluxout >= 0) {
    STATUS_RGB(0, 1, 0);
  } else {
    STATUS_RGB(0, 0, 0);
  }
#endif

  // this is not correct handling of the ready/disk change flag. on my test
  // computer, just leaving the pin HIGH works, while immediately reporting LOW
  // on the "ready / disk change:
#if 0
  digitalWrite(READY_PIN, !motor_pin);
#endif
}
