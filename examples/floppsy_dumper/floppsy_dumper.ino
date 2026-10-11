// Read-only Floppsy disk dumper: IBM IMG and unnormalized SCP flux.
// MIT license. Copyright (c) 2026 Adafruit Industries.
#include <Adafruit_Floppy.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_INA3221.h>
#include <SdFat_Adafruit_Fork.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <Wire.h>
#include <hardware/gpio.h>
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_FloppySCP.h>

#if !defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350) || F_CPU != 240000000
#error Select Adafruit Floppsy RP2350 at 240 MHz, 8 MB PSRAM, Adafruit TinyUSB.
#endif

Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RESET);
Adafruit_INA3221 ina;
SdFs sd;
bool inaOK = false;
uint8_t floppyMenu = 5;
uint8_t selectPin = FLOPPY_SELECT1;
char floppyStatus[48] = "Ready; WRITE OFF";
char floppyProgress[48] = "";
char lastDump[20] = "";
char lastMap[20] = "";

void setup() {
  // Isolate the cable before initializing any output signal.
  gpio_put(FLOPPY_CMD_OE, true);
  pinMode(FLOPPY_CMD_OE, OUTPUT);
  gpio_put(FLOPPY_STAT_OE, true);
  pinMode(FLOPPY_STAT_OE, OUTPUT);
  const uint8_t pins[] = {FLOPPY_SIDE, FLOPPY_WRDATA, FLOPPY_STEP, FLOPPY_DIR,
    FLOPPY_MOTOR, FLOPPY_SELECT1, FLOPPY_SELECT0, FLOPPY_DENSITY, FLOPPY_WRGATE};
  for (uint8_t pin : pins) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
  pinMode(FLOPPY_CMD_DIR, OUTPUT);
  digitalWrite(FLOPPY_CMD_DIR, LOW);
  pinMode(FLOPPY_STAT_DIR, OUTPUT);
  digitalWrite(FLOPPY_STAT_DIR, HIGH);
  pinMode(PIN_USB_HOST_ENABLE, OUTPUT);
  digitalWrite(PIN_USB_HOST_ENABLE, LOW);
  const uint8_t buttons[] = {PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_LEFT,
    PIN_BUTTON_RIGHT, PIN_BUTTON_SELECT, PIN_BUTTON_BOOT, PIN_SD_DETECT};
  for (uint8_t pin : buttons) pinMode(pin, INPUT_PULLUP);
  Serial.begin(115200);
  SPI.setRX(NOPIN);
  tft.begin(24000000);
  tft.setRotation(1);
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  Wire.begin();
  Wire.setTimeout(25);
  inaOK = ina.begin(0x40, &Wire);
  if (inaOK) for (uint8_t channel=0; channel<3; channel++) ina.setShuntResistance(channel, 0.05);
  drawStatus();
}

void loop() {
  static bool connected = false;
  if (Serial && !connected) { printHelp(); connected = true; }
  if (!Serial) connected = false;
  if (Serial.available()) {
    char command = Serial.read();
    if (command == '?') printHelp();
    if (command == 'x') sendDump(lastDump);
    if (command == 'm') sendDump(lastMap);
    if (command == 'r' || command == 'd' || command == 'j' || command == 'g' || command == 'a' ||
        command == 'R' || command == 'D' || command == 'J' || command == 'G' || command == 'A') {
      bool ds0 = command >= 'A' && command <= 'Z';
      selectPin = ds0 ? FLOPPY_SELECT0 : FLOPPY_SELECT1;
      char lower = ds0 ? command + ('a' - 'A') : command;
      floppyMenu = lower == 'a' ? 5 : lower == 'g' ? 4 : lower == 'j' ? 3 : lower == 'd' ? 2 : 1;
      readFloppy(selectPin, floppyMenu);
    }
  }
  static uint32_t lastButton = 0;
  if (millis() - lastButton >= 250) {
    if (digitalRead(PIN_BUTTON_UP) == LOW) { floppyMenu = floppyMenu == 1 ? 5 : floppyMenu - 1; lastButton = millis(); drawStatus(); }
    if (digitalRead(PIN_BUTTON_DOWN) == LOW) { floppyMenu = floppyMenu == 5 ? 1 : floppyMenu + 1; lastButton = millis(); drawStatus(); }
    if (digitalRead(PIN_BUTTON_LEFT) == LOW || digitalRead(PIN_BUTTON_RIGHT) == LOW) {
      selectPin = selectPin == FLOPPY_SELECT0 ? FLOPPY_SELECT1 : FLOPPY_SELECT0;
      lastButton = millis(); drawStatus();
    }
    if (digitalRead(PIN_BUTTON_SELECT) == LOW) {
      readFloppy(selectPin, floppyMenu);
      while (digitalRead(PIN_BUTTON_SELECT) == LOW) delay(10);
      lastButton = millis();
    }
  }
  delay(1);
}

