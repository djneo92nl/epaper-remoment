#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

static const int16_t TODO_LINE_H = 26;

bool TodoWidget::fetch() {
  total_ = haFetchTodoItems(HA_TODO_ENTITY_ID, items_, MAX_ITEMS);
  if (total_ < 0) {
    total_ = 0;
    return false;
  }
  return total_ > 0;
}

int TodoWidget::weight(const struct tm &now) const {
  if (isWeekend(now)) {
    return inHours(now, 9, 18) ? 3 : 1;
  }
  if (inHours(now, 9, 17)) {
    return 0; // work hours
  }
  return inHours(now, 17, 21) ? 3 : 1;
}

void TodoWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "TO DO");

  int fits = (r.y + r.h - y) / TODO_LINE_H;
  int shown = min(min(total_, MAX_ITEMS), fits);
  // Reserve the last line for "+N more" when the list doesn't fit.
  if (shown < total_) {
    shown = max(0, fits - 1);
  }

  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_BLACK);
  for (int i = 0; i < shown; i++) {
    gfx.drawRect(r.x, y, 14, 14, GxEPD_BLACK);
    gfx.setCursor(r.x + 24, y);
    gfx.print(fitText(items_[i], r.w - 24, 2));
    y += TODO_LINE_H;
  }
  if (shown < total_) {
    gfx.setCursor(r.x + 24, y);
    gfx.print(String("+") + (total_ - shown) + " more");
  }
}
