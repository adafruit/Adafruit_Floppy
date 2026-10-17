// Read-only Floppsy disk dumper: IBM IMG and unnormalized SCP flux.
// MIT license. Copyright (c) 2026 Adafruit Industries.
#include <Adafruit_Floppsy.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SdFat_Adafruit_Fork.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <Adafruit_FloppyFluxCapture.h>
#include <Adafruit_FloppySCP.h>
#include <Fonts/FreeSansBold9pt7b.h>

#if F_CPU != 240000000 || !defined(USE_TINYUSB)
#error Select 240 MHz and Adafruit TinyUSB for the Floppsy dumper.
#endif

Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RESET);
// An indexed framebuffer leaves SRAM available for the 300 KB flux buffer.
// Expand one scanline to RGB565 when sending a finished frame to the TFT.
GFXcanvas8 screen(320, 240);
enum NeonColor { INK, GRID, PANEL, CYAN, PINK, WHITE, MUTED, LIME, AMBER };
const uint16_t neonPalette[] = {
  0x0843, 0x18CB, 0x1086, 0x07FF, 0xF8B6, 0xF7BF, 0x9519, 0xAFE8, 0xFDC0
};
Adafruit_Floppsy floppsy;
SdFs sd;
bool boardOK = false;
uint8_t floppyMenu = 5;
uint8_t selectPin = FLOPPY_SELECT1;
char floppyStatus[48] = "Ready to read";
char floppyProgress[48] = "";
char lastDump[20] = "";
char lastMap[20] = "";
bool reading = false;
uint16_t tracksDone = 0;
uint16_t tracksTotal = 0;

void setup() {
  // Initialize the drive pins, BOOT input and power monitor with the cable
  // isolated. Display, navigation and SD setup belong to this example.
  boardOK = floppsy.begin();
  pinMode(PIN_USB_HOST_ENABLE, OUTPUT);
  digitalWrite(PIN_USB_HOST_ENABLE, LOW);
  const uint8_t buttons[] = {PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_LEFT,
    PIN_BUTTON_RIGHT, PIN_BUTTON_SELECT, PIN_SD_DETECT};
  for (uint8_t pin : buttons) pinMode(pin, INPUT_PULLUP);
  Serial.begin(115200);
  SPI.setRX(NOPIN);
  tft.begin(24000000);
  tft.setRotation(1);
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);
  drawStatus();
}

