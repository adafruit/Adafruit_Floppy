// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Read-only track-zero identification on Floppsy RP2350. Select 240 MHz and
// Adafruit TinyUSB. A standard PC cable uses DS1. This does not write a disk.
#include <Adafruit_Floppsy.h>
#include <Adafruit_FloppyCodec.h>
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_TinyUSB.h>

Adafruit_Floppsy floppsy;
Adafruit_Floppy &drive = floppsy.drive1;
Adafruit_FloppyFluxCapture capture;
const bool HIGH_DENSITY = true; // Set false for a DD drive/media arrangement.
const size_t BIT_BUFFER_BYTES = 200000;
uint8_t *bits;

void stopAndReport(const char *message);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  if (!floppsy.begin()) {
    Serial.print("Floppsy setup failed\n\r");
    return;
  }
  bits = (uint8_t *)pmalloc(BIT_BUFFER_BYTES);
  if (!bits) {
    Serial.print("PSRAM allocation failed\n\r");
    return;
  }
  Serial.print("Send r to identify track zero. Read-only; INDEX is optional.\n\r");
}

void loop() {
  if (!bits || !Serial.available() || Serial.read() != 'r') {
    return;
  }
  if (floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V) < 4.5f) {
    stopAndReport("Drive 5 V supply missing");
    return;
  }
  if (!floppsy.enable(true)) {
    stopAndReport("Could not enable the drive interface");
    return;
  }
  drive.select(true);
  if (!drive.spin_motor(true) && !drive.index_pulses_missing()) {
    stopAndReport("Motor startup failed");
    return;
  }
  if (!drive.goto_track(0) || !drive.side(0)) {
    stopAndReport("Could not position the head");
    return;
  }
  drive.set_density(HIGH_DENSITY);
  if (!capture.begin(FLOPPY_RDDATA, FLOPPY_INDEX)) {
    stopAndReport(capture.error);
    return;
  }
  // A bounded timed capture includes overlapping revolutions and works even
  // when the connected INDEX pin never pulses. It makes no synthetic index.
  if (!capture.captureTimed(nullptr, 24000000)) {
    stopAndReport(capture.error);
    return;
  }
  floppsy.enable(false);
  if (capture.count < 2) {
    stopAndReport("No flux transitions captured");
    return;
  }
  for (size_t i = 1; i < capture.count; i++) {
    capture.samples[i - 1] -= capture.samples[i];
  }
  auto result = Adafruit_FloppyCodec::detect(capture.samples, capture.count - 1,
                                            40000000, bits, BIT_BUFFER_BYTES);
  capture.end();
  if (!result.found) {
    Serial.print("No supported layout found; blank, damaged, or custom format\n\r");
    return;
  }
  Serial.print(Adafruit_FloppyCodec::name(result.encoding));
  Serial.print("\t");
  Serial.print(result.uniqueSectors);
  Serial.print(" verified sector IDs\t");
  Serial.print(result.sectorBytes);
  Serial.print(" bytes/sector (0 means mixed sizes)\n\r");
  Serial.print("This identifies one track; disk geometry and filesystem need more reads.\n\r");
}

void stopAndReport(const char *message) {
  floppsy.enable(false);
  Serial.print(message);
  Serial.print("\n\r");
  capture.end();
}
