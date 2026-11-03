// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Exercise the same read-only catalog API used by an Arduino image-file reader.
#include "Adafruit_FloppyImage.h"
#include <stdio.h>

static bool readImage(void *context, uint32_t offset, uint8_t *data,
                      size_t length) {
  FILE *file = (FILE *)context;
  return fseek(file, offset, SEEK_SET) == 0 &&
         fread(data, 1, length, file) == length;
}

static bool showEntry(void *, const Adafruit_FloppyImage::Entry &entry) {
  printf("%s\t%s\t", entry.kind, entry.name);
  if (entry.exactBytes) {
    printf("%lu bytes", (unsigned long)entry.bytes);
  } else {
    printf("%u blocks", entry.blocks);
  }
  printf("%s\n\r", entry.locked ? "\tlocked" : "");
  return true;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: image_catalog image-file\n\r");
    return 2;
  }
  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    perror(argv[1]);
    return 2;
  }
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  if (length < 0 || (unsigned long)length > UINT32_MAX) {
    fclose(file);
    return 2;
  }
  Adafruit_FloppyImage image(readImage, file, length);
  bool success = image.identify();
  if (success) {
    printf("%s\t%s\n\r", image.formatName(), image.volumeName());
    success = image.listRoot(showEntry, nullptr);
  }
  if (!success) {
    fprintf(stderr, "%s\n\r", image.error());
  }
  fclose(file);
  return success ? 0 : 1;
}
