// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#if defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350) || defined(DOXYGEN)
#include "Adafruit_Floppsy.h"
#include <hardware/gpio.h>

/** @brief Construct the two drive interfaces without touching hardware. */
Adafruit_Floppsy::Adafruit_Floppsy()
    : drive0(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT0, FLOPPY_MOTOR,
             FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA, FLOPPY_WRGATE, FLOPPY_TRK0,
             FLOPPY_WRPROT, FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY),
      drive1(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT1, FLOPPY_MOTOR,
             FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA, FLOPPY_WRGATE, FLOPPY_TRK0,
             FLOPPY_WRPROT, FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY) {
  drive0.led_pin = -1;
  drive1.led_pin = -1;
}

/**
 * @brief Initialize the drive pins, level shifters, and INA3221 power monitor.
 * @return True when both drive interfaces and the power monitor are
 * initialized.
 * @note The cable remains isolated on both success and failure. No drive is
 * selected, no motor starts, and write gate remains inactive. Repeat calls
 * stop any active drive and restore the default drive timings.
 */
bool Adafruit_Floppsy::begin() {
  // Isolate the cable before changing pin directions or drive state. Use the
  // RP2 output latch so enabling an output cannot briefly assert active-low OE.
  gpio_put(FLOPPY_CMD_OE, true);
  pinMode(FLOPPY_CMD_OE, OUTPUT);
  gpio_put(FLOPPY_STAT_OE, true);
  pinMode(FLOPPY_STAT_OE, OUTPUT);
  _initialized = false;

  // Preload inactive outputs before the generic drive initialization runs.
  const uint8_t commands[] = {FLOPPY_SIDE,    FLOPPY_WRDATA,  FLOPPY_STEP,
                              FLOPPY_DIR,     FLOPPY_MOTOR,   FLOPPY_SELECT0,
                              FLOPPY_SELECT1, FLOPPY_DENSITY, FLOPPY_WRGATE};
  for (uint8_t pin : commands) {
    gpio_put(pin, true);
    pinMode(pin, OUTPUT);
  }

  // Controller mode sends commands to the drive and receives its status.
  gpio_put(FLOPPY_CMD_DIR, false);
  pinMode(FLOPPY_CMD_DIR, OUTPUT);
  gpio_put(FLOPPY_STAT_DIR, true);
  pinMode(FLOPPY_STAT_DIR, OUTPUT);
  if (!drive0.begin() || !drive1.begin()) {
    return false;
  }

  // Generic drive initialization leaves write gate pulled up as an input.
  // Drive it inactive explicitly before the board's command buffer is enabled.
  gpio_put(FLOPPY_WRGATE, true);
  pinMode(FLOPPY_WRGATE, OUTPUT);

  // The INA driver checks device IDs and configures all three channels for
  // continuous bus/shunt conversions, 1.1 ms each, with one sample per reading.
  Wire.setTimeout(25);
  if (!powerMonitor.begin()) {
    return false;
  }
  for (uint8_t channel = 0; channel < 3; channel++) {
    powerMonitor.setShuntResistance(channel, 0.05);
  }
  // Allow the first six conversions to finish before callers check the rails.
  delay(10);
  _initialized = true;
  return true;
}

/**
 * @brief Connect the drive interface, or stop both drives and isolate the
 * cable.
 * @param enabled True to connect the controller signals; false to stop/isolate.
 * @return False if enabling before a successful begin(); true otherwise.
 * @note This controls signal buffers, not drive power. The caller must check
 * the rails required by its drive. Enabling does not require 12 V universally.
 * Use only between synchronous drive operations, not during capture or writing.
 */
bool Adafruit_Floppsy::enable(bool enabled) {
  if (enabled) {
    if (!_initialized) {
      return false;
    }
    digitalWrite(FLOPPY_STAT_OE, LOW);
    digitalWrite(FLOPPY_CMD_OE, LOW);
    return true;
  }

  // Stop writing before stopping the motor or disconnecting the cable. Force
  // these outputs inactive even if the caller changed them without a drive API.
  const uint8_t inactive[] = {FLOPPY_WRGATE, FLOPPY_WRDATA, FLOPPY_MOTOR,
                              FLOPPY_SELECT0, FLOPPY_SELECT1};
  for (uint8_t pin : inactive) {
    gpio_put(pin, true);
    pinMode(pin, OUTPUT);
  }
  if (_initialized) {
    drive0.spin_motor(false);
    drive1.spin_motor(false);
    drive0.select(false);
    drive1.select(false);
  }
  gpio_put(FLOPPY_CMD_OE, true);
  pinMode(FLOPPY_CMD_OE, OUTPUT);
  gpio_put(FLOPPY_STAT_OE, true);
  pinMode(FLOPPY_STAT_OE, OUTPUT);
  return true;
}

/** @brief Stop both drives, isolate the cable, and release the drive GPIO pins.
 */
void Adafruit_Floppsy::end() {
  enable(false);
  drive0.end();
  drive1.end();
  _initialized = false;
}
#endif
