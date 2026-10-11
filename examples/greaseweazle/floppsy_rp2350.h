// MIT license. Copyright (c) 2026 Adafruit Industries.
// Floppsy's read-only Greaseweazle transport. Protocol reference:
// https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/usb.py
#pragma once
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_FloppySCP.h>
#include <Adafruit_INA3221.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <hardware/gpio.h>

#if !defined(USE_TINYUSB) || F_CPU != 240000000
#error Select 240 MHz and Adafruit TinyUSB for Floppsy Greaseweazle.
#endif

enum GwCommand {
  GW_INFO = 0, GW_SEEK = 2, GW_HEAD = 3, GW_SET_PARAMS = 4, GW_GET_PARAMS = 5,
  GW_MOTOR = 6, GW_READ = 7, GW_WRITE = 8, GW_FLUX_STATUS = 9,
  GW_SELECT = 12, GW_DESELECT = 13, GW_BUS = 14, GW_SET_PIN = 15,
  GW_RESET = 16, GW_ERASE = 17, GW_GET_PIN = 20
};
enum GwAck {
  GW_OK = 0, GW_BAD_COMMAND = 1, GW_NO_INDEX = 2, GW_NO_TRACK0 = 3,
  GW_OVERFLOW = 4, GW_WRITE_PROTECTED = 6, GW_NO_UNIT = 7, GW_NO_BUS = 8,
  GW_BAD_UNIT = 9, GW_BAD_PIN = 10, GW_BAD_CYLINDER = 11
};

