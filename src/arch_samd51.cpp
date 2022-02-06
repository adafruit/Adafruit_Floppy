#if defined(__SAMD51__)
#include <Adafruit_Floppy.h>
#include <wiring_private.h> // pinPeripheral() func

static const struct {
  Tc *tc;         // -> Timer/Counter base address
  IRQn_Type IRQn; // Interrupt number
  int gclk;       // GCLK ID
  int evu;        // EVSYS user ID
} tcList[] = {{TC0, TC0_IRQn, TC0_GCLK_ID, EVSYS_ID_USER_TC0_EVU},
              {TC1, TC1_IRQn, TC1_GCLK_ID, EVSYS_ID_USER_TC1_EVU},
              {TC2, TC2_IRQn, TC2_GCLK_ID, EVSYS_ID_USER_TC2_EVU},
              {TC3, TC3_IRQn, TC3_GCLK_ID, EVSYS_ID_USER_TC3_EVU},
#ifdef TC4
              {TC4, TC4_IRQn, TC4_GCLK_ID, EVSYS_ID_USER_TC4_EVU},
#endif
#ifdef TC5
              {TC5, TC5_IRQn, TC5_GCLK_ID, EVSYS_ID_USER_TC5_EVU},
#endif
#ifdef TC6
              {TC6, TC6_IRQn, TC6_GCLK_ID, EVSYS_ID_USER_TC6_EVU},
#endif
#ifdef TC7
              {TC7, TC7_IRQn, TC7_GCLK_ID, EVSYS_ID_USER_TC7_EVU}
#endif
};

Tc *theReadTimer = NULL;
Tc *theWriteTimer = NULL;

volatile uint8_t *g_flux_pulses = NULL;
volatile uint32_t g_max_pulses = 0;
volatile uint32_t g_num_pulses = 0;
volatile bool g_store_greaseweazle = false;
volatile uint8_t g_timing_div = 2;

void FLOPPY_TC_HANDLER() // Interrupt Service Routine (ISR) for timer TCx
{
  // Check for match counter 0 (MC0) interrupt
  if (theReadTimer && theReadTimer->COUNT16.INTFLAG.bit.MC0) {
    uint16_t ticks =
        theReadTimer->COUNT16.CC[0].reg / g_timing_div; // Copy the period
    if (ticks == 0) {
      // dont do something if its 0 - thats wierd!
    } else if (ticks < 250 || !g_store_greaseweazle) {
      // 1-249: One byte.
      g_flux_pulses[g_num_pulses++] = min(249, ticks);
    } else {
      uint8_t high = (ticks - 250) / 255;
      if (high < 5) {
        // 250-1524: Two bytes.
        g_flux_pulses[g_num_pulses++] = 250 + high;
        g_flux_pulses[g_num_pulses++] = 1 + ((ticks - 250) % 255);
      } else {
        // TODO MEME FIX!
        /* 1525-(2^28-1): Seven bytes.
        g_flux_pulses[g_num_pulses++] = 0xff;
        g_flux_pulses[g_num_pulses++] = FLUXOP_SPACE;
        _write_28bit(ticks - 249);
        u_buf[U_MASK(u_prod++)] = 249;
        }
        */
      }
    }
  }

  // Check for match counter 1 (MC1) interrupt
  if (theReadTimer && theReadTimer->COUNT16.INTFLAG.bit.MC1) {
    uint16_t pulsewidth =
        theReadTimer->COUNT16.CC[1].reg; // Copy the pulse width, DONT REMOVE
    (void)pulsewidth;
  }
}

// this isnt great but how else can we dynamically choose the pin? :/
void TC0_Handler() { FLOPPY_TC_HANDLER(); }
void TC1_Handler() { FLOPPY_TC_HANDLER(); }
void TC2_Handler() { FLOPPY_TC_HANDLER(); }
void TC3_Handler() { FLOPPY_TC_HANDLER(); }

