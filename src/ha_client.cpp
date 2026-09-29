#include "ha_client.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <utility>

#include "ha_config.h"
#include "secrets.h"
#include "text_util.h"

// Local midnight today -> tomorrow, expressed in UTC (HA's calendar API
// wants ISO8601 timestamps). mktime() interprets the tm as local time
// under the currently configured TZ; tm_isdst = -1 lets it work out DST
// for midnight itself, and building tomorrow via tm_mday + 1 (not + 24h)
// keeps 23/25-hour DST-change days right.
static void todayRangeUtc(char *startOut, char *endOut, size_t bufSize) {
  time_t now = time(nullptr);
  struct tm localNow;
  localtime_r(&now, &localNow);

  struct tm dayStart = localNow;
  dayStart.tm_hour = 0;
  dayStart.tm_min = 0;
  dayStart.tm_sec = 0;
  dayStart.tm_isdst = -1;
  struct tm dayEnd = dayStart;
  dayEnd.tm_mday += 1;
  time_t startEpoch = mktime(&dayStart);
  time_t endEpoch = mktime(&dayEnd);

  struct tm startUtc, endUtc;
  gmtime_r(&startEpoch, &startUtc);
  gmtime_r(&endEpoch, &endUtc);

  strftime(startOut, bufSize, "%Y-%m-%dT%H:%M:%SZ", &startUtc);
  strftime(endOut, bufSize, "%Y-%m-%dT%H:%M:%SZ", &endUtc);
}

// Minutes since local midnight `today` ("YYYY-MM-DD") for an HA timestamp
// like "2026-09-29T09:30:00+02:00". HA returns these in local time, so the
// HH:MM digits can be read directly. Timestamps on an earlier/later date
// clamp to 0/1440, so events spanning midnight fill to the day's edge.
static int minutesIntoToday(const String &dateTime, const char *today) {
  int cmp = strcmp(dateTime.substring(0, 10).c_str(), today);
  if (cmp < 0) {
    return 0;
  }
  if (cmp > 0) {
    return 24 * 60;
  }
  int tPos = dateTime.indexOf('T');
  if (tPos < 0 || (int)dateTime.length() < tPos + 6) {
    return 0;
  }
  return dateTime.substring(tPos + 1, tPos + 3).toInt() * 60 +
         dateTime.substring(tPos + 4, tPos + 6).toInt();
}

// HTTPClient returns negative codes for connection-level failures (TLS
// handshake, socket), which happen now and then with ~10 requests per
// refresh. One retry clears nearly all of them.
static int getWithRetry(HTTPClient &http) {
  int code = http.GET();
  if (code < 0) {
    delay(500);
    code = http.GET();
  }
  return code;
}

static int postWithRetry(HTTPClient &http, const String &body) {
  int code = http.POST(body);
  if (code < 0) {
    delay(500);
    code = http.POST(body);
  }
  return code;
}

bool haFetchWeather(WeatherInfo &out) {
  out = WeatherInfo();

  String url = String(HA_BASE_URL) + "/api/states/" + HA_WEATHER_ENTITY_ID;

  HTTPClient http;
  http.useHTTP10(true); // no chunked encoding, so the stream is plain JSON
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = getWithRetry(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ha] weather fetch failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("[ha] weather JSON parse failed: %s\n", err.c_str());
    return false;
  }

  out.condition = doc["state"] | "";
  if (out.condition.length() == 0 || out.condition == "unavailable" || out.condition == "unknown") {
    return false;
  }
  out.temperature = doc["attributes"]["temperature"] | NAN;

  // HA reports this as "°C"/"°F" (UTF-8 multi-byte degree sign), but the
  // display's default font only covers plain ASCII -- drop non-ASCII
  // bytes so we get a clean "C"/"F" instead of garbled glyphs.
  String rawUnit = doc["attributes"]["temperature_unit"] | "";
  String unit;
  for (size_t i = 0; i < rawUnit.length(); i++) {
    if ((unsigned char)rawUnit[i] < 128) {
      unit += rawUnit[i];
    }
  }
  out.temperatureUnit = unit;

  out.valid = true;
  return true;
}

