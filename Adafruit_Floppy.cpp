#include "Adafruit_Floppy.h"

#define read_index() (*indexPort & indexMask)
#define read_data() (*dataPort & dataMask)

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
*/
/**************************************************************************/
void Adafruit_Floppy::begin(void) {
  soft_reset();
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

  indexPort = (BusIO_PortReg *)portInputRegister(digitalPinToPort(_indexpin));
  indexMask = digitalPinToBitMask(_indexpin);

  select_delay_us = 1000;
  step_delay_us = 3000;
  settle_delay_ms = 10;
  motor_delay_ms = 1000;
  watchdog_delay_ms = 1000;
  bus_type = BUSTYPE_IBMPC;
}

void Adafruit_Floppy::select(bool selected) {
  digitalWrite(_selectpin, !selected);  // Selected logic level 0!
  // Select drive
  delayMicroseconds(select_delay_us);
}

void Adafruit_Floppy::side(uint8_t head) {
  digitalWrite(_sidepin, !head);  // Head 0 is logic level 1, head 1 is logic 0!
}


/**************************************************************************/
/*!
    @brief  Turn on or off the floppy motor
    @param motor_on True to turn on motor, False to turn it off
*/
/**************************************************************************/
void Adafruit_Floppy::spin_motor(bool motor_on) {
  digitalWrite(_motorpin, !motor_on);  // Motor on is logic level 0!
  if (motor_on)   // Main motor turn on
    delay(motor_delay_ms);
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
  // got to
  if (track_num == 0) {
    uint8_t max_steps = MAX_TRACKS;
    while (max_steps--) {
      if (!digitalRead(_track0pin)) {
        _track = 0;
        return true;
      }
      step(STEP_OUT, 1);
    }
    return false; // we 'timed' out!
  }
  if (!goto_track(0))
    return false;
  step(STEP_IN, max(track_num, MAX_TRACKS - 1));
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
    delay(step_delay_us); 
    digitalWrite(_steppin, LOW);
    delay(step_delay_us);
    digitalWrite(_steppin, HIGH); // end high
  }
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
    @brief  Capture one track's worth of flux transitions, between two falling
   index pulses
    @param  pulses A pointer to an array of memory we can use to store into
    @param  max_pulses The size of the allocated pulses array
    @return Number of pulses we actually captured
*/
/**************************************************************************/
uint32_t Adafruit_Floppy::capture_track(uint8_t *pulses, uint32_t max_pulses) {
  uint8_t pulse_count;
  uint8_t *pulses_ptr = pulses;
  uint8_t *pulses_end = pulses + max_pulses;

  BusIO_PortReg *dataPort, *ledPort;
  BusIO_PortMask dataMask, ledMask;
  dataPort = (BusIO_PortReg *)portInputRegister(digitalPinToPort(_rddatapin));
  dataMask = digitalPinToBitMask(_rddatapin);
  ledPort = (BusIO_PortReg *)portOutputRegister(digitalPinToPort(led_pin));
  ledMask = digitalPinToBitMask(led_pin);

  memset(pulses, 0, max_pulses); // zero zem out

  noInterrupts();
  wait_for_index_pulse_low();

  // ok we have a h-to-l transition so...
  bool last_index_state = read_index();
  while (true) {
    bool index_state = read_index();
    // ahh another H to L transition, we're done with this track!
    if (last_index_state && !index_state) {
      break;
    }
    last_index_state = index_state;

    // muahaha, now we can read track data!
    pulse_count = 0;
    // while pulse is in the low pulse, count up
    while (!read_data())
      pulse_count++;
    *ledPort |= ledMask;

    // while pulse is high, keep counting up
    while (read_data())
      pulse_count++;
    *ledPort &= ~ledMask;

    pulses_ptr[0] = pulse_count;
    pulses_ptr++;
    if (pulses_ptr == pulses_end) {
      break;
    }
  }
  // whew done
  interrupts();
  return pulses_ptr - pulses;
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
*/
/**************************************************************************/
void Adafruit_Floppy::print_pulses(uint8_t *pulses, uint32_t num_pulses) {
  for (uint32_t i = 0; i < num_pulses; i++) {
    Serial.print(pulses[i]);
    Serial.print(", ");
  }
  Serial.println();
}
/**************************************************************************/
/*!
    @brief  Pretty print a simple histogram of flux transitions
    @param  pulses A pointer to an array of memory containing pulse counts
    @param  num_pulses The size of the pulses in the array
    @param  max_bins The maximum number of histogram bins to use (default 64)
*/
/**************************************************************************/
void Adafruit_Floppy::print_pulse_bins(uint8_t *pulses, uint32_t num_pulses,
                                       uint8_t max_bins) {
  // lets bin em!
  uint32_t bins[max_bins][2];
  memset(bins, 0, max_bins * 2 * sizeof(uint32_t));

  // we'll add each pulse to a bin so we can figure out the 3 buckets
  for (uint32_t i = 0; i < num_pulses; i++) {
    uint8_t p = pulses[i];
    // find a bin for this pulse
    uint8_t bin = 0;
    for (bin = 0; bin < max_bins; bin++) {
      // bin already exists? increment the count!
      if (bins[bin][0] == p) {
        bins[bin][1]++;
        break;
      }
      if (bins[bin][0] == 0) {
        // ok we never found the bin, so lets make it this one!
        bins[bin][0] = p;
        bins[bin][1] = 1;
        break;
      }
    }
    if (bin == max_bins)
      Serial.println("oof we ran out of bins but we'll keep going");
  }
  // this is a very lazy way to print the bins sorted
  for (uint8_t pulse_w = 1; pulse_w < 255; pulse_w++) {
    for (uint8_t b = 0; b < max_bins; b++) {
      if (bins[b][0] == pulse_w) {
        Serial.print(bins[b][0]);
        Serial.print(": ");
        Serial.println(bins[b][1]);
      }
    }
  }
}