static void enable_capture_timer(void) {
  if (!theReadTimer)
    return;

  theReadTimer->COUNT16.CTRLA.bit.ENABLE = 1; // Enable the TC timer
  while (theReadTimer->COUNT16.SYNCBUSY.bit.ENABLE)
    ; // Wait for synchronization
}

static bool init_capture_timer(int _rddatapin, Stream *debug_serial) {
  MCLK->APBBMASK.reg |=
      MCLK_APBBMASK_EVSYS; // Switch on the event system peripheral

  // Enable the port multiplexer on READDATA
  PinDescription pinDesc = g_APinDescription[_rddatapin];
  uint32_t capture_port = pinDesc.ulPort;
  uint32_t capture_pin = pinDesc.ulPin;
  EExt_Interrupts capture_irq = pinDesc.ulExtInt;
  if (capture_irq == NOT_AN_INTERRUPT) {
    if (debug_serial)
      debug_serial->println("Not an interrupt pin!");
    return false;
  }

  uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
  uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

  if (tcNum < TCC_INST_NUM) {
    if (pinDesc.ulTCChannel != NOT_ON_TIMER) {
      if (debug_serial)
        debug_serial->println(
            "PWM is on a TCC not TC, lets look at the TCChannel");
      tcNum = GetTCNumber(pinDesc.ulTCChannel);
      tcChannel = GetTCChannelNumber(pinDesc.ulTCChannel);
    }
    if (tcNum < TCC_INST_NUM) {
      if (debug_serial)
        debug_serial->println("Couldn't find a TC channel for this pin :(");
      return false;
    }
  }
  tcNum -= TCC_INST_NUM; // adjust naming
  if (debug_serial)
    debug_serial->printf("readdata on port %d and pin %d, IRQ #%d, TC%d.%d\n\r",
                         capture_port, capture_pin, capture_irq, tcNum,
                         tcChannel);
  theReadTimer = tcList[tcNum].tc;

  if (debug_serial)
    debug_serial->printf("TC GCLK ID=%d, EVU=%d\n\r", tcList[tcNum].gclk,
                         tcList[tcNum].evu);

  // Setup INPUT capture clock

  GCLK->PCHCTRL[tcList[tcNum].gclk].reg =
      GCLK_PCHCTRL_GEN_GCLK1_Val |
      (1 << GCLK_PCHCTRL_CHEN_Pos); // use GCLK1 to get 48MHz on SAMD51
  PORT->Group[capture_port].PINCFG[capture_pin].bit.PMUXEN = 1;

  // Set-up the pin as an EIC (interrupt) peripheral on READDATA
  if (capture_pin % 2 == 0) { // even pmux
    PORT->Group[capture_port].PMUX[capture_pin >> 1].reg |= PORT_PMUX_PMUXE(0);
  } else {
    PORT->Group[capture_port].PMUX[capture_pin >> 1].reg |= PORT_PMUX_PMUXO(0);
  }

  EIC->CTRLA.bit.ENABLE = 0; // Disable the EIC peripheral
  while (EIC->SYNCBUSY.bit.ENABLE)
    ; // Wait for synchronization
  // Look for right CONFIG register to be addressed
  uint8_t eic_config, eic_config_pos;
  if (capture_irq > EXTERNAL_INT_7) {
    eic_config = 1;
    eic_config_pos = (capture_irq - 8) << 2;
  } else {
    eic_config = 0;
    eic_config_pos = capture_irq << 2;
  }

  // Set event on detecting a HIGH level
  EIC->CONFIG[eic_config].reg &= ~(EIC_CONFIG_SENSE0_Msk << eic_config_pos);
  EIC->CONFIG[eic_config].reg |= EIC_CONFIG_SENSE0_HIGH_Val << eic_config_pos;
  EIC->EVCTRL.reg =
      1 << capture_irq; // Enable event output on external interrupt
  EIC->INTENCLR.reg = 1 << capture_irq; // Clear interrupt on external interrupt
  EIC->ASYNCH.reg = 1 << capture_irq; // Set-up interrupt as asynchronous input
  EIC->CTRLA.bit.ENABLE = 1;          // Enable the EIC peripheral
  while (EIC->SYNCBUSY.bit.ENABLE)
    ; // Wait for synchronization

  // Select the event system user on channel 0 (USER number = channel number +
  // 1)
  EVSYS->USER[tcList[tcNum].evu].reg =
      EVSYS_USER_CHANNEL(1); // Set the event user (receiver) as timer

  // Select the event system generator on channel 0
  EVSYS->Channel[0].CHANNEL.reg =
      EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT | // No event edge detection
      EVSYS_CHANNEL_PATH_ASYNCHRONOUS |    // Set event path as asynchronous
      EVSYS_CHANNEL_EVGEN(
          EVSYS_ID_GEN_EIC_EXTINT_0 +
          capture_irq); // Set event generator (sender) as ext int

  theReadTimer->COUNT16.EVCTRL.reg =
      TC_EVCTRL_TCEI |     // Enable the TCC event input
                           // TC_EVCTRL_TCINV |             // Invert the event
                           // input
      TC_EVCTRL_EVACT_PPW; // Set up the timer for capture: CC0 period, CC1
                           // pulsewidth

  NVIC_SetPriority(tcList[tcNum].IRQn,
                   0); // Set the Nested Vector Interrupt Controller (NVIC)
                       // priority for TCx to 0 (highest)
  NVIC_EnableIRQ(tcList[tcNum].IRQn); // Connect the TCx timer to the Nested
                                      // Vector Interrupt Controller (NVIC)

  theReadTimer->COUNT16.INTENSET.reg =
      TC_INTENSET_MC1 | // Enable compare channel 1 (CC1) interrupts
      TC_INTENSET_MC0;  // Enable compare channel 0 (CC0) interrupts

  theReadTimer->COUNT16.CTRLA.reg =
      TC_CTRLA_CAPTEN1 | // Enable pulse capture on CC1
      TC_CTRLA_CAPTEN0 | // Enable pulse capture on CC0
      // TC_CTRLA_PRESCSYNC_PRESC |     // Roll over on prescaler clock
      // TC_CTRLA_PRESCALER_DIV1 |      // Set the prescaler
      TC_CTRLA_MODE_COUNT16; // Set the timer to 16-bit mode

  return true;
}


