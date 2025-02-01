#include "Adafruit_Floppy.h"

#define DEBUG_FLOPPY (0)

// We need to read and write some pins at optimized speeds - use raw registers
// or native SDK API!
#ifdef BUSIO_USE_FAST_PINIO
#define read_index_fast() (*indexPort & indexMask)
#define read_data() (*dataPort & dataMask)
#define set_debug_led() (*ledPort |= ledMask)
#define clr_debug_led() (*ledPort &= ~ledMask)
#define set_write() (*writePort |= writeMask)
#define clr_write() (*writePort &= ~writeMask)
#elif defined(ARDUINO_ARCH_RP2040)
#include "arch_rp2.h"
#endif

#if !DEBUG_FLOPPY
#undef set_debug_led
#undef clr_debug_led
#define set_debug_led() ((void)0)
#define clr_debug_led() ((void)0)
#endif

#include "mfm_impl.h"

#if defined(__SAMD51__)
extern volatile uint8_t *g_flux_pulses;
extern volatile uint32_t g_max_pulses;
extern volatile uint32_t g_n_pulses;
extern volatile bool g_store_greaseweazle;
extern volatile uint8_t g_timing_div;
extern volatile bool g_writing_pulses;
#endif

/**************************************************************************/
/*!
    @brief  Create a hardware interface to a floppy drive
    @param  indexpin A pin connected to the floppy Index Sensor output
    @param  wrdatapin A pin connected to the floppy Write Data input
    @param  wrgatepin A pin connected to the floppy Write Gate input
    @param  rddatapin A pin connected to the floppy Read Data output
    @param  is_apple2 True if the flux write waveform is like Apple Disk ][

*/
/**************************************************************************/
Adafruit_FloppyBase::Adafruit_FloppyBase(int indexpin, int wrdatapin,
                                         int wrgatepin, int rddatapin,
                                         bool is_apple2)
    : _indexpin(indexpin), _wrdatapin(wrdatapin), _wrgatepin(wrgatepin),
      _rddatapin(rddatapin), _is_apple2(is_apple2) {}

/**************************************************************************/
/*!
    @brief  Create a hardware interface to a floppy drive
    @param  densitypin A pin connected to the floppy Density Select input
    @param  indexpin A pin connected to the floppy Index Sensor output
    @param  selectpin A pin connected to the floppy Drive Select input
    @param  motorpin A pin connected to the floppy Motor Enable input
    @param  directionpin A pin connected to the floppy Stepper Direction input
    @param  steppin A pin connected to the floppy Stepper input
    @param  wrdatapin A pin connected to the floppy Write Data input
    @param  wrgatepin A pin connected to the floppy Write Gate input
    @param  track0pin A pin connected to the floppy Track 00 Sensor output
    @param  protectpin A pin connected to the floppy Write Protect Sensor output
    @param  rddatapin A pin connected to the floppy Read Data output
    @param  sidepin A pin connected to the floppy Side Select input
    @param  readypin A pin connected to the floppy Ready/Disk Change output

*/
/**************************************************************************/

Adafruit_Floppy::Adafruit_Floppy(int8_t densitypin, int8_t indexpin,
                                 int8_t selectpin, int8_t motorpin,
                                 int8_t directionpin, int8_t steppin,
                                 int8_t wrdatapin, int8_t wrgatepin,
                                 int8_t track0pin, int8_t protectpin,
                                 int8_t rddatapin, int8_t sidepin,
                                 int8_t readypin)
    : Adafruit_FloppyBase{indexpin, wrdatapin, wrgatepin, rddatapin} {
  _densitypin = densitypin;
  _selectpin = selectpin;
  _motorpin = motorpin;
  _directionpin = directionpin;
  _steppin = steppin;
  _track0pin = track0pin;
  _protectpin = protectpin;
  _sidepin = sidepin;
  _readypin = readypin;
}

/**************************************************************************/
/*!
    @brief  Initializes the GPIO pins but do not start the motor or anything
    @returns True if able to set up all pins and capture/waveform peripherals
*/
/**************************************************************************/
bool Adafruit_FloppyBase::begin(void) {
  soft_reset();
#if defined(__SAMD51__)
  if (!init_capture()) {
    return false;
  }
  deinit_capture();
  if (!init_generate()) {
    return false;
  }
  deinit_generate();
#endif
  return true;
}

/**************************************************************************/
/*!
    @brief  Initializes the GPIO pins but do not start the motor or anything
*/
/**************************************************************************/
void Adafruit_FloppyBase::soft_reset(void) {
  if (_indexpin >= 0) {
    pinMode(_indexpin, INPUT_PULLUP);
#ifdef BUSIO_USE_FAST_PINIO
    indexPort = (BusIO_PortReg *)portInputRegister(digitalPinToPort(_indexpin));
    indexMask = digitalPinToBitMask(_indexpin);
#endif
  } else {
#ifdef BUSIO_USE_FAST_PINIO
    indexPort = &dummyPort;
    indexMask = 0;
#endif
  }

  // set write OFF
  if (_wrdatapin >= 0) {
    pinMode(_wrdatapin, OUTPUT);
    digitalWrite(_wrdatapin, HIGH);
  }
  if (_wrgatepin >= 0) {
    pinMode(_wrgatepin, INPUT_PULLUP);
  }

  select_delay_us = 10;
  step_delay_us = 12000;
  settle_delay_ms = 15;
  motor_delay_ms = 1000;
  watchdog_delay_ms = 1000;
  bus_type = BUSTYPE_IBMPC;

  is_drive_selected = false;
  is_motor_spinning = false;

  if (led_pin >= 0) {
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);
  }
}

