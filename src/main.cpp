#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SPI.h>
#include <garnet_web.h>
#include <time.h>

#include "display_config.h"
#include "ha_config.h"
#include "ha_client.h"
#include "secrets.h"
#include "weather_icons.h"
#include "art.h"
#include "schedule.h"
#include "web_ui.h"
#include "widget_scheduler.h"
#include "widgets/widgets.h"

// Tried GxEPD2's bundled GFX fonts (FreeSans family) for a smoother look,
// down to the smallest bundled size (9pt) -- still reported too big even
// there, and there's no smaller "smooth" option in that family. Reverted
// to the default bitmap font, which was already confirmed to look fine at
// these sizes; textSize(1) covers the "smaller" ask for news body text
// directly and predictably instead of guessing another custom point size.

// garnet_web: settings UI, WiFi and the setup access point. The name is
// the UI title, the AP's prefix ("Dashboard-1E08") and the splash title.
static const char *DEVICE_NAME = "Dashboard";

// The web UI's password until it's changed there (System > Password).
#ifndef WEB_PASSWORD
#define WEB_PASSWORD OTA_PASSWORD
#endif

GxEPD2_3C<EPD_DRIVER_CLASS, EPD_DRIVER_MAX_HEIGHT> display(
    EPD_DRIVER_CLASS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static void printCentered(const String &text, int16_t y, uint8_t size) {
  display.setTextSize(size);
  display.setCursor((display.width() - (int16_t)text.length() * 6 * size) / 2, y);
  display.print(text);
}

// Start-up / status screen: a logo, the name, and what the device is
// doing (connecting, WiFi setup, OTA update). One full refresh each.
static void showSplash(const String &status, const String &detail) {
  Serial.printf("[display] splash: %s\n", status.c_str());
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextWrap(false);
    display.cp437(true);

    const int16_t iconSize = 110;
    drawWeatherIcon(display, "partlycloudy", (display.width() - iconSize) / 2, 70, iconSize);

    display.setTextColor(GxEPD_BLACK);
    printCentered(DEVICE_NAME, 205, 6);
    display.fillRect((display.width() - 80) / 2, 268, 80, 4, GxEPD_RED);

    printCentered(status, 305, 2);
    if (detail.length() > 0) {
      printCentered(detail, 335, 2);
    }

    display.setTextSize(1);
    printCentered("github.com/djneo92nl/epaper-remoment", display.height() - 24, 1);
  } while (display.nextPage());
}

// NOTE: partial-window refresh was tried and rejected for this panel. On
// this GxEPD2_750c_Z08-driven DEPG0750RWU790F30HP, setPartialWindow() still
// flickers the full panel border (not just the addressed window) and the
// updated region settles visibly lighter/washed-out than a true full
// refresh -- this matches GxEPD2's own warning that this class's partial
// update trades image quality for less flashing, and confirms it's not
// usable here. Every screen update should be a full refresh; don't
// reintroduce a partial-update path without new evidence it works cleanly.

static void syncTime() {
  Serial.println("[time] NTP sync: start");
  configTzTime(LOCAL_TZ, NTP_SERVER_1, NTP_SERVER_2);

  time_t now = time(nullptr);
  unsigned long start = millis();
  while (now < 8 * 3600 * 2 && millis() - start < 10000) {
    delay(200);
    now = time(nullptr);
  }

  if (now < 8 * 3600 * 2) {
    Serial.println("[time] NTP sync: timed out, continuing without accurate time");
  } else {
    Serial.println("[time] NTP sync: done");
  }
}

// Layout: a fixed header -- date (top-left), weather (top-right) -- over
// two widget slots that rotate between calendar, news, energy, etc. (see
// src/widgets/widgets.h for what shows when).
static const int16_t GRID_MID_X = 400;
static const int16_t GRID_HEADER_BOTTOM_Y = 120;
static const int16_t COL_LEFT_X = 30;
static const int16_t COL_RIGHT_X = 420;

