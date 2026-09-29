#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct WeatherInfo {
  bool valid = false;
  String condition; // HA state string, e.g. "partlycloudy"
  float temperature = NAN;
  String temperatureUnit;
};

struct CalendarEvent {
  String summary;
  bool allDay = false;
  // Minutes since local midnight today, clamped to [0, 1440] for events
  // that started before today or run past midnight. Unused when allDay.
  int startMin = 0;
  int endMin = 0;
};

bool haFetchWeather(WeatherInfo &out);

// Fills events[0..count) with today's events, most recent HA_TOKEN/URL
// config from ha_config.h / secrets.h, sorted all-day first, then by start
// time. Returns the number of events found, or -1 on request/parse failure.
int haFetchTodayEvents(CalendarEvent *events, int maxEvents);

// Renders a Home Assistant template server-side (POST /api/template) and
// parses its output as JSON into `out`. Lets a widget pull all the values
// it needs -- already combined/formatted by HA -- in a single request,
// instead of one TLS round-trip per entity.
bool haRenderTemplateJson(const char *tmpl, JsonDocument &out);

// Fills items[0..min(total, maxItems)) with the open items of a to-do
// list. Returns the total number of open items, or -1 on failure.
int haFetchTodoItems(const char *entityId, String *items, int maxItems);
