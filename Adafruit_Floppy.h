#ifndef ADAFRUIT_FLOPPY_H
#define ADAFRUIT_FLOPPY_H

#include "Arduino.h"
#include <Adafruit_SPIDevice.h>

#define MAX_TRACKS 80
#define STEP_OUT HIGH
#define STEP_IN LOW
#define MAX_FLUX_PULSE_PER_TRACK (500000 / 5) // 500khz / 5 hz per track rotation


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
  void spin_up(void);
  void spin_down(void);
  bool goto_track(uint8_t track);
  int8_t track(void);
  void step(bool dir, uint8_t times);

  uint32_t capture_track(uint8_t *pulses, uint32_t max_pulses);
  void print_pulse_bins(uint8_t *pulses, uint32_t num_pulses, uint8_t max_bins = 64);
  void print_pulses(uint8_t *pulses, uint32_t num_pulses);

  int8_t led_pin = LED_BUILTIN;

private:
  void wait_for_index_pulse_low(void);

  // theres a lot of GPIO!
  int8_t _densitypin, _indexpin, _selectpin, _motorpin, _directionpin, _steppin,
    _wrdatapin, _wrgatepin, _track0pin, _protectpin, _rddatapin, _sidepin, _readypin;

  int8_t _track = -1;

  BusIO_PortReg *indexPort;
  BusIO_PortMask indexMask;

};

#endif
