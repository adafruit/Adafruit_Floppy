#include "Adafruit_Floppy.h"

#define DEBUG_FLOPPY (0)

// We need to read and write some pins at optimized speeds - use raw registers
// or native SDK API!
#ifdef BUSIO_USE_FAST_PINIO
#define read_index() (*indexPort & indexMask)
#define read_data() (*dataPort & dataMask)
#define set_debug_led() (*ledPort |= ledMask)
#define clr_debug_led() (*ledPort &= ~ledMask)
#define set_write() (*writePort |= writeMask)
#define clr_write() (*writePort &= ~writeMask)
#elif defined(ARDUINO_ARCH_RP2040)
#define read_index() gpio_get(_indexpin)
#define read_data() gpio_get(_rddatapin)
#define set_debug_led() gpio_put(led_pin, 1)
#define clr_debug_led() gpio_put(led_pin, 0)
#define set_write() gpio_put(_wrdatapin, 1)
#define clr_write() gpio_put(_wrdatapin, 0)
extern uint32_t rp2040_flux_capture(int indexpin, int rdpin,
                                    volatile uint8_t *pulses,
                                    volatile uint8_t *end,
                                    uint32_t *falling_index_offset,
                                    bool store_greaseweazle);
#endif

#if !DEBUG_FLOPPY
#undef set_debug_led
#undef clr_debug_led
#define set_debug_led() ((void)0)
#define clr_debug_led() ((void)0)
#endif

struct mfm_io {
  bool index_state;
  unsigned index_count;
  uint16_t T2_5, T3_5;
};

#include "mfm_impl.h"

#if defined(__SAMD51__)
extern volatile uint8_t *g_flux_pulses;
extern volatile uint32_t g_max_pulses;
extern volatile uint32_t g_num_pulses;
extern volatile bool g_store_greaseweazle;
extern volatile uint8_t g_timing_div;
extern volatile bool g_writing_pulses;
#endif

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
                                 int8_t readypin) {
  _densitypin = densitypin;
  _indexpin = indexpin;
  _selectpin = selectpin;
  _motorpin = motorpin;
  _directionpin = directionpin;
  _steppin = steppin;
  _wrdatapin = wrdatapin;
  _wrgatepin = wrgatepin;
  _track0pin = track0pin;
  _protectpin = protectpin;
  _rddatapin = rddatapin;
  _sidepin = sidepin;
  _readypin = readypin;
}

/**************************************************************************/
/*!
    @brief  Initializes the GPIO pins but do not start the motor or anything
    @returns True if able to set up all pins and capture/waveform peripherals
*/
/**************************************************************************/
bool Adafruit_Floppy::begin(void) {
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
    @brief  Set back the object and pins to initial state
*/
/**************************************************************************/
void Adafruit_Floppy::soft_reset(void) {
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

  pinMode(_indexpin, INPUT_PULLUP);
  pinMode(_track0pin, INPUT_PULLUP);
  pinMode(_protectpin, INPUT_PULLUP);
  pinMode(_readypin, INPUT_PULLUP);
  pinMode(_rddatapin, INPUT_PULLUP);

  // set low density
  pinMode(_densitypin, OUTPUT);
  digitalWrite(_densitypin, LOW);

  // set write OFF
  if (_wrdatapin >= 0) {
    pinMode(_wrdatapin, OUTPUT);
    digitalWrite(_wrdatapin, HIGH);
  }
  if (_wrgatepin >= 0) {
    pinMode(_wrgatepin, OUTPUT);
    digitalWrite(_wrgatepin, HIGH);
  }

#ifdef BUSIO_USE_FAST_PINIO
  indexPort = (BusIO_PortReg *)portInputRegister(digitalPinToPort(_indexpin));
  indexMask = digitalPinToBitMask(_indexpin);
#endif

  select_delay_us = 10;
  step_delay_us = 10000;
  settle_delay_ms = 15;
  motor_delay_ms = 1000;
  watchdog_delay_ms = 1000;
  bus_type = BUSTYPE_IBMPC;

  if (led_pin >= 0) {
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);
  }
}