/**************************************************************************/
/*!
    @brief Disables floppy communication, allowing pins to be used for general
   input and output
*/
void Adafruit_FloppyBase::end(void) {
  pinMode(_rddatapin, INPUT);
  // set write OFF
  if (_wrdatapin >= 0) {
    pinMode(_wrdatapin, INPUT);
  }
  if (_wrgatepin >= 0) {
    pinMode(_wrgatepin, INPUT);
  }
  if (_indexpin >= 0) {
    pinMode(_indexpin, INPUT);
  }
  is_drive_selected = false;
  is_motor_spinning = false;
}

/**************************************************************************/
/*!
    @brief  Set back the object and pins to initial state
*/
/**************************************************************************/
void Adafruit_Floppy::soft_reset(void) {
  Adafruit_FloppyBase::soft_reset();

  // deselect drive
  pinMode(_selectpin, OUTPUT);
  digitalWrite(_selectpin, HIGH);

  // motor enable pin, drive low to turn on motor
  pinMode(_motorpin, OUTPUT);
  digitalWrite(_motorpin, HIGH);

  // set motor direction (low is in, high is out)
  pinMode(_directionpin, OUTPUT);
  digitalWrite(_directionpin, LOW); // move inwards to start

  // step track pin, pulse low for 3us min, 3ms max per pulse
  pinMode(_steppin, OUTPUT);
  digitalWrite(_steppin, HIGH);

  // side selector
  pinMode(_sidepin, OUTPUT);
  digitalWrite(_sidepin, HIGH); // side 0 to start

  pinMode(_track0pin, INPUT_PULLUP);
  pinMode(_protectpin, INPUT_PULLUP);
  pinMode(_readypin, INPUT_PULLUP);

  // set low density
  pinMode(_densitypin, OUTPUT);
  digitalWrite(_densitypin, LOW);
}

/**************************************************************************/
/*!
    @brief  Poll the status of the index pulse
    @returns the status of the index pulse
*/
/**************************************************************************/
bool Adafruit_FloppyBase::read_index() {
#ifdef BUSIO_USE_FAST_PINIO
  return read_index_fast();
#else
  if (_indexpin >= 0) {
    return digitalRead(_indexpin);
  } else {
    return true;
  }
#endif
}

/**************************************************************************/
/*!
    @brief Disables floppy communication, allowing pins to be used for general
   input and output
*/
void Adafruit_Floppy::end(void) {
  pinMode(_selectpin, INPUT);
  pinMode(_motorpin, INPUT);
  pinMode(_directionpin, INPUT);
  pinMode(_steppin, INPUT);
  pinMode(_sidepin, INPUT);
  pinMode(_track0pin, INPUT);
  pinMode(_protectpin, INPUT);
  pinMode(_readypin, INPUT);
  pinMode(_densitypin, INPUT);
  Adafruit_FloppyBase::end();
}

/**************************************************************************/
/*!
    @brief Whether to select this drive
    @param selected True to select/enable
*/
/**************************************************************************/
void Adafruit_Floppy::select(bool selected) {
  digitalWrite(_selectpin, !selected); // Selected logic level 0!
  is_drive_selected = selected;
  // Select drive
  delayMicroseconds(select_delay_us);
}

/**************************************************************************/
/*!
    @brief Which head/side to read from
    @param head Head 0 or 1
    @return true if the head can be selected, false otherwise
    @note If _sidepin is no pin, then only head 0 can be selected
*/
/**************************************************************************/
bool Adafruit_Floppy::side(int head) {
  if (head != 0 && head != 1) {
    return false;
  }
  if (_sidepin == -1 && head != 0) {
    return false;
  }
  _side = head;
  digitalWrite(_sidepin, !head); // Head 0 is logic level 1, head 1 is logic 0!
  return true;
}

/**************************************************************************/
/*!
    @brief  Turn on or off the floppy motor, if on we wait till we get an index
   pulse!
    @param motor_on True to turn on motor, False to turn it off
    @returns False if turning motor on and no index pulse found, true otherwise
*/
/**************************************************************************/
bool Adafruit_Floppy::spin_motor(bool motor_on) {
  if (motor_on == is_motor_spinning)
    return true;  // Already in the correct state

  digitalWrite(_motorpin, !motor_on); // Motor on is logic level 0!
  is_motor_spinning = motor_on;
  if (!motor_on)
    return true; // we're done, easy!

  delay(motor_delay_ms); // Main motor turn on

  uint32_t index_stamp = millis();
  bool timedout = false;

  if (debug_serial)
    debug_serial->print("Waiting for index pulse...");

  while (read_index()) {
    if ((millis() - index_stamp) > 1000) {
      timedout = true; // its been a second
      break;
    }
    yield();
  }

  if (timedout) {
    if (debug_serial)
      debug_serial->println("Didn't find an index pulse!");
    return false;
  }
  if (debug_serial)
    debug_serial->println("Found!");
  return true;
}

