// Destructive engineering test: use only a disposable 1.44 MB floppy.
// Serial W overwrites cylinder 1, head 0. Serial B tests both heads at
// cylinders 0, 40 and 79. Writing begins only after the explicit serial command.
// Serial F receives a 1,474,560-byte IMG from the host, 9216 bytes after
// each IMAGE Cxx Hx READY line, and writes/verifies all 160 track sides.
// Hold BOOT to cancel. Writes require an INDEX pulse from the drive.
// V reads a full IMG. R C H TIMED exports flux and decoded sectors.
// X C H BYTES PACKED replays native 24 MHz flux, without re-encoding sectors.
// D detects DOS geometry. P C H updates sector 8 and checks its neighbours.
// E C H erases one side. Q reports physical write protection. See bench.py.
// I C H RETRIES writes one track from an IMG. F 0 0 RETRIES writes the full IMG.
// Optional rewrites (0..2) are logged, with failed captures saved before retry.

#include <Adafruit_Floppsy.h>
#include <Adafruit_TinyUSB.h>
#include <greasepack.h>

Adafruit_Floppsy floppsy;
// A standard PC floppy drive uses the DS1 select line.
Adafruit_Floppy &drive = floppsy.drive1;
Adafruit_MFM_Floppy disk(&drive, IBMPC1440K);

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

  drive.step_delay_us = 3000;

  Serial.print("Disposable disk only; WRITE EN on, disk tab closed.\n\r");
  Serial.print("W: cylinder 1 head 0. B: cylinders 0/40/79, both heads.\n\r");
  Serial.print("F: destructive full IMG write; host must send each requested track.\n\r");
  Serial.print("V: read IMG. R C H TIMED: capture. X C H BYTES PACKED: replay.\n\r");
  Serial.print("D: detect. P C H: patch sector 8. E C H: erase. Q: write protection.\n\r");
  Serial.print("READY\n\r");
}

