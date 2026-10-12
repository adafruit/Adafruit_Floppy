// Read-only engineering test for Floppsy Rev D and a known-good 1.44 MB disk.
// Tests normal INDEX, no configured pin, and a configured pin that never pulses.
// T tests a normal drive, N tests a real INDEX-less drive, D downloads track 0.
// L repeats N with the MCU's INDEX input view held LOW to test a stuck input.
#include <Adafruit_Floppy.h>
#include <Adafruit_INA3221.h>
#include <Adafruit_TinyUSB.h>
#include <hardware/gpio.h>

#if !defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350)
#error "Select Adafruit Floppsy RP2350, 240 MHz, Adafruit TinyUSB"
#endif

Adafruit_Floppy indexed(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT1,
                       FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA,
                       FLOPPY_WRGATE, FLOPPY_TRK0, FLOPPY_WRPROT,
                       FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
Adafruit_Floppy indexless(FLOPPY_DENSITY, -1, FLOPPY_SELECT1,
                         FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA,
                         FLOPPY_WRGATE, FLOPPY_TRK0, FLOPPY_WRPROT,
                         FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
Adafruit_INA3221 powerMonitor;
uint8_t flux[200000];
uint8_t expected[18 * 512];
uint8_t actual[18 * 512];
uint8_t valid[18];
bool passed;

void startDrive(Adafruit_Floppy &drive,
                int indexOverride = GPIO_OVERRIDE_NORMAL,
                bool allowMissing = false);

void setup() {
  gpio_put(FLOPPY_CMD_OE, true);
  pinMode(FLOPPY_CMD_OE, OUTPUT);
  gpio_put(FLOPPY_STAT_OE, true);
  pinMode(FLOPPY_STAT_OE, OUTPUT);
  const uint8_t pins[] = {FLOPPY_SIDE, FLOPPY_WRDATA, FLOPPY_STEP,
    FLOPPY_DIR, FLOPPY_MOTOR, FLOPPY_SELECT1, FLOPPY_SELECT0,
    FLOPPY_DENSITY, FLOPPY_WRGATE};
  for (uint8_t pin : pins) {
    gpio_put(pin, true);
    pinMode(pin, OUTPUT);
  }
  pinMode(FLOPPY_CMD_DIR, OUTPUT);
  digitalWrite(FLOPPY_CMD_DIR, LOW);
  pinMode(FLOPPY_STAT_DIR, OUTPUT);
  digitalWrite(FLOPPY_STAT_DIR, HIGH);
  Serial.begin(115200);
  // Wait for the Serial Monitor; remove this loop for standalone operation.
  while (!Serial) delay(10);
  delay(250);

  Serial.print("Adafruit Floppsy INDEX-less read verification\n\r");

  if (!powerMonitor.begin()) halt("Power monitor missing");
  delay(10);
  Serial.print("T: indexed drive; N: INDEX-less; L: stuck LOW; D: download track\n\r");
}

void loop() {
  if (!Serial.available()) return;
  char command = Serial.read();
  if (command == 'D' && passed) {
    Serial.write(expected, sizeof(expected));
    return;
  }
  if (command != 'T' && command != 'N' && command != 'L') return;
  passed = false;
  if (powerMonitor.getBusVoltage(0) < 10.8 ||
      powerMonitor.getBusVoltage(1) < 4.5) halt("Connect the 12 V supply");

  if (command == 'N' || command == 'L') {
    // Keep the real INDEX pin configured and connected. N never overrides it.
    uint8_t indexOverride = GPIO_OVERRIDE_NORMAL;
    if (command == 'L') indexOverride = GPIO_OVERRIDE_LOW;
    startDrive(indexed, indexOverride, true);
    Serial.printf("INDEX override %u: seen %u, missing %u\n\r", indexOverride,
                  indexed.index_pulses_seen(), indexed.index_pulses_missing());
    // A timed read works even when startup reported no INDEX pulse.
    readTrack(indexed, expected, 0);
    for (uint8_t attempt = 0; attempt < 4; attempt++) {
      readTrack(indexed, actual, 0);
      if (memcmp(expected, actual, sizeof(actual))) halt("Track bytes differ");
    }
    if (indexed.index_pulses_missing()) {
      int32_t offset = -1;
      if (indexed.capture_track(flux, sizeof(flux), &offset, false, 250, 50))
        halt("Indexed read accepted a silent pin");
      if (indexed.write_track(flux, 1, false, true) ||
          indexed.write_track(flux, 1, false, false)) halt("Silent INDEX write");
      Serial.print("PASS: timed reads; indexed read and writes rejected\n\r");
    }
    stopDrive();
    if (indexed.index_pulses_seen() || indexed.index_pulses_missing())
      halt("INDEX status not cleared on stop");
    passed = true;
    Serial.print("ALL PASS; motor off, cable isolated\n\r");
    return;
  }

  startDrive(indexed);
  if (!indexed.has_index() || !indexed.index_pulses_seen()) halt("INDEX not seen");
  readTrack(indexed, expected, 300);
  indexed.spin_motor(false);
  indexed.select(false);

  startDrive(indexless);
  if (indexless.has_index() || indexless.index_pulses_seen()) halt("False INDEX");
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    readTrack(indexless, actual, 0);
    if (memcmp(expected, actual, sizeof(actual))) halt("Track bytes differ");
  }
  Serial.print("PASS: five INDEX-less reads match indexed data\n\r");

  int32_t offset = 123;
  if (indexless.capture_track(flux, sizeof(flux), &offset) || offset != -1)
    halt("Accepted an unbounded INDEX-less read");
  if (indexless.capture_track(flux, sizeof(flux), &offset, false, 250, 50))
    halt("Accepted a wait for missing INDEX");
  if (indexless.write_track(flux, 1, false, true) ||
      indexless.write_track(flux, 1, false, false)) halt("Accepted INDEX-less write");
  if (!digitalRead(FLOPPY_WRGATE)) halt("WRITE GATE active");
  Serial.print("PASS: invalid reads and unsupported writes rejected\n\r");

  flux[64] = 0x5A;
  size_t count = indexless.capture_track(flux, 64, &offset, false, 250, 0);
  if (count != 64 || flux[64] != 0x5A || offset != -1) halt("Buffer boundary");
  Serial.print("PASS: full buffer stops without overwriting guard\n\r");

  // Isolate the status buffer so READ DATA and INDEX stay at pulled-up HIGH.
  // This does not drive against any output from the floppy drive.
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  pinMode(FLOPPY_RDDATA, INPUT_PULLUP);
  uint32_t started = micros();
  count = indexless.capture_track(flux, sizeof(flux), &offset, false, 150, 0);
  uint32_t elapsed = micros() - started;
  Serial.printf("No flux: %u bytes, %lu us, index %ld\n\r",
                (unsigned)count, (unsigned long)elapsed, (long)offset);
  if (count || offset != -1 || elapsed < 150000 || elapsed > 175000)
    halt("No-flux timeout");
  started = micros();
  count = indexed.capture_track(flux, sizeof(flux), &offset, false, 250, 50);
  elapsed = micros() - started;
  if (count || elapsed > 100000) halt("Missing real INDEX not rejected");
  digitalWrite(FLOPPY_STAT_OE, LOW);
  Serial.print("PASS: no-flux and missing-INDEX timeouts\n\r");

  indexless.spin_motor(false);
  indexless.select(false);
  // Override only the MCU's input view; do not drive the physical INDEX wire.
  const uint8_t levels[] = {GPIO_OVERRIDE_HIGH, GPIO_OVERRIDE_LOW};
  for (uint8_t level : levels) {
    startDrive(indexed, level);
    if (!indexed.has_index() || !indexed.index_pulses_missing() ||
        indexed.index_pulses_seen() || !indexed.motor_is_spinning())
      halt("Silent configured INDEX status");
    readTrack(indexed, actual, 0);
    if (memcmp(expected, actual, sizeof(actual))) halt("Silent INDEX track bytes");
    if (indexed.capture_track(flux, sizeof(flux), &offset, false, 250, 50))
      halt("Indexed read accepted a silent pin");
    if (indexed.write_track(flux, 1, false, true) ||
        indexed.write_track(flux, 1, false, false)) halt("Silent INDEX write");
    if (!digitalRead(FLOPPY_WRGATE)) halt("WRITE GATE active");
    Serial.printf("PASS: configured INDEX held %s; timed read matches\n\r",
                  level == GPIO_OVERRIDE_HIGH ? "HIGH" : "LOW");
    gpio_set_inover(FLOPPY_INDEX, GPIO_OVERRIDE_NORMAL);
    // A later successful check must clear the missing-pulse status.
    if (!indexed.spin_motor(true) || indexed.index_pulses_missing())
      halt("INDEX status did not recover");
    indexed.spin_motor(false);
    if (indexed.index_pulses_seen() || indexed.index_pulses_missing())
      halt("INDEX status not cleared on stop");
    indexed.select(false);
  }
  startDrive(indexed);
  readTrack(indexed, actual, 300);
  if (memcmp(expected, actual, sizeof(actual))) halt("Indexed regression");
  stopDrive();
  passed = true;
  Serial.print("ALL PASS; motor off, cable isolated\n\r");
}

void startDrive(Adafruit_Floppy &drive, int indexOverride, bool allowMissing) {
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  drive.led_pin = -1;
  if (!drive.begin()) halt("Floppy initialization failed");
  if (drive.index_pulses_missing()) halt("INDEX status not cleared on begin");
  gpio_set_inover(FLOPPY_INDEX, indexOverride);
  drive.step_delay_us = 3000;
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);
  digitalWrite(FLOPPY_STAT_OE, LOW);
  digitalWrite(FLOPPY_CMD_OE, LOW);
  drive.select(true);
  uint32_t started = millis();
  bool foundIndex = drive.spin_motor(true);
  if (!foundIndex && indexOverride == GPIO_OVERRIDE_NORMAL && !allowMissing)
    halt("Motor start failed");
  Serial.printf("Motor settled: %lu ms, INDEX configured %u, seen %u, missing %u\n\r",
                (unsigned long)(millis() - started), drive.has_index(),
                drive.index_pulses_seen(), drive.index_pulses_missing());
  if (!drive.goto_track(0)) halt("Homing failed");
  Serial.print("Homed to cylinder 0\n\r");
  drive.side(0);
  drive.set_density(true);
}

