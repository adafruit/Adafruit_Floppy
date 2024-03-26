#pragma once
#include "display_state.h"
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#define HAVE_DISPLAY (1)

Adafruit_ST7789 display = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RESET);

enum { SZ = 3 };

static void setCursor(int x, int y) {
  display.setCursor(x * SZ * 6 + 12, y * SZ * 8 + 12);
}

void init_display() {
  display.init(240, 240);
  display.fillScreen(0);
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, 1);
  display.setTextSize(SZ);
}

/*!
  @brief   Convert hue, saturation and value into a packed 16-bit RGB color
           that can be passed to TFT
  @param   H  The Hue ranging from 0 to 359
  @param   S  Saturation, 8-bit value, 0 (min or pure grayscale) to 100
                (max or pure hue)
  @param   V  Value (brightness), 8-bit value, 0 (min / black / off) to
                100 (max or full brightness)
  @return  Packed 16-bit 5-6-5 RGB. Result is linearly but not perceptually
           correct for LEDs. Intended for TFT use only.
*/
// https://gist.github.com/kuathadianto/200148f53616cbd226d993b400214a7f
uint16_t ColorHSV565(int16_t H, uint8_t S = 100, uint8_t V = 100) {
  float C = S * V / 10000.0f;
  float X = C * (1 - abs(fmod(H / 60.0f, 2) - 1));
  float m = (V / 100.0f) - C;
  float Rs, Gs, Bs;

  if (H >= 0 && H < 60) {
    Rs = C;
    Gs = X;
    Bs = 0;
  } else if (H >= 60 && H < 120) {
    Rs = X;
    Gs = C;
    Bs = 0;
  } else if (H >= 120 && H < 180) {
    Rs = 0;
    Gs = C;
    Bs = X;
  } else if (H >= 180 && H < 240) {
    Rs = 0;
    Gs = X;
    Bs = C;
  } else if (H >= 240 && H < 300) {
    Rs = X;
    Gs = 0;
    Bs = C;
  } else {
    Rs = C;
    Gs = 0;
    Bs = X;
  }

  uint8_t red = (Rs + m) * 255;
  uint8_t green = (Gs + m) * 255;
  uint8_t blue = (Bs + m) * 255;
  return display.color565(red, green, blue);
}

void update_display(bool force_refresh) {
  int x = 3;
  int y = 3;

  if (force_refresh) {
    display.fillScreen(0);
  }

  static int phase = 0;
  phase = phase + 53;

  // Top row
  int row = 0;
  setCursor(2, 0);
  for (int i = 0; i < 7; i++) {
    display.setTextColor(ColorHSV565((i * 360 / 7 + phase) % 360), 0);
    display.print("FLOPPSY"[i]);
  }

  // Media row
  row += 2;
  if (force_refresh || new_state.capacity_kib != old_state.capacity_kib) {
    setCursor(0, row);
    Serial.printf("row 2 dirty capacity_kib=%d\n", new_state.capacity_kib);
    if (new_state.capacity_kib) {
      display.setTextColor(ST77XX_WHITE, 0);
      display.printf("%d KiB   ", new_state.capacity_kib);
    } else {
      display.setTextColor(ST77XX_MAGENTA, 0);
      display.printf("NO MEDIA");
    }
  }

  // Head position row
  row += 3;
  printf("new trk=%d old_trk=%d\n", new_state.trk, old_state.trk);
  if (force_refresh || new_state.trk != old_state.trk) {
    if (force_refresh) {
      setCursor(0, row);
      display.setTextColor(ST77XX_WHITE, 0);
      display.print("T:");
    }
    setCursor(2, row);
    if (new_state.trk < 0 || new_state.trk > 99) {
      display.setTextColor(ST77XX_RED, 0);
      display.print("??");
    } else {
      display.setTextColor(ST77XX_GREEN, 0);
      display.printf("%02d", new_state.trk);
    }
  }
  if (force_refresh || new_state.side != old_state.side) {
    display.setTextColor(ST77XX_WHITE, 0);
    if (force_refresh) {
      setCursor(5, row);
      display.print("S:");
    }
    setCursor(7, row);
    display.printf("%d", new_state.side);
  }

  // Dirty row
  row += 2;
  if (force_refresh || new_state.dirty != old_state.dirty) {
    display.setTextColor(ST77XX_MAGENTA, 0);
    setCursor(0, row);
    display.print(new_state.dirty ? "dirty" : "     ");
  }

  // Sense row
  row += 1;
  if (force_refresh || new_state.trk0 != old_state.trk0) {
    setCursor(0, row);
    display.setTextColor(ST77XX_GREEN, 0);
    display.print(new_state.trk0 ? "TRK0" : "    ");
  };

  if (force_refresh || new_state.wp != old_state.wp) {
    setCursor(5, row);
    display.setTextColor(ST77XX_MAGENTA, 0);
    display.print(new_state.wp ? "R/O" : "   ");
  };

  if (force_refresh || new_state.rdy != old_state.rdy) {
    setCursor(9, row);
    display.setTextColor(ST77XX_CYAN, 0);
    display.print(new_state.rdy ? "RDY" : "   ");
  };
}
