#include "SdFat.h"
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

SdFat SD;

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

uint8_t track_data[track_max_bytes];
uint32_t flux_data[(flux_max_bits + 31) / 32];

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool appUsing = false;

#if 0
// Called by FatFSUSB when the drive is released.  We note this, restart FatFS, and tell the main loop to rescan.
void unplug(uint32_t i) {
  (void) i;
  driveConnected = false;
  updated = true;
  FatFS.begin();
}

// Called by FatFSUSB when the drive is mounted by the PC.  Have to stop FatFS, since the drive data can change, note it, and continue.
void plug(uint32_t i) {
  (void) i;
  driveConnected = true;
  FatFS.end();
}

// Called by FatFSUSB to determine if it is safe to let the PC mount the USB drive.  If we're accessing the FS in any way, have any Files open, etc. then it's not safe to let the PC mount the drive.
bool mountable(uint32_t i) {
  (void) i;
  return !appUsing;
}
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
  Serial.printf("stepped direction=%d new track=%d\n", direction, new_track);
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

  Serial.printf("fluxout sm%d offset%d\n", sm_fluxout, offset_fluxout);
  Serial.printf("index_pulse sm%d offset%d\n", sm_index_pulse,
                offset_index_pulse);
}

void setup1() {
  while (!Serial) {
    delay(1);
  }
  Serial.println("(in setup1, naughty)");
  setupPio();
}

void setup() {
  pinMode(DIR_PIN, INPUT_PULLUP);
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(SIDE_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, INPUT_PULLUP);
  pinMode(SELECT_PIN, INPUT_PULLUP);
  pinMode(TRK0_PIN, OUTPUT);
  pinMode(PROT_PIN, OUTPUT);
  digitalWrite(PROT_PIN, LOW); // always write-protected, no write support
  // pinMode(INDEX_PIN, OUTPUT);

#if defined(FLOPPY_DIRECTION_PIN)
  pinMode(FLOPPY_DIRECTION_PIN, OUTPUT);
  digitalWrite(FLOPPY_DIRECTION_PIN, LOW); // we are emulating a floppy
#endif

  for (auto &d : flux_data)
    d = 0xaa55cc11;

  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }
  // delay(5000);
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), stepped, FALLING);

#if defined(PIN_CARD_CS)
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

#if 0
                                                                                             
  if (!FatFS.begin()) {                                                                      
    Serial.println("FatFS initialization failed!");                                          
    while (1) {                                                                              
      delay(1);                                                                              
    }                                                                                        
  }
  Serial.println("FatFS initialization done.");

  FatFSUSB.onUnplug(unplug);
  FatFSUSB.onPlug(plug);
  FatFSUSB.driveReady(mountable);

  Serial.println("FatFSUSB started.");
  Serial.println("Connect drive via USB to upload/erase files and re-display");
#endif
}

volatile int revs;

void __not_in_flash_func(loop1)() {
  static bool once;
  if (fluxout) {
    pio_sm_put_blocking(pio, sm_index_pulse,
                        4000); // ??? put index high for 4ms (out of 200ms)
    revs++;
    for (auto d : flux_data) {
      if (!fluxout)
        break;
      pio_sm_put_blocking(pio, sm_fluxout, __builtin_bswap32(d));
    }
  }
}

static void encode_track_mfm(uint8_t head, uint8_t cylinder,
                             uint8_t n_sectors) {

  mfm_io_t io = {
      .encode_compact = true,
      .T2_max = 5,
      .T3_max = 7,
      .T1_nom = 2,
      .pulses = reinterpret_cast<uint8_t *>(flux_data),
      .n_pulses = sizeof(flux_data),
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
  static int cached_trackno = -1, cached_side = -1;
  auto new_trackno = trackno;
  int motor_pin = digitalRead(MOTOR_PIN);
  int select_pin = digitalRead(SELECT_PIN);
  int side_pin = digitalRead(SIDE_PIN);
  auto new_side = !side_pin;

  auto enabled = !motor_pin && !select_pin;
  if (enabled && new_trackno != cached_trackno || new_side != cached_side) {
    fluxout = 0;
    Serial.printf("C%dS%d\n", new_trackno, new_side);

#if defined(PIN_CARD_CS)
    FsFile file;
    int r = file.open("disk.img");
    file.seek(512 * sector_count * (2 * new_trackno + new_side));
    int n = file.read(track_data, 512 * sector_count);
    if (n != 512 * sector_count) {
      Serial.println("Read failed");
      std::fill(track_data, std::end(track_data), new_trackno);
    }
#else
    std::fill(track_data, std::end(track_data), new_trackno * 2 + new_side);
#endif

    encode_track_mfm(new_side, new_trackno, sector_count);
    cached_trackno = new_trackno;
    cached_side = new_side;
  }
  fluxout = cached_trackno == trackno && cached_side == new_side && enabled;
}