/**************************************************************************/
/*!
    @brief  Seek to the desired track, requires the motor to be spun up!
    @param  track_num The track to step to
    @return True If we were able to get to the track location
*/
/**************************************************************************/
bool Adafruit_Floppy::goto_track(int track_num) {
  // track 0 is a very special case because its the only one we actually know we
  // got to. if we dont know where we are, or we're going to track zero, step
  // back till we get there.
  if ((_track < 0) || track_num == 0) {
    if (debug_serial)
      debug_serial->println("Going to track 0");

    // step back a lil more than expected just in case we really seeked out
    uint8_t max_steps = 100;
    while (max_steps--) {
      if (!digitalRead(_track0pin)) {
        _track = 0;
        break;
      }
      step(STEP_OUT, 1);
    }

    if (digitalRead(_track0pin)) {
      // we never got a track 0 indicator :(
      // what if we try stepping in a bit??

      max_steps = 20;
      while (max_steps--) {
        if (!digitalRead(_track0pin)) {
          _track = 0;
          break;
        }
        step(STEP_IN, 1);
      }

      if (digitalRead(_track0pin)) {
        // STILL not found!
        _track = -1; // We don't know where we really are
        if (debug_serial)
          debug_serial->println("Could not find track 0");
        return false; // we 'timed' out, were not able to locate track 0
      }
    }
  }
  delay(settle_delay_ms);

  // ok its a non-track 0 step, first, we cant go past 79 ok?
  track_num = min(track_num, FLOPPY_IBMPC_HD_TRACKS - 1);
  if (debug_serial)
    debug_serial->printf("Going to track %d\n\r", track_num);

  if (_track == track_num) { // we are there already
    return true;
  }

  int8_t steps = (int8_t)track_num - (int8_t)_track;
  if (steps > 0) {
    if (debug_serial)
      debug_serial->printf("Step in %d times\n\r", steps);
    step(STEP_IN, steps);
  } else {
    steps = abs(steps);
    if (debug_serial)
      debug_serial->printf("Step out %d times\n\r", steps);
    step(STEP_OUT, steps);
  }
  delay(settle_delay_ms);
  _track = track_num;

  return true;
}

/**************************************************************************/
/*!
    @brief  Step the track motor
    @param  dir STEP_OUT or STEP_IN depending on desired direction
    @param  times How many steps to take
*/
/**************************************************************************/
void Adafruit_Floppy::step(bool dir, uint8_t times) {
  digitalWrite(_directionpin, dir);
  delayMicroseconds(10); // 1 microsecond, but we're generous

  while (times--) {
    digitalWrite(_steppin, HIGH);
    delay((step_delay_us / 1000UL) + 1); // round up to at least 1ms
    digitalWrite(_steppin, LOW);
    delay((step_delay_us / 1000UL) + 1);
    digitalWrite(_steppin, HIGH); // end high
    yield();
  }
  // one more for good measure (5.25" drives seemed to like this)
  delay((step_delay_us / 1000UL) + 1);
}

/**************************************************************************/
/*!
    @brief  The current track location, based on internal caching
    @return The cached track location
*/
/**************************************************************************/
int Adafruit_Floppy::track(void) { return _track; }

int Adafruit_Floppy::get_side(void) { return _side; }

bool Adafruit_Floppy::get_write_protect(void) {
  if (_protectpin == -1) {
    return false;
  }
  return !digitalRead(_protectpin);
}

bool Adafruit_Floppy::get_ready_sense(void) {
  if (_readypin == 0) {
    return true;
  }
  return !digitalRead(_readypin);
}

bool Adafruit_Floppy::get_track0_sense(void) {
  if (_track0pin == 0) {
    return track() != 0;
  }
  return !digitalRead(_track0pin);
}

bool Adafruit_Floppy::set_density(bool high_density) {
  if (_densitypin == 0) {
    return !high_density;
  }
  digitalWrite(_densitypin, high_density);
  return true;
}

static void set_timings(uint32_t sampleFrequency, mfm_io_t &io,
                        float nominal_bit_time_us) {
  io.T1_nom = round(sampleFrequency * nominal_bit_time_us * 1e-6);
  io.T2_max = round(sampleFrequency * nominal_bit_time_us * 2.5e-6);
  io.T3_max = round(sampleFrequency * nominal_bit_time_us * 3.5e-6);
#if 0
  Serial.printf("sampleFrequency=%d nominal_bit_time=%fns T1_nom = %d T2_max = %d T3_max = %d\n",
          sampleFrequency, round(1000*nominal_bit_time_us), io.T1_nom, io.T2_max, io.T3_max);
#endif
}

