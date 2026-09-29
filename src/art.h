#pragma once

#include <Adafruit_GFX.h>

#include "art_catalog.h"

// Draws artwork `index` from the embedded art.bin (see tools/convert_art.py)
// with a caption strip underneath. Call inside the display's page loop.
void drawArt(Adafruit_GFX &gfx, int index);
