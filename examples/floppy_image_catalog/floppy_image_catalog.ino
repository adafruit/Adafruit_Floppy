// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Copy a decoded sector image to the microSD card as DISK.IMG, then open Serial.
// The filename extension does not select the filesystem. Nothing is written.
#include <Adafruit_FloppyImage.h>
#include <Adafruit_TinyUSB.h>
#include <SdFat.h>

const char IMAGE_NAME[] = "DISK.IMG";
const uint8_t SD_CS = 10;  // SPI boards only; set to your card's chip-select pin.
SdFs sd;
FsFile imageFile;

bool readImage(void *context, uint32_t offset, uint8_t *data, size_t length);
bool showEntry(void *context, const Adafruit_FloppyImage::Entry &entry);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Serial.print("Read-only floppy image catalog\n\r");

#if defined(ARDUINO_ADAFRUIT_FLOPPSY_RP2350)
  bool mounted = sd.begin(SdioConfig(PIN_SD_CLK, PIN_SD_CMD_MOSI,
                                    PIN_SD_DAT0_MISO, 3.0));
#else
  bool mounted = sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(12)));
#endif
  if (!mounted) {
    Serial.print("Could not mount the microSD card\n\r");
    return;
  }
  imageFile = sd.open(IMAGE_NAME, O_RDONLY);
  if (!imageFile) {
    Serial.print("Could not open DISK.IMG\n\r");
    return;
  }
  if (imageFile.fileSize() > UINT32_MAX) {
    Serial.print("Image is larger than this reader supports\n\r");
    imageFile.close();
    return;
  }

  Adafruit_FloppyImage image(readImage, &imageFile, imageFile.fileSize());
  if (!image.identify()) {
    Serial.print(image.error());
    Serial.print("\n\r");
    imageFile.close();
    return;
  }
  Serial.print(image.formatName());
  Serial.print("\t");
  Serial.print(image.volumeName());
  Serial.print("\n\rType\tName\tSize\n\r");
  if (!image.listRoot(showEntry, nullptr)) {
    Serial.print(image.error());
    Serial.print("\n\r");
  }
  imageFile.close();
}

void loop() {
  delay(10);
}

bool readImage(void *context, uint32_t offset, uint8_t *data, size_t length) {
  FsFile *file = (FsFile *)context;
  return file->seekSet(offset) && file->read(data, length) == (int)length;
}

bool showEntry(void *, const Adafruit_FloppyImage::Entry &entry) {
  Serial.print(entry.kind);
  Serial.print("\t");
  Serial.print(entry.name);
  Serial.print("\t");
  if (entry.exactBytes) {
    Serial.print(entry.bytes);
    Serial.print(" bytes");
  } else if (!entry.directory) {
    Serial.print(entry.blocks);
    Serial.print(" allocated blocks");
  }
  if (entry.locked) {
    Serial.print("\tlocked");
  }
  Serial.print("\n\r");
  return true;
}
