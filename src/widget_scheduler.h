#pragma once

#include "widgets/widget.h"

static const int WIDGET_SLOT_COUNT = 2;
static const int MAX_WIDGETS = 16;

// Decides which widgets fill the dashboard's slots on each refresh:
//
//  1. Every widget is fetched; one with nothing to show sits out.
//  2. Urgent widgets (meeting about to start, bad air) take a slot right
//     away, in the order they're registered.
//  3. Otherwise a widget keeps its slot for WIDGET_MIN_DWELL_REFRESHES, and
//     at most one slot rotates per refresh so the screen never changes
//     completely at once.
//  4. A freed slot goes to the widget with the highest
//     weight(now) x refreshes-since-last-shown, which rotates through
//     everything while favoring what the time-of-day rules prefer.
class WidgetScheduler {
 public:
  WidgetScheduler(Widget **widgets, int count);

  // Fetches all widgets, then fills slotsOut[0..WIDGET_SLOT_COUNT) (nullptr
  // for a slot with nothing to show). Returns false if no widget had
  // anything to show.
  bool refresh(const struct tm &now, Widget **slotsOut);

 private:
  Widget **widgets_;
  int count_;
  uint32_t refreshNo_ = 0;
  uint32_t lastShown_[MAX_WIDGETS] = {};
  int slot_[WIDGET_SLOT_COUNT];
  int shownFor_[WIDGET_SLOT_COUNT] = {};
};