static const Rect SLOT_RECTS[WIDGET_SLOT_COUNT] = {
    {12, GRID_HEADER_BOTTOM_Y + 8, GRID_MID_X - 6 - 12, 336},
    {GRID_MID_X + 12, GRID_HEADER_BOTTOM_Y + 8, 788 - (GRID_MID_X + 12), 336},
};

// Registration order doubles as priority between urgent widgets.
static CalendarWidget calendarWidget;
static RainWidget rainWidget;
static AirWidget airWidget;
static EnergyPriceWidget energyPriceWidget;
static PowerWidget powerWidget;
static BatteryWidget batteryWidget;
static SolarWidget solarWidget;
static HeatPumpWidget heatPumpWidget;
static NewsWidget nosWidget("nos", "NOS NIEUWS", NOS_FEED_URL, [](const struct tm &now) {
  return inHours(now, 6, 9) || inHours(now, 12, 14) || inHours(now, 18, 23) ? 3 : 1;
});
static NewsWidget tweakersWidget("tweakers", "TWEAKERS", TWEAKERS_FEED_URL,
                                 [](const struct tm &now) {
                                   return !isWeekend(now) && inHours(now, 9, 18) ? 3 : 1;
                                 });
static TodoWidget todoWidget;

static Widget *allWidgets[] = {
    &calendarWidget, &batteryWidget,  &rainWidget, &airWidget,      &energyPriceWidget,
    &powerWidget,    &solarWidget,    &heatPumpWidget, &nosWidget, &tweakersWidget,
    &todoWidget,
};
static WidgetScheduler scheduler(allWidgets, sizeof(allWidgets) / sizeof(allWidgets[0]));

static void renderDashboard(const WeatherInfo &weather, Widget **slots,
                            const struct tm &localNow) {
  Serial.println("[display] dashboard refresh: start");

  char dateStr[32];
  strftime(dateStr, sizeof(dateStr), "%a, %b %d", &localNow); // short form: fits the column at 9pt

  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // Adafruit GFX doesn't clip text to any bounding box, and its default
    // auto-wrap breaks at the FULL PANEL width (800px), not our column
    // width -- either one lets long text bleed into the neighboring
    // column. Wrapping is handled manually (see drawWrapped) everywhere
    // it's needed, so disable the automatic kind entirely.
    display.setTextWrap(false);
    // Real CP437 glyph indexes, so toDisplayText()'s accented letters and
    // degree sign come out right (see text_util.h).
    display.cp437(true);
    display.drawRect(4, 4, display.width() - 8, display.height() - 8, GxEPD_BLACK);
    // Vertical divider only below the header, between the widget slots.
    display.drawLine(GRID_MID_X, GRID_HEADER_BOTTOM_Y, GRID_MID_X, display.height() - 10,
                     GxEPD_BLACK);
    display.drawLine(10, GRID_HEADER_BOTTOM_Y, display.width() - 10, GRID_HEADER_BOTTOM_Y,
                     GxEPD_BLACK);

    // Top-left: date
    display.setTextSize(3);
    display.setTextColor(GxEPD_RED);
    display.setCursor(COL_LEFT_X, 55);
    display.print(dateStr);

    // Top-right: weather icon + condition/temperature
    drawWeatherIcon(display, weather.condition, COL_RIGHT_X, 15, 90);
    display.setTextSize(2);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(COL_RIGHT_X + 100, 55);
    if (weather.valid && !isnan(weather.temperature)) {
      display.printf("%s  %.0f%s", weather.condition.c_str(), weather.temperature,
                     weather.temperatureUnit.c_str());
    } else if (weather.valid) {
      display.print(weather.condition);
    } else {
      display.print("Weather unavailable");
    }

    for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
      if (slots[s] != nullptr) {
        slots[s]->render(display, SLOT_RECTS[s], localNow);
      }
    }
  } while (display.nextPage());

  Serial.println("[display] dashboard refresh: done");
}

