#include "draw_util.h"

#include <GxEPD2.h>

int16_t drawWrapped(Adafruit_GFX &gfx, const String &text, int16_t x, int16_t y,
                    int16_t maxWidth, int16_t lineHeight, int maxLines) {
  int len = text.length();
  int start = 0;
  int lines = 0;

  while (start < len && lines < maxLines) {
    int end = start;
    int lastSpace = -1;

    while (end < len) {
      if (text[end] == ' ') {
        lastSpace = end;
      }
      int16_t bx, by;
      uint16_t bw, bh;
      gfx.getTextBounds(text.substring(start, end + 1), 0, 0, &bx, &by, &bw, &bh);
      if (bw > (uint16_t)maxWidth) {
        break;
      }
      end++;
    }

    int breakAt;
    if (end >= len) {
      breakAt = len;
    } else if (lastSpace > start) {
      breakAt = lastSpace;
    } else {
      breakAt = end; // single word wider than the column: hard-break it
    }

    gfx.setCursor(x, y);
    gfx.print(text.substring(start, breakAt));
    y += lineHeight;
    lines++;

    start = breakAt;
    while (start < len && text[start] == ' ') {
      start++;
    }
  }

  return y;
}

void drawDottedHLine(Adafruit_GFX &gfx, int16_t x0, int16_t x1, int16_t y) {
  for (int16_t x = x0; x <= x1; x += 3) {
    gfx.drawPixel(x, y, GxEPD_BLACK);
  }
}

int16_t drawCaption(Adafruit_GFX &gfx, const Rect &r, const char *caption) {
  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x, r.y);
  gfx.print(caption);
  gfx.fillRect(r.x, r.y + 11, 24, 2, GxEPD_RED);
  return r.y + 22;
}

int16_t drawHero(Adafruit_GFX &gfx, int16_t x, int16_t y, const String &value,
                 const String &unit, const String &sub) {
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setTextSize(5);
  gfx.setCursor(x, y);
  gfx.print(value);
  if (unit.length() > 0) {
    // Bottom-align the size-3 unit with the size-5 number.
    gfx.setTextSize(3);
    gfx.setCursor(x + value.length() * 30 + 8, y + 40 - 24);
    gfx.print(unit);
  }
  y += 40 + 8;

  if (sub.length() > 0) {
    gfx.setTextSize(2);
    gfx.setCursor(x, y);
    gfx.print(sub);
    y += 16 + 8;
  }
  return y + 6;
}

int16_t drawStatRow(Adafruit_GFX &gfx, const Rect &r, int16_t y, const String &label,
                    const String &value, uint16_t valueColor) {
  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x, y);
  gfx.print(label);
  gfx.setTextColor(valueColor);
  gfx.setCursor(r.x + r.w - value.length() * 12, y);
  gfx.print(value);
  drawDottedHLine(gfx, r.x, r.x + r.w, y + 19);
  return y + 26;
}

void drawProgressBar(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                     float fraction, uint16_t fillColor) {
  fraction = constrain(fraction, 0.0f, 1.0f);
  gfx.drawRect(x, y, w, h, GxEPD_BLACK);
  int16_t fillW = (int16_t)((w - 4) * fraction);
  if (fillW > 0) {
    gfx.fillRect(x + 2, y + 2, fillW, h - 4, fillColor);
  }
}