void stopFloppy(const char *message) {
  digitalWrite(FLOPPY_WRGATE, HIGH);
  digitalWrite(FLOPPY_WRDATA, HIGH);
  digitalWrite(FLOPPY_MOTOR, HIGH);
  digitalWrite(FLOPPY_SELECT0, HIGH);
  digitalWrite(FLOPPY_SELECT1, HIGH);
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  snprintf(floppyStatus, sizeof(floppyStatus), "%s", message);
  Serial.println(message);
}

bool floppyCancelled() {
  if (digitalRead(PIN_BUTTON_BOOT) == LOW) return true;
  // Consume line endings too, so a queued newline cannot hide the stop command.
  while (Serial.available()) {
    if (Serial.read() == '!') return true;
  }
  return false;
}

void readFloppy(uint8_t selectPin, uint8_t mode) {
  bool dump = mode >= 2;
  bool raw = mode == 4;
  bool doubleDensity = mode == 3;
  stopFloppy(raw ? "Preparing raw flux capture" : "Preparing read-only MFM test");
  if (!inaOK || ina.getBusVoltage(1) < 4.5) {
    stopFloppy("FAIL: floppy 5V missing");
    return;
  }
  // The first prototype browned out starting this drive on computer USB alone.
  if (ina.getBusVoltage(0) < 10.8) {
    stopFloppy("Connect 12V for drive testing");
    return;
  }
  const size_t fluxCapacity = 300000;
  uint8_t sectorsPerTrack = doubleDensity ? 9 : 18;
  size_t trackBytes = sectorsPerTrack * 512;
  uint8_t *flux = raw ? nullptr : (uint8_t *)malloc(fluxCapacity);
  uint8_t *sectors = raw ? nullptr : (uint8_t *)malloc(36 * 512);
  uint8_t *rawTrack = raw ? (uint8_t *)pmalloc(SCP_TRACK_CAPACITY) : nullptr;
  Adafruit_FloppyFluxCapture capture;
  if ((!raw && (!flux || !sectors)) ||
      (raw && (!rawTrack || !capture.begin(FLOPPY_RDDATA, FLOPPY_INDEX)))) {
    free(flux);
    free(sectors);
    free(rawTrack);
    capture.end();
    stopFloppy("FAIL: capture buffer allocation");
    return;
  }

  FsFile image;
  FsFile map;
  char partialName[20] = "";
  char imageName[20] = "";
  char mapName[20] = "";
  char badName[20] = "";
  uint8_t scp[SCP_HEADER_SIZE];
  uint32_t checksum = 0;
  bool success = true;
  bool imageCreated = false;
  if (dump) {
    sd.end();
    if (digitalRead(PIN_SD_DETECT) == LOW ||
        !sd.begin(SdioConfig(PIN_SD_CLK, PIN_SD_CMD_MOSI, PIN_SD_DAT0_MISO, 3.0))) {
      stopFloppy("FAIL: insert readable microSD");
      success = false;
    } else {
      // Reserve an unused name. Incomplete captures keep their .PART suffix.
      for (uint16_t number = 0; number < 1000; number++) {
        snprintf(partialName, sizeof(partialName), "%s%03u.PART", raw ? "FX" : "FD", number);
        snprintf(imageName, sizeof(imageName), "%s%03u.%s", raw ? "FX" : "FD", number, raw ? "SCP" : "IMG");
        snprintf(mapName, sizeof(mapName), "FD%03u.MAP", number);
        snprintf(badName, sizeof(badName), "FD%03u.BAD.IMG", number);
        if (!sd.exists(partialName) && !sd.exists(imageName) &&
            (raw || (!sd.exists(mapName) && !sd.exists(badName)))) {
          if (image.open(partialName, O_RDWR | O_CREAT | O_EXCL)) {
            if (raw || map.open(mapName, O_WRONLY | O_CREAT | O_EXCL)) break;
            image.close();
          }
        }
      }
      if (!image) {
        stopFloppy("FAIL: cannot create SD image");
        success = false;
      } else if (raw) {
        imageCreated = true;
        scpHeader(scp);
        success = image.write(scp, sizeof(scp)) == sizeof(scp);
        if (!success) stopFloppy("FAIL: initial SCP header write");
      } else {
        imageCreated = true;
        map.println("# IBM MFM, 80 cylinders, 2 heads, 512 bytes/sector");
        map.println("# BAD means unreadable: corresponding IMG sector is filled with zeros");
        map.println("cylinder,head,sector,status");
        success = map.sync() && !map.getWriteError();
        if (!success) stopFloppy("FAIL: initial sector map write");
      }
    }
  }

  Adafruit_Floppy drive(FLOPPY_DENSITY, FLOPPY_INDEX, selectPin,
                       FLOPPY_MOTOR, FLOPPY_DIR, FLOPPY_STEP,
                       FLOPPY_WRDATA, FLOPPY_WRGATE, FLOPPY_TRK0,
                       FLOPPY_WRPROT, FLOPPY_RDDATA, FLOPPY_SIDE, FLOPPY_READY);
  drive.led_pin = -1;
  uint32_t started = millis();
  if (success) {
    drive.begin(); // Both transceiver banks are still disabled here.
    // begin() leaves WRITE GATE in INPUT_PULLUP mode. On Arduino-Pico,
    // digitalWrite(HIGH) in that mode releases the pin without setting its
    // output latch. Set OUTPUT first while the cable drivers are DISABLED,
    // then drive HIGH and check both write signals before enabling the cable.
    pinMode(FLOPPY_WRGATE, OUTPUT);
    digitalWrite(FLOPPY_WRGATE, HIGH);
    digitalWrite(FLOPPY_WRDATA, HIGH);
    if (digitalRead(FLOPPY_WRGATE) != HIGH || digitalRead(FLOPPY_WRDATA) != HIGH) {
      stopFloppy("FAIL: write inhibit check");
      success = false;
    }
  }
  if (success) {
    Serial.println("WRITE GATE and WRITE DATA verified HIGH before cable enable");
    drive.step_delay_us = 3000;
    drive.set_density(!doubleDensity);
    digitalWrite(FLOPPY_STAT_OE, LOW);
    digitalWrite(FLOPPY_CMD_OE, LOW);
    drive.select(true);
    snprintf(floppyProgress, sizeof(floppyProgress), "Spinning up; BOOT cancels");
    drawStatus();
    if (!drive.spin_motor(true)) {
      stopFloppy("FAIL: no index pulse");
      success = false;
    } else if (!drive.goto_track(0)) {
      stopFloppy("FAIL: initial seek");
      success = false;
    }
  }
  if (success && mode == 5) {
    uint8_t detected = detectIBM(drive, flux, fluxCapacity, sectors);
    if (!detected) {
      stopFloppy("Unknown format; use RAW FLUX");
      success = false;
    } else {
      sectorsPerTrack = detected;
      doubleDensity = detected <= 11;
      trackBytes = sectorsPerTrack * 512;
      drive.set_density(!doubleDensity);
    }
  }
  Serial.printf("DS%u: %s; 80 cylinders, 2 heads; %u sectors/track; READ ONLY\n",
                selectPin == FLOPPY_SELECT0 ? 0 : 1,
                raw ? "raw SCP, 3 revolutions at 25ns" : "IBM MFM IMG", sectorsPerTrack);
  uint16_t completed = 0;
  uint16_t decodedSectors = 0;
  uint16_t badSectors = 0;
  uint16_t trackCount = dump ? 160 : 2;
  for (uint16_t track = 0; success && track < trackCount; track++) {
    uint8_t cylinder = track / 2;
    uint8_t head = track % 2;
    if (floppyCancelled()) {
      stopFloppy("Cancelled; motor OFF");
      success = false;
      break;
    }
    if (!drive.goto_track(cylinder) || !drive.side(head)) {
      stopFloppy("FAIL: seek / head select");
      success = false;
      break;
    }
    delay(15);
    if (raw) {
      snprintf(floppyProgress, sizeof(floppyProgress), "C%02u H%u  3 revs  BOOT=stop", cylinder, head);
      snprintf(floppyStatus, sizeof(floppyStatus), "%u/160 tracks  %.1f MB", completed, image.curPosition() / 1000000.0);
      drawStatus();
      if (!capture.capture(floppyCancelled)) {
        stopFloppy(capture.error);
        success = false;
        break;
      }
      size_t badPulse = 0;
      size_t bytes = scpTrack(track, capture.samples, capture.count, capture.index,
                             rawTrack, SCP_TRACK_CAPACITY, &badPulse);
      if (!bytes) {
        Serial.printf("SCP encode failed: count=%u index=%08lx/%08lx/%08lx/%08lx first=%08lx/%08lx last=%08lx\n",
                      (unsigned)capture.count, (unsigned long)capture.index[0], (unsigned long)capture.index[1],
                      (unsigned long)capture.index[2], (unsigned long)capture.index[3],
                      (unsigned long)capture.samples[0], (unsigned long)capture.samples[1],
                      (unsigned long)(capture.count ? capture.samples[capture.count - 1] : 0));
        Serial.printf("Bad pulse %u: %08lx/%08lx/%08lx\n", (unsigned)badPulse,
                      (unsigned long)(badPulse ? capture.samples[badPulse - 1] : 0),
                      (unsigned long)capture.samples[badPulse], (unsigned long)capture.samples[badPulse + 1]);
      }
      uint32_t offset = image.curPosition();
      if (!bytes || !writeVerified(image, rawTrack, bytes)) {
        stopFloppy("FAIL: SCP encoding / SD verify");
        success = false;
        break;
      }
      scpPut32(scp + 16 + 4 * track, offset);
      checksum += scpChecksum(rawTrack, bytes);
      // Save a consistent partial file after every track; no unwritten offsets.
      scp[7] = track;
      scpPut32(scp + 12, checksum + scpChecksum(scp + 16, sizeof(scp) - 16));
      uint32_t end = image.curPosition();
      if (!image.seekSet(0) || !writeVerified(image, scp, sizeof(scp)) || !image.seekSet(end)) {
        stopFloppy("FAIL: SCP header update");
        success = false;
        break;
      }
      Serial.printf("C%02u H%u: %u samples, %u bytes, %.2f RPM, saved + verified\n",
                    cylinder, head, (unsigned)capture.count, (unsigned)bytes,
                    2400000000.0 / (capture.index[0] - capture.index[1]));
      completed++;
      continue;
    }
    uint8_t valid[36] = {};
    memset(sectors, 0, trackBytes);
    size_t count = 0;
    for (uint8_t attempt = 1; attempt <= 3 && count < sectorsPerTrack; attempt++) {
      snprintf(floppyProgress, sizeof(floppyProgress), "C%02u H%u  try %u  BOOT=stop", cylinder, head, attempt);
      snprintf(floppyStatus, sizeof(floppyStatus), "%u/%u tracks  %u/%u sectors", completed, trackCount, (unsigned)count, sectorsPerTrack);
      drawStatus();
      int32_t indexOffset = -1;
      size_t pulses = drive.capture_track(flux, fluxCapacity, &indexOffset, false, 220, 300);
      count = drive.decode_track_mfm(sectors, sectorsPerTrack, valid, flux, pulses,
                                     doubleDensity ? 2.0 : sectorsPerTrack == 36 ? 0.5 : 1.0, false, nullptr, cylinder);
      Serial.printf("C%02u H%u try %u: %u pulses, %u/%u CRC-valid matching sectors\n",
                    cylinder, head, attempt, (unsigned)pulses, (unsigned)count, sectorsPerTrack);
      if (floppyCancelled()) {
        stopFloppy("Cancelled; motor OFF");
        success = false;
        break;
      }
    }
    if (!success) break;
    decodedSectors += count;
    badSectors += sectorsPerTrack - count;
    for (uint8_t sector = 0; sector < sectorsPerTrack; sector++) {
      // The decoder can leave bytes from a CRC-failed candidate in the buffer.
      if (!valid[sector]) memset(sectors + sector * 512, 0, 512);
      if (dump) map.printf("%u,%u,%u,%s\n", cylinder, head, sector + 1, valid[sector] ? "OK" : "BAD");
    }
    if (track == 0 && valid[0]) {
      Serial.printf("Boot sector: bytes/sector=%u, sectors/track=%u, heads=%u, signature=%02X%02X\n",
                    sectors[11] + 256 * sectors[12], sectors[24] + 256 * sectors[25],
                    sectors[26] + 256 * sectors[27], sectors[510], sectors[511]);
    }
    if (dump) {
      if (!writeVerified(image, sectors, trackBytes) || !map.sync() || map.getWriteError()) {
        stopFloppy("FAIL: SD image/map write verify");
        success = false;
        break;
      }
    }
    if (success) completed++;
  }
  // Stop the mechanics before final SD metadata operations, including errors.
  digitalWrite(FLOPPY_MOTOR, HIGH);
  digitalWrite(FLOPPY_SELECT0, HIGH);
  digitalWrite(FLOPPY_SELECT1, HIGH);
  digitalWrite(FLOPPY_CMD_OE, HIGH);
  digitalWrite(FLOPPY_STAT_OE, HIGH);
  if (map) {
    map.printf("# %s: %u/160 tracks, %u good sectors, %u bad sectors\n",
               success ? "FINISHED" : "INCOMPLETE", completed, decodedSectors, badSectors);
    if (!map.sync() || map.getWriteError()) { stopFloppy("FAIL: final map sync"); success = false; }
    map.close();
  }
  if (image) {
    if (!image.sync() || (success && !raw && image.fileSize() != trackBytes * 160)) {
      stopFloppy("FAIL: SD image final size/sync");
      success = false;
    }
    if (success && badSectors) snprintf(imageName, sizeof(imageName), "%s", badName);
    if (success && !image.rename(imageName)) {
      stopFloppy("FAIL: image rename; kept PART");
      success = false;
    }
    image.close();
  }
  if (imageCreated) {
    snprintf(lastDump, sizeof(lastDump), "%s", success ? imageName : partialName);
    if (!raw) snprintf(lastMap, sizeof(lastMap), "%s", mapName);
  }
  if (dump) sd.end();
  free(flux);
  free(sectors);
  free(rawTrack);
  capture.end();
  if (!dump && badSectors && success) {
    char result[48];
    snprintf(result, sizeof(result), "FAIL track 0: %u/36 sectors", decodedSectors);
    stopFloppy(result);
    success = false;
  }
  if (success) {
    char result[48];
    if (dump) snprintf(result, sizeof(result), "%s saved%s", imageName, badSectors ? " *" : " / OK");
    else snprintf(result, sizeof(result), "Track 0: 36/36 sectors OK");
    stopFloppy(result);
  }
  snprintf(floppyProgress, sizeof(floppyProgress), "%u/%u tracks in %.1fs; WRITE OFF",
           completed, trackCount, (millis() - started) / 1000.0);
  Serial.println(floppyProgress);
  if (dump && !raw) Serial.printf("%u CRC-valid sectors, %u BAD zero-filled sectors; map: %s\n", decodedSectors, badSectors, mapName);
  if (dump && !success && partialName[0]) Serial.printf("Incomplete capture, if created: %s\n", partialName);
  drawStatus();
}

