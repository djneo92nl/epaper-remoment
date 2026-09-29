#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

static const int16_t GAUGE_W = 140;
static const int16_t GAUGE_H = 60;
static const int16_t GAUGE_NUB_W = 8;

// `alarms` collects every Victron sensor (MultiPlus + BMS) currently in
// "warning" or "alarm" -- Victron alarm sensors are otherwise "no_alarm".
static const char *BATTERY_TEMPLATE =
    "{%- set p = '" HA_VICTRON_PREFIX "' -%}"
    "{%- set s = p ~ '" HA_VICTRON_SYSTEM "' -%}"
    "{%- set v = p ~ '" HA_VICTRON_VEBUS "' -%}"
    "{%- set b = p ~ '" HA_VICTRON_BATTERY "' -%}"
    "{{ {"
    "'soc': states(s ~ 'dc_battery_soc')|float(none),"
    "'power_w': states(s ~ 'dc_battery_power')|float(0),"
    "'state': states(s ~ 'dc_battery_state'),"
    "'mode': states(v ~ 'inverter_state'),"
    "'voltage': states(s ~ 'dc_battery_voltage')|float(none),"
    "'temp_c': states(b ~ 'temperature')|float(none),"
    "'time_to_go_s': states(b ~ 'time_to_go')|float(none),"
    "'target_soc': states(s ~ 'dynamicess_target_soc')|float(none),"
    "'min_soc': states(s ~ 'control_active_soc_limit')|float(none),"
    "'capacity_ah': states(b ~ 'installed_capacity')|float(none),"
    "'alarms': states.sensor | selectattr('entity_id', 'match', p)"
    " | selectattr('state', 'in', ['warning', 'alarm']) | map(attribute='name') | list"
    "} | to_json }}";

// "bulk" / "external_control" -> "Bulk" / "External control"
static String humanize(String s) {
  s.replace('_', ' ');
  if (s.length() > 0) {
    s.setCharAt(0, toupper(s[0]));
  }
  return s;
}

static String durationText(float seconds) {
  int minutes = (int)(seconds / 60);
  return String(minutes / 60) + "h " + String(minutes % 60) + "m";
}

bool BatteryWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(BATTERY_TEMPLATE, doc)) {
    return false;
  }
  soc_ = doc["soc"] | NAN;
  powerW_ = doc["power_w"] | 0.0f;
  state_ = doc["state"] | "";
  mode_ = humanize(doc["mode"] | "");
  voltage_ = doc["voltage"] | NAN;
  tempC_ = doc["temp_c"] | NAN;
  timeToGoS_ = doc["time_to_go_s"] | NAN;
  targetSoc_ = doc["target_soc"] | NAN;
  minSoc_ = doc["min_soc"] | NAN;
  capacityAh_ = doc["capacity_ah"] | NAN;

  alarmCount_ = 0;
  for (JsonVariant name : doc["alarms"].as<JsonArray>()) {
    if (alarmCount_ >= MAX_ALARMS) {
      break;
    }
    // Names are "<device name> <alarm>", e.g. "MultiPlus 12/1200/50-16
    // (ID: 289) Overload alarm" / "SerialBattery(JKBMS) Cell imbalance" --
    // keep only the part after the device name's closing parenthesis.
    String full = toDisplayText(name.as<String>());
    int cut = full.lastIndexOf(") ");
    alarms_[alarmCount_++] = cut >= 0 ? full.substring(cut + 2) : full;
  }
  return !isnan(soc_);
}

int BatteryWidget::weight(const struct tm &now) const {
  return fabsf(powerW_) >= BATTERY_ACTIVE_W ? 3 : 1;
}

void BatteryWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "HOME BATTERY");

  // Battery-shaped gauge, red when close to the minimum SoC, with a red
  // tick marking that minimum.
  bool low = !isnan(minSoc_) && soc_ <= minSoc_ + 5;
  gfx.drawRect(r.x, y, GAUGE_W, GAUGE_H, GxEPD_BLACK);
  gfx.drawRect(r.x + 1, y + 1, GAUGE_W - 2, GAUGE_H - 2, GxEPD_BLACK);
  gfx.fillRect(r.x + GAUGE_W, y + GAUGE_H / 3, GAUGE_NUB_W, GAUGE_H / 3, GxEPD_BLACK);
  int16_t innerW = GAUGE_W - 8;
  int16_t fillW = (int16_t)(innerW * constrain(soc_, 0.0f, 100.0f) / 100);
  gfx.fillRect(r.x + 4, y + 4, fillW, GAUGE_H - 8, low ? GxEPD_RED : GxEPD_BLACK);
  if (!isnan(minSoc_)) {
    int16_t minX = r.x + 4 + (int16_t)(innerW * minSoc_ / 100);
    gfx.fillRect(minX, y - 4, 2, GAUGE_H + 8, GxEPD_RED);
  }

  gfx.setTextSize(5);
  gfx.setTextColor(low ? GxEPD_RED : GxEPD_BLACK);
  gfx.setCursor(r.x + GAUGE_W + GAUGE_NUB_W + 16, y + (GAUGE_H - 40) / 2);
  gfx.print(String(soc_, 0) + "%");
  y += GAUGE_H + 12;

  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x, y);
  if (fabsf(powerW_) < BATTERY_ACTIVE_W) {
    gfx.print("Idle");
  } else {
    gfx.print(humanize(state_) + " " + String(fabsf(powerW_), 0) + " W");
  }
  y += 16 + 14;

  for (int i = 0; i < alarmCount_; i++) {
    y = drawStatRow(gfx, r, y, "Alarm", fitText(alarms_[i], r.w - 7 * 12, 2), GxEPD_RED);
  }

  if (!isnan(capacityAh_) && !isnan(voltage_)) {
    float storedKwh = soc_ / 100 * capacityAh_ * voltage_ / 1000;
    y = drawStatRow(gfx, r, y, "Stored", String(storedKwh, 1) + " kWh", GxEPD_BLACK);
  }
  if (powerW_ <= -BATTERY_ACTIVE_W && !isnan(timeToGoS_) && timeToGoS_ > 0) {
    y = drawStatRow(gfx, r, y, "Time to go", durationText(timeToGoS_), GxEPD_BLACK);
  }
  if (mode_.length() > 0) {
    y = drawStatRow(gfx, r, y, "MultiPlus", fitText(mode_, r.w - 10 * 12, 2), GxEPD_BLACK);
  }
  if (!isnan(targetSoc_)) {
    y = drawStatRow(gfx, r, y, "ESS target", String(targetSoc_, 0) + "%", GxEPD_BLACK);
  }
  y = drawStatRow(gfx, r, y, "Temperature", fmtNum(tempC_, 1) + TXT_DEGREE "C", GxEPD_BLACK);
}
