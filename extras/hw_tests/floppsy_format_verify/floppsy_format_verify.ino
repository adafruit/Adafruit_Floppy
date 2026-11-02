// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Engineering test for a DISPOSABLE disk in a standard DS1 floppy drive.
// R C H HD: capture and identify. T C H HD: force a timed, INDEX-less capture.
// W C H HD BYTES: receive packed 24 MHz flux and write, then capture/identify.
// No write happens on startup. BOOT cancels. The production self-test is separate.
#include <Adafruit_Floppsy.h>
#include <Adafruit_FloppyBitstream.h>
#include <Adafruit_FloppyCodec.h>
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_TinyUSB.h>
#include <greasepack.h>

Adafruit_Floppsy floppsy;
Adafruit_Floppy &drive = floppsy.drive1;
Adafruit_FloppyFluxCapture capture;
uint8_t writeFlux[300000];
uint8_t *bits;
const size_t BIT_BUFFER_BYTES = 200000;

bool cancelled();
void halt(const char *message);
void sendBytes(const uint8_t *data, size_t length);
void receiveBytes(uint8_t *data, size_t length);
bool showSector(void *context, const Adafruit_FloppyCodec::Sector &sector);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  if (!floppsy.begin()) {
    halt("Floppsy setup failed");
  }
  drive.step_delay_us = 3000;
  bits = (uint8_t *)pmalloc(BIT_BUFFER_BYTES);
  if (!bits) {
    halt("Bit buffer allocation failed");
  }
  Serial.print("Format bench ready; R/T read, W explicitly writes scratch media\n\r");
}

void loop() {
  if (!Serial.available()) {
    return;
  }
  char line[80] = {}, command = 0;
  size_t length = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  line[length] = 0;
  unsigned cylinder = 0, head = 0, hd = 0, bytes = 0;
  int fields = sscanf(line, "%c %u %u %u %u", &command, &cylinder, &head, &hd, &bytes);
  if ((command != 'R' && command != 'T' && command != 'W') ||
      cylinder >= 80 || head > 1 || hd > 1 || fields < 4 ||
      (command == 'W' && (fields != 5 || !bytes || bytes > sizeof(writeFlux)))) {
    Serial.print("REJECT command\n\r");
    return;
  }

  // Receive and validate the complete write before turning the motor on.
  if (command == 'W') {
    Serial.printf("READY %u\n\r", bytes);
    receiveBytes(writeFlux, bytes);
    uint8_t *next = writeFlux;
    uint64_t ticks = 0;
    while (next < writeFlux + bytes) {
      unsigned interval = greaseunpack(&next, writeFlux + bytes, true);
      if (interval < 20 || interval == 0xffff) {
        halt("Invalid packed flux interval");
      }
      ticks += interval;
    }
    if (ticks < 5040000 || ticks > 24000000) {
      halt("Write must cover 210 ms to 1 second");
    }
  }

  if (floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V) < 4.5f) {
    halt("Drive 5 V missing");
  }
  if (!floppsy.enable(true)) {
    halt("Cable enable failed");
  }
  drive.select(true);
  if (!drive.spin_motor(true) && !drive.index_pulses_missing()) {
    halt("Motor startup failed");
  }
  if (!drive.goto_track(0) || !drive.goto_track(cylinder) || !drive.side(head)) {
    halt("Drive positioning failed");
  }
  drive.set_density(hd);
  delay(20);

  if (command == 'W') {
    if (!drive.index_pulses_seen() || drive.get_write_protect() || cancelled()) {
      halt("Write requires INDEX, writable media, and no cancel");
    }
    if (!drive.write_track(writeFlux, bytes, true)) {
      halt("Raw write failed");
    }
    Serial.print("WRITE completed\n\r");
  }

  if (!capture.begin(FLOPPY_RDDATA, FLOPPY_INDEX)) {
    halt(capture.error);
  }
  bool timed = command == 'T' || drive.index_pulses_missing();
  bool captured;
  if (timed) {
    captured = capture.captureTimed(cancelled, 24000000); // 0.6 s at 40 MHz.
  } else {
    captured = capture.capture(cancelled, 3);
  }
  floppsy.enable(false); // Stop the motor before transfer or decoding.
  if (!captured || capture.count < 2) {
    halt(capture.error);
  }

  Serial.printf("CAPTURE %u %u %lu %lu %lu %lu\n\r", (unsigned)capture.count,
                timed, capture.index[0], capture.index[1], capture.index[2], capture.index[3]);
  sendBytes((const uint8_t *)capture.samples, capture.count * sizeof(uint32_t));
  Serial.print("\n\r");

  // Keep the original timestamps in the host log, then reuse the PSRAM buffer
  // for positive 25 ns intervals. The next capture replaces this entire array.
  for (size_t i = 1; i < capture.count; i++) {
    capture.samples[i - 1] -= capture.samples[i];
  }
  auto found = Adafruit_FloppyCodec::detect(capture.samples, capture.count - 1,
                                           40000000, bits, BIT_BUFFER_BYTES);
  Serial.printf("DETECT %u %u %u %u %u %.6f %u\n\r", found.found,
                found.encoding, found.uniqueSectors, found.cylinder, found.head,
                found.bitCellMicroseconds, found.sectorBytes);
  if (found.found) {
    auto result = Adafruit_FloppyCodec::decode(found.encoding, bits, found.bitCount,
                                               showSector, nullptr);
    Serial.printf("RESULT %u %u %u\n\r", (unsigned)result.sectors,
                  (unsigned)result.badHeaders, (unsigned)result.badData);
  }
  capture.end();
  Serial.print("DONE\n\r");
}

bool cancelled() {
  return !Serial || digitalRead(PIN_BUTTON_BOOT) == LOW;
}

void halt(const char *message) {
  floppsy.enable(false);
  Serial.printf("FAIL %s; motor stopped\n\r", message);
  while (true) {
    delay(10);
  }
}

void sendBytes(const uint8_t *data, size_t length) {
  size_t sent = 0;
  uint32_t progress = millis();
  while (sent < length) {
    if (!Serial || millis() - progress > 10000) {
      halt("USB send timeout");
    }
    size_t count = Serial.write(data + sent, min((size_t)64, length - sent));
    sent += count;
    if (count) {
      progress = millis();
    }
    yield();
  }
  Serial.flush();
}

void receiveBytes(uint8_t *data, size_t length) {
  size_t received = 0;
  uint32_t progress = millis();
  while (received < length) {
    if (cancelled() || millis() - progress > 10000) {
      halt("USB receive timeout");
    }
    if (Serial.available()) {
      data[received++] = Serial.read();
      progress = millis();
    } else {
      yield();
    }
  }
}

bool showSector(void *, const Adafruit_FloppyCodec::Sector &sector) {
  Serial.printf("SECTOR %u %u %u %u %u %u\n\r", sector.cylinder, sector.head,
                sector.number, sector.size, sector.volume, sector.deleted);
  sendBytes(sector.data, sector.size);
  Serial.print("\n\r");
  return true;
}
