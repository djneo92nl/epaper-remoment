#pragma once

#include "../ha_client.h"
#include "../rss_client.h"
#include "ha_config.h"
#include "widget.h"

// Rules: when each widget shows. Weights are relative; the scheduler
// multiplies them by time off screen, so a weight-3 widget comes back
// about three times as often as a weight-1 one.
//
//   Calendar      urgent when a meeting starts within CALENDAR_LEAD_MINUTES;
//                 never otherwise
//   Indoor air    urgent above AIR_CO2_URGENT_PPM / AIR_PM25_URGENT_UGM3;
//                 otherwise 1
//   Energy price  3 in the morning (6-9) and evening (16-22), when you plan
//                 appliance use; otherwise 1
//   Power         2
//   Home battery  urgent on any MultiPlus/BMS warning or alarm;
//                 3 while charging/discharging, otherwise 1
//   Solar         only while producing / forecast left today;
//                 3 midday (10-16), otherwise 1
//   Heat pump     3 while heating or on boiler assist (the thermostat's
//                 hot-water flag is always on, so it's ignored);
//                 otherwise 1
//   NOS           3 at breakfast (6-9), lunch (12-14), evening (18-23);
//                 otherwise 1
//   Tweakers      3 on weekday work hours (9-18); otherwise 1
//   To-do         only with open items; 3 weekday evenings (17-21) and
//                 weekend daytime (9-18), 0 during weekday work hours,
//                 otherwise 1

class CalendarWidget : public Widget {
 public:
  const char *name() const override { return "calendar"; }
  bool fetch() override;
  bool urgent(const struct tm &now) const override;
  int weight(const struct tm &now) const override { return 0; }
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  int nextMeetingIndex(const struct tm &now) const;
  CalendarEvent events_[MAX_CALENDAR_EVENTS];
  int count_ = 0;
};

class AirWidget : public Widget {
 public:
  const char *name() const override { return "air"; }
  bool fetch() override;
  bool urgent(const struct tm &now) const override;
  int weight(const struct tm &now) const override { return 1; }
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  float co2_ = NAN, pm25_ = NAN, humidity_ = NAN, tempC_ = NAN;
  String quality_;
};

class EnergyPriceWidget : public Widget {
 public:
  const char *name() const override { return "energy-price"; }
  bool fetch() override;
  int weight(const struct tm &now) const override;
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  float now_ = NAN;
  float hourly_[24];
  bool low_ = false;
};

class PowerWidget : public Widget {
 public:
  const char *name() const override { return "power"; }
  bool fetch() override;
  int weight(const struct tm &now) const override { return 2; }
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  float importW_ = 0, exportW_ = 0, todayKwh_ = NAN, gasM3_ = NAN;
};

class BatteryWidget : public Widget {
 public:
  const char *name() const override { return "battery"; }
  bool fetch() override;
  bool urgent(const struct tm &now) const override { return alarmCount_ > 0; }
  int weight(const struct tm &now) const override;
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  static const int MAX_ALARMS = 3;
  float soc_ = NAN, powerW_ = 0, voltage_ = NAN, tempC_ = NAN, timeToGoS_ = NAN,
        targetSoc_ = NAN, minSoc_ = NAN, capacityAh_ = NAN;
  String state_, mode_;
  String alarms_[MAX_ALARMS];
  int alarmCount_ = 0;
};

class SolarWidget : public Widget {
 public:
  const char *name() const override { return "solar"; }
  bool fetch() override;
  int weight(const struct tm &now) const override;
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  float nowW_ = 0, todayKwh_ = NAN, remainingKwh_ = NAN, tomorrowKwh_ = NAN;
  String peak_;
};

class HeatPumpWidget : public Widget {
 public:
  const char *name() const override { return "heat-pump"; }
  bool fetch() override;
  int weight(const struct tm &now) const override { return active() ? 3 : 1; }
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  bool active() const { return heatW_ > 0 || boiler_; }
  String mode_;
  float heatW_ = 0, electricW_ = NAN, cop_ = NAN, outsideC_ = NAN, waterInC_ = NAN,
        waterOutC_ = NAN, roomC_ = NAN, setpointC_ = NAN, todayKwh_ = NAN;
  bool boiler_ = false;
};

class NewsWidget : public Widget {
 public:
  typedef int (*WeightFn)(const struct tm &now);
  NewsWidget(const char *name, const char *caption, const char *url, WeightFn weightFn)
      : name_(name), caption_(caption), url_(url), weightFn_(weightFn) {}

  const char *name() const override { return name_; }
  bool fetch() override;
  int weight(const struct tm &now) const override { return weightFn_(now); }
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  const char *name_;
  const char *caption_;
  const char *url_;
  WeightFn weightFn_;
  RssHeadline headline_;
};

class TodoWidget : public Widget {
 public:
  const char *name() const override { return "todo"; }
  bool fetch() override;
  int weight(const struct tm &now) const override;
  void render(Adafruit_GFX &gfx, const Rect &r, const struct tm &now) override;

 private:
  static const int MAX_ITEMS = 12;
  String items_[MAX_ITEMS];
  int total_ = 0;
};
