# Adafruit Floppy [![Build Status](https://github.com/adafruit/Adafruit_Floppy/workflows/Arduino%20Library%20CI/badge.svg)](https://github.com/adafruit/Adafruit_Floppy/actions)


This is a helper library to abstract away interfacing with floppy disk drives in a cross-platform and open source library.

Currently we are focusing on high-RAM (> 128KB SRAM) and high speed (> 100MHz) processors, so that we can buffer a full track of flux transitions at once, and not require the use of special peripherals such as timers. (Of course, those are welcome later!)

Tested working on:
   * SAMD51 chipset hardware - Please overclock to 180MHz, select Fastest optimization, and use TinyUSB stack for best performance

Adafruit invests time and resources providing this open source code, please support Adafruit and open-source hardware by purchasing products from Adafruit!

MIT license, all text above must be included in any redistribution