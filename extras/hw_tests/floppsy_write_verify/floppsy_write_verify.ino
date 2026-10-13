// Destructive engineering test: use only a disposable 1.44 MB floppy.
// Serial W overwrites cylinder 1, head 0. Serial B tests both heads at
// cylinders 1 and 79. The production self-test remains read-only.
// Serial F receives a 1,474,560-byte IMG from the host, 9216 bytes after
// each IMAGE Cxx Hx READY line, and writes/verifies all 160 track sides.
// Hold BOOT to cancel. This test requires an INDEX pulse from the drive.

#include <Adafruit_Floppsy.h>
#include <Adafruit_TinyUSB.h>

Adafruit_Floppsy floppsy;
// A standard PC floppy drive uses the DS1 select line.
Adafruit_Floppy &drive = floppsy.drive1;

// One track side holds 18 sectors of 512 bytes. Keep the expected data separate
// from the readback buffer so verification compares every byte we wrote.
uint8_t flux[300000];
uint8_t expected[18 * 512];
uint8_t actual[18 * 512];
uint8_t valid[18];

void setup() {
  Serial.begin(115200);
  // Wait for the Serial Monitor; no disk is written on startup.
  while (!Serial)
    delay(10);
  delay(250);
  Serial.print("Adafruit Floppsy destructive write/read verification\n\r");

  // Set up the drive pins, level shifters, and power monitor. The helper keeps
  // the cable isolated and write gate inactive until we explicitly enable it.
  if (!floppsy.begin())
    halt("Floppsy initialization failed");

  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
  drive.step_delay_us = 3000;

  Serial.print("Disposable disk only; WRITE EN on, disk tab closed.\n\r");
  Serial.print("W: cylinder 1 head 0. B: cylinders 1/79, both heads.\n\r");
  Serial.print("F: destructive full IMG write; host must send each requested track.\n\r");
}

void loop() {
  // Wait for an explicit test command; ignore line endings and other input.
  if (!Serial.available())
    return;
  char command = Serial.read();
  if (command != 'W' && command != 'B' && command != 'F')
    return;

  // 1. Check both drive supplies before enabling the cable or starting the motor.
  float supply12V = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_12V);
  float supply5V = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V);
  Serial.printf("Drive supplies: %.2f V / %.2f V\n\r", supply12V, supply5V);
  if (!(supply12V >= 10.8 && supply5V >= 4.5)) {
    halt("Connect the 12 V supply; floppy 5 V must be present");
  }

  // 2. Enable the interface, select the drive, and establish its starting track.
  if (!floppsy.enable(true))
    halt("Floppy interface enable failed");

  drive.select(true);
  if (!drive.spin_motor(true))
    halt("No index pulse");
  Serial.print("Motor/index PASS\n\r");

  if (!drive.goto_track(0))
    halt("Homing failed");
  Serial.print("Homing PASS\n\r");

  if (drive.get_write_protect())
    halt("Disk is write-protected");
  drive.set_density(true);

  // W checks one track side. B checks both heads near each end of the disk.
  // F writes the whole image: 80 cylinders, with two heads per cylinder.
  uint8_t count = 1;
  if (command == 'B')
    count = 4;
  if (command == 'F')
    count = 160;

  for (uint8_t position = 0; position < count; position++) {
    uint8_t cylinder = 1;
    if (position >= 2)
      cylinder = 79;
    if (command == 'F')
      cylinder = position / 2;
    uint8_t head = position % 2;

    // Move to the requested cylinder/head and let the head settle.
    if (!drive.goto_track(cylinder))
      halt("Seek failed");
    if (!drive.side(head))
      halt("Head selection failed");
    delay(20);

    // Pattern tests exercise five data patterns; an IMG supplies its own data.
    uint8_t patterns = 5;
    if (command == 'F')
      patterns = 1;

    for (uint8_t pattern = 0; pattern < patterns; pattern++) {
      if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
        halt("Cancelled");
      if (drive.get_write_protect())
        halt("Disk is write-protected");

      // 3. Fill the expected-data buffer before issuing any write.
      if (command == 'F') {
        // Ask the host for exactly one track side, with a bounded transfer wait.
        Serial.printf("IMAGE C%02u H%u READY\n\r", cylinder, head);
        size_t received = 0;
        uint32_t started = millis();
        while (received < sizeof(expected)) {
          if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
            halt("Cancelled");
          if (millis() - started > 5000)
            halt("Host image transfer timed out");
          if (Serial.available())
            expected[received++] = Serial.read();
          else
            yield();
        }
      } else {
        // All zeros, all ones, alternating bits, then a byte/sector pattern.
        for (size_t i = 0; i < sizeof(expected); i++) {
          switch (pattern) {
            case 0:
              expected[i] = 0;
              break;
            case 1:
              expected[i] = 255;
              break;
            case 2:
              expected[i] = 0x55;
              break;
            case 3:
              expected[i] = 0xAA;
              break;
            default:
              expected[i] = (uint8_t)(i + i / 512 + cylinder * 3 + head);
              break;
          }
        }
      }

      // 4. Encode the data as MFM flux, then write one complete track side.
      // This overwrites the selected track; write_track waits for INDEX.
      size_t pulses = drive.encode_track_mfm(expected, 18, flux, sizeof(flux),
                                              1, cylinder);
      Serial.printf("Writing C%02u H%u pattern %u, %u pulses\n\r",
                    cylinder, head, pattern, (unsigned)pulses);
      if (!pulses || !drive.write_track(flux, pulses, false))
        halt("Flux write failed");
      delay(20);

      // 5. Read the track back. Keep valid sectors between attempts so up to
      // three captures can recover a complete set of 18 sectors.
      memset(actual, 0, sizeof(actual));
      memset(valid, 0, sizeof(valid));
      size_t sectors = 0;
      for (uint8_t attempt = 0; attempt < 3 && sectors < 18; attempt++) {
        int32_t indexOffset = -1;
        pulses = drive.capture_track(flux, sizeof(flux), &indexOffset,
                                     false, 220, 300);
        sectors = drive.decode_track_mfm(actual, 18, valid, flux, pulses, 1,
                                        false, nullptr, cylinder);
        Serial.printf("Read %u pulses, %u/18 CRC-valid sectors\n\r",
                      (unsigned)pulses, (unsigned)sectors);
      }

      // CRC checks establish readable sectors; the byte comparison proves
      // that the recovered contents match the pattern or IMG data we sent.
      if (sectors != 18)
        halt("Missing sectors after write");
      if (memcmp(expected, actual, sizeof(expected)))
        halt("Readback differs from written bytes");
      Serial.printf("PASS C%02u H%u pattern %u: all 9216 bytes and 18 sector CRCs\n\r",
                    cylinder, head, pattern);
    }
  }

  // 6. Finish with writes disabled, the motor stopped, and the cable isolated.
  floppsy.enable(false);
  Serial.print("WRITE/READ PASS; motor off, cable isolated\n\r");
}

// Stop safely and retain the failure message until the board is reset.
void halt(const char *message) {
  floppsy.enable(false);
  Serial.printf("FAIL: %s; motor off, cable isolated\n\r", message);
  while (true)
    delay(10);
}