/**************************************************************************/
/*!
    @brief  Decode one track of previously captured MFM data
    @param  sectors A pointer to an array of memory we can use to store into,
   512*n_sectors bytes
    @param  n_sectors The number of sectors (e.g., 18 for a
   standard 3.5", 1.44MB format)
    @param  sector_validity An array of values set to 1 if the sector was
   captured, 0 if not captured (no IDAM, CRC error, etc)
    @param  pulses An array of pulses from capture_track
    @param  n_pulses An array of pulses from capture_track
    @param  nominal_bit_time_us The nominal time of one MFM bit, usually 1.0f
   (double density) or 2.0f (high density)
    @param  clear_validity Whether to clear the validity flag. Set to false if
   re-reading a track with errors.
    @param  logical_track If not NULL, updated with the logical track number of
   the last sector read. (track & side numbers are not otherwise verified)
    @return Number of sectors we actually captured
*/
/**************************************************************************/
size_t Adafruit_FloppyBase::decode_track_mfm(
    uint8_t *sectors, size_t n_sectors, uint8_t *sector_validity,
    const uint8_t *pulses, size_t n_pulses, float nominal_bit_time_us,
    bool clear_validity, uint8_t *logical_track) {
  mfm_io_t io;

  if (clear_validity)
    memset(sector_validity, 0, n_sectors);
  set_timings(getSampleFrequency(), io, nominal_bit_time_us);

  io.pulses = const_cast<uint8_t *>(pulses);
  io.n_pulses = n_pulses;
  io.sectors = sectors;
  io.n_sectors = n_sectors;
  io.n = 2;
  io.head = get_side();
  io.cylinder_ptr = logical_track;
  io.sector_validity = sector_validity;

  return ::decode_track_mfm(&io);
}

/**************************************************************************/
/*!
    @brief  Encode one track of previously captured MFM data
    @param  sectors A pointer to an array of memory we can use to store into,
   512*n_sectors bytes
    @param  n_sectors The number of sectors (e.g., 18 for a
   standard 3.5", 1.44MB format)
    @param  pulses An array of pulses from capture_track
    @param  max_pulses The maximum number of pulses that may be stored
    @param  nominal_bit_time_us The nominal time of one MFM bit, usually 1.0f
   (double density) or 2.0f (high density)
    @param  logical_track The logical track number, or -1 to use track()
    @return Number of pulses actually generated
*/
/**************************************************************************/
size_t Adafruit_FloppyBase::encode_track_mfm(const uint8_t *sectors,
                                             size_t n_sectors, uint8_t *pulses,
                                             size_t max_pulses,
                                             float nominal_bit_time_us,
                                             uint8_t logical_track) {
  mfm_io_t io;

  set_timings(getSampleFrequency(), io, nominal_bit_time_us);

  io.pulses = pulses;
  io.n_pulses = max_pulses;
  io.sectors = const_cast<uint8_t *>(sectors);
  io.n_sectors = n_sectors;
  io.n = 2;
  io.head = get_side();
  io.cylinder = logical_track;
  io.sector_validity = NULL;

  ::encode_track_mfm(&io);
  return io.pos;
}

/**************************************************************************/
/*!
    @brief  Get the sample rate that we read and emit pulses at, platform and
   implementation-dependant
    @return Sample frequency in Hz, or 0 if not known
*/
/**************************************************************************/
uint32_t Adafruit_FloppyBase::getSampleFrequency(void) {
#if defined(__SAMD51__)
  return 48000000UL / g_timing_div;
#endif
#if defined(ARDUINO_ARCH_RP2040)
  return 24000000UL;
#endif
  return 0;
}

