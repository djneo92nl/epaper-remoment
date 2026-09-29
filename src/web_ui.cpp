#include "web_ui.h"

#include "art_catalog.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <time.h>

static WebServer server(80);
static Preferences prefs;
static WidgetScheduler *sched = nullptr;
static bool refreshRequested = false;
static ScheduleOverride overrideRequested = OVERRIDE_NONE;
static time_t lastRefresh = 0;
static int shownArt = -1;

static const char *DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static String timeValue(int minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

// "HH:MM" from an <input type=time>; keeps `fallback` if it doesn't parse.
static int parseTime(const String &s, int fallback) {
  int colon = s.indexOf(':');
  if (colon < 1) {
    return fallback;
  }
  int h = s.substring(0, colon).toInt();
  int m = s.substring(colon + 1).toInt();
  return (h >= 0 && h < 24 && m >= 0 && m < 60) ? h * 60 + m : fallback;
}

static const char *SLOT_NAMES[WIDGET_SLOT_COUNT] = {"Left slot", "Right slot"};

// Saved as widget names rather than indexes, so settings survive widgets
// being added or reordered in a firmware update.
static void loadSettings() {
  prefs.begin("dashboard", true);
  String off = "," + prefs.getString("off", "") + ",";
  for (int i = 0; i < sched->count(); i++) {
    sched->setEnabled(i, off.indexOf(String(",") + sched->widget(i)->name() + ",") < 0);
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    sched->setPin(s, sched->indexOf(prefs.getString(("pin" + String(s)).c_str(), "")));
  }
  prefs.end();
}

static void saveSettings() {
  String off;
  for (int i = 0; i < sched->count(); i++) {
    if (!sched->enabled(i)) {
      off += (off.length() ? "," : "") + String(sched->widget(i)->name());
    }
  }
  prefs.begin("dashboard", false);
  prefs.putString("off", off);
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    int p = sched->pin(s);
    prefs.putString(("pin" + String(s)).c_str(), p >= 0 ? sched->widget(p)->name() : "");
  }
  prefs.end();
}

static String statusOf(int i) {
  if (!sched->enabled(i)) {
    return "<span class=muted>off</span>";
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    if (sched->shownIn(s) == i) {
      return String("<b>on screen</b> (") + SLOT_NAMES[s] + ")";
    }
  }
  return sched->hadContent(i) ? "has content" : "<span class=muted>nothing to show</span>";
}

static void handleRoot() {
  String html;
  html.reserve(6000);
  html += F("<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>E-paper dashboard</title><style>"
            "body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;"
            "color:#111;background:#fff}"
            "h1{font-size:1.3em}h2{font-size:1em;margin-top:1.6em}"
            "table{width:100%;border-collapse:collapse}td{padding:8px 4px;border-bottom:1px solid #ddd}"
            "label{display:flex;gap:8px;align-items:center}select{font-size:1em;padding:4px}"
            ".muted{color:#888}.note{color:#555;font-size:.9em}"
            "button{font-size:1em;padding:8px 16px;margin:12px 8px 0 0;border-radius:6px;"
            "border:1px solid #111;background:#111;color:#fff}"
            "button.alt{background:#fff;color:#111}"
            "@media(prefers-color-scheme:dark){body{background:#111;color:#eee}"
            "td{border-color:#333}button{background:#eee;color:#111;border-color:#eee}"
            "button.alt{background:#111;color:#eee}.note{color:#aaa}}"
            "</style></head><body><h1>E-paper dashboard</h1>");

  if (server.hasArg("saved")) {
    html += F("<p class=note>Saved &mdash; the panel is refreshing (takes ~30&nbsp;s).</p>");
  } else if (server.hasArg("refreshing")) {
    html += F("<p class=note>Refreshing &mdash; takes ~30&nbsp;s.</p>");
  }
  if (lastRefresh > 0) {
    char buf[32];
    struct tm t;
    localtime_r(&lastRefresh, &t);
    strftime(buf, sizeof(buf), "%H:%M", &t);
    html += String("<p class=note>Showing ") +
            (shownArt >= 0 ? String("art: ") + ART_CATALOG[shownArt].title : String("the dashboard")) +
            ", since " + buf + "</p>";
  }

  const ScheduleSettings &sch = scheduleSettings();
  html += F("<form method=post action=/save><h2>Schedule</h2>"
            "<p class=note>Outside these hours, and on days off, the panel shows art "
            "and stops fetching.</p><table>");
  html += String("<tr><td colspan=2><label><input type=checkbox name=sched value=1") +
          (sch.enabled ? " checked" : "") + ">Use schedule</label></td></tr>";
  html += "<tr><td>Dashboard from</td><td><input type=time name=on value=" + timeValue(sch.onMin) +
          "> to <input type=time name=off value=" + timeValue(sch.offMin) + "></td></tr>";
  html += F("<tr><td>Days off</td><td>");
  // Monday-first, as calendars here are.
  for (int k = 1; k <= 7; k++) {
    int d = k % 7;
    html += String("<label style='display:inline-flex;margin-right:8px'>"
                   "<input type=checkbox name=offday value=") +
            d + ((sch.offDays & (1 << d)) ? " checked" : "") + ">" + DAY_NAMES[d] + "</label>";
  }
  html += F("</td></tr></table>");

  html += F("<h2>Pinned slots</h2>"
            "<p class=note>A pinned slot always shows that widget while it has "
            "something to show, and falls back to automatic otherwise.</p><table>");
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    html += String("<tr><td>") + SLOT_NAMES[s] + "</td><td><select name=pin" + s + ">";
    html += String("<option value=''") + (sched->pin(s) < 0 ? " selected" : "") +
            ">Automatic</option>";
    for (int i = 0; i < sched->count(); i++) {
      html += String("<option value='") + sched->widget(i)->name() + "'" +
              (sched->pin(s) == i ? " selected" : "") + ">" + sched->widget(i)->label() +
              "</option>";
    }
    html += F("</select></td></tr>");
  }

  html += F("</table><h2>Widgets</h2><table>");
  for (int i = 0; i < sched->count(); i++) {
    html += String("<tr><td><label><input type=checkbox name=on value='") +
            sched->widget(i)->name() + "'" + (sched->enabled(i) ? " checked" : "") + ">" +
            sched->widget(i)->label() + "</label></td><td>" + statusOf(i) + "</td></tr>";
  }
  html += F("</table><button type=submit>Save</button>"
            "<button class=alt formaction=/refresh>Refresh now</button>"
            "<button class=alt formaction=/art>Show art now</button>"
            "<button class=alt formaction=/dashboard>Show dashboard now</button></form>"
            "<p class=note>&ldquo;Show &hellip; now&rdquo; lasts until the next scheduled switch.</p>"
            "</body></html>");

  server.send(200, "text/html", html);
}

