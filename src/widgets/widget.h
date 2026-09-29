#pragma once

#include <Adafruit_GFX.h>
#include <time.h>

#include "../draw_util.h"

// A widget fills one of the dashboard's two bottom slots. Every refresh
// the scheduler (widget_scheduler.cpp) calls fetch() on each widget, then
// picks what to show from urgent() and weight(). The per-widget rules are
// summarized in widgets.h.
class Widget {
 public:
  virtual ~Widget() {}

  // Stable id (logs, saved settings) and a human-readable label (web UI).
  virtual const char *name() const = 0;
  virtual const char *label() const { return name(); }

  // Pulls fresh data. Returns true if there is something worth showing --
  // a failed fetch or an empty result just means the widget sits out this
  // refresh and another one takes the slot.
  virtual bool fetch() = 0;

  // An urgent widget skips the rotation and takes a slot right away.
  virtual bool urgent(const struct tm &now) const { return false; }

  // How much this widget wants a slot at this time of day. The scheduler
  // multiplies it by how long the widget has been off screen, so higher
  // weights come back sooner. 0 means never (outside of urgent()).
  virtual int weight(const struct tm &now) const = 0;

  virtual void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) = 0;
};

inline bool inHours(const struct tm &t, int fromHour, int toHour) {
  return t.tm_hour >= fromHour && t.tm_hour < toHour;
}

inline bool isWeekend(const struct tm &t) { return t.tm_wday == 0 || t.tm_wday == 6; }

inline int minutesOfDay(const struct tm &t) { return t.tm_hour * 60 + t.tm_min; }
