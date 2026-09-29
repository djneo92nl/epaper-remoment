#include <Arduino.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "display_config.h"
#include "ha_config.h"
#include "ha_client.h"
#include "secrets.h"
#include "weather_icons.h"
#include "widget_scheduler.h"
#include "widgets/widgets.h"

// Tried GxEPD2's bundled GFX fonts (FreeSans family) for a smoother look,
// down to the smallest bundled size (9pt) -- still reported too big even
// there, and there's no smaller "smooth" option in that family. Reverted
// to the default bitmap font, which was already confirmed to look fine at
// these sizes; textSize(1) covers the "smaller" ask for news body text
// directly and predictably instead of guessing another custom point size.

// WiFiManager fallback AP credentials (used only when no WiFi is saved yet,
// or saved credentials fail to connect).
static const char *AP_SSID = "EPaper-Setup";
static const char *AP_PASSWORD = "epaper1234"; // min 8 chars
static const uint16_t CONFIG_PORTAL_TIMEOUT_S = 300; // 5 min

GxEPD2_3C<EPD_DRIVER_CLASS, EPD_DRIVER_MAX_HEIGHT> display(
    EPD_DRIVER_CLASS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Region the uptime line is drawn in.
static const int16_t UPTIME_X = 20;
static const int16_t UPTIME_Y = 210;

static void drawTestPattern(const String &statusLine) {
  Serial.println("[display] first full refresh: start");

  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Border, to confirm full-panel SPI addressing is correct.
    display.drawRect(4, 4, display.width() - 8, display.height() - 8, GxEPD_BLACK);

    // Filled rectangle in the panel's red plane.
    display.fillRect(30, 30, 160, 80, GxEPD_RED);

    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(3);
    display.setCursor(30, 150);
    display.print("E-Paper bring-up OK");

    display.setTextSize(2);
    display.setCursor(30, 190);
    display.print(statusLine);

    display.setCursor(UPTIME_X, UPTIME_Y + 20);
    display.printf("Uptime: %lus", millis() / 1000UL);
  } while (display.nextPage());

  Serial.println("[display] first full refresh: done");
}

static void showStatusScreen(const String &line1, const String &line2) {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(4, 4, display.width() - 8, display.height() - 8, GxEPD_BLACK);

    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(3);
    display.setCursor(30, 150);
    display.print(line1);

    display.setTextSize(2);
    display.setCursor(30, 190);
    display.print(line2);

    display.setCursor(UPTIME_X, UPTIME_Y + 20);
    display.printf("Uptime: %lus", millis() / 1000UL);
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
    &calendarWidget, &batteryWidget, &airWidget,     &energyPriceWidget, &powerWidget,
    &solarWidget,    &heatPumpWidget, &nosWidget, &tweakersWidget,    &todoWidget,
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
}

static void configModeCallback(WiFiManager *wm) {
  Serial.println();
  Serial.println("=================================================");
  Serial.println("WiFi not configured yet (or saved credentials failed).");
  Serial.println("Connect a phone/laptop to this WiFi network:");
  Serial.printf("  SSID:     %s\n", wm->getConfigPortalSSID().c_str());
  Serial.printf("  Password: %s\n", AP_PASSWORD);
  Serial.println("Then open a browser to: http://192.168.4.1");
  Serial.println("to enter your home WiFi credentials.");
  Serial.printf("Portal times out after %u seconds if untouched.\n", CONFIG_PORTAL_TIMEOUT_S);
  Serial.println("=================================================");

  showStatusScreen("WiFi setup needed", String("Join: ") + AP_SSID);
}

static void connectWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_S);
  wm.setAPCallback(configModeCallback);

  Serial.println("[wifi] attempting to connect (or starting config portal if no saved credentials)...");
  bool connected = wm.autoConnect(AP_SSID, AP_PASSWORD);

  if (!connected) {
    Serial.println("[wifi] config portal timed out without a successful connection.");
    Serial.println("[wifi] restarting in 3s to try again...");
    showStatusScreen("WiFi setup timed out", "Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.print("[wifi] connected. IP address: ");
  Serial.println(WiFi.localIP());
}

// Wireless firmware updates (push from `pio run -e esp32dev_ota -t upload`).
// Note: this board's partition table (huge_app.csv) has only one OTA app
// slot, so an update overwrites the currently running app in place --
// there's no second slot to roll back to if a write is interrupted. Keep
// updates to a stable WiFi connection; a failed write needs a USB re-flash
// to recover.
static void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[ota] update starting");
    showStatusScreen("OTA update", "Uploading...");
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
  Serial.printf("[ota] ready, hostname: %s.local\n", OTA_HOSTNAME);
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

  drawTestPattern("WiFi: connecting...");

  connectWiFi();
  setupOTA();

  showStatusScreen("WiFi connected", WiFi.localIP().toString());

  syncTime();

  Serial.println("[boot] bring-up complete");

  refreshDashboard();
}

void loop() {
  ArduinoOTA.handle();

  static unsigned long lastRefresh = 0;

  if (millis() - lastRefresh >= HA_REFRESH_INTERVAL_MS) {
    lastRefresh = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] not connected, reconnecting before dashboard refresh...");
      WiFi.reconnect();
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
      }
    }

    refreshDashboard();
  }
}