static void redirectHome(const char *flag) {
  server.sendHeader("Location", String("/?") + flag);
  server.send(303);
}

static void handleSave() {
  for (int i = 0; i < sched->count(); i++) {
    bool on = false;
    for (int a = 0; a < server.args(); a++) {
      if (server.argName(a) == "on" && server.arg(a) == sched->widget(i)->name()) {
        on = true;
      }
    }
    sched->setEnabled(i, on);
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    sched->setPin(s, sched->indexOf(server.arg("pin" + String(s))));
  }
  saveSettings();

  ScheduleSettings &sch = scheduleSettings();
  sch.enabled = server.hasArg("sched");
  sch.onMin = parseTime(server.arg("on"), sch.onMin);
  sch.offMin = parseTime(server.arg("off"), sch.offMin);
  sch.offDays = 0;
  for (int a = 0; a < server.args(); a++) {
    if (server.argName(a) == "offday") {
      int d = server.arg(a).toInt();
      if (d >= 0 && d < 7) {
        sch.offDays |= 1 << d;
      }
    }
  }
  scheduleSave();
  refreshRequested = true;
  redirectHome("saved");
}

static void handleRefresh() {
  refreshRequested = true;
  redirectHome("refreshing");
}

static void handleArt() {
  overrideRequested = OVERRIDE_ART;
  redirectHome("refreshing");
}

static void handleDashboard() {
  overrideRequested = OVERRIDE_DASHBOARD;
  redirectHome("refreshing");
}

void webUiBegin(WidgetScheduler &scheduler) {
  sched = &scheduler;
  loadSettings();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/refresh", HTTP_POST, handleRefresh);
  server.on("/art", HTTP_POST, handleArt);
  server.on("/dashboard", HTTP_POST, handleDashboard);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  // mDNS itself is already running (ArduinoOTA starts it).
  MDNS.addService("http", "tcp", 80);
  Serial.println("[web] settings page on port 80");
}

void webUiHandle() { server.handleClient(); }

bool webUiTakeRefreshRequest() {
  bool r = refreshRequested;
  refreshRequested = false;
  return r;
}

ScheduleOverride webUiTakeOverrideRequest() {
  ScheduleOverride r = overrideRequested;
  overrideRequested = OVERRIDE_NONE;
  return r;
}

void webUiNoteRefreshed(int artIndex) {
  lastRefresh = time(nullptr);
  shownArt = artIndex;
}
