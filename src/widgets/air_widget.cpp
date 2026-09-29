#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

static const char *AIR_TEMPLATE =
    "{{ {"
    "'co2': states('" HA_AIR_CO2_ENTITY_ID "')|float(none),"
    "'pm25': states('" HA_AIR_PM25_ENTITY_ID "')|float(none),"
    "'humidity': states('" HA_AIR_HUMIDITY_ENTITY_ID "')|float(none),"
    "'temp_c': states('" HA_AIR_TEMP_ENTITY_ID "')|float(none),"
    "'quality': states('" HA_AIR_QUALITY_ENTITY_ID "')"
    "} | to_json }}";

bool AirWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(AIR_TEMPLATE, doc)) {
    return false;
  }
  co2_ = doc["co2"] | NAN;
  pm25_ = doc["pm25"] | NAN;
  humidity_ = doc["humidity"] | NAN;
  tempC_ = doc["temp_c"] | NAN;
  quality_ = doc["quality"] | "";
  quality_.replace('_', ' '); // "very_poor" -> "very poor"
  return !isnan(co2_);
}

bool AirWidget::urgent(const struct tm &now) const {
  return (!isnan(co2_) && co2_ >= AIR_CO2_URGENT_PPM) ||
         (!isnan(pm25_) && pm25_ >= AIR_PM25_URGENT_UGM3);
}

void AirWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "INDOOR AIR");
  y = drawHero(gfx, r.x, y, String(co2_, 0), "ppm", "CO2");

  if (urgent(now)) {
    gfx.fillRoundRect(r.x, y, r.w, 32, 4, GxEPD_RED);
    gfx.setTextSize(2);
    gfx.setTextColor(GxEPD_WHITE);
    gfx.setCursor(r.x + 10, y + 8);
    gfx.print("Open a window");
    y += 32 + 14;
  }

  bool pmHigh = !isnan(pm25_) && pm25_ >= AIR_PM25_URGENT_UGM3;
  y = drawStatRow(gfx, r, y, "PM2.5", fmtNum(pm25_, 0) + " " TXT_MICRO "g/m3",
                  pmHigh ? GxEPD_RED : GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Humidity", fmtNum(humidity_, 0) + "%", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Temperature", fmtNum(tempC_, 1) + TXT_DEGREE "C", GxEPD_BLACK);
  if (quality_.length() > 0) {
    y = drawStatRow(gfx, r, y, "Quality", quality_, GxEPD_BLACK);
  }
}
