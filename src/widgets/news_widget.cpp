#include <GxEPD2.h>

#include "widgets.h"

bool NewsWidget::fetch() { return rssFetchLatest(url_, headline_) == 1; }

// The single latest headline: title in red, a snippet of its body in black
// (smaller text), both word-wrapped to fit the slot.
void NewsWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, caption_);

  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_RED);
  y = drawWrapped(gfx, headline_.title, r.x, y, r.w, 22, 4);

  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  int maxLines = (r.y + r.h - (y + 10)) / 14;
  drawWrapped(gfx, headline_.description, r.x, y + 10, r.w, 14, maxLines);
}
