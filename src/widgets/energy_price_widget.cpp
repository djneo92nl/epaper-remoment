#include <GxEPD2.h>

#include "../text_util.h"
#include "widgets.h"

// Length of the cheapest-window suggestion ("run the dishwasher at ...").
static const int CHEAPEST_WINDOW_HOURS = 3;

// Hourly averages keyed by local clock hour, computed by HA from the
// per-slot timestamps in raw_today. That handles hourly and quarter-hourly
// markets alike, and DST-change days (23 or 25 hours) without guessing
// from the array length: the skipped hour comes back as null, the
// repeated one averages both.
static const char *ENERGY_PRICE_TEMPLATE =
    "{%- set p = '" HA_ENERGY_PRICE_ENTITY_ID "' -%}"
    "{%- set raw = state_attr(p, 'raw_today') or [] -%}"
    "{%- set out = namespace(hours=[]) -%}"
    "{%- for h in range(24) -%}"
    "{%- set ns = namespace(sum=0, n=0) -%}"
    "{%- for e in raw -%}"
    "{%- if (e.start | string | as_datetime | as_local).hour == h -%}"
    "{%- set ns.sum = ns.sum + e.value -%}{%- set ns.n = ns.n + 1 -%}"
    "{%- endif -%}"
    "{%- endfor -%}"
    "{%- set out.hours = out.hours + [(ns.sum / ns.n) | round(4) if ns.n else none] -%}"
    "{%- endfor -%}"
    "{{ {'now': state_attr(p, 'current_price'), 'low': state_attr(p, 'low_price'),"
    " 'hourly': out.hours} | to_json }}";

bool EnergyPriceWidget::fetch() {
  JsonDocument doc;
  if (!haRenderTemplateJson(ENERGY_PRICE_TEMPLATE, doc)) {
    return false;
  }
  now_ = doc["now"] | NAN;
  low_ = doc["low"] | false;

  JsonArray hourly = doc["hourly"].as<JsonArray>();
  int known = 0;
  for (int h = 0; h < 24; h++) {
    hourly_[h] = hourly[h] | NAN;
    known += isnan(hourly_[h]) ? 0 : 1;
  }
  return !isnan(now_) && known >= 20;
}

int EnergyPriceWidget::weight(const struct tm &now) const {
  return inHours(now, 6, 9) || inHours(now, 16, 22) ? 3 : 1;
}

void EnergyPriceWidget::render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) {
  int16_t y = drawCaption(gfx, r, "ENERGY PRICE");
  y = drawHero(gfx, r.x, y, String(now_, 3), "EUR/kWh",
               low_ ? "below today's average" : "above today's average");

  // Cheapest CHEAPEST_WINDOW_HOURS-hour block from the current hour on.
  int bestStart = -1;
  float bestAvg = 0;
  for (int h = now.tm_hour; h + CHEAPEST_WINDOW_HOURS <= 24; h++) {
    float sum = 0;
    for (int k = 0; k < CHEAPEST_WINDOW_HOURS; k++) {
      sum += hourly_[h + k];
    }
    float avg = sum / CHEAPEST_WINDOW_HOURS;
    if (isnan(avg)) {
      continue; // window crosses a DST-skipped hour
    }
    if (bestStart < 0 || avg < bestAvg) {
      bestStart = h;
      bestAvg = avg;
    }
  }

  // Bar chart of today's hourly prices, zero baseline (prices can go
  // negative, so the baseline isn't always the bottom). Past hours
  // outlined, current hour red, the cheapest window underlined in red.
  float maxV = 0.01f, minV = 0;
  for (int h = 0; h < 24; h++) {
    if (!isnan(hourly_[h])) {
      maxV = max(maxV, hourly_[h]);
      minV = min(minV, hourly_[h]);
    }
  }
  int16_t chartTop = y + 10;
  int16_t chartBottom = r.y + r.h - 48; // room for axis labels + summary line
  int16_t chartH = chartBottom - chartTop;
  int16_t zeroY = chartTop + (int16_t)(chartH * maxV / (maxV - minV));
  int16_t barPitch = r.w / 24;

  gfx.setTextSize(1);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor(r.x, chartTop - 10);
  gfx.print(String(maxV, 2));

  for (int h = 0; h < 24; h++) {
    int16_t x = r.x + h * barPitch;
    int16_t w = barPitch - 3;
    if (h % 6 == 0) {
      gfx.setCursor(x, chartBottom + 10);
      gfx.print(h);
    }
    if (isnan(hourly_[h])) {
      continue;
    }
    int16_t valueY = chartTop + (int16_t)(chartH * (maxV - hourly_[h]) / (maxV - minV));
    int16_t top = min(valueY, zeroY);
    int16_t barH = max<int16_t>(1, abs(valueY - zeroY));

    if (h < now.tm_hour) {
      gfx.drawRect(x, top, w, barH, GxEPD_BLACK);
    } else {
      gfx.fillRect(x, top, w, barH, h == now.tm_hour ? GxEPD_RED : GxEPD_BLACK);
    }
    if (bestStart >= 0 && h >= bestStart && h < bestStart + CHEAPEST_WINDOW_HOURS) {
      gfx.fillRect(x, chartBottom + 3, w, 3, GxEPD_RED);
    }
  }
  gfx.drawLine(r.x, zeroY, r.x + 24 * barPitch, zeroY, GxEPD_BLACK);

  if (bestStart >= 0) {
    gfx.setTextSize(2);
    gfx.setTextColor(GxEPD_BLACK);
    gfx.setCursor(r.x, r.y + r.h - 16);
    gfx.print(fitText("Cheapest: " + hhmm(bestStart * 60) + "-" +
                          hhmm((bestStart + CHEAPEST_WINDOW_HOURS) * 60) + " (" +
                          String(bestAvg, 2) + ")",
                      r.w, 2));
  }
}