void loop() {
  // Wait for an explicit test command; ignore line endings and other input.
  if (!Serial.available())
    return;
  char line[64] = {};
  size_t length = Serial.readBytesUntil('\n', line, sizeof(line) - 1);
  line[length] = 0;
  char command = 0;
  unsigned cylinderArg = 0, headArg = 0, value = 0, packed = 0;
  int fields = sscanf(line, "%c %u %u %u %u", &command, &cylinderArg,
                      &headArg, &value, &packed);
  if (!strchr("WBFIVRXDPEQ", command) || command == 0)
    return;
  if (cylinderArg >= 80 || headArg > 1 ||
      (command == 'R' && (fields != 4 || value > 1)) ||
      (command == 'X' && (fields != 5 || !value || value > sizeof(flux) || packed > 1)) ||
      ((command == 'F' || command == 'I') && value > 2) ||
      ((command == 'P' || command == 'E') && fields != 3)) {
    Serial.print("REJECT arguments\n\r");
    Serial.print("DONE\n\r");
    return;
  }

  // This test is for a 5 V 3.5-inch drive. Report the optional 12 V rail too.
  float supply12V = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_12V);
  float supply5V = floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V);
  Serial.printf("Drive supplies: %.2f V / %.2f V\n\r", supply12V, supply5V);
  if (!(supply5V >= 4.5)) {
    halt("Floppy 5 V must be present");
  }

  // 2. Enable the interface, select the drive, and establish its starting track.
  if (!floppsy.enable(true))
    halt("Floppy interface enable failed");

  drive.select(true);
  if (!drive.spin_motor(true)) {
    // Only an explicitly timed, read-only capture may continue without INDEX.
    if (command != 'R' || !value || !drive.index_pulses_missing())
      halt("No index pulse");
    Serial.print("No INDEX pulse; bounded timed read only\n\r");
  } else {
    Serial.print("Motor/index PASS\n\r");
  }

  if (!drive.goto_track(0))
    halt("Homing failed");
  Serial.print("Homing PASS\n\r");

  drive.set_density(true);

  if (command == 'Q') {
    Serial.printf("WRITE_PROTECTED %u\n\r", drive.get_write_protect());
    finish();
    return;
  }
  bool writes = strchr("WBFIXPE", command) != nullptr;
  if (writes && drive.get_write_protect()) {
    Serial.print("REJECT WRITE_PROTECTED\n\r");
    finish();
    return;
  }

  if (command == 'D') {
    bool found = disk.inserted(AUTODETECT);
    if (found) {
      Serial.printf("DETECTED %lu %u %u\n\r", (unsigned long)disk.size(),
                    disk.tracks_per_side(), disk.sectors_per_track());
    } else {
      Serial.print("UNKNOWN\n\r");
    }
    finish();
    return;
  }

  if (command == 'R' || command == 'X' || command == 'P' || command == 'E') {
    seekSide(cylinderArg, headArg);
    if (command == 'X') {
      Serial.printf("RAW READY %u\n\r", value);
      receiveBytes(flux, value);
      uint8_t *next = flux;
      uint8_t *end = flux + value;
      uint64_t ticks = 0;
      bool malformed = false;
      while (next < end) {
        unsigned interval = greaseunpack(&next, end, packed);
        // The RP2 writer spends 20 clocks in its fixed pulse/loop overhead.
        // Shorter intervals would be lengthened instead of replayed exactly.
        if (interval < 20 || interval == 0xffff)
          malformed = true;
        ticks += interval;
      }
      // This bench uses a 300 RPM drive. Require source flux with an overlap
      // beyond one revolution, and reject malformed or unbounded input.
      if (malformed || ticks < 5040000 || ticks > 24000000) {
        Serial.print("REJECT raw flux\n\r");
        finish();
        return;
      }
      if (drive.get_write_protect() || !drive.write_track(flux, value, packed))
        halt("Raw flux replay failed");
      Serial.printf("RAW WRITE PASS %u bytes at 24000000 Hz\n\r", value);
    } else if (command == 'E') {
      if (!drive.erase_track(250000))
        halt("Erase failed");
      Serial.print("ERASE PASS\n\r");
    } else if (command == 'P') {
      // Exercise the block API, then independently read all 18 sectors back.
      if (!disk.inserted(IBMPC1440K))
        halt("Block driver setup failed");
      uint32_t block = (cylinderArg * 2 + headArg) * 18;
      if (!disk.readSectors(block, expected, 18))
        halt("Patch source read failed");
      for (size_t i = 0; i < 512; i++)
        expected[7 * 512 + i] ^= 0xa5;
      if (!disk.writeSector(block + 7, expected + 7 * 512) || !disk.syncDevice())
        halt("Sector update failed");
      seekSide(cylinderArg, headArg);
      if (readSide(cylinderArg, headArg, false) != 18 ||
          memcmp(actual, expected, sizeof(actual)))
        halt("Patch changed neighbours");
      Serial.print("PATCH PASS sector 8 changed; 17 neighbours identical\n\r");
    }
    // R, X and E all retain the actual post-operation flux and decoded bytes.
    if (command != 'P')
      exportSide(cylinderArg, headArg, command == 'R' && value);
    finish();
    return;
  }

  if (command == 'V') {
    for (uint8_t side = 0; side < 160; side++) {
      uint8_t cylinder = side / 2, head = side % 2;
      seekSide(cylinder, head);
      size_t count = readSide(cylinder, head, false);
      Serial.printf("SECTORS %u %u %u 9216\n\r", cylinder, head, (unsigned)count);
      sendBytes(actual, sizeof(actual));
      Serial.print("END SECTORS\n\r");
    }
    finish();
    return;
  }

  // W checks one track side. B checks both heads at the outside/middle/inside.
  // F writes the whole image: 80 cylinders, with two heads per cylinder.
  uint8_t count = 1;
  if (command == 'B')
    count = 6;
  if (command == 'F')
    count = 160;

  for (uint8_t position = 0; position < count; position++) {
    uint8_t cylinder = 1;
    if (command == 'B') {
      const uint8_t testCylinders[] = {0, 40, 79};
      cylinder = testCylinders[position / 2];
    }
    if (command == 'F')
      cylinder = position / 2;
    uint8_t head = position % 2;
    if (command == 'I') {
      cylinder = cylinderArg;
      head = headArg;
    }

    // Move to the requested cylinder/head and let the head settle.
    if (!drive.goto_track(cylinder))
      halt("Seek failed");
    if (!drive.side(head))
      halt("Head selection failed");
    delay(20);

    // Pattern tests exercise five data patterns; an IMG supplies its own data.
    uint8_t patterns = 5;
    if (command == 'F' || command == 'I')
      patterns = 1;

    for (uint8_t pattern = 0; pattern < patterns; pattern++) {
      if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
        halt("Cancelled");
      if (drive.get_write_protect())
        halt("Disk is write-protected");

      // 3. Fill the expected-data buffer before issuing any write.
      if (command == 'F' || command == 'I') {
        // Ask the host for exactly one track side, with a bounded transfer wait.
        Serial.printf("IMAGE C%02u H%u READY\n\r", cylinder, head);
        receiveBytes(expected, sizeof(expected));
      } else {
        // All zeros, all ones, alternating bits, then a byte/sector pattern.
        uint32_t randomState = 0x12345678UL + cylinder * 2 + head;
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
              // A fixed LCG seed makes this pseudo-random pattern repeatable.
              randomState = randomState * 1664525UL + 1013904223UL;
              expected[i] = randomState >> 24;
              break;
          }
        }
      }

      uint8_t writeRetries = 0;
      if (command == 'F' || command == 'I')
        writeRetries = value;
      writeVerify(cylinder, head, pattern, writeRetries);
      Serial.printf("PASS C%02u H%u pattern %u: all 9216 bytes and 18 sector CRCs\n\r",
                    cylinder, head, pattern);
    }
  }

  // 6. Finish with writes disabled, the motor stopped, and the cable isolated.
  floppsy.enable(false);
  Serial.print("WRITE/READ PASS; motor off, cable isolated\n\r");
  Serial.print("DONE\n\r");
}

