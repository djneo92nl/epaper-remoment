#include <GxEPD2.h>
#include <HTTPClient.h>

#include "../text_util.h"
#include "widgets.h"

// Buienradar's raintext API: one "LLL|HH:MM" line per 5 minutes for the
// next 2 hours, LLL being 0-255 on a log scale where
// mm/h = 10^((LLL - 109) / 32).
static const char *BUIENRADAR_URL = "https://gpsgadget.buienradar.nl/data/raintext";

// Buienradar's own thresholds: 77 ~ 0.1 mm/h (drizzle, the lowest it
// reports as rain), 109 = 1 mm/h (light), 141 = 10 mm/h (heavy).
static const uint8_t LEVEL_RAIN = 77;
static const uint8_t LEVEL_LIGHT = 109;
static const uint8_t LEVEL_HEAVY = 141;

static const char *LOCATION_TEMPLATE =
    "{{ {"
    "'lat': state_attr('" HA_HOME_ZONE_ENTITY_ID "', 'latitude') | round(2),"
    "'lon': state_attr('" HA_HOME_ZONE_ENTITY_ID "', 'longitude') | round(2)"
    "} | to_json }}";

static float levelToMmPerHour(uint8_t level) { return powf(10, (level - 109) / 32.0f); }

// The location doesn't change; fetch it once and keep it.
bool RainWidget::fetchLocation() {
  if (lat_.length() > 0) {
    return true;
  }
  JsonDocument doc;
  if (!haRenderTemplateJson(LOCATION_TEMPLATE, doc) || doc["lat"].isNull() ||
      doc["lon"].isNull()) {
    return false;
  }
  lat_ = String(doc["lat"].as<float>(), 2);
  lon_ = String(doc["lon"].as<float>(), 2);
  return true;
}

bool RainWidget::fetch() {
  steps_ = 0;
  if (!fetchLocation()) {
    return false;
  }

  HTTPClient http;
  http.useHTTP10(true);
  http.setUserAgent("epaper-remoment/1.0");
  http.begin(String(BUIENRADAR_URL) + "?lat=" + lat_ + "&lon=" + lon_);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[rain] fetch failed, HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString(); // ~250 bytes
  http.end();

  int pos = 0;
  while (pos < (int)body.length() && steps_ < MAX_STEPS) {
    int eol = body.indexOf('\n', pos);
    if (eol < 0) {
      eol = body.length();
    }
    String line = body.substring(pos, eol);
    line.trim();
    int bar = line.indexOf('|');
    if (bar > 0) {
      level_[steps_] = constrain(line.substring(0, bar).toInt(), 0, 255);
      time_[steps_] = line.substring(bar + 1, bar + 6);
      steps_++;
    }
    pos = eol + 1;
  }

  // Only worth a slot if it's actually going to rain.
  return firstRainStep() >= 0;
}

int RainWidget::firstRainStep() const {
  for (int i = 0; i < steps_; i++) {
    if (level_[i] >= LEVEL_RAIN) {
      return i;
    }
  }
  return -1;
}

bool RainWidget::urgent(const struct tm &now) const {
  int first = firstRainStep();
  return first > 0 && first * 5 <= RAIN_URGENT_LEAD_MINUTES;
}

void RainWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "RAIN NEXT 2 HOURS");

  int first = firstRainStep();
  int peak = first;
  for (int i = first; i < steps_; i++) {
    if (level_[i] > level_[peak]) {
      peak = i;
    }
  }

  if (first == 0) {
    // Raining now: when does it stop (first dry step after the rain)?
    int dry = -1;
    for (int i = 1; i < steps_; i++) {
      if (level_[i] < LEVEL_RAIN) {
        dry = i;
        break;
      }
    }
    y = drawHero(gfx, r.x, y, "Now", "",
                 dry >= 0 ? "raining, dry from " + time_[dry] : "raining for 2+ hours");
  } else {
    y = drawHero(gfx, r.x, y, time_[first], "", "rain starts in " + String(first * 5) + " min");
  }

  // Bar per 5 minutes, height on Buienradar's log scale from "drizzle" up
  // to a cap well past heavy; heavy rain in red. Dotted guides mark the
  // light and heavy thresholds.
  int16_t chartTop = y + 6;
  int16_t chartBottom = r.y + r.h - 44; // room for time labels + summary
  int16_t chartH = chartBottom - chartTop;
  const uint8_t levelMax = 173; // 100 mm/h
  auto yFor = [&](uint8_t level) -> int16_t {
    int clamped = constrain((int)level, (int)LEVEL_RAIN, (int)levelMax);
    return chartBottom - (int32_t)(clamped - LEVEL_RAIN) * chartH / (levelMax - LEVEL_RAIN);
  };

  drawDottedHLine(gfx, r.x, r.x + r.w, yFor(LEVEL_LIGHT));
  drawDottedHLine(gfx, r.x, r.x + r.w, yFor(LEVEL_HEAVY));
  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x + r.w - 6 * 6, yFor(LEVEL_LIGHT) - 10);
  gfx.print("light");
  gfx.setCursor(r.x + r.w - 6 * 6, yFor(LEVEL_HEAVY) - 10);
  gfx.print("heavy");

  int16_t barPitch = r.w / MAX_STEPS;
  for (int i = 0; i < steps_; i++) {
    int16_t x = r.x + i * barPitch;
    if (level_[i] >= LEVEL_RAIN) {
      int16_t top = yFor(level_[i]);
      gfx.fillRect(x, top, barPitch - 3, max<int16_t>(2, chartBottom - top),
                   level_[i] >= LEVEL_HEAVY ? GxEPD_RED : GxEPD_BLACK);
    }
    if (i % 6 == 0) {
      gfx.setCursor(x, chartBottom + 6);
      gfx.print(time_[i]);
    }
  }
  gfx.drawLine(r.x, chartBottom, r.x + MAX_STEPS * barPitch, chartBottom, GxEPD_BLACK);

  gfx.setTextSize(2);
  gfx.setCursor(r.x, r.y + r.h - 16);
  gfx.print(fitText("Max " + String(levelToMmPerHour(level_[peak]), 1) + " mm/h at " + time_[peak],
                    r.w, 2));
}