uint8_t detectIBM(Adafruit_Floppy &drive, uint8_t *flux, size_t capacity, uint8_t *sectors) {
  snprintf(floppyStatus, sizeof(floppyStatus), "Detecting IBM MFM format");
  // Probe 500, 250 and 1000 kbit/s with strict sector IDs and data CRCs.
  // All supported geometries have 80 cylinders, two heads and 512-byte sectors.
  const uint16_t bitRates[] = {500, 250, 1000};
  const uint8_t candidates[][3] = {{18, 21, 22}, {9, 10, 11}, {36, 0, 0}};
  uint8_t detected = 0;
  for (uint8_t rate = 0; rate < 3; rate++) {
    uint8_t completeSides[3] = {};
    bool bootMatches[3] = {};
    bool bootConflicts[3] = {};
    drive.set_density(bitRates[rate] != 250);
    for (uint8_t probe = 0; probe < 4; probe++) {
      uint8_t cylinder = probe < 2 ? 0 : 2;
      uint8_t head = probe % 2;
      if (floppyCancelled() || !drive.goto_track(cylinder) || !drive.side(head)) return 0;
      delay(15);
      snprintf(floppyProgress, sizeof(floppyProgress), "%ukbit/s C%u H%u", bitRates[rate], cylinder, head);
      drawStatus();
      uint8_t valid[36] = {};
      memset(sectors, 0, 36 * 512);
      int32_t indexOffset = -1;
      size_t pulses = drive.capture_track(flux, capacity, &indexOffset, false, 220, 300);
      size_t found = drive.decode_track_mfm(sectors, 36, valid, flux, pulses,
                                           500.0 / bitRates[rate], true, nullptr, cylinder);
      for (uint8_t option = 0; option < 3; option++) {
        uint8_t expected = candidates[rate][option];
        if (!expected) continue;
        bool complete = found == expected;
        for (uint8_t sector = 0; sector < 36; sector++) {
          if ((valid[sector] != 0) != (sector < expected)) complete = false;
        }
        if (complete) completeSides[option]++;
        if (probe == 0 && valid[0]) {
          uint16_t bps = sectors[11] + 256 * sectors[12];
          uint16_t total = sectors[19] + 256 * sectors[20];
          uint16_t spt = sectors[24] + 256 * sectors[25];
          uint16_t heads = sectors[26] + 256 * sectors[27];
          // Only treat a plausible DOS BPB as geometry evidence.
          bool plausible = bps == 512 && spt >= 8 && spt <= 36 &&
                           heads >= 1 && heads <= 2 && total;
          bootMatches[option] = plausible && spt == expected && heads == 2 && total == expected * 160;
          bootConflicts[option] = plausible && !bootMatches[option];
        }
      }
      Serial.printf("Detect %ukbit/s C%u H%u: %u CRC-valid matching sectors\n",
                    bitRates[rate], cylinder, head, (unsigned)found);
    }
    for (uint8_t option = 0; option < 3; option++) {
      if (!candidates[rate][option] || bootConflicts[option]) continue;
      if (completeSides[option] >= 2 || (bootMatches[option] && completeSides[option] >= 1)) {
        if (detected) return 0; // Conflicting evidence: preserve raw instead.
        detected = candidates[rate][option];
        Serial.printf("Detected %u KB IBM MFM (%u sectors/track)%s\n", detected * 80, detected,
                      bootMatches[option] ? "; BPB agrees" : "; standard 80-cylinder geometry inferred from matching sector IDs");
      }
    }
  }
  return detected;
}

