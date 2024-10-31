#include "drive.pio.h"
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DEBUG_ASSERT(x)                                                        \
  do {                                                                         \
    if (!(x)) {                                                                \
      Serial.printf(__FILE__ ":%d: Assert fail: " #x "\n", __LINE__);          \
    }                                                                          \
  } while (0)
#include "mfm_impl.h"

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
#else
#error "Please set up pin definitions!"
#endif

volatile int trackno;
volatile int fluxout;

/*
8 - index - D11
20 - step - D9
18 - direction - D8
30 - read data - D13
26 - trk0 - D10
*/
#define FLUX_OUT_PIN (READ_PIN) // "read pin" is named from the controller's POV
#define STEP_IN (HIGH)

enum {
  flux_max_bits = 200000
}; // 300RPM (200ms rotational time), 1us bit times
// enum { flux_max_bits = 10000 }; // 300RPM (200ms rotational time), 1us bit
// times

enum { sector_count = 18, track_max_bytes = sector_count * mfm_io_block_size };

enum { flux_count = (flux_max_bits + 31) / 32 };
uint8_t track_data[track_max_bytes];
uint32_t flux_data[2][flux_count];

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool appUsing = false;

#if defined(PIN_CARD_CS)
#define USE_SDFAT (1)
#include "SdFat.h"
SdFat SD;
#endif

void stepped() {
  auto enabled =
      !digitalRead(SELECT_PIN); // motor need not be enabled to seek tracks
  auto direction = digitalRead(DIR_PIN);
  int new_track = trackno;
  if (direction == STEP_IN) {
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

PIO pio = pio0;
uint sm_fluxout, offset_fluxout;
uint sm_index_pulse, offset_index_pulse;

void setupPio() {
  offset_fluxout = pio_add_program(pio, &fluxout_compact_program);
  sm_fluxout = pio_claim_unused_sm(pio, true);
  fluxout_compact_program_init(pio, sm_fluxout, offset_fluxout, FLUX_OUT_PIN,
                               1000);

  offset_index_pulse = pio_add_program(pio, &index_pulse_program);
  sm_index_pulse = pio_claim_unused_sm(pio, true);
  index_pulse_program_init(pio, sm_index_pulse, offset_index_pulse, INDEX_PIN,
                           1000);
}

void setup1() {
  while (!Serial) {
    delay(1);
  }
  setupPio();
}

void setup() {
  pinMode(DIR_PIN, INPUT_PULLUP);
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(SIDE_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, INPUT_PULLUP);
  pinMode(SELECT_PIN, INPUT_PULLUP);
  pinMode(TRK0_PIN, OUTPUT);
#if defined(PROT_PIN)
  pinMode(PROT_PIN, OUTPUT);
  digitalWrite(PROT_PIN, LOW); // always write-protected, no write support
#endif
  // pinMode(INDEX_PIN, OUTPUT);

#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, LOW); // we are emulating a floppy
#endif

  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }
  // delay(5000);
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), stepped, FALLING);

#if USE_SDFAT
  Serial.println("about to init sd");
  if (!SD.begin(PIN_CARD_CS)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("sd init done");

  FsFile dir;
  FsFile file;

  if (!dir.open("/")) {
    Serial.println("dir.open failed");
  }
  // Open next file in root.
  // Warning, openNext starts at the current position of dir so a
  // rewind may be necessary in your application.
  while (file.openNext(&dir, O_RDONLY)) {
    file.printFileSize(&Serial);
    Serial.write(' ');
    file.printModifyDateTime(&Serial);
    Serial.write(' ');
    file.printName(&Serial);
    if (file.isDir()) {
      // Indicate a directory.
      Serial.write('/');
    }
    Serial.println();
    file.close();
  }
#endif
}

void __not_in_flash_func(loop1)() {
  static bool once;
  if (fluxout >= 0) {
    pio_sm_put_blocking(pio, sm_index_pulse,
                        4000); // ??? put index high for 4ms (out of 200ms)
    for (size_t i=0; i<flux_count; i++) {
      int f = fluxout;
      if (f < 0) break;
      auto d = flux_data[fluxout][i];
      pio_sm_put_blocking(pio, sm_fluxout, __builtin_bswap32(d));
    }
    // terminate index pulse if ongoing
    pio_sm_exec(pio, sm_index_pulse, 0 | offset_index_pulse); // JMP to the first instruction
  }
}

static void encode_track_mfm(uint8_t head, uint8_t cylinder,
                             uint8_t n_sectors) {

  mfm_io_t io = {
      .encode_compact = true,
      .T2_max = 5,
      .T3_max = 7,
      .T1_nom = 2,
      .pulses = reinterpret_cast<uint8_t *>(flux_data[head]),
      .n_pulses = sizeof(flux_data[head]),
      //.pos = 0,
      .sectors = track_data,
      .n_sectors = n_sectors,
      //.sector_validity = NULL,
      .head = head,
      .cylinder = cylinder,
  };

  size_t pos = encode_track_mfm(&io);
  Serial.printf("encoded to %zu bits\n", pos * CHAR_BIT);
}

void loop() {
  static int cached_trackno = -1;
  auto new_trackno = trackno;
  int motor_pin = !digitalRead(MOTOR_PIN);
  int select_pin = !digitalRead(SELECT_PIN);
  int side = !digitalRead(SIDE_PIN);
  auto enabled = motor_pin && select_pin;
  if (new_trackno != cached_trackno) {
    fluxout = -1;
    Serial.printf("T%d\n", new_trackno);
    size_t offset = 512 * sector_count * 2 * new_trackno;
    size_t count = 512 * sector_count;
    int dummy_byte = new_trackno * 2;
#if USE_SDFAT
    FsFile file;
    int r = file.open("disk.img");
    file.seek(offset);
    int n = file.read(track_data, count);
    if (n != count) {
      Serial.println("Read failed -- using dummy data");
      std::fill(track_data, track_data + count, dummy_byte);
    }
    encode_track_mfm(0, new_trackno, sector_count);
    n = file.read(track_data, count);
    if (n != count) {
      Serial.println("Read failed -- using dummy data");
      std::fill(track_data, track_data + count, dummy_byte + 1);
    }
    encode_track_mfm(1, new_trackno, sector_count);
#else
    Serial.println("No filesystem - using dummy data");
    std::fill(track_data, track_data + count, dummy_byte);
    encode_track_mfm(flux_data[0], new_side, new_trackno, sector_count);
    std::fill(track_data, track_data + count, dummy_byte + 1);
    encode_track_mfm(flux_data[1], new_side, new_trackno, sector_count);
#endif

    cached_trackno = new_trackno;
  }
  fluxout = (enabled && cached_trackno == trackno) ? side : -1;
}
