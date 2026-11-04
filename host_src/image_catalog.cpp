// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
// Exercise the same read-only catalog API used by an Arduino image-file reader.
#include "Adafruit_FloppyImage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Selection {
  const char *name;
  Adafruit_FloppyImage::Entry entry;
  bool found;
};

static bool selectEntry(void *context,
                        const Adafruit_FloppyImage::Entry &entry) {
  Selection &selection = *(Selection *)context;
  if (strcmp(selection.name, entry.name)) {
    return true;
  }
  selection.entry = entry;
  selection.found = true;
  return false;
}

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
  if (argc != 2 && argc != 5) {
    fprintf(stderr, "Usage: image_catalog image-file [path offset length]\n\r");
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
    if (argc == 2) {
      success = image.listRoot(showEntry, nullptr);
    } else {
      // Exercise folder traversal and random-access reads through the same
      // public API used by the TFT viewer, including reads across block lists.
      char path[256];
      snprintf(path, sizeof(path), "%s", argv[2]);
      uint32_t directory = 0;
      Selection selected = {};
      for (char *name = strtok(path, "/"); name; name = strtok(nullptr, "/")) {
        selected = {name, {}, false};
        success = image.listDirectory(directory, selectEntry, &selected) &&
                  selected.found;
        if (!success) {
          break;
        }
        directory = selected.entry.id;
      }
      if (success && selected.entry.directory) {
        success = image.listDirectory(directory, showEntry, nullptr);
      } else if (success) {
        Adafruit_FloppyImage::Entry entry;
        success = image.openFile(selected.entry.id, entry);
        uint8_t bytes[4096];
        size_t requested = strtoul(argv[4], nullptr, 0);
        int32_t count = -1;
        if (success && requested <= sizeof(bytes)) {
          count = image.readFile(entry, strtoul(argv[3], nullptr, 0), bytes,
                                 requested);
        }
        success = success && count >= 0;
        if (success) {
          printf("SIZE %lu READ %ld\n\r", (unsigned long)entry.bytes,
                 (long)count);
          for (int32_t i = 0; i < count; i++) {
            printf("%02x", bytes[i]);
          }
          printf("\n\r");
        }
      }
    }
  }
  if (!success) {
    fprintf(stderr, "%s\n\r", image.error());
  }
  fclose(file);
  return success ? 0 : 1;
}