static void refreshDashboard() {
  time_t now = time(nullptr);
  struct tm localNow;
  localtime_r(&now, &localNow);

  WeatherInfo weather;
  Serial.println("[ha] fetching weather...");
  bool weatherOk = haFetchWeather(weather);

  Serial.println("[widgets] fetching...");
  Widget *slots[WIDGET_SLOT_COUNT];
  bool anyWidget = scheduler.refresh(localNow, slots);

  if (!weatherOk && !anyWidget) {
    Serial.println("[widgets] weather and every widget failed, skipping refresh");
    return;
  }

  renderDashboard(weather, slots, localNow);
  webUiNoteRefreshed(-1);
}

static void showArt(int index) {
  Serial.printf("[art] showing %s\n", ART_CATALOG[index].title);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.cp437(true);
    drawArt(display, index);
  } while (display.nextPage());
  webUiNoteRefreshed(index);
}

// garnet_web picks the hostname from its System group, falling back to
// "<name>-xxxx". Seed it with OTA_HOSTNAME on first boot so the device is
// at OTA_HOSTNAME.local (what the OTA env uploads to); it can still be
// changed in the UI.
static void seedHostname() {
  if (gsNamespaceExists("system")) {
    return;
  }
  Preferences p;
  p.begin("system", false);
  p.putString("hostname", OTA_HOSTNAME);
  p.end();
}

// garnet_web joins the strongest saved network, or opens an open setup AP
// with a captive portal when none connects (retrying the saved ones every
// few minutes). Nothing to show without WiFi, so wait here until it's up.
static void connectWiFi() {
  bool portalShown = false;
  while (gwNetMode() != GwNetMode::WiFi) {
    gwLoop();
    if (!portalShown && gwNetMode() == GwNetMode::Portal) {
      portalShown = true;
      // Same name garnet_web gives the AP: last 2 MAC bytes.
      String ap = String(DEVICE_NAME) + "-" + gwChipId().substring(8);
      Serial.printf("[wifi] no network, setup AP '%s' at http://%s/\n", ap.c_str(),
                    gwNetIP().toString().c_str());
      showSplash("WiFi setup needed", "Join " + ap + ", open " + gwNetIP().toString());
    }
    delay(10);
  }
  Serial.printf("[wifi] connected. IP address: %s, http://%s.local/\n",
                gwNetIP().toString().c_str(), gwHostname().c_str());
}

// Wireless firmware updates (push from `pio run -e esp32dev_ota -t upload`).
// Note: this board's partition table (huge_app.csv) has only one OTA app
// slot, so an update overwrites the currently running app in place --
// there's no second slot to roll back to if a write is interrupted. Keep
// updates to a stable WiFi connection; a failed write needs a USB re-flash
// to recover.
static void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  // garnet_web already runs mDNS under the device's hostname.
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[ota] update starting");
    showSplash("Updating firmware...", "");
  });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] update complete, rebooting"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[ota] progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[ota] error [%u]: ", error);
    switch (error) {
      case OTA_AUTH_ERROR: Serial.println("auth failed"); break;
      case OTA_BEGIN_ERROR: Serial.println("begin failed"); break;
      case OTA_CONNECT_ERROR: Serial.println("connect failed"); break;
      case OTA_RECEIVE_ERROR: Serial.println("receive failed"); break;
      case OTA_END_ERROR: Serial.println("end failed"); break;
      default: Serial.println("unknown error"); break;
    }
  });

  ArduinoOTA.begin();
  Serial.printf("[ota] ready, hostname: %s.local\n", gwHostname().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[boot] e-paper bring-up starting");

  Serial.println("[spi] SPI.begin()");
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);

  Serial.println("[display] GxEPD2 init(): start");
  display.init(115200, true, 2, false);
  Serial.println("[display] GxEPD2 init(): done");

  showSplash("Connecting to WiFi...", "");

  seedHostname();
  webUiBegin(scheduler);
  GwConfig cfg;
  cfg.name = DEVICE_NAME;
  cfg.defaultPassword = WEB_PASSWORD;
  gwBegin(cfg);

  connectWiFi();
  setupOTA();

  // No "connected" screen: the dashboard (or art) follows right away,
  // which saves a full-panel flash.

  syncTime();

  Serial.println("[boot] bring-up complete");

  // The first dashboard/art update happens in loop().
}

