#include "schedule.h"

#include <Preferences.h>

#include "art_catalog.h"

static ScheduleSettings settings;
static Preferences prefs;

ScheduleSettings &scheduleSettings() { return settings; }

bool scheduleDashboardTime(const struct tm &now) {
  if (!settings.enabled) {
    return true;
  }
  if (settings.offDays & (1 << now.tm_wday)) {
    return false;
  }
  int m = now.tm_hour * 60 + now.tm_min;
  if (settings.onMin < settings.offMin) {
    return m >= settings.onMin && m < settings.offMin;
  }
  // Off time before on time: the dashboard runs across midnight.
  return m >= settings.onMin || m < settings.offMin;
}

long schedulePeriodId(const struct tm &now) {
  int m = now.tm_hour * 60 + now.tm_min;
  int first = min(settings.onMin, settings.offMin);
  int second = max(settings.onMin, settings.offMin);
  int segment = (m >= first) + (m >= second);
  long day = (long)now.tm_year * 366 + now.tm_yday;
  return day * 3 + segment;
}

// Kept in the Schedule group's NVS namespace, next to its fields;
// garnet_settings ignores keys that aren't fields.
int scheduleNextArt() {
  prefs.begin("schedule", false);
  int index = prefs.getInt("art", 0) % ART_COUNT;
  prefs.putInt("art", (index + 1) % ART_COUNT);
  prefs.end();
  return index;
}
