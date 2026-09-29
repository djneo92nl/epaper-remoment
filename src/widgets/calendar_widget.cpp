#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

// Drawn as a Google Calendar-style day timeline: all-day events as bars on
// top, hour gridlines with labels down the left, timed events as blocks
// spanning their start..end, overlapping events cascaded to the right, and
// a red line with a dot marking now. Finished events are drawn outlined
// instead of filled, like Calendar's faded past events; the upcoming
// meeting that triggered the widget is filled red.
static const int16_t CAL_LABEL_W = 38; // "HH:MM" at size 1 plus a gap
static const int16_t CAL_ALLDAY_H = 22;
static const int CAL_MAX_ALLDAY_BARS = 2;
static const int16_t CAL_CASCADE_INDENT = 32;
static const int CAL_MAX_CASCADE_DEPTH = 3;
static const int16_t CAL_MIN_BLOCK_H = 12;
static const int CAL_MIN_SPAN_HOURS = 6;
static const int16_t CAL_BLOCK_RADIUS = 4;
static const int16_t CAL_BLOCK_PAD = 5;

enum BlockStyle { BLOCK_PAST, BLOCK_UPCOMING, BLOCK_NEXT };

static void drawEventBlock(Adafruit_GFX &gfx, const CalendarEvent &ev, int16_t x, int16_t y,
                           int16_t w, int16_t h, BlockStyle style) {
  // White margin so a cascaded block visibly separates from the one below.
  gfx.fillRoundRect(x - 1, y - 1, w + 2, h + 2, CAL_BLOCK_RADIUS, GxEPD_WHITE);
  if (style == BLOCK_PAST) {
    gfx.drawRoundRect(x, y, w, h, CAL_BLOCK_RADIUS, GxEPD_BLACK);
    gfx.setTextColor(GxEPD_BLACK);
  } else {
    gfx.fillRoundRect(x, y, w, h, CAL_BLOCK_RADIUS,
                      style == BLOCK_NEXT ? GxEPD_RED : GxEPD_BLACK);
    gfx.setTextColor(GxEPD_WHITE);
  }

  int16_t textW = w - 2 * CAL_BLOCK_PAD;
  int16_t textX = x + CAL_BLOCK_PAD;
  if (h >= 2 * CAL_BLOCK_PAD + 16 + 4 + 8) {
    // Tall enough for Calendar's two-line layout: title, then time range.
    gfx.setTextSize(2);
    gfx.setCursor(textX, y + CAL_BLOCK_PAD);
    gfx.print(fitText(ev.summary, textW, 2));
    gfx.setTextSize(1);
    gfx.setCursor(textX, y + CAL_BLOCK_PAD + 16 + 4);
    gfx.print(hhmm(ev.startMin) + " - " + hhmm(ev.endMin));
  } else {
    // Short event: single "Title, HH:MM" line, like Calendar does.
    uint8_t size = h >= 16 + 4 ? 2 : 1;
    String suffix = ", " + hhmm(ev.startMin);
    gfx.setTextSize(size);
    gfx.setCursor(textX, y + (h - 8 * size) / 2);
    gfx.print(fitText(ev.summary, textW - suffix.length() * 6 * size, size) + suffix);
  }
}

bool CalendarWidget::fetch() {
  count_ = haFetchTodayEvents(events_, MAX_CALENDAR_EVENTS);
  if (count_ < 0) {
    count_ = 0;
    return false;
  }
  return count_ > 0;
}

int CalendarWidget::nextMeetingIndex(const struct tm &now) const {
  int nowMin = minutesOfDay(now);
  for (int i = 0; i < count_; i++) {
    const CalendarEvent &ev = events_[i];
    if (!ev.allDay && ev.startMin >= nowMin && ev.startMin - nowMin <= CALENDAR_LEAD_MINUTES) {
      return i;
    }
  }
  return -1;
}

bool CalendarWidget::urgent(const struct tm &now) const { return nextMeetingIndex(now) >= 0; }

void CalendarWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int nowMin = minutesOfDay(now);
  int nextIndex = nextMeetingIndex(now);
  int16_t eventsX = r.x + CAL_LABEL_W;
  int16_t eventsRight = r.x + r.w;

  // All-day bars across the top.
  int allDayCount = 0;
  while (allDayCount < count_ && events_[allDayCount].allDay) {
    allDayCount++;
  }
  int16_t y = r.y;
  int bars = min(allDayCount, CAL_MAX_ALLDAY_BARS);
  for (int i = 0; i < bars; i++) {
    String text = events_[i].summary;
    if (i == bars - 1 && allDayCount > bars) {
      text = String("+") + (allDayCount - i) + " all-day events";
    }
    gfx.fillRoundRect(eventsX, y, eventsRight - eventsX, CAL_ALLDAY_H - 2, CAL_BLOCK_RADIUS,
                      GxEPD_BLACK);
    gfx.setTextSize(2);
    gfx.setTextColor(GxEPD_WHITE);
    gfx.setCursor(eventsX + CAL_BLOCK_PAD, y + (CAL_ALLDAY_H - 2 - 16) / 2);
    gfx.print(fitText(text, eventsRight - eventsX - 2 * CAL_BLOCK_PAD, 2));
    y += CAL_ALLDAY_H;
  }

  const CalendarEvent *timed = events_ + allDayCount;
  int timedCount = count_ - allDayCount;

  // Visible window: whole hours covering every timed event and now,
  // padded out to at least CAL_MIN_SPAN_HOURS.
  int firstMin = nowMin;
  int lastMin = nowMin;
  for (int i = 0; i < timedCount; i++) {
    firstMin = min(firstMin, timed[i].startMin);
    lastMin = max(lastMin, timed[i].endMin);
  }
  int startHour = firstMin / 60;
  int endHour = min(24, (lastMin + 59) / 60);
  if (endHour - startHour < CAL_MIN_SPAN_HOURS) {
    endHour = min(24, startHour + CAL_MIN_SPAN_HOURS);
    startHour = max(0, endHour - CAL_MIN_SPAN_HOURS);
  }

  // Leave room above the first gridline for its centered label.
  int16_t top = y + 6;
  int16_t bottom = r.y + r.h;
  int spanMin = (endHour - startHour) * 60;
  auto yFor = [&](int minutes) -> int16_t {
    return top + (int32_t)(minutes - startHour * 60) * (bottom - top) / spanMin;
  };

  // Gridlines and hour labels; thin labels out if hours get too cramped.
  int16_t pxPerHour = (bottom - top) / (endHour - startHour);
  int labelEvery = pxPerHour >= 14 ? 1 : 2;
  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  for (int hour = startHour; hour <= endHour; hour++) {
    int16_t lineY = yFor(hour * 60);
    drawDottedHLine(gfx, eventsX - 4, eventsRight, lineY);
    if ((hour - startHour) % labelEvery == 0) {
      gfx.setCursor(r.x, lineY - 3);
      gfx.print(hhmm(hour * 60));
    }
  }
  gfx.drawLine(eventsX - 1, top, eventsX - 1, bottom, GxEPD_BLACK);

  // Cascade: each event sits one step right of the deepest earlier event
  // it overlaps (events are sorted by start, so earlier = drawn below).
  int depth[MAX_CALENDAR_EVENTS];
  for (int i = 0; i < timedCount; i++) {
    depth[i] = 0;
    for (int j = 0; j < i; j++) {
      if (timed[j].endMin > timed[i].startMin) {
        depth[i] = max(depth[i], depth[j] + 1);
      }
    }
    depth[i] = min(depth[i], CAL_MAX_CASCADE_DEPTH);

    BlockStyle style = timed[i].endMin <= nowMin          ? BLOCK_PAST
                       : allDayCount + i == nextIndex ? BLOCK_NEXT
                                                          : BLOCK_UPCOMING;
    int16_t blockX = eventsX + 1 + depth[i] * CAL_CASCADE_INDENT;
    // Keep short events readable, but inside the slot even near midnight.
    int16_t blockY = min<int16_t>(yFor(timed[i].startMin) + 1, bottom - CAL_MIN_BLOCK_H);
    int16_t blockH = max<int16_t>(CAL_MIN_BLOCK_H, yFor(timed[i].endMin) - blockY - 1);
    drawEventBlock(gfx, timed[i], blockX, blockY, eventsRight - blockX, blockH, style);
  }

  // Now line, on top of everything.
  int16_t nowY = yFor(nowMin);
  gfx.fillRect(eventsX, nowY - 1, eventsRight - eventsX, 2, GxEPD_RED);
  gfx.fillCircle(eventsX, nowY, 4, GxEPD_RED);
}
