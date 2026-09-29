#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

static const char *SOLAR_TEMPLATE =
    "{{ {"
    "'now_w': states('" HA_SOLAR_POWER_ENTITY_ID "')|float(0),"
    "'today_kwh': states('" HA_SOLAR_FORECAST_TODAY_ENTITY_ID "')|float(none),"
    "'remaining_kwh': states('" HA_SOLAR_FORECAST_REMAINING_ENTITY_ID "')|float(none),"
    "'tomorrow_kwh': states('" HA_SOLAR_FORECAST_TOMORROW_ENTITY_ID "')|float(none),"
    "'peak': (states('" HA_SOLAR_PEAK_TIME_ENTITY_ID "')|as_datetime|as_local).strftime('%H:%M')"
    " if has_value('" HA_SOLAR_PEAK_TIME_ENTITY_ID "') else ''"
    "} | to_json }}";

bool SolarWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(SOLAR_TEMPLATE, doc)) {
    return false;
  }
  nowW_ = doc["now_w"] | 0.0f;
  todayKwh_ = doc["today_kwh"] | NAN;
  remainingKwh_ = doc["remaining_kwh"] | NAN;
  tomorrowKwh_ = doc["tomorrow_kwh"] | NAN;
  peak_ = doc["peak"] | "";

  // Nothing to show at night: no output now and none expected today.
  bool producing = nowW_ >= 5;
  bool moreToCome = !isnan(remainingKwh_) && remainingKwh_ >= 0.05f;
  return producing || moreToCome;
}

int SolarWidget::weight(const struct tm &now) const { return inHours(now, 10, 16) ? 3 : 1; }

void SolarWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "SOLAR");
  y = drawHero(gfx, r.x, y, String(nowW_, 0), "W", "producing now");

  // Progress through today's forecast: forecast minus what's still to come.
  if (!isnan(todayKwh_) && !isnan(remainingKwh_) && todayKwh_ > 0) {
    float produced = todayKwh_ - remainingKwh_;
    drawProgressBar(gfx, r.x, y, r.w, 16, produced / todayKwh_, GxEPD_RED);
    y += 16 + 6;
    gfx.setTextSize(1);
    gfx.setTextColor(GxEPD_BLACK);
    gfx.setCursor(r.x, y);
    gfx.print(String(produced, 1) + " of " + String(todayKwh_, 1) + " kWh forecast today");
    y += 8 + 18;
  }

  y = drawStatRow(gfx, r, y, "Still to come", fmtNum(remainingKwh_, 1) + " kWh", GxEPD_BLACK);
  if (peak_.length() > 0) {
    y = drawStatRow(gfx, r, y, "Peak at", peak_, GxEPD_BLACK);
  }
  y = drawStatRow(gfx, r, y, "Tomorrow", fmtNum(tomorrowKwh_, 1) + " kWh", GxEPD_BLACK);
}
