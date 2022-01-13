#ifndef ADAFRUIT_FLOPPY_H
#define ADAFRUIT_FLOPPY_H

#include "Arduino.h"
#include <Adafruit_SPIDevice.h>

#define MAX_TRACKS 80
#define STEP_OUT HIGH
#define STEP_IN LOW
#define MAX_FLUX_PULSE_PER_TRACK                                               \
  (uint32_t)(500000UL / 5 *                                                    \
             1.5) // 500khz / 5 hz per track rotation, 1.5 rotations

#define BUSTYPE_IBMPC 1
#define BUSTYPE_SHUGART 2

/**************************************************************************/
/*!
    @brief A helper class for chattin with floppy drives
*/
/**************************************************************************/
class Adafruit_Floppy {
public:
  Adafruit_Floppy(int8_t densitypin, int8_t indexpin, int8_t selectpin,
                  int8_t motorpin, int8_t directionpin, int8_t steppin,
                  int8_t wrdatapin, int8_t wrgatepin, int8_t track0pin,
                  int8_t protectpin, int8_t rddatapin, int8_t sidepin,
                  int8_t readypin);
  void begin(void);
  void soft_reset(void);

  void select(bool selected);
  bool spin_motor(bool motor_on);
  bool goto_track(uint8_t track);
  void side(uint8_t head);
  int8_t track(void);
  void step(bool dir, uint8_t times);

  uint32_t read_track_mfm(uint8_t *sectors, size_t n_sectors, uint8_t *sector_validity);
  uint32_t capture_track(uint8_t *pulses, uint32_t max_pulses)
      __attribute__((optimize("O3")));
  void print_pulse_bins(uint8_t *pulses, uint32_t num_pulses,
                        uint8_t max_bins = 64);
  void print_pulses(uint8_t *pulses, uint32_t num_pulses);

  int8_t led_pin = LED_BUILTIN; ///< Debug LED output for tracing

  uint16_t select_delay_us = 10;  ///< delay after drive select (usecs)
  uint16_t step_delay_us = 10000; ///< delay between head steps (usecs)
  uint16_t settle_delay_ms = 15;  ///< settle delay after seek (msecs)
  uint16_t motor_delay_ms = 1000; ///< delay after motor on (msecs)
  uint16_t watchdog_delay_ms =
      1000; ///< quiescent time until drives reset (msecs)
  uint8_t bus_type = BUSTYPE_IBMPC; ///< what kind of floppy drive we're using

  Stream *debug_serial = NULL; ///< optional debug stream for serial output

private:
  void wait_for_index_pulse_low(void);

  // theres a lot of GPIO!
  int8_t _densitypin, _indexpin, _selectpin, _motorpin, _directionpin, _steppin,
      _wrdatapin, _wrgatepin, _track0pin, _protectpin, _rddatapin, _sidepin,
      _readypin;

  int8_t _track = -1;

#ifdef BUSIO_USE_FAST_PINIO
  BusIO_PortReg *indexPort;
  BusIO_PortMask indexMask;
#endif
};

#endif