Adafruit_Floppy drive0(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT0,
  FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA, FLOPPY_WRGATE,
  FLOPPY_TRK0, FLOPPY_WRPROT, FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
Adafruit_Floppy drive1(FLOPPY_DENSITY, FLOPPY_INDEX, FLOPPY_SELECT1,
  FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP, FLOPPY_WRDATA, FLOPPY_WRGATE,
  FLOPPY_TRK0, FLOPPY_WRPROT, FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
Adafruit_Floppy *drive = &drive1;
Adafruit_FloppyFluxCapture fluxCapture;
Adafruit_INA3221 powerMonitor;
Adafruit_ILI9341 gwTft(&SPI, TFT_DC, TFT_CS, TFT_RESET);
bool powerOK = false, captureOK = false;
uint8_t busType = 0, selectedUnit = 255, fluxStatus = GW_OK;
uint32_t lastCommand = 0;
uint16_t driveDelays[5] = {10, 12000, 15, 1000, 10000};
uint8_t streamBuffer[1024];
size_t streamCount = 0;

void gwStop();
bool gwCancelled();
bool gwSend(const uint8_t *data, size_t count);
bool gwFlush();
bool gwByte(uint8_t value);
bool gwOpcode(uint8_t opcode, uint32_t ticks);
bool gwInterval(uint32_t ticks);
bool gwReadFlux(uint8_t revolutions);
void gwReply(uint8_t command, uint8_t status);
void gwDispatch(const uint8_t *command, uint8_t length);
void gwApplyDelays();

void setup() {
  // Isolate the cable before changing any command output or level-shifter DIR.
  gpio_put(FLOPPY_CMD_OE, true); pinMode(FLOPPY_CMD_OE, OUTPUT);
  gpio_put(FLOPPY_STAT_OE, true); pinMode(FLOPPY_STAT_OE, OUTPUT);
  drive0.begin(); drive1.begin();
  pinMode(FLOPPY_WRGATE, OUTPUT); digitalWrite(FLOPPY_WRGATE, HIGH);
  pinMode(FLOPPY_CMD_DIR, OUTPUT); digitalWrite(FLOPPY_CMD_DIR, LOW);
  pinMode(FLOPPY_STAT_DIR, OUTPUT); digitalWrite(FLOPPY_STAT_DIR, HIGH);
  pinMode(PIN_USB_HOST_ENABLE, OUTPUT); digitalWrite(PIN_USB_HOST_ENABLE, LOW);
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
  gwStop();
  Serial.begin(9600); // Binary protocol only; no debug text on this port.
  SPI.setRX(NOPIN);
  gwTft.begin(24000000); gwTft.setRotation(1);
  pinMode(TFT_BACKLIGHT, OUTPUT); digitalWrite(TFT_BACKLIGHT, HIGH);
  gwTft.fillScreen(ILI9341_BLACK); gwTft.setTextColor(ILI9341_WHITE);
  gwTft.setTextSize(2); gwTft.setCursor(12, 20);
  gwTft.println("Floppsy Greaseweazle");
  gwTft.println("\n  USB read-only mode\n\n  Keep WRITE OFF");
  Wire.begin(); Wire.setTimeout(25);
  powerOK = powerMonitor.begin(0x40, &Wire);
  captureOK = fluxCapture.begin(FLOPPY_RDDATA, FLOPPY_INDEX);
}

void loop() {
  static uint8_t command[32];
  static uint8_t used = 0;
  static uint32_t lastByte = 0;
  if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW ||
      (driveDelays[4] && millis() - lastCommand > driveDelays[4])) {
    gwStop();
    used = 0;
    lastCommand = millis();
  }
  if (used && millis() - lastByte > 250) {
    gwReply(command[0], GW_BAD_COMMAND);
    used = 0;
  }
  while (Serial.available()) {
    command[used++] = Serial.read();
    lastByte = millis();
    if (used == 2 && (command[1] < 2 || command[1] > sizeof(command))) {
      gwReply(command[0], GW_BAD_COMMAND);
      used = 0;
      continue;
    }
    if (used >= 2 && used == command[1]) {
      lastCommand = millis();
      gwDispatch(command, used);
      used = 0;
    }
  }
}

void gwStop() {
  digitalWrite(FLOPPY_WRGATE, HIGH);
  digitalWrite(FLOPPY_WRDATA, HIGH);
  drive0.spin_motor(false); drive1.spin_motor(false);
  drive0.select(false); drive1.select(false);
  digitalWrite(FLOPPY_MOTOR, HIGH);
  digitalWrite(FLOPPY_SELECT0, HIGH);
  digitalWrite(FLOPPY_SELECT1, HIGH);
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  selectedUnit = 255;
}

bool gwCancelled() {
  return !Serial || digitalRead(PIN_BUTTON_BOOT) == LOW;
}

bool gwSend(const uint8_t *data, size_t count) {
  uint32_t progress = millis();
  while (count) {
    if (gwCancelled() || millis() - progress > 3000) { gwStop(); return false; }
    size_t available = Serial.availableForWrite();
    size_t sent = Serial.write(data, min(count, available));
    if (sent) { count -= sent; data += sent; progress = millis(); }
    else delay(1);
  }
  return true;
}

bool gwFlush() {
  bool ok = gwSend(streamBuffer, streamCount);
  streamCount = 0;
  return ok;
}

bool gwByte(uint8_t value) {
  if (streamCount == sizeof(streamBuffer) && !gwFlush()) return false;
  streamBuffer[streamCount++] = value;
  return true;
}

bool gwOpcode(uint8_t opcode, uint32_t ticks) {
  // GW encodes 28-bit values as four nonzero bytes, seven payload bits each.
  if (!gwByte(255) || !gwByte(opcode)) return false;
  for (uint8_t i = 0; i < 4; i++) {
    if (!gwByte(1 | ((ticks >> (i * 7)) & 127) * 2)) return false;
  }
  return true;
}

bool gwInterval(uint32_t ticks) {
  if (!ticks) return false;
  if (ticks < 250) return gwByte(ticks);
  if (ticks < 1525) return gwByte(250 + (ticks - 250) / 255) &&
    gwByte(1 + (ticks - 250) % 255);
  // SPACE extends time without fabricating an edge; 249 ends the interval.
  return gwOpcode(2, ticks - 249) && gwByte(249);
}

bool gwReadFlux(uint8_t revolutions) {
  streamCount = 0;
  if (!fluxCapture.capture(gwCancelled, revolutions)) {
    fluxStatus = strcmp(fluxCapture.error, "Missing index / slow drive") == 0 ?
      GW_NO_INDEX : GW_OVERFLOW;
    return false;
  }
  uint32_t start = fluxCapture.index[0];
  uint32_t end = start - fluxCapture.index[revolutions];
  uint32_t previous = 0;
  uint8_t mark = 0;
  for (size_t i = 0; i < fluxCapture.count; i++) {
    int32_t elapsed = (int32_t)(start - fluxCapture.samples[i]);
    if (elapsed <= 0) {
      if (previous) { fluxStatus = GW_OVERFLOW; return false; }
      continue; // Discard the partial lead-in revolution.
    }
    if ((uint32_t)elapsed > end) break;
    if ((uint32_t)elapsed <= previous) { fluxStatus = GW_OVERFLOW; return false; }
    while (mark <= revolutions && start - fluxCapture.index[mark] <= (uint32_t)elapsed) {
      if (!gwOpcode(1, start - fluxCapture.index[mark++] - previous)) return false;
    }
    if (!gwInterval(elapsed - previous)) return false;
    previous = elapsed;
  }
  while (mark <= revolutions) {
    if (!gwOpcode(1, start - fluxCapture.index[mark++] - previous)) return false;
  }
  return gwFlush();
}

void gwReply(uint8_t command, uint8_t status) {
  const uint8_t response[] = {command, status};
  gwSend(response, sizeof(response));
}

void gwApplyDelays() {
  drive->select_delay_us = driveDelays[0];
  drive->step_delay_us = driveDelays[1];
  drive->settle_delay_ms = driveDelays[2];
  drive->motor_delay_ms = driveDelays[3];
  drive->watchdog_delay_ms = driveDelays[4];
}

void gwDispatch(const uint8_t *command, uint8_t length) {
  uint8_t op = command[0];
  uint8_t reply[32] = {};
  // Commands not listed here are explicitly unsupported, including firmware update.
  switch (op) {
    case GW_INFO:
      if (length != 3) break;
      if (command[2] == 0) {
        reply[0] = 1; reply[1] = 0; // Adafruity protocol version 1.0.
        reply[2] = 1; reply[3] = GW_GET_PIN;
        scpPut32(reply + 4, 40000000);
        reply[8] = 8; // Existing Greaseweazle Adafruity hardware family.
        reply[12] = 240; reply[15] = 2; // CPU MHz, 512 KiB SRAM.
      } else if (command[2] == 7) {
        scpPut32(reply, (drive->track() >= 0 ? 1 : 0) |
          (digitalRead(FLOPPY_MOTOR) == LOW ? 2 : 0));
        scpPut32(reply + 4, drive->track());
      } else break;
      gwReply(op, GW_OK); gwSend(reply, sizeof(reply)); return;
    case GW_BUS:
      if (length != 3 || (command[2] != 1 && command[2] != 2)) break;
      gwStop(); busType = command[2]; gwReply(op, GW_OK); return;
    case GW_RESET:
      if (length != 2) break;
      gwStop(); busType = 0; fluxStatus = GW_OK;
      drive0.soft_reset(); drive1.soft_reset();
      pinMode(FLOPPY_WRGATE, OUTPUT); digitalWrite(FLOPPY_WRGATE, HIGH);
      gwReply(op, GW_OK); return;
    case GW_SELECT:
      if (length != 3) break;
      if (!busType) { gwReply(op, GW_NO_BUS); return; }
      if (command[2] > 1) { gwReply(op, GW_BAD_UNIT); return; }
      gwStop();
      if (!powerOK || powerMonitor.getBusVoltage(1) < 4.5 ||
          powerMonitor.getBusVoltage(0) < 10.8) { gwReply(op, GW_NO_UNIT); return; }
      drive = command[2] == 0 ? &drive0 : &drive1;
      gwApplyDelays();
      if (!digitalRead(FLOPPY_WRGATE) || !digitalRead(FLOPPY_WRDATA)) {
        gwReply(op, GW_WRITE_PROTECTED); return;
      }
      selectedUnit = command[2];
      digitalWrite(FLOPPY_STAT_OE, LOW); digitalWrite(FLOPPY_CMD_OE, LOW);
      drive->select(true); gwReply(op, GW_OK); return;
    case GW_DESELECT:
      if (length != 2) break;
      gwStop(); gwReply(op, GW_OK); return;
    case GW_SET_PARAMS:
      if (length < 5 || length > 13 || command[2] != 0 || (length - 3) % 2) break;
      for (uint8_t i = 0; i < (length - 3) / 2; i++) {
        driveDelays[i] = command[3 + i * 2] | (uint16_t)command[4 + i * 2] << 8;
      }
      gwApplyDelays(); gwReply(op, GW_OK); return;
    case GW_GET_PARAMS:
      if (length != 4 || command[2] != 0 || command[3] > 10) break;
      for (uint8_t i = 0; i < 5; i++) {
        reply[i * 2] = driveDelays[i]; reply[i * 2 + 1] = driveDelays[i] >> 8;
      }
      gwReply(op, GW_OK); gwSend(reply, command[3]); return;
    case GW_FLUX_STATUS:
      if (length != 2) break;
      gwReply(op, fluxStatus); return;
    case GW_WRITE: case GW_ERASE:
      // This Floppsy implementation cannot assert WRITE GATE, even with WREN on.
      gwReply(op, GW_WRITE_PROTECTED); return;
    case GW_GET_PIN: case GW_SET_PIN: {
      if (length != (op == GW_GET_PIN ? 3 : 4)) break;
      int pin = -1;
      switch (command[2]) {
        case 2: pin = FLOPPY_DENSITY; break;
        case 8: pin = FLOPPY_INDEX; break;
        case 26: pin = FLOPPY_TRK0; break;
        case 28: pin = FLOPPY_WRPROT; break;
        case 34: pin = FLOPPY_READY; break;
      }
      if (pin < 0 || (op == GW_SET_PIN && command[2] != 2)) {
        gwReply(op, GW_BAD_PIN); return;
      }
      if (op == GW_SET_PIN) digitalWrite(pin, command[3] != 0);
      gwReply(op, GW_OK);
      if (op == GW_GET_PIN) { reply[0] = digitalRead(pin); gwSend(reply, 1); }
      return;
    }
    case GW_MOTOR:
      if (length != 4 || command[3] > 1) break;
      if (!command[3]) { drive->spin_motor(false); gwReply(op, GW_OK); return; }
      if (command[2] != selectedUnit) { gwReply(op, GW_NO_UNIT); return; }
      gwReply(op, drive->spin_motor(true) ? GW_OK : GW_NO_INDEX); return;
    case GW_HEAD:
      if (length != 3 || command[2] > 1) break;
      if (selectedUnit > 1) { gwReply(op, GW_NO_UNIT); return; }
      drive->side(command[2]); gwReply(op, GW_OK); return;
    case GW_SEEK: {
      if (length != 3 && length != 4) break;
      if (selectedUnit > 1) { gwReply(op, GW_NO_UNIT); return; }
      int16_t cylinder = length == 3 ? (int8_t)command[2] :
        (int16_t)(command[2] | (uint16_t)command[3] << 8);
      if (cylinder < 0 || cylinder > 83) { gwReply(op, GW_BAD_CYLINDER); return; }
      gwReply(op, drive->goto_track(cylinder) ? GW_OK : GW_NO_TRACK0); return;
    }
    case GW_READ: {
      if (length != 8) break;
      if (selectedUnit > 1 || digitalRead(FLOPPY_MOTOR) != LOW) {
        gwReply(op, GW_NO_UNIT); return;
      }
      uint32_t ticks = command[2] | (uint32_t)command[3] << 8 |
        (uint32_t)command[4] << 16 | (uint32_t)command[5] << 24;
      uint16_t marks = command[6] | (uint16_t)command[7] << 8;
      // A tick-limited read still uses real index and returns whole revolutions.
      // Fake index and hard-sector modes are not supported by this firmware.
      uint32_t revolutions = marks > 1 ? marks - 1 : 0;
      if (ticks) revolutions = max(revolutions, (ticks + 5999999ull) / 6000000ull);
      if (!revolutions || revolutions > Adafruit_FloppyFluxCapture::MAX_REVOLUTIONS || !captureOK) break;
      fluxStatus = GW_OK;
      gwReply(op, GW_OK);
      if (!gwReadFlux(revolutions) && fluxStatus == GW_OK) fluxStatus = GW_OVERFLOW;
      gwFlush(); reply[0] = 0; gwSend(reply, 1); // End of binary stream.
      lastCommand = millis(); return;
    }
  }
  gwReply(op, GW_BAD_COMMAND);
}