void loop() {
  static bool connected = false;
  if (Serial && !connected) { printHelp(); connected = true; drawStatus(); }
  if (!Serial && connected) { connected = false; drawStatus(); }
  static bool lastUSB = false;
  bool usb = TinyUSBDevice.mounted();
  if (usb != lastUSB) { lastUSB = usb; drawStatus(); }
  static int lastCard = -1;
  int card = digitalRead(PIN_SD_DETECT);
  if (card != lastCard) { lastCard = card; drawStatus(); }
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
  floppsy.enable(false);
  reading = false;
  snprintf(floppyStatus, sizeof(floppyStatus), "%s", message);
  Serial.println(message);
  drawStatus();
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
  tracksDone = 0;
  tracksTotal = 0;
  floppyProgress[0] = 0;
  stopFloppy(raw ? "Preparing raw flux capture" : "Preparing read-only MFM test");
  reading = true;
  // A new disk may have been inserted since the previous request. Reset the
  // cached track/index state before checking supplies and enabling the drive.
  boardOK = floppsy.begin();
  if (!boardOK || floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_5V) < 4.5) {
    stopFloppy("FAIL: floppy 5V missing");
    return;
  }
  // The first prototype browned out starting this drive on computer USB alone.
  if (floppsy.powerMonitor.getBusVoltage(FLOPPSY_POWER_12V) < 10.8) {
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
        snprintf(mapName, sizeof(mapName), "%s%03u.%s", raw ? "FX" : "FD", number, raw ? "TXT" : "MAP");
        snprintf(badName, sizeof(badName), "FD%03u.BAD.IMG", number);
        if (!sd.exists(partialName) && !sd.exists(imageName) &&
            !sd.exists(mapName) && (raw || !sd.exists(badName))) {
          if (image.open(partialName, O_RDWR | O_CREAT | O_EXCL)) {
            if (map.open(mapName, O_WRONLY | O_CREAT | O_EXCL)) break;
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

  Adafruit_Floppy *selectedDrive = &floppsy.drive1;
  if (selectPin == FLOPPY_SELECT0)
    selectedDrive = &floppsy.drive0;
  Adafruit_Floppy &drive = *selectedDrive;
  uint32_t started = millis();
  if (success) {
    // The board helper drives both write outputs inactive while isolated.
    // Verify their levels before connecting the cable to this read-only sketch.
    if (digitalRead(FLOPPY_WRGATE) != HIGH || digitalRead(FLOPPY_WRDATA) != HIGH) {
      stopFloppy("FAIL: write inhibit check");
      success = false;
    }
  }
  if (success) {
    Serial.println("WRITE GATE and WRITE DATA verified HIGH before cable enable");
    drive.step_delay_us = 3000;
    drive.set_density(!doubleDensity);
    if (!floppsy.enable(true)) {
      stopFloppy("FAIL: floppy interface enable");
      success = false;
    }
  }
  if (success) {
    drive.select(true);
    snprintf(floppyProgress, sizeof(floppyProgress), "Spinning up; BOOT cancels");
    drawStatus();
    if (!drive.spin_motor(true) && !drive.index_pulses_missing()) {
      stopFloppy("FAIL: motor startup");
      success = false;
    } else if (!drive.goto_track(0)) {
      stopFloppy("FAIL: initial seek");
      success = false;
    }
  }
  bool timed = success && drive.index_pulses_missing();
  if (success && timed) {
    Serial.print("No INDEX pulse found: using timed reads; rotation is not measured.\n\r");
  }
  if (success && raw) {
    scpHeader(scp, !timed);
    map.print(timed ? "INDEX-less: 600 ms capture, three nominal 200 ms windows; 300 RPM assumed.\n\r"
                    : "Three revolutions bounded by measured INDEX pulses.\n\r");
    map.print("Unnormalized 25 ns flux timing. Timed windows are not physical INDEX marks.\n\r");
    success = map.sync() && !map.getWriteError() && image.seekSet(0) &&
              writeVerified(image, scp, sizeof(scp));
    if (!success) stopFloppy("FAIL: capture metadata write");
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
                raw ? (timed ? "raw SCP, 600ms at 25ns" : "raw SCP, 3 revolutions at 25ns") : "IBM MFM IMG", sectorsPerTrack);
  uint16_t completed = 0;
  uint16_t decodedSectors = 0;
  uint16_t badSectors = 0;
  uint16_t trackCount = dump ? 160 : 2;
  tracksTotal = trackCount;
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
      snprintf(floppyProgress, sizeof(floppyProgress), "C%02u H%u  %s  BOOT=stop", cylinder, head, timed ? "600ms" : "3 revs");
      snprintf(floppyStatus, sizeof(floppyStatus), "%u/160 tracks  %.1f MB", completed, image.curPosition() / 1000000.0);
      drawStatus();
      if (!(timed ? capture.captureTimed(floppyCancelled, 24000000) : capture.capture(floppyCancelled))) {
        stopFloppy(capture.error);
        success = false;
        break;
      }
      uint32_t boundaries[4];
      for (uint8_t i = 0; i < 4; i++) {
        boundaries[i] = timed ? capture.start - 8000000u * i : capture.index[i];
      }
      size_t badPulse = 0;
      size_t bytes = scpTrack(track, capture.samples, capture.count, boundaries,
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
      Serial.printf("C%02u H%u: %u samples, %u bytes, %.2f RPM%s, saved + verified\n\r",
                    cylinder, head, (unsigned)capture.count, (unsigned)bytes,
                    2400000000.0 / (boundaries[0] - boundaries[1]), timed ? " assumed" : " measured");
      completed++;
      tracksDone = completed;
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
      size_t pulses = drive.capture_track(flux, fluxCapacity, &indexOffset, false,
                                          timed ? 250 : 220, timed ? 0 : 300);
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
    if (success) {
      completed++;
      tracksDone = completed;
    }
  }
  // Stop the mechanics before final SD metadata operations, including errors.
  floppsy.enable(false);
  if (map) {
    if (raw) map.printf("%s: %u/160 tracks\n\r", success ? "FINISHED" : "INCOMPLETE", completed);
    else map.printf("# %s: %u/160 tracks, %u good sectors, %u bad sectors\n",
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
    snprintf(lastMap, sizeof(lastMap), "%s", mapName);
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
      size_t pulses = drive.capture_track(flux, capacity, &indexOffset, false,
                        drive.index_pulses_missing() ? 250 : 220,
                        drive.index_pulses_missing() ? 0 : 300);
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
  drawStatus();
}

// Keep complete words inside the result panel, including longer fault messages.
void drawResult(const char *message) {
  char words[48];
  snprintf(words, sizeof(words), "%s", message);
  screen.setFont(&FreeSansBold9pt7b);
  screen.setTextSize(1);
  int16_t x = 19;
  int16_t y = 160;
  char *word = strtok(words, " ");
  while (word) {
    int16_t left, top;
    uint16_t width, height;
    screen.getTextBounds(word, x, y, &left, &top, &width, &height);
    if (left + width > 301 && x > 19) {
      x = 19;
      y += 21;
    }
    screen.setCursor(x, y);
    screen.print(word);
    screen.print(' ');
    x = screen.getCursorX();
    word = strtok(nullptr, " ");
  }
  screen.setFont();
}

void drawStatus() {
  if (!screen.getBuffer()) return;
  screen.fillScreen(INK);
  screen.setFont();
  screen.setTextWrap(false);
  screen.setTextSize(1);

  // A subdued arcade grid stays behind the cards, never behind result text.
  for (int16_t y = 75; y < 215; y += 16) {
    screen.drawFastHLine(0, y, 320, GRID);
  }
  for (int16_t x = -120; x < 440; x += 40) {
    screen.drawLine(160 + (x - 160) / 3, 75, x, 215, GRID);
  }

  // Pixel lettering and an offset pink shadow give the title its arcade look.
  screen.setTextSize(4);
  screen.setCursor(14, 13);
  screen.setTextColor(PINK);
  screen.print("FLOPPSY");
  screen.setCursor(11, 10);
  screen.setTextColor(WHITE);
  screen.print("FLOPPSY");
  screen.setTextSize(1);
  screen.setTextColor(CYAN);
  screen.setCursor(12, 46);
  screen.print("N E O N  A R C A D E");
  screen.drawFastHLine(12, 62, 190, PINK);
  screen.drawFastHLine(202, 62, 106, CYAN);

  screen.drawRoundRect(246, 9, 62, 31, 5, CYAN);
  screen.setTextSize(2);
  screen.setCursor(259, 17);
  screen.printf("DS%u", selectPin == FLOPPY_SELECT0 ? 0 : 1);
  screen.setTextSize(1);
  screen.setTextColor(LIME);
  screen.setCursor(254, 46);
  screen.print("READ ONLY");

  // All five modes stay visible. UP/DOWN moves the highlighted card.
  const char *labels[] = {"CHECK", "IMAGE", "IMAGE", "FLUX", "AUTO"};
  const char *details[] = {"TRK 0", "1.44M", "720K", "SCP", "IBM"};
  for (uint8_t mode = 1; mode <= 5; mode++) {
    int16_t x = 10 + (mode - 1) * 61;
    bool selected = mode == floppyMenu;
    screen.fillRoundRect(x, 77, 56, 38, 4, PANEL);
    screen.drawRoundRect(x, 77, 56, 38, 4, selected ? PINK : GRID);
    if (selected) screen.fillRect(x + 7, 113, 42, 3, CYAN);
    screen.setTextColor(selected ? WHITE : MUTED);
    screen.setCursor(x + 13, 85);
    screen.print(labels[mode - 1]);
    screen.setTextColor(selected ? CYAN : MUTED);
    screen.setCursor(x + 13, 99);
    screen.print(details[mode - 1]);
  }

  bool warning = strstr(floppyStatus, "FAIL") ||
                 strstr(floppyStatus, "Connect") ||
                 strstr(floppyStatus, "Unknown") ||
                 strstr(floppyStatus, " *");
  screen.fillRoundRect(10, 125, 300, 84, 5, PANEL);
  screen.drawFastVLine(10, 133, 67, warning ? AMBER : CYAN);
  screen.setTextColor(warning ? AMBER : CYAN);
  screen.setCursor(19, 133);
  if (warning) screen.print("CHECK DRIVE / MEDIA");
  else if (reading) screen.print("CAPTURE IN PROGRESS");
  else screen.print("DISK LAB // STATUS");
  screen.setTextColor(warning ? AMBER : WHITE);
  drawResult(floppyStatus);

  screen.setTextColor(MUTED);
  screen.setCursor(19, 194);
  if (floppyProgress[0]) screen.print(floppyProgress);
  else screen.print("Choose a mode, then press SELECT");

  // Progress counts completed track sides, including sides with bad sectors.
  if (tracksTotal) {
    screen.fillRect(19, 205, 282 * tracksDone / tracksTotal, 2, CYAN);
  }
  screen.fillRect(0, 216, 320, 24, INK);
  screen.setTextColor(digitalRead(PIN_SD_DETECT) == HIGH ? LIME : AMBER);
  screen.setCursor(12, 217);
  screen.print(digitalRead(PIN_SD_DETECT) == HIGH ? "SD READY" : "NO SD CARD");
  screen.setTextColor(MUTED);
  screen.setCursor(248, 217);
  screen.print(TinyUSBDevice.mounted() ? "USB ONLINE" : "STANDALONE");
  screen.setTextColor(WHITE);
  screen.setCursor(12, 230);
  screen.print("UP/DN MODE  L/R DS  SEL GO  BOOT STOP");

  // Send only finished frames: no visible clear/redraw flash between screens.
  uint16_t row[320];
  uint8_t *pixels = screen.getBuffer();
  tft.startWrite();
  tft.setAddrWindow(0, 0, 320, 240);
  for (uint16_t y = 0; y < 240; y++) {
    for (uint16_t x = 0; x < 320; x++) {
      row[x] = neonPalette[pixels[y * 320 + x]];
    }
    tft.writePixels(row, 320);
  }
  tft.endWrite();
}

void printHelp() {
  Serial.println("Floppsy standalone dumper; 12V connected, hardware WRITE OFF.");
  Serial.println("a=auto IBM IMG, r=track zero, d=IMG 1.44MB, j=IMG 720KB, g=raw SCP (DS1)");
  Serial.println("A/R/D/J/G use DS0. BOOT or !=cancel. No floppy write commands.");
  Serial.println("AUTO: 720/800/880/1440/1680/1760/2880 KiB IBM MFM; other encodings use SCP.");
  Serial.print("x=download last dump; m=IMG map / SCP notes. FILE name size, exact bytes, END.\n\r");
  Serial.println("BAD sectors: zero-filled + .MAP, .BAD.IMG filename. Incomplete: .PART.");
  Serial.print("SCP: 80 cylinders, 2 heads, 25ns, no normalization.\n\r");
  Serial.print("No INDEX: 600ms raw capture; nominal 300 RPM windows, marked non-index-cued.\n\r");
}