/**************************************************************************/
/*!
    @brief Whether to select this drive
    @param selected True to select/enable
*/
/**************************************************************************/
void Adafruit_Floppy::select(bool selected) {
  digitalWrite(_selectpin, !selected); // Selected logic level 0!
  // Select drive
  delayMicroseconds(select_delay_us);
}

/**************************************************************************/
/*!
    @brief Which head/side to read from
    @param head Head 0 or 1
*/
/**************************************************************************/
void Adafruit_Floppy::side(uint8_t head) {
  digitalWrite(_sidepin, !head); // Head 0 is logic level 1, head 1 is logic 0!
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
  digitalWrite(_motorpin, !motor_on); // Motor on is logic level 0!
  if (!motor_on)
    return true; // we're done, easy!

  delay(motor_delay_ms); // Main motor turn on

  uint32_t index_stamp = millis();
  bool timedout = false;

  if (debug_serial)
    debug_serial->print("Waiting for index pulse...");

  while (digitalRead(_indexpin)) {
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
  return true;
}

/**************************************************************************/
/*!
    @brief  Seek to the desired track, requires the motor to be spun up!
    @param  track_num The track to step to
    @return True If we were able to get to the track location
*/
/**************************************************************************/
bool Adafruit_Floppy::goto_track(uint8_t track_num) {
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
int8_t Adafruit_Floppy::track(void) { return _track; }

/**************************************************************************/
/*!
    @brief  Capture and decode one track of MFM data
    @param  sectors A pointer to an array of memory we can use to store into,
   512*n_sectors bytes
    @param  n_sectors The number of sectors (e.g., 18 for a
   standard 3.5", 1.44MB format)
    @param  sector_validity An array of values set to 1 if the sector was
   captured, 0 if not captured (no IDAM, CRC error, etc)
    @return Number of sectors we actually captured
*/
/**************************************************************************/
uint32_t Adafruit_Floppy::read_track_mfm(uint8_t *sectors, size_t n_sectors,
                                         uint8_t *sector_validity, bool high_density) {
  mfm_io_t io;

  if (high_density) {
    io.T2_5 = getSampleFrequency() * 5 / 2 / 1000000;
    io.T3_5 = getSampleFrequency() * 7 / 2 / 1000000;
  } else {
    io.T2_5 = getSampleFrequency() * 5 / 1000000;
    io.T3_5 = getSampleFrequency() * 7 / 1000000;
  }
  init_capture();
  start_polled_capture();
  int result = read_track(io, n_sectors, sectors, sector_validity);
  disable_capture();

  return result;
}

/**************************************************************************/
/*!
    @brief  Get the sample rate that we read and emit pulses at, platform and
   implementation-dependant
    @return Sample frequency in Hz, or 0 if not known
*/
/**************************************************************************/
uint32_t Adafruit_Floppy::getSampleFrequency(void) {
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
    @return Number of pulses we actually captured
*/
/**************************************************************************/
uint32_t Adafruit_Floppy::capture_track(volatile uint8_t *pulses,
                                        uint32_t max_pulses,
                                        uint32_t *falling_index_offset,
                                        bool store_greaseweazle) {
  memset((void *)pulses, 0, max_pulses); // zero zem out

#if defined(ARDUINO_ARCH_RP2040)
  return rp2040_flux_capture(_indexpin, _rddatapin, pulses, pulses + max_pulses,
                             falling_index_offset, store_greaseweazle);
#elif defined(__SAMD51__)
  noInterrupts();
  wait_for_index_pulse_low();

  disable_capture();
  // in case the timer was reused, we will re-init it each time!
  init_capture();
  // allow interrupts
  interrupts();
  // init global interrupt data
  g_flux_pulses = pulses;
  g_max_pulses = max_pulses;
  g_num_pulses = 0;
  g_store_greaseweazle = store_greaseweazle;
  // enable capture
  enable_capture();
  // meanwhile... wait for *second* low pulse
  wait_for_index_pulse_low();
  // track when it happened for later...
  *falling_index_offset = g_num_pulses;
  // wait another 50ms which is about 1/4 of a track
  delay(50);
  // ok we're done, clean up!
  disable_capture();
  deinit_capture();
  return g_num_pulses;

#else // bitbang it!

  noInterrupts();
  wait_for_index_pulse_low();

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
    if (!last_index_state && index_state) {
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


void Adafruit_Floppy::write_track(uint8_t *pulses, uint32_t num_pulses,
                                  bool store_greaseweazle) {
#if defined(__SAMD51__)

  pinMode(_wrdatapin, OUTPUT);
  digitalWrite(_wrdatapin, HIGH);

  pinMode(_wrgatepin, OUTPUT);
  digitalWrite(_wrgatepin, HIGH);

  disable_generate();
  // in case the timer was reused, we will re-init it each time!
  init_generate();

  // init global interrupt data
  g_flux_pulses = pulses;
  g_max_pulses = num_pulses;
  g_num_pulses = 1;  // Pulse 0 is config'd below...this is NEXT pulse index
  g_store_greaseweazle = store_greaseweazle;
  g_writing_pulses = true;

  wait_for_index_pulse_low();
  // start teh writin'
  digitalWrite(_wrgatepin, LOW);
  enable_generate();

  bool last_index_state = read_index();
  uint8_t index_transitions = 0;
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

#else // bitbang it!
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
  while (num_pulses--) {
    uint8_t pulse_count = pulses_ptr[0];
    pulses_ptr++;
    // ?? lets bail
    if (pulse_count == 0) break;

    clr_write();
    pulse_count -= 11;
    while(pulse_count--) {
      asm("nop; nop; nop; nop; nop;");
    }
    set_write();
    pulse_count = 8;
    while(pulse_count--) {
      asm("nop; nop; nop; nop; nop; nop; nop; nop; nop;");
    }
  }
  // whew done
  digitalWrite(_wrgatepin, HIGH);
  digitalWrite(_wrdatapin, HIGH);
  interrupts();
#endif
  return;
}

/**************************************************************************/
/*!
    @brief  Busy wait until the index line goes from high to low
*/
/**************************************************************************/
void Adafruit_Floppy::wait_for_index_pulse_low(void) {
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
    @param  num_pulses The size of the pulses in the array
    @param  is_gw_format Set to true if we pack long pulses with two bytes
*/
/**************************************************************************/
void Adafruit_Floppy::print_pulses(uint8_t *pulses, uint32_t num_pulses,
                                   bool is_gw_format) {
  if (!debug_serial)
    return;

  uint16_t pulse_len;
  for (uint32_t i = 0; i < num_pulses; i++) {
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
    @param  num_pulses The size of the pulses in the array
    @param  max_bins The maximum number of histogram bins to use (default 64)
    @param  is_gw_format Set to true if we pack long pulses with two bytes
*/
/**************************************************************************/
void Adafruit_Floppy::print_pulse_bins(uint8_t *pulses, uint32_t num_pulses,
                                       uint8_t max_bins, bool is_gw_format) {
  if (!debug_serial)
    return;
  uint32_t pulse_len = 0;

  // lets bin em!
  uint32_t bins[max_bins][2];
  memset(bins, 0, max_bins * 2 * sizeof(uint32_t));
  // we'll add each pulse to a bin so we can figure out the 3 buckets
  for (uint32_t i = 0; i < num_pulses; i++) {
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
  for (uint16_t pulse_w = 1; pulse_w < 512; pulse_w++) {
    for (uint8_t b = 0; b < max_bins; b++) {
      if (bins[b][0] == pulse_w) {
        debug_serial->print(bins[b][0]);
        debug_serial->print(": ");
        debug_serial->println(bins[b][1]);
      }
    }
  }
}

uint16_t mfm_io_sample_flux(bool *index);

static inline mfm_io_symbol_t mfm_io_read_symbol(mfm_io_t *io) {
    bool new_index_state;
    uint16_t fluxval = mfm_io_sample_flux(&new_index_state);
    if(io->index_state && !new_index_state) {
        io->index_count ++;
    }
    io->index_state = new_index_state;
    if(fluxval > io->T3_5) { return pulse_1000; }
    if(fluxval > io->T2_5) { return pulse_100; }
    return pulse_10;
}

static inline void mfm_io_reset_sync_count(mfm_io_t *io) {
    io->index_count = 0;
}

static inline int mfm_io_get_sync_count(mfm_io_t *io) {
    return io->index_count;
}

uint16_t Adafruit_Floppy::sample_flux(bool &index) {
  return ::mfm_io_sample_flux(&index);
}
