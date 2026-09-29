#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

static const char *HEATPUMP_TEMPLATE =
    "{{ {"
    "'ok': has_value('" HA_HEATPUMP_MODE_ENTITY_ID "'),"
    "'mode': states('" HA_HEATPUMP_MODE_ENTITY_ID "'),"
    "'heat_w': states('" HA_HEATPUMP_HEAT_POWER_ENTITY_ID "')|float(0),"
    "'electric_w': states('" HA_HEATPUMP_ELECTRIC_POWER_ENTITY_ID "')|float(none),"
    "'cop': states('" HA_HEATPUMP_COP_ENTITY_ID "')|float(none),"
    "'outside_c': states('" HA_HEATPUMP_OUTSIDE_TEMP_ENTITY_ID "')|float(none),"
    "'water_in_c': states('" HA_HEATPUMP_WATER_IN_ENTITY_ID "')|float(none),"
    "'water_out_c': states('" HA_HEATPUMP_WATER_OUT_ENTITY_ID "')|float(none),"
    "'room_c': states('" HA_HEATPUMP_ROOM_TEMP_ENTITY_ID "')|float(none),"
    "'setpoint_c': states('" HA_HEATPUMP_ROOM_SETPOINT_ENTITY_ID "')|float(none),"
    "'boiler': is_state('" HA_HEATPUMP_BOILER_ENTITY_ID "', 'on')"
    " or is_state('" HA_HEATPUMP_BOILER_FLAME_ENTITY_ID "', 'on'),"
    "'today_kwh': states('" HA_HEATPUMP_ENERGY_TODAY_ENTITY_ID "')|float(none)"
    "} | to_json }}";

static String celsius(float v) { return fmtNum(v, 1) + TXT_DEGREE "C"; }

bool HeatPumpWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(HEATPUMP_TEMPLATE, doc) || !(doc["ok"] | false)) {
    return false;
  }
  mode_ = toDisplayText(doc["mode"] | "");
  heatW_ = doc["heat_w"] | 0.0f;
  electricW_ = doc["electric_w"] | NAN;
  cop_ = doc["cop"] | NAN;
  outsideC_ = doc["outside_c"] | NAN;
  waterInC_ = doc["water_in_c"] | NAN;
  waterOutC_ = doc["water_out_c"] | NAN;
  roomC_ = doc["room_c"] | NAN;
  setpointC_ = doc["setpoint_c"] | NAN;
  boiler_ = doc["boiler"] | false;
  todayKwh_ = doc["today_kwh"] | NAN;
  return true;
}

void HeatPumpWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "HEAT PUMP");

  if (heatW_ > 0) {
    String copText = isnan(cop_) || cop_ <= 0 ? "" : "  COP " + String(cop_, 1);
    y = drawHero(gfx, r.x, y, String(heatW_, 0), "W",
                 fitText(mode_, r.w - copText.length() * 12, 2) + copText);
  } else {
    y = drawHero(gfx, r.x, y, fitText(mode_, r.w, 5), "", "no heat demand");
  }

  if (boiler_) {
    y = drawStatRow(gfx, r, y, "Boiler", "ON", GxEPD_RED);
  }
  y = drawStatRow(gfx, r, y, "Room", celsius(roomC_) + " / " + celsius(setpointC_), GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Outside", celsius(outsideC_), GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Water in/out", fmtNum(waterInC_, 1) + " / " + celsius(waterOutC_),
                  GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Electric now", fmtNum(electricW_, 0) + " W", GxEPD_BLACK);
  y = drawStatRow(gfx, r, y, "Used today", fmtNum(todayKwh_, 1) + " kWh", GxEPD_BLACK);
}