bool writeVerified(FsFile &file, const uint8_t *data, size_t size) {
  uint32_t start = file.curPosition();
  // PIO SDIO's time-critical writer must read from SRAM, not cached PSRAM.
  // Stage blocks so a PSRAM cache miss cannot starve the SD data stream.
  alignas(4) uint8_t verify[4096];
  for (size_t offset = 0; offset < size; offset += sizeof(verify)) {
    size_t bytes = size - offset;
    if (bytes > sizeof(verify)) bytes = sizeof(verify);
    memcpy(verify, data + offset, bytes);
    if (digitalRead(PIN_SD_DETECT) == LOW || file.write(verify, bytes) != bytes) {
      Serial.printf("SD write failed at %lu: card error %u/%lu\n", (unsigned long)(start + offset),
                    sd.sdErrorCode(), (unsigned long)sd.sdErrorData());
      return false;
    }
  }
  if (!file.sync() || !file.seekSet(start)) return false;
  for (size_t offset = 0; offset < size; offset += sizeof(verify)) {
    size_t bytes = size - offset;
    if (bytes > sizeof(verify)) bytes = sizeof(verify);
    if (file.read(verify, bytes) != (int)bytes || memcmp(verify, data + offset, bytes)) return false;
  }
  return true; // File position is back at the end of the verified data.
}

