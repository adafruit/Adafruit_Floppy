// Destructive engineering test: use only a disposable 1.44 MB floppy.
// Serial W overwrites cylinder 1, head 0. Serial B tests both heads at
// cylinders 1 and 79. The production self-test remains read-only.
// Serial F receives a 1,474,560-byte IMG from the host, 9216 bytes after
// each IMAGE Cxx Hx READY line, and writes/verifies all 160 track sides.
#include <Adafruit_Floppy.h>
#include <Adafruit_INA3221.h>
#include <Adafruit_TinyUSB.h>
#include <hardware/gpio.h>

#if !defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350)
#error "Select Adafruit Floppsy RP2350, 240 MHz, Adafruit TinyUSB"
#endif

Adafruit_Floppy drive(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT1,
                     FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA,
                     FLOPPY_WRGATE, FLOPPY_TRK0, FLOPPY_WRPROT,
                     FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
Adafruit_INA3221 ina;
uint8_t flux[300000];
uint8_t expected[18 * 512];
uint8_t actual[18 * 512];
uint8_t valid[18];

void setup() {
  // Keep the cable isolated until every command has its inactive value.
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
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
  drive.led_pin = -1;
  Serial.begin(115200);
  // Wait for the Serial Monitor; no disk is written on startup.
  while (!Serial) delay(10);
  delay(250);
  Serial.print("Adafruit Floppsy destructive write/read verification\n\r");

  if (!ina.begin()) halt("Power monitor missing");
  // begin() resets the readings; allow all three bus/shunt conversions to finish.
  delay(10);
  if (!drive.begin()) halt("Floppy initialization failed");
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);
  drive.step_delay_us = 3000;
  Serial.print("Disposable disk only; WRITE EN on, disk tab closed.\n\r");
  Serial.print("W: cylinder 1 head 0. B: cylinders 1/79, both heads.\n\r");
  Serial.print("F: destructive full IMG write; host must send each requested track.\n\r");
}

void loop() {
  if (!Serial.available()) return;
  char command = Serial.read();
  if (command != 'W' && command != 'B' && command != 'F') return;
  float supply12V = ina.getBusVoltage(0);
  float supply5V = ina.getBusVoltage(1);
  Serial.printf("Drive supplies: %.2f V / %.2f V\n\r", supply12V, supply5V);
  if (!(supply12V >= 10.8 && supply5V >= 4.5)) {
    halt("Connect the 12 V supply; floppy 5 V must be present");
  }
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);
  digitalWrite(FLOPPY_STAT_OE, LOW);
  digitalWrite(FLOPPY_CMD_OE, LOW);
  drive.select(true);
  if (!drive.spin_motor(true)) halt("No index pulse");
  Serial.print("Motor/index PASS\n\r");
  if (!drive.goto_track(0)) halt("Homing failed");
  Serial.print("Homing PASS\n\r");
  if (digitalRead(FLOPPY_WRPROT) == LOW) halt("Disk is write-protected");
  drive.set_density(true);
  uint8_t count = 1;
  if (command == 'B') count = 4;
  if (command == 'F') count = 160;
  for (uint8_t position = 0; position < count; position++) {
    uint8_t cylinder = 1;
    if (position >= 2) cylinder = 79;
    if (command == 'F') cylinder = position / 2;
    uint8_t head = position % 2;
    if (!drive.goto_track(cylinder)) halt("Seek failed");
    if (!drive.side(head)) halt("Head selection failed");
    delay(20);
    uint8_t patterns = 5;
    if (command == 'F') patterns = 1;
    for (uint8_t pattern = 0; pattern < patterns; pattern++) {
      if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW) halt("Cancelled");
      if (digitalRead(FLOPPY_WRPROT) == LOW) halt("Disk is write-protected");
      if (command == 'F') {
        Serial.printf("IMAGE C%02u H%u READY\n\r", cylinder, head);
        size_t received = 0;
        uint32_t started = millis();
        while (received < sizeof(expected)) {
          if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW) halt("Cancelled");
          if (millis() - started > 5000) halt("Host image transfer timed out");
          if (Serial.available()) expected[received++] = Serial.read();
          else yield();
        }
      } else {
        for (size_t i = 0; i < sizeof(expected); i++) {
          switch (pattern) {
            case 0: expected[i] = 0; break;
            case 1: expected[i] = 255; break;
            case 2: expected[i] = 0x55; break;
            case 3: expected[i] = 0xAA; break;
            default: expected[i] = (uint8_t)(i + i / 512 + cylinder * 3 + head); break;
          }
        }
      }
      size_t pulses = drive.encode_track_mfm(expected, 18, flux, sizeof(flux), 1, cylinder);
      Serial.printf("Writing C%02u H%u pattern %u, %u pulses\n\r",
                    cylinder, head, pattern, (unsigned)pulses);
      if (!pulses || !drive.write_track(flux, pulses, false)) halt("Flux write failed");
      delay(20);
      memset(actual, 0, sizeof(actual));
      memset(valid, 0, sizeof(valid));
      size_t sectors = 0;
      for (uint8_t attempt = 0; attempt < 3 && sectors < 18; attempt++) {
        int32_t indexOffset = -1;
        pulses = drive.capture_track(flux, sizeof(flux), &indexOffset, false, 220, 300);
        sectors = drive.decode_track_mfm(actual, 18, valid, flux, pulses, 1,
                                        false, nullptr, cylinder);
        Serial.printf("Read %u pulses, %u/18 CRC-valid sectors\n\r",
                      (unsigned)pulses, (unsigned)sectors);
      }
      if (sectors != 18) halt("Missing sectors after write");
      if (memcmp(expected, actual, sizeof(expected))) halt("Readback differs from written bytes");
      Serial.printf("PASS C%02u H%u pattern %u: all 9216 bytes and 18 sector CRCs\n\r",
                    cylinder, head, pattern);
    }
  }
  stopDrive();
  Serial.print("WRITE/READ PASS; motor off, cable isolated\n\r");
}

void stopDrive() {
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  drive.spin_motor(false);
  drive.select(false);
}

void halt(const char *message) {
  stopDrive();
  Serial.printf("FAIL: %s; motor off, cable isolated\n\r", message);
  while (true) delay(10);
}