// Checks the schedule every SCHEDULE_CHECK_MS, which is also how late a
// refresh can start after its time.
static const unsigned long SCHEDULE_CHECK_MS = 1000UL;
// WiFi down: how long until the next refresh attempt.
static const unsigned long WIFI_RETRY_MS = 20UL * 1000UL;

// Which refresh interval `now` is in. The dashboard refreshes when this
// changes, so on the clock: every 10 minutes means 10:20, 10:30, 10:40...
// (every interval in the web UI divides an hour, and so a day).
static long refreshSlot(const struct tm &now, int minutes) {
  long day = (long)now.tm_year * 366 + now.tm_yday;
  return day * (24 * 60 / minutes) + (now.tm_hour * 60 + now.tm_min) / minutes;
}

void loop() {
  gwLoop();
  ArduinoOTA.handle();

  enum Mode { MODE_NONE, MODE_DASHBOARD, MODE_ART };
  static Mode mode = MODE_NONE;
  static ScheduleOverride manual = OVERRIDE_NONE;
  static long period = -1;
  static unsigned long lastCheck = 0;
  static long lastSlot = -1;
  static unsigned long wifiRetryAt = 0;

  bool refreshAsked = webUiTakeRefreshRequest();
  ScheduleOverride asked = webUiTakeOverrideRequest();
  if (asked != OVERRIDE_NONE) {
    manual = asked;
  }
  bool due = mode == MODE_NONE || refreshAsked || asked != OVERRIDE_NONE ||
             millis() - lastCheck >= SCHEDULE_CHECK_MS;
  if (!due) {
    return;
  }
  lastCheck = millis();

  // Without a valid clock (NTP failed) there's no schedule: just run the
  // dashboard.
  time_t nowEpoch = time(nullptr);
  struct tm now;
  localtime_r(&nowEpoch, &now);
  bool clockValid = nowEpoch > 1700000000;

  // A new scheduled period ends any manual override and, in art mode,
  // brings the next artwork.
  bool newPeriod = false;
  if (clockValid) {
    long p = schedulePeriodId(now);
    if (period >= 0 && p != period) {
      newPeriod = true;
      manual = OVERRIDE_NONE;
    }
    period = p;
  }

  bool wantArt = clockValid && (manual == OVERRIDE_ART ||
                                (manual == OVERRIDE_NONE && !scheduleDashboardTime(now)));

  if (wantArt) {
    if (mode != MODE_ART || newPeriod || refreshAsked || asked == OVERRIDE_ART) {
      showArt(scheduleNextArt());
      mode = MODE_ART;
    }
    return;
  }

  // Without a clock, the same interval counted from boot.
  int minutes = webUiRefreshMinutes();
  long slot = clockValid ? refreshSlot(now, minutes) : (long)(millis() / (minutes * 60000UL));
  if (mode != MODE_DASHBOARD || refreshAsked || slot != lastSlot) {
    if (wifiRetryAt != 0 && (long)(millis() - wifiRetryAt) < 0 && !refreshAsked) {
      return;
    }
    // WiFi down (garnet_web is reconnecting): try again in a bit.
    if (gwNetMode() != GwNetMode::WiFi) {
      Serial.println("[wifi] not connected, skipping dashboard refresh");
      wifiRetryAt = millis() + WIFI_RETRY_MS;
      return;
    }
    wifiRetryAt = 0;
    lastSlot = slot;
    mode = MODE_DASHBOARD;
    refreshDashboard();
  }
}
