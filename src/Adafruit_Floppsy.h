// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#ifndef ADAFRUIT_FLOPPSY_H
#define ADAFRUIT_FLOPPSY_H

#if !defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350) && !defined(DOXYGEN)
#error "Select Adafruit Floppsy RP2350 in Tools > Board"
#else
#include "Adafruit_Floppy.h"
#include <Adafruit_INA3221.h>

/** @brief INA3221 channel assignments on Floppsy Rev D. */
typedef enum {
  FLOPPSY_POWER_12V = 0, ///< Floppy 12 V supply.
  FLOPPSY_POWER_5V = 1,  ///< Floppy 5 V supply.
  FLOPPSY_POWER_3V3 = 2, ///< Board 3.3 V supply.
} floppsy_power_rail_t;

/**
 * @brief Board setup for RP2350 Floppsy in drive-controller mode.
 *
 * Own one instance per board. The two drive interfaces share the cable and
 * motor signal; select only one drive at a time. Call begin(), check the supply
 * voltages required by your drive, then enable(true) before drive operations.
 * Display, touch, SD, USB host, and drive-emulation setup are separate.
 */
class Adafruit_Floppsy {
public:
  Adafruit_Floppsy();
  bool begin();
  bool enable(bool enabled);
  void end();

  Adafruit_Floppy drive0; ///< Drive interface using the DS0 output.
  Adafruit_Floppy drive1; ///< Drive interface using the DS1 output.
  Adafruit_INA3221
      powerMonitor; ///< Three supply monitors, with 50 milliohm shunts.

  /** @brief Board hardware ownership cannot be copied. */
  Adafruit_Floppsy(const Adafruit_Floppsy &) = delete;
  /** @brief Board hardware ownership cannot be assigned.
   * @return Unavailable because copy assignment is deleted. */
  Adafruit_Floppsy &operator=(const Adafruit_Floppsy &) = delete;

private:
  bool _initialized = false; ///< Drive and power-monitor setup completed.
};
#endif
#endif
