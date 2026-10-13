// MIT license. Copyright (c) 2026 Adafruit Industries.
// Floppsy board setup and continuous PSRAM capture for greaseweazle.ino.
// USB command parsing and replies are shared with the other boards in the
// sketch.
#pragma once
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_TinyUSB.h>

#if !defined(USE_TINYUSB) || F_CPU != 240000000
#error Select 240 MHz and Adafruit TinyUSB for Floppsy Greaseweazle.
#endif

Adafruit_FloppyFluxCapture fluxCapture;
Adafruit_ILI9341 gwTft(&SPI, TFT_DC, TFT_CS, TFT_RESET);
bool boardOK = false;
bool captureOK = false;
uint8_t streamBuffer[1024];
size_t streamCount = 0;

void floppsySetup() {
  // The board helper isolates the cable, configures directions and the BOOT
  // input, and initializes the power monitor. No drive is enabled on startup.
  boardOK = floppsy.begin();
  floppsy.drive0.watchdog_delay_ms = 10000;
  floppsy.drive1.watchdog_delay_ms = 10000;
  pinMode(PIN_USB_HOST_ENABLE, OUTPUT);
  digitalWrite(PIN_USB_HOST_ENABLE, LOW);

  SPI.setRX(NOPIN);
  gwTft.begin(24000000);
  gwTft.setRotation(1);
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  gwTft.fillScreen(ILI9341_BLACK);
  gwTft.setTextColor(ILI9341_WHITE);
  gwTft.setTextSize(2);
  gwTft.setCursor(12, 20);
  gwTft.print("Floppsy Greaseweazle\n\r");
  gwTft.print("\n\r  USB read-only mode\n\r\n\r  Keep WRITE OFF");
  captureOK = fluxCapture.begin(FLOPPY_RDDATA, FLOPPY_INDEX);
}

uint8_t floppsySelect(uint8_t unit) {
  if (!busType)
    return GW_ACK_NOBUS;
  if (unit > 1)
    return GW_ACK_BADUNIT;

  stop_drive();
  if (!boardOK || floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V) < 4.5 ||
      floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_12V) < 10.8)
    return GW_ACK_NOUNIT;

  Adafruit_Floppy *next = &floppsy.drive0;
  if (unit == 1)
    next = &floppsy.drive1;
  // Greaseweazle timings apply to the interface, including after switching DS.
  next->select_delay_us = floppy->select_delay_us;
  next->step_delay_us = floppy->step_delay_us;
  next->settle_delay_ms = floppy->settle_delay_ms;
  next->motor_delay_ms = floppy->motor_delay_ms;
  next->watchdog_delay_ms = floppy->watchdog_delay_ms;
  floppy = next;

  if (!digitalRead(FLOPPY_WRGATE) || !digitalRead(FLOPPY_WRDATA))
    return GW_ACK_WRPROT;
  if (!floppsy.enable(true))
    return GW_ACK_NOUNIT;
  floppy->select(true);
  selectedUnit = unit;
  return GW_ACK_OK;
}

uint8_t floppsyReadStatus(uint32_t ticks, uint16_t marks) {
  if (!floppy->motor_is_spinning())
    return GW_ACK_NOUNIT;
  if (!captureOK)
    return GW_ACK_BADCMD;
  if (ticks) {
    // Timed reads are bounded to one second and never fabricate INDEX marks.
    if (marks || ticks > 40000000)
      return GW_ACK_BADCMD;
  } else if (marks < 2 ||
             marks > Adafruit_FloppyFluxCapture::MAX_REVOLUTIONS + 1) {
    return GW_ACK_BADCMD;
  }
  return GW_ACK_OK;
}

bool gwCancelled() { return !Serial || digitalRead(PIN_BUTTON_BOOT) == LOW; }

bool gwSend(const uint8_t *data, size_t count) {
  uint32_t progress = millis();
  while (count) {
    if (gwCancelled() || millis() - progress > 3000) {
      stop_drive();
      return false;
    }
    size_t available = Serial.availableForWrite();
    size_t sent = Serial.write(data, min(count, available));
    if (sent) {
      count -= sent;
      data += sent;
      progress = millis();
    } else {
      delay(1);
    }
  }
  return true;
}

bool gwFlush() {
  bool ok = gwSend(streamBuffer, streamCount);
  streamCount = 0;
  return ok;
}

bool gwByte(uint8_t value) {
  if (streamCount == sizeof(streamBuffer) && !gwFlush())
    return false;
  streamBuffer[streamCount++] = value;
  return true;
}

bool gwOpcode(uint8_t opcode, uint32_t ticks) {
  // The wire format uses four nonzero bytes with seven payload bits each.
  if (!gwByte(255) || !gwByte(opcode))
    return false;
  for (uint8_t i = 0; i < 4; i++) {
    if (!gwByte(1 | ((ticks >> (i * 7)) & 127) * 2))
      return false;
  }
  return true;
}

bool gwInterval(uint32_t ticks) {
  if (!ticks)
    return false;
  if (ticks < 250)
    return gwByte(ticks);
  if (ticks < 1525)
    return gwByte(250 + (ticks - 250) / 255) && gwByte(1 + (ticks - 250) % 255);
  // SPACE extends time without fabricating an edge; 249 ends the interval.
  return gwOpcode(2, ticks - 249) && gwByte(249);
}

bool gwReadFlux(uint8_t revolutions, uint32_t ticks) {
  streamCount = 0;
  bool timed = ticks != 0;
  bool captured;
  if (timed)
    captured = fluxCapture.captureTimed(gwCancelled, ticks);
  else
    captured = fluxCapture.capture(gwCancelled, revolutions);
  if (!captured) {
    flux_status = GW_ACK_OVERFLOW;
    if (strcmp(fluxCapture.error, "Missing index / slow drive") == 0)
      flux_status = GW_ACK_NOINDEX;
    return false;
  }

  uint32_t start = fluxCapture.index[0];
  uint32_t end = start - fluxCapture.index[revolutions];
  if (timed) {
    start = fluxCapture.start;
    end = ticks;
  }
  uint32_t previous = 0;
  uint8_t mark = 0;
  for (size_t i = 0; i < fluxCapture.count; i++) {
    int32_t elapsed = (int32_t)(start - fluxCapture.samples[i]);
    if (elapsed <= 0) {
      if (previous) {
        flux_status = GW_ACK_OVERFLOW;
        return false;
      }
      continue; // Discard the partial lead-in revolution.
    }
    if ((uint32_t)elapsed > end)
      break;
    if ((uint32_t)elapsed <= previous) {
      flux_status = GW_ACK_OVERFLOW;
      return false;
    }
    while (!timed && mark <= revolutions &&
           start - fluxCapture.index[mark] <= (uint32_t)elapsed) {
      if (!gwOpcode(1, start - fluxCapture.index[mark++] - previous))
        return false;
    }
    if (!gwInterval(elapsed - previous))
      return false;
    previous = elapsed;
  }
  while (!timed && mark <= revolutions) {
    if (!gwOpcode(1, start - fluxCapture.index[mark++] - previous))
      return false;
  }
  if (timed && end > previous && !gwOpcode(2, end - previous))
    return false;
  return gwFlush();
}