void writeVerify(uint8_t cylinder, uint8_t head, uint8_t pattern, uint8_t retries) {
  for (uint8_t attempt = 0; attempt <= retries; attempt++) {
    if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
      halt("Cancelled");
    if (drive.get_write_protect())
      halt("Disk is write-protected");

    // Re-encode before each write: the readback overwrites the flux buffer.
    // write_track starts at INDEX and stops at the following INDEX.
    size_t pulses = drive.encode_track_mfm(expected, 18, flux, sizeof(flux),
                                          1, cylinder);
    Serial.printf("Writing C%02u H%u pattern %u, %u pulses\n\r",
                  cylinder, head, pattern, (unsigned)pulses);
    if (!pulses || !drive.write_track(flux, pulses, false))
      halt("Flux write failed");
    delay(20);

    // Keep CRC-valid sectors between up to three read attempts. Then compare
    // every byte against the separate input buffer, including all neighbours.
    size_t sectors = readSide(cylinder, head, false);
    if (sectors == 18 && memcmp(expected, actual, sizeof(expected)) == 0)
      return;

    // A rewrite is never silent. Save a fresh capture of the failing track
    // before overwriting it, so another decoder can inspect the failure.
    Serial.printf("VERIFY FAILED C%02u H%u write attempt %u; saving flux\n\r",
                  cylinder, head, attempt + 1);
    exportSide(cylinder, head, false);
    if (attempt < retries)
      Serial.print("RETRY WRITE\n\r");
  }
  halt("Missing or different sectors after write");
}

void seekSide(uint8_t cylinder, uint8_t head) {
  if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
    halt("Cancelled");
  if (!drive.goto_track(cylinder) || !drive.side(head))
    halt("Seek failed");
  delay(20);
}

size_t readSide(uint8_t cylinder, uint8_t head, bool timed) {
  memset(actual, 0, sizeof(actual));
  memset(valid, 0, sizeof(valid));
  size_t count = 0;
  for (uint8_t attempt = 1; attempt <= 3 && count < 18; attempt++) {
    int32_t indexOffset = -1;
    size_t pulses = drive.capture_track(flux, sizeof(flux), &indexOffset, false,
                                       timed ? 250 : 220, timed ? 0 : 300);
    count = drive.decode_track_mfm(actual, 18, valid, flux, pulses, 1,
                                  false, nullptr, cylinder);
    Serial.printf("READ %u %u attempt=%u sectors=%u\n\r", cylinder, head,
                  attempt, (unsigned)count);
  }
  for (uint8_t i = 0; i < 18; i++) {
    if (!valid[i])
      memset(actual + i * 512, 0, 512);
  }
  return count;
}

void exportSide(uint8_t cylinder, uint8_t head, bool timed) {
  int32_t indexOffset = -1;
  size_t pulses = drive.capture_track(flux, sizeof(flux), &indexOffset, false,
                                     timed ? 250 : 220, timed ? 0 : 300);
  memset(actual, 0, sizeof(actual));
  size_t count = drive.decode_track_mfm(actual, 18, valid, flux, pulses, 1,
                                      true, nullptr, cylinder);
  uint32_t mask = 0;
  for (uint8_t i = 0; i < 18; i++) {
    if (valid[i])
      mask |= 1UL << i;
    else
      memset(actual + i * 512, 0, 512);
  }
  Serial.printf("FLUX %u %u %u %ld %u %08lx 24000000\n\r", cylinder, head,
                (unsigned)pulses, (long)indexOffset, (unsigned)count,
                (unsigned long)mask);
  sendBytes(flux, pulses);
  sendBytes(actual, sizeof(actual));
  Serial.print("END FLUX\n\r");
}

void receiveBytes(uint8_t *data, size_t length) {
  size_t received = 0;
  uint32_t progress = millis();
  while (received < length) {
    if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
      halt("Cancelled");
    if (millis() - progress > 10000)
      halt("Host transfer timed out");
    int available = Serial.available();
    if (available) {
      size_t amount = min((size_t)available, length - received);
      received += Serial.readBytes(data + received, amount);
      progress = millis();
    } else {
      yield();
    }
  }
}

void sendBytes(const uint8_t *data, size_t length) {
  size_t sent = 0;
  uint32_t progress = millis();
  while (sent < length) {
    if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW)
      halt("Cancelled");
    if (millis() - progress > 10000)
      halt("USB transfer timed out");
    size_t count = Serial.write(data + sent, min((size_t)64, length - sent));
    sent += count;
    if (count)
      progress = millis();
    yield();
  }
  Serial.flush();
  delay(20);
}

void finish() {
  floppsy.enable(false);
  Serial.print("DONE\n\r");
}

// Stop safely and retain the failure message until the board is reset.
void halt(const char *message) {
  floppsy.enable(false);
  Serial.printf("FAIL: %s; motor off, cable isolated\n\r", message);
  while (true)
    delay(10);
}
