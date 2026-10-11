// MIT license. Copyright (c) 2026 Adafruit Industries.
#pragma once
#include <Arduino.h>
#if defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2350) && PICO_RP2350
#include <hardware/pio.h>

// PSRAM holds up to five complete revolutions plus a partial lead-in.
// Samples and index marks are descending 40 MHz timestamps, not clipped bytes.
/** @brief Continuous 25 ns flux/index timestamp capture into RP2350 PSRAM. */
class Adafruit_FloppyFluxCapture {
public:
  static const size_t CAPACITY = 900000;    ///< Maximum captured transitions.
  static const uint8_t MAX_REVOLUTIONS = 5; ///< Complete indexed revolutions.
  bool begin(uint16_t readPin, uint16_t indexPin);
  bool capture(bool (*cancelled)(), uint8_t revolutions = 3);
  void end();
  uint32_t *samples =
      nullptr;      ///< Descending 40 MHz timestamps, including lead-in.
  size_t count = 0; ///< Number of captured transitions.
  uint32_t index[MAX_REVOLUTIONS + 1] = {}; ///< Descending index timestamps.
  const char *error =
      ""; ///< Empty on success, otherwise a static error message.

private:
  void *allocation = nullptr;
  PIO pio = nullptr;
  int fluxSM = -1, indexSM = -1;
  int fluxDMA = -1, indexDMA = -1;
  uint offset = 0;
  bool programLoaded = false;
};
#endif