void sendDump(const char *name) {
  // A bounded binary transfer over the computer USB port, with the drive off.
  // Receive the FILE line, then exactly the announced byte count, then END.
  stopFloppy("USB file download; motor OFF");
  sd.end();
  if (!name[0] || digitalRead(PIN_SD_DETECT) == LOW ||
      !sd.begin(SdioConfig(PIN_SD_CLK, PIN_SD_CMD_MOSI, PIN_SD_DAT0_MISO, 3.0))) {
    Serial.println("ERROR: no dump available / SD missing");
    return;
  }
  FsFile file = sd.open(name, O_RDONLY);
  if (!file) { Serial.println("ERROR: cannot open dump"); sd.end(); return; }
  Serial.printf("FILE %s %lu\n", name, (unsigned long)file.fileSize());
  uint8_t buffer[4096];
  bool success = true;
  while (file.available() && success) {
    int bytes = file.read(buffer, sizeof(buffer));
    if (bytes <= 0) { success = false; break; }
    int sent = 0;
    uint32_t lastSent = millis();
    while (sent < bytes) {
      int room = Serial.availableForWrite();
      if (room > bytes - sent) room = bytes - sent;
      if (room > 0) {
        size_t written = Serial.write(buffer + sent, room);
        sent += written;
        if (written) lastSent = millis();
      }
      if (!Serial || digitalRead(PIN_BUTTON_BOOT) == LOW || millis() - lastSent > 3000) {
        success = false;
        break;
      }
      yield();
    }
  }
  file.close();
  sd.end();
  Serial.println(success ? "\nEND" : "\nERROR: transfer interrupted");
  snprintf(floppyStatus, sizeof(floppyStatus), "%s", success ? "USB download complete" : "USB download interrupted");
}