/**************************************************************************/
/*!
    @brief  Capture one track's worth of flux transitions, between two falling
   index pulses
    @param  pulses A pointer to an array of memory we can use to store into
    @param  max_pulses The size of the allocated pulses array
    @param  falling_index_offset Pointer to a uint32_t where we will store the
    "flux index" where the second index pulse fell. usually we read 110-125% of
    one track so there is an overlap of index pulse reads
    @param  store_greaseweazle Pass in true to pack long pulses with two bytes
    @param  capture_ms If not zero, we will capture at least one revolution and
   extra time will be determined by this variable. e.g. 250ms means one
   revolution plus about 50 ms post-index
    @param  index_wait_ms If not zero, wait at most this many ms for an index
   pulse to arrive
    @return Number of pulses we actually captured
*/
/**************************************************************************/
size_t Adafruit_FloppyBase::capture_track(
    volatile uint8_t *pulses, size_t max_pulses, int32_t *falling_index_offset,
    bool store_greaseweazle, uint32_t capture_ms, uint32_t index_wait_ms) {
  memset((void *)pulses, 0, max_pulses); // zero zem out

#if defined(ARDUINO_ARCH_RP2040)
  return rp2040_flux_capture(_indexpin, _rddatapin, pulses, pulses + max_pulses,
                             falling_index_offset, store_greaseweazle,
                             capture_ms * (getSampleFrequency() / 1000),
                             index_wait_ms);
#elif defined(__SAMD51__)
  noInterrupts();
  if (index_wait_ms) {
    wait_for_index_pulse_low();
  }

  disable_capture();
  // in case the timer was reused, we will re-init it each time!
  init_capture();
  // allow interrupts
  interrupts();
  int32_t start_time = millis();
  // init global interrupt data
  g_flux_pulses = pulses;
  g_max_pulses = max_pulses;
  g_n_pulses = 0;
  g_store_greaseweazle = store_greaseweazle;
  // enable capture
  enable_capture();
  // meanwhile... wait for *second* low pulse
  if (index_wait_ms) {
    wait_for_index_pulse_low();
    // track when it happened for later...
    *falling_index_offset = g_n_pulses;
  }

  if (!capture_ms) {
    // wait another 50ms which is about 1/4 of a track
    delay(50);
  } else {
    int32_t remaining = capture_ms - (millis() - start_time);
    if (remaining > 0) {
      debug_serial->printf("Delaying another %d ms post-index\n\r", remaining);
      delay(remaining);
    }
  }
  // ok we're done, clean up!
  disable_capture();
  deinit_capture();
  return g_n_pulses;

#else // bitbang it!

  noInterrupts();
  if (index_wait_ms) {
    wait_for_index_pulse_low();
  }
#ifdef BUSIO_USE_FAST_PINIO
  BusIO_PortReg *dataPort, *ledPort;
  BusIO_PortMask dataMask, ledMask;
  dataPort = (BusIO_PortReg *)portInputRegister(digitalPinToPort(_rddatapin));
  dataMask = digitalPinToBitMask(_rddatapin);
  ledPort = (BusIO_PortReg *)portOutputRegister(digitalPinToPort(led_pin));
  ledMask = digitalPinToBitMask(led_pin);
  (void)ledPort;
  (void)ledMask;
#endif

  unsigned pulse_count;
  volatile uint8_t *pulses_ptr = pulses;
  volatile uint8_t *pulses_end = pulses + max_pulses;

  // wait for one clean flux pulse so we dont get cut off.
  // don't worry about losing this pulse, we'll get it on our
  // overlap run!

  // ok we have a h-to-l transition so...
  bool last_index_state = read_index();
  uint8_t index_transitions = 0;

  // if data line is low, wait till it rises
  if (!read_data()) {
    while (!read_data())
      ;
  }
  // if data line is high, wait till it drops down
  if (read_data()) {
    while (read_data())
      ;
  }

  while (true) {
    bool index_state = read_index();
    // ahh a L to H transition
    if (index_wait_ms && !last_index_state && index_state) {
      index_transitions++;
      if (index_transitions == 2)
        break; // and its the second one, so we're done with this track!
    }
    // ooh a H to L transition, thats 1 revolution
    else if (last_index_state && !index_state) {
      // we'll keep track of when it happened
      *falling_index_offset = (pulses_ptr - pulses);
    }
    last_index_state = index_state;

    // muahaha, now we can read track data!
    // Don't start counting at zero because we lost some time checking for
    // index. Empirically, at 180MHz and -O3 on M4, this gives the most 'even'
    // timings, moving the bins from 41/63/83 to 44/66/89
    pulse_count = 3;

    // while pulse is in the low pulse, count up
    while (!read_data()) {
      pulse_count++;
    }
    set_debug_led();

    // while pulse is high, keep counting up
    while (read_data())
      pulse_count++;
    clr_debug_led();

    pulses_ptr[0] = min(255u, pulse_count);
    pulses_ptr++;
    if (pulses_ptr == pulses_end) {
      break;
    }
  }
  // whew done
  interrupts();
  return pulses_ptr - pulses;
#endif
}

/**************************************************************************/
/*!
    @brief  Write one track of flux pulse data, starting at the index pulse
    @param  pulses An array of timer-count pulses
    @param  n_pulses How many bytes are in the pulse array
    @param  store_greaseweazle If true, long pulses are 'packed' in gw format
    @param  use_index If true, write starts at the index pulse.
    @returns False if the data could not be written (samd51 cannot write apple
   flux format)
*/
/**************************************************************************/
bool Adafruit_FloppyBase::write_track(uint8_t *pulses, size_t n_pulses,
                                      bool store_greaseweazle, bool use_index) {
#if defined(ARDUINO_ARCH_RP2040)
  return rp2040_flux_write(_indexpin, _wrgatepin, _wrdatapin, pulses,
                           pulses + n_pulses, store_greaseweazle, _is_apple2,
                           use_index);
#elif defined(__SAMD51__)
  if (_is_apple2) {
    return false;
  }
  pinMode(_wrdatapin, OUTPUT);
  digitalWrite(_wrdatapin, HIGH);

  pinMode(_wrgatepin, OUTPUT);
  digitalWrite(_wrgatepin, HIGH);

  disable_generate();
  // in case the timer was reused, we will re-init it each time!
  init_generate();

  // init global interrupt data
  g_flux_pulses = pulses;
  g_max_pulses = n_pulses;
  g_n_pulses = 1; // Pulse 0 is config'd below...this is NEXT pulse index
  g_store_greaseweazle = store_greaseweazle;
  g_writing_pulses = true;

  wait_for_index_pulse_low();
  // start teh writin'
  digitalWrite(_wrgatepin, LOW);
  enable_generate();

  bool last_index_state = read_index();
  while (g_writing_pulses) {
    bool index_state = read_index();
    // ahh a H to L transition, we have done one revolution
    if (last_index_state && !index_state) {
      break;
    }
    last_index_state = index_state;
    yield();
  }

  // ok we're done, clean up!
  digitalWrite(_wrgatepin, HIGH);
  disable_generate();
  deinit_generate();

  return true;
#else // bitbang it!
  if (_is_apple2) {
    return false;
  }
  uint8_t *pulses_ptr = pulses;

#ifdef BUSIO_USE_FAST_PINIO
  BusIO_PortReg *writePort, *ledPort;
  BusIO_PortMask writeMask, ledMask;
  writePort = (BusIO_PortReg *)portOutputRegister(digitalPinToPort(_wrdatapin));
  writeMask = digitalPinToBitMask(_wrdatapin);
  ledPort = (BusIO_PortReg *)portOutputRegister(digitalPinToPort(led_pin));
  ledMask = digitalPinToBitMask(led_pin);
  (void)ledPort;
  (void)ledMask;
#endif

  pinMode(_wrdatapin, OUTPUT);
  digitalWrite(_wrdatapin, HIGH);

  pinMode(_wrgatepin, OUTPUT);
  digitalWrite(_wrgatepin, HIGH);

  noInterrupts();
  wait_for_index_pulse_low();
  digitalWrite(_wrgatepin, LOW);

  // write track data
  while (n_pulses--) {
    uint8_t pulse_count = pulses_ptr[0];
    pulses_ptr++;
    // ?? lets bail
    if (pulse_count == 0)
      break;

    clr_write();
    pulse_count -= 11;
    while (pulse_count--) {
      asm("nop; nop; nop; nop; nop;");
    }
    set_write();
    pulse_count = 8;
    while (pulse_count--) {
      asm("nop; nop; nop; nop; nop; nop; nop; nop; nop;");
    }
  }
  // whew done
  digitalWrite(_wrgatepin, HIGH);
  digitalWrite(_wrdatapin, HIGH);
  interrupts();
  return true;
#endif
}

