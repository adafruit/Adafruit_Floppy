# Adafruit Floppy [![Build Status](https://github.com/adafruit/Adafruit_Floppy/workflows/Arduino%20Library%20CI/badge.svg)](https://github.com/adafruit/Adafruit_Floppy/actions)

![Adafruit Floppy](./images/rabbit.png)

This is a helper library to abstract away interfacing with floppy disk drives in a cross-platform and open source library.

Adafruit Floppy is a project to make a flexible, full-stack, open source hardware/software device for reading, archiving, accessing and duplicating floppy disk media. It joins a family of open source hardware and software such as greaseweazle and fluxengine, and will attempt to increase the availability and accessibility of floppy disk controllers by:

1. **porting the greaseweazle / fluxengine firmware to Arduino** so that it is less tied to specific hardware. this is important as, during 2021 we learned that silicon shortages can make specific chips extremely difficult to find - having a cross-platform firmware alleviates dependancies on specific chips.

2. **adding firmware support for the RP2040 chip / pico**. this is an ultra low cost dev board, at $4 each - and can make for an excellent alternative to higher cost atmel/stm chips. (of course, the firmware should be able to run on many chips, but we want to make sure this is one of them!)

3. **adding hardware support for reading apple ii disks**. many flux readers focus on 34-pin disk drives but do not have interfacing for apple disk ii drives. the drives are available and could be used for archiving a vast number of floppies out there! this will require adding an index sensor so we can image disks into 'woz' formats. currently, applesauce hardware and software can do this for apple ii disks - applesauce is amazing and an excellent tool and we recommend it to folks! at this time, it appears to be closed source hardware, firmware and software, so we are not able to integrate their design into an open source design.

4. **adding woz/a2r support to greaseweazle / fluxengine**. once hardware support is in place, we can then add woz/a2r file format support to the open source tools in existence, which will benefit the entire community

5. as 'extra credit' we may look into **analog flux data acquisition methods** for repair of damaged disks.

Any hardware, firmware, or software we write is going to be fully open source under permissive licenses such as MIT, BSD or Unlicense. we will probably sell accessories, assembled PCBs, cables, etc in the Adafruit shop to help get hardware into folks hands but the designs will always be re-createable by others without any licensing agreements, NDAs, or discussion.

https://user-images.githubusercontent.com/1685947/147865571-c9ea1d68-6603-436d-9980-bc5ade148db8.mp4

https://user-images.githubusercontent.com/1685947/147864181-c5885b15-1809-4e54-8680-4cfba3f54faa.mp4

Latest video Jan 1, 2022 - 9pm EDT

Currently we are focusing on high-RAM (> 128KB SRAM) and high speed (> 100MHz) processors, so that we can buffer a full track of flux transitions at once, and not require the use of special peripherals such as timers. (Of course, those are welcome later!)

Tested working on:
   * SAMD51 chipset hardware - Please overclock to 180MHz, select Fastest optimization, and use TinyUSB stack for best performance
   * RP2040 chipset hardware - Please use philhower core, overclock to 200MHz, and select -O3 optimization for best performance
Longer version!

## Frequently Asked/Accused Questions

There's a LOT of preconceptions about floppy disks and how / why we have this library. Here are some answers!

* **How are you connecting a 3.3V logic microcontroller to a 5V Floppy Drive directly WITHOUT a level shifter, won't this destroy the board?**
Floppy drives are powered by 5V, and they use open drain outputs. That means that if the microcontroller pulls the index pin (for example) high to 3.3V, the logic level will be 3V. Is this out of spec? Maybe! But it does seem to work. Of course its always polite to use a level shifter, so if you can please add it to your hardware design. We do recommend a stronger external pullup on the READDATA line, 4.7K or so seems fine, other lines are fine with an internal pullup.
* **Did you know there are USB Floppy Drives for $10 on Amazon?** Yes, we are aware. These are recycled laptop floppy drives with a controller chip that presents a mass storage interface to the sectors on disk. They are great for basic access to 1.44MB IBM PC MFM-formatted diskettes. They will *not* work for GCR formatted diskettes (we know because we tried) and may not work with non-FAT formatted diskettes (we don't have any but the controller chip is very specialized and may freak out). USB floppy drive controllers will not get you flux-level readings, and can't cope with damaged diskettes to read sector data that does not pass CRC to perform data recovery. They are also, of course, no good with 5.25" floppy diskettes.
* **Why do you need a flux level reading of disks???** Flux level readings are essential for data recovery, restoration, archiving of damaged or copy-protected floppies. You can read more about floppy disk preservation efforts here: https://wiki.archiveteam.org/index.php/Rescuing_Floppy_Disks

Adafruit invests time and resources providing this open source code, please support Adafruit and open-source hardware by purchasing products from [Adafruit](https://adafruit.com)!

MIT license, all text above must be included in any redistribution.