int haFetchTodayEvents(CalendarEvent *events, int maxEvents) {
  char startStr[25];
  char endStr[25];
  todayRangeUtc(startStr, endStr, sizeof(startStr));

  String url = String(HA_BASE_URL) + "/api/calendars/" + HA_CALENDAR_ENTITY_ID +
               "?start=" + startStr + "&end=" + endStr;

  HTTPClient http;
  http.useHTTP10(true);
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = getWithRetry(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ha] calendar fetch failed, HTTP %d\n", code);
    http.end();
    return -1;
  }

  // Only keep what's drawn: meeting invites carry multi-KB descriptions
  // that would otherwise all land on the heap.
  JsonDocument filter;
  filter[0]["summary"] = true;
  filter[0]["start"] = true;
  filter[0]["end"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    Serial.printf("[ha] calendar JSON parse failed: %s\n", err.c_str());
    return -1;
  }

  char today[11];
  time_t now = time(nullptr);
  struct tm localNow;
  localtime_r(&now, &localNow);
  strftime(today, sizeof(today), "%Y-%m-%d", &localNow);

  JsonArray items = doc.as<JsonArray>();
  int count = 0;
  for (JsonObject item : items) {
    if (count >= maxEvents) {
      Serial.printf("[ha] more than %d events today, truncating\n", maxEvents);
      break;
    }

    events[count].summary = toDisplayText(item["summary"] | "");

    // HA's calendar API nests the timestamp: {"dateTime": "..."} for timed
    // events, {"date": "YYYY-MM-DD"} (no "dateTime") for all-day ones.
    JsonVariant startDateTime = item["start"]["dateTime"];
    JsonVariant endDateTime = item["end"]["dateTime"];
    if (startDateTime.is<const char *>() && endDateTime.is<const char *>()) {
      events[count].allDay = false;
      events[count].startMin = minutesIntoToday(startDateTime.as<String>(), today);
      events[count].endMin = minutesIntoToday(endDateTime.as<String>(), today);
    } else {
      events[count].allDay = true;
    }

    count++;
  }

  // Insertion sort (count is tiny): all-day events first, then by start.
  for (int i = 1; i < count; i++) {
    for (int j = i; j > 0; j--) {
      const CalendarEvent &a = events[j - 1];
      const CalendarEvent &b = events[j];
      bool outOfOrder = (!a.allDay && b.allDay) ||
                        (a.allDay == b.allDay && a.startMin > b.startMin);
      if (!outOfOrder) {
        break;
      }
      std::swap(events[j - 1], events[j]);
    }
  }

  return count;
}

bool haRenderTemplateJson(const char *tmpl, JsonDocument &out) {
  JsonDocument req;
  req["template"] = tmpl;
  String body;
  serializeJson(req, body);

  HTTPClient http;
  http.useHTTP10(true); // no chunked encoding, so the stream is plain JSON
  http.begin(String(HA_BASE_URL) + "/api/template");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = postWithRetry(http, body);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ha] template request failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  DeserializationError err = deserializeJson(out, http.getStream());
  http.end();
  if (err) {
    Serial.printf("[ha] template JSON parse failed: %s\n", err.c_str());
    return false;
  }
  return true;
}

int haFetchTodoItems(const char *entityId, String *items, int maxItems) {
  JsonDocument req;
  req["entity_id"] = entityId;
  req["status"] = "needs_action";
  String body;
  serializeJson(req, body);

  HTTPClient http;
  http.useHTTP10(true);
  http.begin(String(HA_BASE_URL) + "/api/services/todo/get_items?return_response");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = postWithRetry(http, body);
  if (code != HTTP_CODE_OK) {
    Serial.printf("[ha] todo fetch failed, HTTP %d\n", code);
    http.end();
    return -1;
  }

  // Only keep the item titles (the response also carries uids etc.).
  JsonDocument filter;
  filter["service_response"][entityId]["items"][0]["summary"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[ha] todo JSON parse failed: %s\n", err.c_str());
    return -1;
  }

  JsonArray list = doc["service_response"][entityId]["items"].as<JsonArray>();
  int total = 0;
  for (JsonObject item : list) {
    if (total < maxItems) {
      items[total] = toDisplayText(item["summary"] | "");
    }
    total++;
  }
  return total;
}
