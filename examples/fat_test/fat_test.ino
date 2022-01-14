/*
 * Print size, modify date/time, and name for all files in root.
 */

/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!
*********************************************************************/

#include <SPI.h>
#include "SdFat.h"
#include <Adafruit_Floppy.h>

// If using SAMD51, turn on TINYUSB USB stack
#if defined(ADAFRUIT_FEATHER_M4_EXPRESS)
  #define DENSITY_PIN  A0    // IDC 2
  #define INDEX_PIN    A1    // IDC 8
  #define SELECT_PIN   A2    // IDC 12
  #define MOTOR_PIN    A3    // IDC 16
  #define DIR_PIN      A4    // IDC 18
  #define STEP_PIN     A5    // IDC 20
  #define WRDATA_PIN   13    // IDC 22 (not used during read)
  #define WRGATE_PIN   12    // IDC 24 (not used during read)
  #define TRK0_PIN     11    // IDC 26
  #define PROT_PIN     10    // IDC 28
  #define READ_PIN      9    // IDC 30
  #define SIDE_PIN      6    // IDC 32
  #define READY_PIN     5    // IDC 34
#if F_CPU != 180000000L
  #warning "please set CPU speed to 180MHz overclock"
#endif
#elif defined (ARDUINO_ADAFRUIT_FEATHER_RP2040)
  #define DENSITY_PIN  A0    // IDC 2
  #define INDEX_PIN    A1    // IDC 8
  #define SELECT_PIN   A2    // IDC 12
  #define MOTOR_PIN    A3    // IDC 16
  #define DIR_PIN      24    // IDC 18
  #define STEP_PIN     25    // IDC 20
  #define WRDATA_PIN   13    // IDC 22 (not used during read)
  #define WRGATE_PIN   12    // IDC 24 (not used during read)
  #define TRK0_PIN     11    // IDC 26
  #define PROT_PIN     10    // IDC 28
  #define READ_PIN      9    // IDC 30
  #define SIDE_PIN      8    // IDC 32
  #define READY_PIN     7    // IDC 34
#if F_CPU != 200000000L
  #warning "please set CPU speed to 200MHz overclock"
#endif
#elif defined (ARDUINO_RASPBERRY_PI_PICO)
  #define DENSITY_PIN  2     // IDC 2
  #define INDEX_PIN    3     // IDC 8
  #define SELECT_PIN   4     // IDC 12
  #define MOTOR_PIN    5     // IDC 16
  #define DIR_PIN      6     // IDC 18
  #define STEP_PIN     7     // IDC 20
  #define WRDATA_PIN   8     // IDC 22 (not used during read)
  #define WRGATE_PIN   9     // IDC 24 (not used during read)
  #define TRK0_PIN    10     // IDC 26
  #define PROT_PIN    11     // IDC 28
  #define READ_PIN    12     // IDC 30
  #define SIDE_PIN    13     // IDC 32
  #define READY_PIN   14     // IDC 34
#if F_CPU != 200000000L
  #warning "please set CPU speed to 200MHz overclock"
#endif
#else
#error "Please set up pin definitions!"
#endif

Adafruit_Floppy floppy(DENSITY_PIN, INDEX_PIN, SELECT_PIN,
                       MOTOR_PIN, DIR_PIN, STEP_PIN,
                       WRDATA_PIN, WRGATE_PIN, TRK0_PIN,
                       PROT_PIN, READ_PIN, SIDE_PIN, READY_PIN);
Adafruit_MFM_Floppy mfm_floppy(&floppy);

// file system object from SdFat
FatFileSystem fatfs;

FatFile root;
FatFile file;

//------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Wait for USB Serial 
  while (!Serial) {
    SysCall::yield();
  }

  Serial.println("Floppy FAT directory listing demo");
  
  // Init floppy drive - must spin up and find index
  if (! mfm_floppy.begin()) {
    Serial.println("Floppy didn't initialize - check wiring and diskette!");
  }

  // Init file system on the flash
  fatfs.begin(&mfm_floppy);
  
  if (!root.open("/")) {
    Serial.println("open root failed");
  }
  // Open next file in root.
  // Warning, openNext starts at the current directory position
  // so a rewind of the directory may be required.
  while (file.openNext(&root, O_RDONLY)) {
    file.printFileSize(&Serial);
    Serial.write(' ');
    file.printModifyDateTime(&Serial);
    Serial.write(' ');
    file.printName(&Serial);
    if (file.isDir()) {
      // Indicate a directory.
      Serial.write('/');
    }
    Serial.println();
    file.close();
  }
  
  if (root.getError()) {
    Serial.println("openNext failed");
  } else {
    Serial.println("Done!");
  }
}
//------------------------------------------------------------------------------
void loop() {
  Serial.print("Read a file? >");
  String filename;
  do {
    filename = Serial.readStringUntil('\n');
    filename.trim();
  } while (filename.length() == 0);
  
  Serial.print("Reading file name: ");
  Serial.println(filename);

  // Open the file for reading and check that it was successfully opened.
  // The FILE_READ mode will open the file for reading.
  File dataFile = fatfs.open(filename, FILE_READ);
  if (!dataFile) {
    Serial.println("Failed to open data file! Does it exist?");
    return;
  }
  // File was opened, now print out data character by character until at the
  // end of the file.
  Serial.println("Opened file, printing contents below:");
  while (dataFile.available()) {
    // Use the read function to read the next character.
    // You can alternatively use other functions like readUntil, readString, etc.
    // See the fatfs_full_usage example for more details.
    char c = dataFile.read();
    Serial.print(c);
  }
}
