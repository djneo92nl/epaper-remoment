#pragma once

#include <Arduino.h>
#include <time.h>

// When the dashboard runs vs. when the panel shows art instead: daily
// on/off times plus whole days off. In art mode nothing is fetched and the
// panel just holds the artwork (e-paper needs no power to keep an image),
// changing to the next artwork at each scheduled switch. The settings are
// filled in (and kept up to date) by web_ui.cpp from its Schedule group.
struct ScheduleSettings {
  bool enabled = true;
  int onMin = 7 * 60 + 30; // minutes since midnight
  int offMin = 18 * 60;
  // Bit per tm_wday (0 = Sunday). Default: Wednesday, Saturday, Sunday.
  uint8_t offDays = (1 << 3) | (1 << 6) | (1 << 0);
};

// Temporary manual choice from the web UI, until the next scheduled switch.
enum ScheduleOverride { OVERRIDE_NONE, OVERRIDE_ART, OVERRIDE_DASHBOARD };

ScheduleSettings &scheduleSettings();

// True while the dashboard should run (ignores overrides).
bool scheduleDashboardTime(const struct tm &now);

// Changes at every scheduled switch (on time, off time, midnight), so the
// caller can tell a new period started.
long schedulePeriodId(const struct tm &now);

// Next artwork in rotation (persisted, so a reboot doesn't repeat one).
int scheduleNextArt();
