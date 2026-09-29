#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

// Draws a simple vector icon for a Home Assistant weather condition string
// (e.g. "sunny", "partlycloudy", "rainy", ...) into a size x size box with
// top-left corner at (x, y). Falls back to a plain circle for unrecognized
// conditions. Takes a plain Adafruit_GFX& so it doesn't need to know the
// specific GxEPD2 driver/template type.
void drawWeatherIcon(Adafruit_GFX &gfx, const String &condition, int16_t x, int16_t y, int16_t size);
