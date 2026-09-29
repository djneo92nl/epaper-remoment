#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

// Scale for the usage bar: roughly "everything's on" for a household.
static const float POWER_BAR_MAX_W = 3000;

static const char *POWER_TEMPLATE =
    "{{ {"
    "'ok': has_value('" HA_POWER_IMPORT_ENTITY_ID "'),"
    "'import_w': states('" HA_POWER_IMPORT_ENTITY_ID "')|float(0),"
    "'export_w': states('" HA_POWER_EXPORT_ENTITY_ID "')|float(0),"
    "'today_kwh': states('" HA_POWER_TODAY_ENTITY_ID "')|float(none),"
    "'gas_m3': states('" HA_GAS_TODAY_ENTITY_ID "')|float(none),"
    "'solar_kwh': states('" HA_SOLAR_TODAY_ENTITY_ID "')|float(none),"
    "'returned_kwh': states('" HA_GRID_RETURNED_TODAY_ENTITY_ID "')|float(none),"
    "'battery_in_kwh': states('" HA_BATTERY_CHARGED_TODAY_ENTITY_ID "')|float(none),"
    "'battery_out_kwh': states('" HA_BATTERY_DISCHARGED_TODAY_ENTITY_ID "')|float(none)"
    "} | to_json }}";

bool PowerWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(POWER_TEMPLATE, doc) || !(doc["ok"] | false)) {
    return false;
  }
  importW_ = doc["import_w"] | 0.0f;
  exportW_ = doc["export_w"] | 0.0f;
  todayKwh_ = doc["today_kwh"] | NAN;
  gasM3_ = doc["gas_m3"] | NAN;
  solarKwh_ = doc["solar_kwh"] | NAN;
  returnedKwh_ = doc["returned_kwh"] | NAN;
  batteryInKwh_ = doc["battery_in_kwh"] | NAN;
  batteryOutKwh_ = doc["battery_out_kwh"] | NAN;
  return true;
}

void PowerWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "POWER");

  // The P1 meter reports import and export separately; only one is
  // non-zero at a time.
  bool exporting = exportW_ > importW_;
  float watts = exporting ? exportW_ : importW_;
  y = drawHero(gfx, r.x, y, String(watts, 0), "W",
               exporting ? "returning to grid" : "from grid");

  drawProgressBar(gfx, r.x, y, r.w, 16, watts / POWER_BAR_MAX_W,
                  exporting ? GxEPD_RED : GxEPD_BLACK);
  y += 16 + 18;

  // Today's totals.
  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x, y);
  gfx.print("TODAY");
  y += 8 + 8;
  y = drawStatRow(gfx, r, y, "Solar", fmtNum(solarKwh_, 1) + " kWh", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Grid used", fmtNum(todayKwh_, 1) + " kWh", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Grid returned", fmtNum(returnedKwh_, 1) + " kWh", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Battery in/out",
                  fmtNum(batteryInKwh_, 1) + " / " + fmtNum(batteryOutKwh_, 1) + " kWh", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Gas", fmtNum(gasM3_, 2) + " m3", GxEPD_BLACK);
}
