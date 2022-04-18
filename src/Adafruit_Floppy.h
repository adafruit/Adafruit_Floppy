#ifndef ADAFRUIT_FLOPPY_H
#define ADAFRUIT_FLOPPY_H

/*! \mainpage
 *
 * \image html rabbit.png
 *
 * This is a helper library to abstract away interfacing with floppy disk drives in a cross-platform and open source library.
 *
 * Adafruit Floppy is a project to make a flexible, full-stack, open source
 * hardware/software device for reading, archiving, accessing and duplicating
 * floppy disk media. It joins a family of open source hardware and software
 * such as greaseweazle and fluxengine, and increases the availability and
 * accessibility of floppy disk controllers.
 */

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
    @brief An abstract base class for chattin with floppy drives
*/
/**************************************************************************/
class Adafruit_FloppyBase {
protected:
  Adafruit_FloppyBase(int indexpin, int wrdatapin, int wrgatepin, int rddatapin,
                      bool is_apple2 = false);

public:
  bool begin(void);
  virtual void end();

  virtual void soft_reset(void);

  /**************************************************************************/
  /*!
      @brief Whether to select this drive
      @param selected True to select/enable
  */
  /**************************************************************************/
  virtual void select(bool selected) = 0;
  /**************************************************************************/
  /*!
      @brief  Turn on or off the floppy motor, if on we wait till we get an
     index pulse!
      @param motor_on True to turn on motor, False to turn it off
      @returns False if turning motor on and no index pulse found, true
     otherwise
  */
  /**************************************************************************/
  virtual bool spin_motor(bool motor_on) = 0;
  /**************************************************************************/
  /*!
      @brief  Seek to the desired track, requires the motor to be spun up!
      @param  track_num The track to step to
      @return True If we were able to get to the track location
  */
  /**************************************************************************/
  virtual bool goto_track(uint8_t track_num) = 0;
  /**************************************************************************/
  /*!
      @brief Which head/side to read from
      @param head Head 0 or 1
      @return true if the head exists, false otherwise
  */
  /**************************************************************************/
  virtual bool side(uint8_t head) = 0;
  /**************************************************************************/
  /*!
      @brief  The current track location, based on internal caching
      @return The cached track location
      @note Returns -1 if the track is not known.
  */
  /**************************************************************************/
  virtual int8_t track(void) = 0;
  /**************************************************************************/
  /*!
      @brief  Check whether the floppy in the drive is write protected
      @returns False if the floppy is writable, true otherwise
  */
  /**************************************************************************/
  virtual bool get_write_protect() = 0;

  /**************************************************************************/
  /*!
      @brief  Check whether the track0 sensor is active
      @returns True if the track0 sensor is active, false otherwise
      @note On devices without a track0 sensor, this returns true when
     track()==0
  */
  /**************************************************************************/
  virtual bool get_track0_sense() = 0;

  /**************************************************************************/
  /*!
      @brief  Set the density for flux reading and writing
      @param high_density false for low density, true for high density
      @returns True if the drive interface supports the given density.
  */
  /**************************************************************************/
  virtual bool set_density(bool high_density) = 0;

  uint32_t read_track_mfm(uint8_t *sectors, size_t n_sectors,
                          uint8_t *sector_validity, bool high_density = true);
  uint32_t capture_track(volatile uint8_t *pulses, uint32_t max_pulses,
                         int32_t *falling_index_offset,
                         bool store_greaseweazle = false,
                         uint32_t capture_ms = 0)
      __attribute__((optimize("O3")));

  bool write_track(uint8_t *pulses, uint32_t num_pulses,
                   bool store_greaseweazle = false)
      __attribute__((optimize("O3")));
  void print_pulse_bins(uint8_t *pulses, uint32_t num_pulses,
                        uint8_t max_bins = 64, bool is_gw_format = false);
  void print_pulses(uint8_t *pulses, uint32_t num_pulses,
                    bool is_gw_format = false);
  uint32_t getSampleFrequency(void);

  int8_t led_pin = LED_BUILTIN; ///< Debug LED output for tracing

  uint16_t select_delay_us = 10;  ///< delay after drive select (usecs)
  uint16_t step_delay_us = 10000; ///< delay between head steps (usecs)
  uint16_t settle_delay_ms = 15;  ///< settle delay after seek (msecs)
  uint16_t motor_delay_ms = 1000; ///< delay after motor on (msecs)
  uint16_t watchdog_delay_ms =
      1000; ///< quiescent time until drives reset (msecs)
  uint8_t bus_type = BUSTYPE_IBMPC; ///< what kind of floppy drive we're using

