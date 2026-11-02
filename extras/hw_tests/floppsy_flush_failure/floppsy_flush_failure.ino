// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Floppsy RP2350 / 240 MHz / TinyUSB. Read-only failure-injection test.
// A readable 1.44 MB track zero is needed. No write reaches the hardware:
// seek failure and simulated write protection block every attempted flush.
#include <Adafruit_Floppsy.h>
#include <Adafruit_TinyUSB.h>
#include <new>

Adafruit_Floppsy floppsy;

class TestDrive : public Adafruit_Floppy {
public:
  TestDrive(const Adafruit_Floppy &source) : Adafruit_Floppy(source) {}
  bool failSeek = false;
  bool protectedDisk = false;
  unsigned seekCalls = 0;

  bool goto_track(int track) override {
    seekCalls++;
    return !failSeek && Adafruit_Floppy::goto_track(track);
  }

  bool get_write_protect() override {
    return protectedDisk || Adafruit_Floppy::get_write_protect();
  }
};

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.print("Send t for read-only cache failure tests\n\r");
}

void runTests() {
  if (!floppsy.begin() || floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V) < 4.5f) {
    Serial.print("FAIL board/power setup\n\r");
    return;
  }
  TestDrive drive(floppsy.drive1);
  auto *disk = new (std::nothrow) Adafruit_MFM_Floppy(&drive, IBMPC1440K);
  if (!disk) {
    Serial.print("FAIL track-buffer allocation\n\r");
    return;
  }
  bool passed = false;
  do {
    if (!floppsy.enable(true)) {
      break;
    }
    drive.select(true);
    drive.set_density(true);
    if (!drive.spin_motor(true) || !disk->inserted(IBMPC1440K) ||
        disk->readTrack(0, false) != 18) {
      Serial.print("FAIL readable track zero required\n\r");
      break;
    }
    uint8_t changed[512], recovered[512];
    if (!disk->readSector(0, changed)) {
      break;
    }
    changed[100] ^= 0x5a; // A visible cache-only edit, never written to media.
    if (!disk->writeSector(0, changed) || !disk->dirty()) {
      break;
    }

    drive.failSeek = true;
    if (disk->syncDevice() || !disk->dirty() || disk->readTrack(1, false) != -1 ||
        !disk->dirty() || !disk->readSector(0, recovered) ||
        memcmp(changed, recovered, sizeof(changed))) {
      Serial.print("FAIL seek error lost pending data\n\r");
      break;
    }
    Serial.print("PASS seek failure retained cache and blocked track replacement\n\r");

    // Keep the seek failure as a second barrier against a hardware write.
    // Protection should reject the flush even before attempting that seek.
    unsigned before = drive.seekCalls;
    drive.protectedDisk = true;
    if (disk->syncDevice() || drive.seekCalls != before || !disk->dirty() ||
        !disk->readSector(0, recovered) ||
        memcmp(changed, recovered, sizeof(changed))) {
      Serial.print("FAIL write protection lost pending data\n\r");
      break;
    }
    Serial.print("PASS changed write protection retained pending data\n\r");
    passed = true;
  } while (false);

  floppsy.enable(false);
  // Deliberately discard the cache-only test edit after both blocked flushes.
  // Do not release the injected failure and call syncDevice(): that would write.
  delete disk;
  Serial.printf("%s cache failure test; motor stopped; no hardware write\n\r",
                passed ? "PASS" : "FAIL");
}

void loop() {
  if (Serial.available() && Serial.read() == 't') {
    runTests();
  }
  delay(10);
}