void drawStatus() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextWrap(true);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setCursor(8, 10);
  tft.println("FLOPPSY DUMPER");
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(8, 40);
  tft.printf("DS%u   ", selectPin == FLOPPY_SELECT0 ? 0 : 1);
  const char *names[] = {"", "TRACK 0", "IMG 1.44MB", "IMG 720KB", "FLUX SCP", "AUTO IBM"};
  tft.println(names[floppyMenu]);
  tft.setCursor(8, 82);
  tft.println(floppyStatus);
  tft.setCursor(8, 140);
  tft.println(floppyProgress);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(8, 205);
  tft.println("UP/DN: mode   L/R: drive   SEL: start");
  tft.setCursor(8, 220);
  tft.println("BOOT: cancel   Keep hardware WRITE OFF");
}

void printHelp() {
  Serial.println("Floppsy standalone dumper; 12V connected, hardware WRITE OFF.");
  Serial.println("a=auto IBM IMG, r=track zero, d=IMG 1.44MB, j=IMG 720KB, g=raw SCP (DS1)");
  Serial.println("A/R/D/J/G use DS0. BOOT or !=cancel. No floppy write commands.");
  Serial.println("AUTO: 720/800/880/1440/1680/1760/2880 KiB IBM MFM; other encodings use SCP.");
  Serial.println("x=download last dump; m=last IMG map. FILE name size\n, exact bytes, \nEND.");
  Serial.println("BAD sectors: zero-filled + .MAP, .BAD.IMG filename. Incomplete: .PART.");
  Serial.println("SCP: 80 cylinders, 2 heads, 3 index-cued revolutions at 25ns, no normalization.");
}
