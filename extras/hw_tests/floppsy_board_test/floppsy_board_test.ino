// Non-destructive Floppsy board-helper test. Startup does not move the drive.
// Send M to briefly spin the DS1 drive, then check safe shutdown. No disk writes.
#include <Adafruit_Floppsy.h>
#include <Adafruit_TinyUSB.h>
#include <hardware/gpio.h>

Adafruit_Floppsy floppsy;

void check(bool passed, const char *message) {
  if (!passed) {
    floppsy.enable(false);
    Serial.printf("FAIL: %s; cable isolated\n\r", message);
    while (true) delay(10);
  }
  Serial.printf("PASS: %s\n\r", message);
}

void checkStopped() {
  check(digitalRead(FLOPPY_CMD_OE) && digitalRead(FLOPPY_STAT_OE),
        "Both signal buffers disabled");
  check(digitalRead(FLOPPY_WRGATE) && digitalRead(FLOPPY_WRDATA) &&
        digitalRead(FLOPPY_MOTOR) && digitalRead(FLOPPY_SELECT0) &&
        digitalRead(FLOPPY_SELECT1), "Write, motor, and select outputs inactive");
  check(!floppsy.drive0.motor_is_spinning() &&
        !floppsy.drive1.motor_is_spinning() &&
        !floppsy.drive0.drive_is_selected() &&
        !floppsy.drive1.drive_is_selected(), "Drive objects agree with stopped pins");
}

void setup() {
  Serial.begin(115200);
  // Wait for the Serial Monitor before starting the hardware checks.
  while (!Serial) delay(10);
  delay(250);

  Serial.print("Adafruit Floppsy board-helper test\n\r");

  check(floppsy.begin(), "Board initialization");
  checkStopped();
  check(!digitalRead(FLOPPY_CMD_DIR) && digitalRead(FLOPPY_STAT_DIR),
        "Controller-mode level-shifter directions");
  check(gpio_get_dir(FLOPPY_WRGATE), "Write gate actively driven inactive");

  // The monitor must report real rails as well as the expected configuration.
  check(floppsy.powerMonitor.getAveragingMode() == INA3221_AVG_1_SAMPLE &&
        floppsy.powerMonitor.getBusVoltageConvTime() == INA3221_CONVTIME_1MS &&
        floppsy.powerMonitor.getShuntVoltageConvTime() == INA3221_CONVTIME_1MS &&
        floppsy.powerMonitor.getMode() == INA3221_MODE_SHUNT_BUS_CONT,
        "INA3221 continuous conversion configuration");

  for (uint8_t channel = 0; channel < 3; channel++) {
    check(floppsy.powerMonitor.isChannelEnabled(channel), "Monitor channel enabled");
    check(abs(floppsy.powerMonitor.getShuntResistance(channel) - 0.05) < 0.001,
          "50 milliohm shunt calibration");
    Serial.printf("Rail %u: %.3f V, %.3f A\n\r", channel,
                  floppsy.powerMonitor.getBusVoltage(channel),
                  floppsy.powerMonitor.getCurrentAmps(channel));
  }
  float volts3V3 = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_3V3);
  check(volts3V3 > 3.0 && volts3V3 < 3.6, "Live 3.3 V rail reading");

  // Select each output with the motor stopped, then prove disable releases it.
  check(floppsy.enable(true), "Enable controller signals");
  check(!digitalRead(FLOPPY_CMD_OE) && !digitalRead(FLOPPY_STAT_OE),
        "Both signal buffers enabled");
  floppsy.drive0.select(true);
  check(!digitalRead(FLOPPY_SELECT0) && digitalRead(FLOPPY_SELECT1), "DS0 selection");
  floppsy.enable(false);
  checkStopped();

  check(floppsy.enable(true), "Re-enable controller signals");
  floppsy.drive1.select(true);
  check(digitalRead(FLOPPY_SELECT0) && !digitalRead(FLOPPY_SELECT1), "DS1 selection");
  floppsy.enable(false);
  checkStopped();

  // Reinitialization and end() must also leave the cable isolated.
  check(floppsy.begin(), "Repeated initialization");
  checkStopped();
  floppsy.end();
  check(digitalRead(FLOPPY_CMD_OE) && digitalRead(FLOPPY_STAT_OE), "End isolates cable");
  check(!floppsy.enable(true), "End requires a new begin before enabling");
  check(floppsy.begin(), "Initialization after end");
  checkStopped();

  Serial.print("BOARD PASS. M: optional motor/shutdown check (12 V connected).\n\r");
}

void loop() {
  if (!Serial.available()) return;
  if (Serial.read() != 'M') return;

  // This fixture previously browned out from computer USB alone. Require its
  // barrel/PD supply for the optional motor test, without imposing that policy
  // on every application of the board helper.
  float volts12 = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_12V);
  float volts5 = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V);
  if (!(volts12 >= 10.8 && volts5 >= 4.5)) {
    Serial.print("Motor test skipped: connect 12 V and check the floppy 5 V rail.\n\r");
    return;
  }

  check(floppsy.enable(true), "Enable for motor test");
  floppsy.drive1.select(true);
  bool index = floppsy.drive1.spin_motor(true);
  Serial.printf("Motor running: INDEX %s; 5 V current %.3f A\n\r",
                index ? "seen" : "missing (allowed for this test)",
                floppsy.powerMonitor.getCurrentAmps(FLOPPSY_POWER_5V));
  check(!digitalRead(FLOPPY_MOTOR), "Motor output asserted");

  floppsy.enable(false);
  checkStopped();
  delay(500);
  Serial.printf("Motor stopped: 5 V current %.3f A\n\r",
                floppsy.powerMonitor.getCurrentAmps(FLOPPSY_POWER_5V));
  Serial.print("MOTOR/SHUTDOWN PASS; no disk write commands used.\n\r");
}