  Stream *debug_serial = NULL; ///< optional debug stream for serial output

protected:
  bool read_index();

private:
#if defined(__SAMD51__)
  void deinit_capture(void);
  void enable_capture(void);

  bool init_generate(void);
  void deinit_generate(void);
  void enable_generate(void);
  void disable_generate(void);
#endif

  bool start_polled_capture(void);
  void disable_capture(void);
  uint16_t sample_flux(bool &new_index_state);
  uint16_t sample_flux() {
    bool unused;
    return sample_flux(unused);
  }

  bool init_capture(void);
  void enable_background_capture(void);
  void wait_for_index_pulse_low(void);

  int8_t _indexpin, _wrdatapin, _wrgatepin, _rddatapin;
  bool _is_apple2;

#ifdef BUSIO_USE_FAST_PINIO
  BusIO_PortReg *indexPort;
  BusIO_PortMask indexMask;
  uint32_t dummyPort = 0;
#endif
};

/**************************************************************************/
/*!
    @brief A helper class for chattin with PC & Shugart floppy drives
*/
/**************************************************************************/
class Adafruit_Floppy : public Adafruit_FloppyBase {
public:
  Adafruit_Floppy(int8_t densitypin, int8_t indexpin, int8_t selectpin,
                  int8_t motorpin, int8_t directionpin, int8_t steppin,
                  int8_t wrdatapin, int8_t wrgatepin, int8_t track0pin,
                  int8_t protectpin, int8_t rddatapin, int8_t sidepin,
                  int8_t readypin);
  void end() override;
  void soft_reset(void) override;

  void select(bool selected) override;
  bool spin_motor(bool motor_on) override;
  bool goto_track(uint8_t track) override;
  bool side(uint8_t head) override;
  int8_t track(void) override;
  void step(bool dir, uint8_t times);
  bool set_density(bool high_density) override;
  bool get_write_protect() override;
  bool get_track0_sense() override;

private:
  // theres a lot of GPIO!
  int8_t _densitypin, _selectpin, _motorpin, _directionpin, _steppin,
      _track0pin, _protectpin, _sidepin, _readypin;

  int8_t _track = -1;
};

/**************************************************************************/
/*!
    @brief A helper class for chattin with Apple 2 floppy drives
*/
/**************************************************************************/
class Adafruit_Apple2Floppy : public Adafruit_FloppyBase {
public:
  /**************************************************************************/
  /*!
      @brief Constants for use with the step_mode method
  */
  /**************************************************************************/
  enum StepMode {
    STEP_MODE_WHOLE,   //< One step moves by one data track
    STEP_MODE_HALF,    //< Two steps move by one data track
    STEP_MODE_QUARTER, //< Four steps move by one data track
  };

  Adafruit_Apple2Floppy(int8_t indexpin, int8_t selectpin, int8_t phase1pin,
                        int8_t phase2pin, int8_t phase3pin, int8_t phase4pin,
                        int8_t wrdatapin, int8_t wrgatepin, int8_t protectpin,
                        int8_t rddatapin);
  void end() override;
  void soft_reset(void) override;

  void select(bool selected) override;
  bool spin_motor(bool motor_on) override;
  bool goto_track(uint8_t track) override;
  bool side(uint8_t head) override;
  int8_t track(void) override;
  bool set_density(bool high_density) override;
  bool get_write_protect() override;
  bool get_track0_sense() override;

  int8_t quartertrack();
  bool goto_quartertrack(int);
  void step_mode(StepMode mode);

private:
  int _step_multiplier() const;
  // theres not much GPIO!
  int8_t _selectpin, _phase1pin, _phase2pin, _phase3pin, _phase4pin,
      _protectpin;
  int _quartertrack = -1;
  StepMode _step_mode = STEP_MODE_HALF;
  void _step(int dir, int times);
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
#if defined(PICO_BOARD) || defined(__RP2040__) || defined(ARDUINO_ARCH_RP2040)
  uint16_t _last;
#endif
  uint8_t _sectors_per_track = 0;
  uint8_t _tracks_per_side = 0;
  int8_t _last_track_read = -1; // last cached track
  bool _high_density = true;
  Adafruit_Floppy *_floppy = NULL;
  adafruit_floppy_disk_t _format = IBMPC1440K;
};

#endif
