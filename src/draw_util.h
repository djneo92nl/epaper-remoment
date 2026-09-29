#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

struct Rect {
  int16_t x, y, w, h;
};

// Greedy word-wrap of `text` into lines no wider than maxWidth, using
// whatever font/color is currently set on gfx. Stops after maxLines.
// Returns the y position just below the last line drawn.
int16_t drawWrapped(Adafruit_GFX &gfx, const String &text, int16_t x, int16_t y,
                    int16_t maxWidth, int16_t lineHeight, int maxLines);

// Dotted line, the closest this panel gets to a light-gray rule.
void drawDottedHLine(Adafruit_GFX &gfx, int16_t x0, int16_t x1, int16_t y);

// Small uppercase label at the top of a widget slot with a short red
// underline, so it's clear which widget is showing after a rotation.
// Returns the y where the widget's content starts.
int16_t drawCaption(Adafruit_GFX &gfx, const Rect &r, const char *caption);

// Big headline number with a smaller unit after it and an optional line of
// context underneath. Returns the y just below it.
int16_t drawHero(Adafruit_GFX &gfx, int16_t x, int16_t y, const String &value,
                 const String &unit, const String &sub);

// One "Label ........ value" row in text size 2, value right-aligned to the
// slot's edge. Returns the y of the next row.
int16_t drawStatRow(Adafruit_GFX &gfx, const Rect &r, int16_t y, const String &label,
                    const String &value, uint16_t valueColor);

// Outlined horizontal bar, filled left-to-right by `fraction` (0..1).
void drawProgressBar(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h,
                     float fraction, uint16_t fillColor);