/**************************************************************************/
/*!
    @brief  Busy wait until the index line goes from high to low
*/
/**************************************************************************/
void Adafruit_FloppyBase::wait_for_index_pulse_low(void) {
  // initial state
  bool index_state = read_index();
  bool last_index_state = index_state;

  // wait until last index state is H and current state is L
  while (true) {
    index_state = read_index();
    if (last_index_state && !index_state) {
      return;
    }
    last_index_state = index_state;
  }
}

/**************************************************************************/
/*!
    @brief  Pretty print the counts in a list of flux transitions
    @param  pulses A pointer to an array of memory containing pulse counts
    @param  n_pulses The size of the pulses in the array
    @param  is_gw_format Set to true if we pack long pulses with two bytes
*/
/**************************************************************************/
void Adafruit_FloppyBase::print_pulses(uint8_t *pulses, size_t n_pulses,
                                       bool is_gw_format) {
  if (!debug_serial)
    return;

  uint16_t pulse_len;
  for (uint32_t i = 0; i < n_pulses; i++) {
    uint8_t p = pulses[i];
    if (p < 250 || !is_gw_format) {
      pulse_len = p;
    } else {
      // Serial.printf("long pulse! %d and %d ->", p, pulses[i+1]);
      pulse_len = 250 + ((uint16_t)p - 250) * 255;
      i++;
      pulse_len += pulses[i] - 1;
      // Serial.printf(" %d \n\r", pulse_len);
    }

    debug_serial->print(pulse_len);
    debug_serial->print(", ");
  }
  debug_serial->println();
}
/**************************************************************************/
/*!
    @brief  Pretty print a simple histogram of flux transitions
    @param  pulses A pointer to an array of memory containing pulse counts
    @param  n_pulses The size of the pulses in the array
    @param  max_bins The maximum number of histogram bins to use (default 64)
    @param  is_gw_format Set to true if we pack long pulses with two bytes
    @param  min_bin_size Bins with fewer samples than this are skipped, not
   printed
*/
/**************************************************************************/
void Adafruit_FloppyBase::print_pulse_bins(uint8_t *pulses, size_t n_pulses,
                                           uint8_t max_bins, bool is_gw_format,
                                           uint32_t min_bin_size) {
  (void)is_gw_format;
  if (!debug_serial) {
    return;
  }
  uint32_t pulse_len = 0;

  // lets bin em!
  uint32_t bins[max_bins][2];
  memset(bins, 0, max_bins * 2 * sizeof(uint32_t));
  // we'll add each pulse to a bin so we can figure out the 3 buckets
  for (uint32_t i = 0; i < n_pulses; i++) {
    uint8_t p = pulses[i];
    if (p < 250) {
      pulse_len = p;
    } else {
      // Serial.printf("long pulse! %d and %d ->", p, pulses[i+1]);
      pulse_len = 250 + ((uint16_t)p - 250) * 255;
      i++;
      pulse_len += pulses[i] - 1;
      // Serial.printf(" %d \n\r", pulse_len);
    }
    // find a bin for this pulse
    uint8_t bin = 0;
    for (bin = 0; bin < max_bins; bin++) {
      // bin already exists? increment the count!
      if (bins[bin][0] == pulse_len) {
        bins[bin][1]++;
        break;
      }
      if (bins[bin][0] == 0) {
        // ok we never found the bin, so lets make it this one!
        bins[bin][0] = pulse_len;
        bins[bin][1] = 1;
        break;
      }
    }
    if (bin == max_bins)
      debug_serial->println("oof we ran out of bins but we'll keep going");
  }
  // this is a very lazy way to print the bins sorted
  bool gap = false;
  for (uint16_t pulse_w = 1; pulse_w < 512; pulse_w++) {
    for (uint8_t b = 0; b < max_bins; b++) {
      if (bins[b][0] == pulse_w) {
        if (bins[b][1] >= min_bin_size) {
          if (gap)
            debug_serial->println("-------");
          gap = false;
          debug_serial->print(bins[b][0]);
          debug_serial->print(": ");
          debug_serial->println(bins[b][1]);
        } else {
          gap = true;
        }
      }
    }
  }
}

