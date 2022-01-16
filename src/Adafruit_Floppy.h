#ifndef ADAFRUIT_FLOPPY_H
#define ADAFRUIT_FLOPPY_H

#include "Arduino.h"
#include <Adafruit_SPIDevice.h>
// to implement SdFat Block Driver
#include "SdFat.h"
#include "SdFatConfig.h"

#define FLOPPY_IBMPC_HD_TRACKS 80
#define FLOPPY_IBMPC_DD_TRACKS 40
#define FLOPPY_HEADS 2

#define MFM_IBMPC1440K_SECTORS_PER_TRACK 18
#define MFM_IBMPC360K_SECTORS_PER_TRACK 9
#define MFM_BYTES_PER_SECTOR 512UL

#ifdef BUSIO_USE_FAST_PINIO
#define FLOPPYIO_SAMPLERATE (F_CPU * 11u / 90u) // empirical on SAM D51 @ 120MHz
#endif

#if defined(ARDUINO_ARCH_RP2040)
#undef FLOPPYIO_SAMPLERATE
#define FLOPPYIO_SAMPLERATE (F_CPU * 13u / 100u) // empirical on RP2040 @ 200MHz
#endif

#define T2_5_IBMPC_HD (FLOPPYIO_SAMPLERATE * 5 / 2 / 1000000)
#define T3_5_IBMPC_HD (FLOPPYIO_SAMPLERATE * 7 / 2 / 1000000)
#define T2_5_IBMPC_DD (T2_5_IBMPC_HD * 2)
#define T3_5_IBMPC_DD (T3_5_IBMPC_HD * 2)

#define STEP_OUT HIGH
#define STEP_IN LOW
#define MAX_FLUX_PULSE_PER_TRACK                                               \
  (uint32_t)(500000UL / 5 *                                                    \
             1.5) // 500khz / 5 hz per track rotation, 1.5 rotations

#define BUSTYPE_IBMPC 1
#define BUSTYPE_SHUGART 2

typedef enum {
  IBMPC1440K,
  IBMPC360K,
} adafruit_floppy_disk_t;

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

  uint32_t read_track_mfm(uint8_t *sectors, size_t n_sectors,
                          uint8_t *sector_validity);
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

/**************************************************************************/
/*!
    This class adds support for the BaseBlockDriver interface to an MFM
    encoded floppy disk. This allows it to be used with SdFat's FatFileSystem
   class. or for a mass storage device
*/
/**************************************************************************/
class Adafruit_MFM_Floppy : public BaseBlockDriver {
public:
  Adafruit_MFM_Floppy(Adafruit_Floppy *floppy,
                      adafruit_floppy_disk_t format = IBMPC1440K);

  bool begin(void);
  bool end(void);

  uint32_t size(void);
  int32_t readTrack(uint8_t track, bool head);

  /**! @brief The expected number of sectors per track in this format
       @returns The number of sectors per track */
  uint8_t sectors_per_track(void) { return _sectors_per_track; }
  /**! @brief The expected number of tracks per side in this format
       @returns The number of tracks per side */
  uint8_t tracks_per_side(void) { return _tracks_per_side; }

  //------------- SdFat BaseBlockDRiver API -------------//
  virtual bool readBlock(uint32_t block, uint8_t *dst);
  virtual bool writeBlock(uint32_t block, const uint8_t *src);
  virtual bool syncBlocks();
  virtual bool readBlocks(uint32_t block, uint8_t *dst, size_t nb);
  virtual bool writeBlocks(uint32_t block, const uint8_t *src, size_t nb);

  /**! The raw byte decoded data from the last track read */
  uint8_t track_data[MFM_IBMPC1440K_SECTORS_PER_TRACK * MFM_BYTES_PER_SECTOR];

  /**! Which tracks from the last track-read were valid MFM/CRC! */
  uint8_t track_validity[MFM_IBMPC1440K_SECTORS_PER_TRACK];

private:
  uint8_t _sectors_per_track = 0;
  uint8_t _tracks_per_side = 0;
  int8_t _last_track_read = -1; // last cached track
  Adafruit_Floppy *_floppy = NULL;
  adafruit_floppy_disk_t _format = IBMPC1440K;
};

#endif
