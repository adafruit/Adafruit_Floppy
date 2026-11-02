// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Constructor/bounds regression: run on RP2350 with TinyUSB and >=150 KB RAM.
// No drive is connected or written. Each object is allocated separately because
// Adafruit_MFM_Floppy owns a large track buffer that must not go on the stack.
#include <Adafruit_Floppy.h>
#include <Adafruit_TinyUSB.h>
#include <new>

struct ExpectedFormat {
  adafruit_floppy_disk_t format;
  uint8_t cylinders;
  uint8_t sectors;
};

const ExpectedFormat formats[] = {
  {IBMPC360K, 40, 9},
  {IBMPC720K, 80, 9},
  {IBMPC720K_360RPM, 80, 9},
  {IBMPC1200K, 80, 15},
  {IBMPC1440K, 80, 18},
  {IBMPC1440K_360RPM, 80, 18},
  {AUTODETECT, 0, 0},
  {(adafruit_floppy_disk_t)7, 0, 0},
};

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.print("Send t to test MFM format settings; no drive access\n\r");
}

void runTests() {
  uint8_t sector[512] = {};
  for (const auto &expected : formats) {
    auto *disk = new (std::nothrow) Adafruit_MFM_Floppy(nullptr, expected.format);
    if (!disk) {
      Serial.print("FAIL allocation\n\r");
      return;
    }
    uint32_t count = (uint32_t)expected.cylinders * 2 * expected.sectors;
    bool passed = disk->tracks_per_side() == expected.cylinders &&
                  disk->sectors_per_track() == expected.sectors &&
                  disk->sectorCount() == count && disk->size() == count * 512;
    // One-past-the-end is rejected before any drive access, including an empty
    // AUTODETECT object. It previously passed the check and divided by zero.
    passed = passed && !disk->readSector(count, sector) &&
             !disk->writeSector(count, sector) &&
             !disk->inserted(expected.format);
    Serial.printf("%s format %u: %lu sectors\n\r",
                  passed ? "PASS" : "FAIL", expected.format, count);
    delete disk;
    if (!passed) {
      return;
    }
  }
  Serial.print("PASS all format constructors and bounds; no drive accessed\n\r");
}

void loop() {
  if (Serial.available() && Serial.read() == 't') {
    runTests();
  }
  delay(10);
}
