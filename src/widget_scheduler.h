#pragma once

#include "widgets/widget.h"

static const int WIDGET_SLOT_COUNT = 2;
static const int MAX_WIDGETS = 16;

// Decides which widgets fill the dashboard's slots on each refresh:
//
//  0. Widgets switched off (web UI) aren't fetched or shown. A slot pinned
//     to a widget (web UI) shows it whenever it has content, overriding
//     everything below; otherwise the slot is automatic.
//  1. Every widget is fetched; one with nothing to show sits out.
//  2. Urgent widgets (meeting about to start, bad air) take an automatic
//     slot right away, in the order they're registered.
//  3. Otherwise a widget keeps its slot for WIDGET_MIN_DWELL_REFRESHES, and
//     at most one slot rotates per refresh so the screen never changes
//     completely at once.
//  4. A freed slot goes to the widget with the highest
//     weight(now) x refreshes-since-last-shown, which rotates through
//     everything while favoring what the time-of-day rules prefer.
class WidgetScheduler {
 public:
  WidgetScheduler(Widget **widgets, int count);

  // Fetches all enabled widgets, then fills slotsOut[0..WIDGET_SLOT_COUNT)
  // (nullptr for a slot with nothing to show). Returns false if no widget
  // had anything to show.
  bool refresh(const struct tm &now, Widget **slotsOut);

  int count() const { return count_; }
  Widget *widget(int i) const { return widgets_[i]; }
  int indexOf(const String &name) const;

  void setEnabled(int i, bool enabled) { enabled_[i] = enabled; }
  bool enabled(int i) const { return enabled_[i]; }
  // -1 = automatic.
  void setPin(int slot, int widgetIndex) { pin_[slot] = widgetIndex; }
  int pin(int slot) const { return pin_[slot]; }

  // State as of the last refresh, for the web UI.
  bool hadContent(int i) const { return hasContent_[i]; }
  int shownIn(int slot) const { return slot_[slot]; }

 private:
  Widget **widgets_;
  int count_;
  uint32_t refreshNo_ = 0;
  uint32_t lastShown_[MAX_WIDGETS] = {};
  bool enabled_[MAX_WIDGETS];
  bool hasContent_[MAX_WIDGETS] = {};
  int pin_[WIDGET_SLOT_COUNT];
  int slot_[WIDGET_SLOT_COUNT];
  int shownFor_[WIDGET_SLOT_COUNT] = {};
};
