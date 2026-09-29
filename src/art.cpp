#include "art.h"

#include <GxEPD2.h>

#include "text_util.h"

// art/art.bin, linked into flash via board_build.embed_files.
extern const uint8_t ART_BIN[] asm("_binary_art_art_bin_start");

static const uint32_t PLANE_BYTES = (uint32_t)ART_WIDTH / 8 * ART_HEIGHT;

void drawArt(Adafruit_GFX &gfx, int index) {
  const ArtInfo &art = ART_CATALOG[index % ART_COUNT];
  const uint8_t *black = ART_BIN + art.offset;
  const uint8_t *red = black + PLANE_BYTES;

  gfx.fillScreen(GxEPD_WHITE);
  gfx.drawBitmap(0, 0, black, ART_WIDTH, ART_HEIGHT, GxEPD_BLACK);
  gfx.drawBitmap(0, 0, red, ART_WIDTH, ART_HEIGHT, GxEPD_RED);

  String caption = String(art.artist) + " - " + art.title + ", " + art.year;
  caption = fitText(caption, gfx.width() - 20, 2);
  gfx.setTextWrap(false);
  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor((gfx.width() - caption.length() * 12) / 2, ART_HEIGHT + 6);
  gfx.print(caption);
}
