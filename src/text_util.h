#pragma once

#include <Arduino.h>

// Display text uses the default Adafruit GFX font with cp437(true), which
// is fixed-width (6px per char at textSize 1) and has CP437's accented
// letters and symbols. Byte strings below are in that encoding.
#define TXT_DEGREE "\xF8" // degree sign
#define TXT_MICRO "\xE6"  // micro sign

// Converts UTF-8 (feeds, calendar titles, HA strings) to the display
// font's CP437: common accented letters map to their CP437 glyph, the rest
// fold to a plain ASCII look-alike or are dropped. Without this, every
// UTF-8 multi-byte sequence renders as 2-3 garbage glyphs.
String toDisplayText(const String &utf8);

String truncated(const String &s, size_t maxLen);

// Truncates s to fit maxWidth px at textSize in the fixed-width font.
String fitText(const String &s, int16_t maxWidth, uint8_t textSize);

// Minutes since midnight -> "HH:MM".
String hhmm(int minutes);

// Formats a float with `decimals`, or "--" when it's NaN (sensor unavailable).
String fmtNum(float v, int decimals);