/**************************************************************************/
/*!
    @brief  Create a hardware interface to a floppy drive
    @param  indexpin A pin connected to the floppy Index Sensor output
    @param  selectpin A pin connected to the floppy Drive Select input
    @param  phase1pin A pin connected to the floppy "phase 1" output
    @param  phase2pin A pin connected to the floppy "phase 2" output
    @param  phase3pin A pin connected to the floppy "phase 3" output
    @param  phase4pin A pin connected to the floppy "phase 4" output
    @param  wrdatapin A pin connected to the floppy Write Data input
    @param  wrgatepin A pin connected to the floppy Write Gate input
    @param  protectpin A pin connected to the floppy Write Protect Sensor output
    @param  rddatapin A pin connected to the floppy Read Data output
*/
/**************************************************************************/

Adafruit_Apple2Floppy::Adafruit_Apple2Floppy(int8_t indexpin, int8_t selectpin,
                                             int8_t phase1pin, int8_t phase2pin,
                                             int8_t phase3pin, int8_t phase4pin,
                                             int8_t wrdatapin, int8_t wrgatepin,
                                             int8_t protectpin,
                                             int8_t rddatapin)
    : Adafruit_FloppyBase{indexpin, wrdatapin, wrgatepin, rddatapin, true},
      _selectpin{selectpin}, _phase1pin{phase1pin}, _phase2pin{phase2pin},
      _phase3pin{phase3pin}, _phase4pin{phase4pin}, _protectpin{protectpin} {}

void Adafruit_Apple2Floppy::end() {
  pinMode(_selectpin, INPUT);
  pinMode(_phase1pin, INPUT);
  pinMode(_phase2pin, INPUT);
  pinMode(_phase3pin, INPUT);
  pinMode(_phase4pin, INPUT);
  Adafruit_FloppyBase::end();
}

/**************************************************************************/
/*!
    @brief  Initializes the GPIO pins but do not start the motor or anything
*/
/**************************************************************************/
void Adafruit_Apple2Floppy::soft_reset() {
  Adafruit_FloppyBase::soft_reset();

  // deselect drive
  pinMode(_selectpin, OUTPUT);
  digitalWrite(_selectpin, HIGH);

  // Turn off stepper motors
  pinMode(_phase1pin, OUTPUT);
  digitalWrite(_phase1pin, LOW);

  pinMode(_phase2pin, OUTPUT);
  digitalWrite(_phase2pin, LOW);

  pinMode(_phase3pin, OUTPUT);
  digitalWrite(_phase3pin, LOW);

  pinMode(_phase4pin, OUTPUT);
  digitalWrite(_phase4pin, LOW);
}

/**************************************************************************/
/*!
    @brief Whether to select this drive
    @param selected True to select/enable
*/
/**************************************************************************/
void Adafruit_Apple2Floppy::select(bool selected) {
  digitalWrite(_selectpin, !selected);
  is_drive_selected = selected;
  // Selecting the drive also turns the motor on, but we need to look
  // for index pulses, so leave that job to spin_motor. Deselecting the
  // drive will turn it off though.
  if (!selected)
    is_motor_spinning = false;

  if (debug_serial)
    debug_serial->printf("set selectpin %d to %d\n", _selectpin, !selected);
}