void readTrack(Adafruit_Floppy &drive, uint8_t *sectors, uint32_t waitMs) {
  int32_t offset = -1;
  uint32_t started = micros();
  size_t count = drive.capture_track(flux, sizeof(flux), &offset, false, 250, waitMs);
  uint32_t elapsed = micros() - started;
  Serial.printf("Capture returned %u bytes\n\r", (unsigned)count);
  if (!count || count == sizeof(flux)) halt("Empty or full flux buffer");
  if (!waitMs && (elapsed < 250000 || elapsed > 275000))
    halt("INDEX-less time or marker");
  if ((!drive.has_index() || drive.index_pulses_missing()) && offset != -1)
    halt("False captured INDEX");
  if (waitMs && offset < 0) halt("Missing captured INDEX");
  size_t decoded = drive.decode_track_mfm(sectors, 18, valid, flux, count, 1, true);
  Serial.printf("Read: %u pulses, %lu us, index %ld, %u/18 CRC-valid sectors\n\r",
                (unsigned)count, (unsigned long)elapsed, (long)offset,
                (unsigned)decoded);
  if (decoded != 18) halt("Missing sectors");
}

void stopDrive() {
  gpio_set_inover(FLOPPY_INDEX, GPIO_OVERRIDE_NORMAL);
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);
  indexed.spin_motor(false);
  indexed.select(false);
  indexless.spin_motor(false);
  indexless.select(false);
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
}

void halt(const char *message) {
  stopDrive();
  Serial.printf("FAIL: %s; motor off, cable isolated\n\r", message);
  while (true) delay(10);
}