static bool init_generate_timer(int _wrdatapin, Stream *debug_serial) {
  MCLK->APBBMASK.reg |=
      MCLK_APBBMASK_EVSYS; // Switch on the event system peripheral

  // Enable the port multiplexer on WRITEDATA
  PinDescription pinDesc = g_APinDescription[_wrdatapin];
  uint32_t generate_port = pinDesc.ulPort;
  uint32_t generate_pin = pinDesc.ulPin;

  uint32_t tcNum = GetTCNumber(pinDesc.ulPWMChannel);
  uint8_t tcChannel = GetTCChannelNumber(pinDesc.ulPWMChannel);

  if (tcNum < TCC_INST_NUM) {
    if (pinDesc.ulTCChannel != NOT_ON_TIMER) {
      if (debug_serial)
        debug_serial->println(
            "PWM is on a TCC not TC, lets look at the TCChannel");
      tcNum = GetTCNumber(pinDesc.ulTCChannel);
      tcChannel = GetTCChannelNumber(pinDesc.ulTCChannel);

      pinPeripheral(_wrdatapin, PIO_TIMER_ALT); // PIO_TIMER_ALT if using a TCC periph
    }
    if (tcNum < TCC_INST_NUM) {
      if (debug_serial)
        debug_serial->println("Couldn't find a TC channel for this pin :(");
      return false;
    }
  } else {
    pinPeripheral(_wrdatapin, PIO_TIMER); // PIO_TIMER if using a TC periph
  }

  tcNum -= TCC_INST_NUM; // adjust naming
  if (debug_serial)
    debug_serial->printf("writedata on port %d and pin %d,, TC%d.%d\n\r",
                         generate_port, generate_pin, tcNum,
                         tcChannel);

  // Because of the PWM mode used, we MUST use a TC#/WO[1] pin, can't
  // use WO[0]. Different timers would need different pins,
  // but the WO[1] thing is NOT negotiable.
  if (tcChannel != 1) {
    debug_serial->println("Must be on TCx.1, but we're not!");
    return false;
  }

  theWriteTimer = tcList[tcNum].tc;

  if (debug_serial)
    debug_serial->printf("TC GCLK ID=%d, EVU=%d\n\r", tcList[tcNum].gclk,
                         tcList[tcNum].evu);

  // Configure TC timer source as GCLK1 (48 MHz peripheral clock)
  GCLK->PCHCTRL[tcList[tcNum].gclk].bit.CHEN = 0;
  while (GCLK->PCHCTRL[tcList[tcNum].gclk].bit.CHEN)
    ; // Wait for disable
  GCLK_PCHCTRL_Type pchctrl;
  pchctrl.bit.GEN = GCLK_PCHCTRL_GEN_GCLK1_Val; // GCLK1 is 48 MHz
  pchctrl.bit.CHEN = 1;
  GCLK->PCHCTRL[tcList[tcNum].gclk].reg = pchctrl.reg;
  while (!GCLK->PCHCTRL[tcList[tcNum].gclk].bit.CHEN)
    ; // Wait for enable

  // Software reset timer/counter to default state (also disables it)
  theWriteTimer->COUNT16.CTRLA.bit.SWRST = 1;
  while (theWriteTimer->COUNT16.SYNCBUSY.bit.SWRST)
    ;

  // Configure for MPWM, 1:2 prescale (24 MHz). 16-bit mode is defailt.
  theWriteTimer->COUNT16.WAVE.bit.WAVEGEN = 3; // Match PWM mode
  theWriteTimer->COUNT16.CTRLA.bit.PRESCALER = TC_CTRLA_PRESCALER_DIV2_Val;
  // MPWM mode is weird but necessary because Normal PWM has a fixed TOP value.

  // Count-up, no one-shot is default state, no need to fiddle those bits.

  // Invert PWM channel 1 so it starts low, goes high on match
  theWriteTimer->COUNT16.DRVCTRL.bit.INVEN1 = 1; // INVEN1 = channel 1

  // Enable timer
  theWriteTimer->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
  while (theWriteTimer->COUNT16.SYNCBUSY.bit.ENABLE)
    ;

  // IRQ is enabled but match-compare interrupt isn't enabled
  // until send_pulses() is called.

  NVIC_DisableIRQ(tcList[tcNum].IRQn);
  NVIC_ClearPendingIRQ(tcList[tcNum].IRQn);
  NVIC_SetPriority(tcList[tcNum].IRQn, 0); // Top priority
  NVIC_EnableIRQ(tcList[tcNum].IRQn);

  return true;
}

#ifdef __cplusplus
void Adafruit_Floppy::enable_capture(void) { enable_capture_timer(); }

void Adafruit_Floppy::disable_capture(void) {
  if (!theReadTimer)
    return;

  theReadTimer->COUNT16.CTRLA.bit.ENABLE = 0; // disable the TC timer

  //theReadTimer = NULL; // we're done with it for now, don't accidentally use it
}

bool Adafruit_Floppy::init_capture(void) {
  return init_capture_timer(_rddatapin, debug_serial);
}

void Adafruit_Floppy::deinit_capture(void) {
  if (!theReadTimer)
    return;

  // Software reset timer/counter to default state (also disables it)
  theReadTimer->COUNT16.CTRLA.bit.SWRST = 1;
  while (theReadTimer->COUNT16.SYNCBUSY.bit.SWRST)
    ;
  theReadTimer = NULL;
}

bool Adafruit_Floppy::init_generate(void) {
  return init_generate_timer(_wrdatapin, debug_serial);
}
#endif

#endif