/**************************************************************************/
/*!
    @brief  Wait for index pulse
    @param motor_on True to wait for index pulse, false to do nothing
    @returns False if turning motor on and no index pulse found, true otherwise

    @note The Apple II floppy has a single "select/enable" pin which selects the
   drive and turns on the spindle.
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::spin_motor(bool motor_on) {
  if (motor_on == is_motor_spinning) return true;  // already in correct state
  if (motor_on) {
    delay(motor_delay_ms); // Main motor turn on

    uint32_t index_stamp = millis();
    bool timedout = false;

    if (debug_serial)
      debug_serial->print("Waiting for index pulse...");

    while (!read_index()) {
      if ((millis() - index_stamp) > 10000) {
        timedout = true; // its been 10 seconds?
        break;
      }
    }

    while (read_index()) {
      if ((millis() - index_stamp) > 10000) {
        timedout = true; // its been 10 seconds?
        break;
      }
    }

    if (timedout) {
      if (debug_serial)
        debug_serial->println("Didn't find an index pulse!");
      return false;
    }
    if (debug_serial)
      debug_serial->println("Found!");
  }
  is_motor_spinning = motor_on;
  return true;
}

// stepping FORWARD through phases steps OUT towards SMALLER track numbers
// stepping BACKWARD through phases steps IN towards BIGGER track numbers
static const uint8_t phases[] = {
    0b1000, 0b1100, 0b0100, 0b0110, 0b0010, 0b0011, 0b0001, 0b1001,
};

enum {
  STEP_OUT_QUARTER = -1,
  STEP_OUT_HALF = -2,
  STEP_IN_HALF = 2,
  STEP_IN_QUARTER = 1,
};

/**************************************************************************/
/*!
    @brief  Seek to the desired track, requires the motor to be spun up!
    @param  track_num The track to step to
    @return True If we were able to get to the track location
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::goto_track(int track_num) {
  if (_quartertrack == -1) {
    _quartertrack = 160;
    goto_quartertrack(0);
  }
  int quartertrack = track_num * _step_multiplier();
  return goto_quartertrack(quartertrack);
}

/**************************************************************************/
/*!
    @brief  Seek to the desired quarter track, requires the motor to be spun up!
    @param  quartertrack The position to step to
    @return True If we were able to get to the track location
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::goto_quartertrack(int quartertrack) {
  if (quartertrack < 0 || quartertrack >= 160) {
    return false;
  }

  int diff = (int)quartertrack - (int)this->_quartertrack;

  if (debug_serial) {
    debug_serial->printf("Stepping from %d -> %d, diff = %d\n",
                         this->_quartertrack, quartertrack, diff);
  }

  if (diff != 0) {
    if (diff < 0) {
      // step OUT to SMALLER track_num numbers
      _step(STEP_OUT_QUARTER, -diff);
    } else {
      // step IN to LARGER track_num numbers
      _step(STEP_IN_QUARTER, diff);
    }
    delay(settle_delay_ms);
  }

  // according to legend, Apple DOS always disables all phases after settling
  digitalWrite(_phase1pin, 0);
  digitalWrite(_phase2pin, 0);
  digitalWrite(_phase3pin, 0);
  digitalWrite(_phase4pin, 0);

  _quartertrack = quartertrack;

  return true;
}

void Adafruit_Apple2Floppy::_step(int direction, int count) {
  if (debug_serial)
    debug_serial->printf("Step by %d x %d\n", direction, count);
  for (; count--;) {
    _quartertrack += direction;
    auto phase = _quartertrack % 8;

    digitalWrite(_phase1pin, phases[phase] & 8);
    digitalWrite(_phase2pin, phases[phase] & 4);
    digitalWrite(_phase3pin, phases[phase] & 2);
    digitalWrite(_phase4pin, phases[phase] & 1);
    delay((step_delay_us / 1000UL) + 1); // round up to at least 1ms
  }
}

/**************************************************************************/
/*!
    @brief Which head/side to read from
    @param head Head must be 0
    @return true if the head is 0, false otherwise
    @note Apple II floppy drives only have a single side
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::side(int head) { return head == 0; }

/**************************************************************************/
/*!
    @brief  The current track location, based on internal caching
    @return The cached track location
    @note Partial tracks are rounded, with quarter tracks always rounded down.
*/
/**************************************************************************/
int Adafruit_Apple2Floppy::track(void) {
  if (_quartertrack == -1) {
    return -1;
  }
  auto m = _step_multiplier();
  return (_quartertrack + m / 2) / m;
}

/**************************************************************************/
/*!
    @brief  Check the write protect status of the floppy
    @return true if the floppy is write protected, false otherwise

    @note The write protect circuit in the Apple II floppy drive is
    "interesting".  Because of how it was read by the Apple II Disk Interface
   Card, the protect output is only active when the "phase 1" winding is
   energized; having the "phase 1" winding active also prevents writing, but at
   the Disk Interface Card, not at the drive. So, it's necessary for us to check
    write_protected in software!

    If_protectpin is -1 (not available), then we always report that the disk is
   write protected.
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::get_write_protect(void) {
  if (_protectpin == -1) {
    return true;
  }

  auto t = quartertrack();
  // we need to be on an even-numbered track, so that activating the "phase 1"
  // winding doesn't pull out of position. We'll return to the right spot below.
  goto_quartertrack(t & ~7);

  // goto_track() has deenergized all windings, we need to enable phase1
  // temporarily
  digitalWrite(_phase1pin, 1);
  // ensure that the output has time to rise, 1ms is plenty
  delay(1);

  // only now can we read the protect pin status
  bool result = digitalRead(_protectpin);

  // Return to where we were before...!
  goto_quartertrack(t);

  return result;
}

/**************************************************************************/
/*!
    @brief  Set the density for flux reading and writing
    @param high_density true to select high density, false to select low
   density.
    @return true if low density mode is selected, false if high density is
   selected
    @note The drive hardware is only capable of single density operation
*/
bool Adafruit_Apple2Floppy::set_density(bool high_density) {
  return high_density == false;
}

/**************************************************************************/
/*!
    @brief  Check whether the track0 sensor is active
    @returns True if the track0 sensor is active, false otherwise
    @note This device has no home sensor so it just returns track() == 0.
*/
/**************************************************************************/
bool Adafruit_Apple2Floppy::get_track0_sense(void) { return track() == 0; }

/**************************************************************************/
/*!
    @brief  Get the drive position in quarter tracks
    @returns True if the track0 sensor is active, false otherwise
    @note Returns -1 if the position is unknown
*/
/**************************************************************************/
int Adafruit_Apple2Floppy::quartertrack() { return _quartertrack; }

/**************************************************************************/
/*!
    @brief  Set the positioning mode
    @param step_mode The new positioning mode
    @note This does not re-position the drive
*/
/**************************************************************************/
void Adafruit_Apple2Floppy::step_mode(StepMode step_mode) {
  _step_mode = step_mode;
}

int Adafruit_Apple2Floppy::_step_multiplier(void) const {
  switch (_step_mode) {
  case STEP_MODE_WHOLE:
    return 4;
  case STEP_MODE_HALF:
    return 2;
  default:
  case STEP_MODE_QUARTER:
    return 1;
  }
}
