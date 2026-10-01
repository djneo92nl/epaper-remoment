#include "web_ui.h"

#include "art_catalog.h"

#include <garnet_web.h>
#include <time.h>

// Every hook below (onSave, button actions) runs on the loop task, via
// gwLoop(), so it can touch the scheduler directly. Info getters run on the
// web server's task and only read.

static WidgetScheduler *sched = nullptr;
static bool refreshRequested = false;
static ScheduleOverride overrideRequested = OVERRIDE_NONE;
static time_t lastRefresh = 0;
static int shownArt = -1;

static const char *WIDGETS_GROUP = "widgets";
static const char *SCHEDULE_GROUP = "schedule";

// ---- Panel ---------------------------------------------------------------

static String showingStr() {
  if (lastRefresh == 0) {
    return "-";
  }
  char since[8];
  struct tm t;
  localtime_r(&lastRefresh, &t);
  strftime(since, sizeof(since), "%H:%M", &t);
  return String(shownArt >= 0 ? ART_CATALOG[shownArt].title : "Dashboard") + ", since " + since;
}

static String slotStr(int slot) {
  int i = sched->shownIn(slot);
  return shownArt < 0 && i >= 0 ? String(sched->widget(i)->label()) : String("-");
}
static String leftSlotStr() { return slotStr(0); }
static String rightSlotStr() { return slotStr(1); }

// Refresh every N minutes, on the clock. All divide an hour, so the
// refreshes land on the same minutes every hour.
static const char *const REFRESH_OPTIONS[] = {"5 min", "10 min", "15 min",
                                              "20 min", "30 min", "60 min"};
static const int REFRESH_MINUTES[] = {5, 10, 15, 20, 30, 60};
static const int REFRESH_COUNT = sizeof(REFRESH_MINUTES) / sizeof(REFRESH_MINUTES[0]);
static int refreshMinutes = 15;

static void applyPanel() {
  int i = gsGetInt("panel", "every");
  refreshMinutes = REFRESH_MINUTES[i >= 0 && i < REFRESH_COUNT ? i : 2];
}

static void onPanelSaved(const GsGroup &) { applyPanel(); }

static void refreshNow() { refreshRequested = true; }
static void artNow() { overrideRequested = OVERRIDE_ART; }
static void dashboardNow() { overrideRequested = OVERRIDE_DASHBOARD; }

static const GsField PANEL_FIELDS[] = {
    gsInfo("showing", "Showing", showingStr),
    gsInfo("left", "Left slot", leftSlotStr),
    gsInfo("right", "Right slot", rightSlotStr),
    gsWithHelp(gsSelect("every", "Refresh every", REFRESH_OPTIONS, REFRESH_COUNT, 2),
               "On the clock: 15 min is :00, :15, :30, :45"),
    gsButton("refresh", "Refresh now", refreshNow),
    gsWithHelp(gsButton("art", "Show art now", artNow), "Until the next scheduled switch"),
    gsWithHelp(gsButton("dashboard", "Show dashboard now", dashboardNow),
               "Until the next scheduled switch"),
};
static const GsGroup PANEL_GROUP =
    gsWithOnSave(gsGroup("panel", "Panel", "display", PANEL_FIELDS, "Dashboard"), onPanelSaved);

// ---- Widgets -------------------------------------------------------------

// Built at boot from the scheduler's widget list: a pin Select per slot
// ("Automatic" + every widget), then a toggle per widget keyed by its
// name. Pins are stored as the option index, so reordering allWidgets in
// main.cpp moves a pin to another widget.
static const char *pinOptions[1 + MAX_WIDGETS];
static GsField widgetFields[WIDGET_SLOT_COUNT + MAX_WIDGETS];
static GsGroup widgetsGroup;

static void applyWidgets() {
  for (int i = 0; i < sched->count(); i++) {
    sched->setEnabled(i, gsGetBool(WIDGETS_GROUP, sched->widget(i)->name()));
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    int p = gsGetInt(WIDGETS_GROUP, widgetFields[s].key) - 1;
    sched->setPin(s, p < sched->count() ? p : -1);
  }
}

static void onWidgetsSaved(const GsGroup &) {
  applyWidgets();
  refreshRequested = true;
}

static void buildWidgetsGroup() {
  static const char *const PIN_KEYS[WIDGET_SLOT_COUNT] = {"pin0", "pin1"};
  static const char *const PIN_LABELS[WIDGET_SLOT_COUNT] = {"Left slot", "Right slot"};

  int n = sched->count();
  pinOptions[0] = "Automatic";
  for (int i = 0; i < n; i++) {
    pinOptions[1 + i] = sched->widget(i)->label();
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    widgetFields[s] = gsSelect(PIN_KEYS[s], PIN_LABELS[s], pinOptions, 1 + n, 0);
  }
  widgetFields[WIDGET_SLOT_COUNT - 1] =
      gsWithHelp(widgetFields[WIDGET_SLOT_COUNT - 1],
                 "A pinned widget falls back to automatic when it has nothing to show");
  for (int i = 0; i < n; i++) {
    widgetFields[WIDGET_SLOT_COUNT + i] =
        gsToggle(sched->widget(i)->name(), sched->widget(i)->label(), true);
  }

  widgetsGroup.id = WIDGETS_GROUP;
  widgetsGroup.title = "Widgets";
  widgetsGroup.icon = "sliders";
  widgetsGroup.section = "Dashboard";
  widgetsGroup.fields = widgetFields;
  widgetsGroup.fieldCount = WIDGET_SLOT_COUNT + n;
  widgetsGroup.onSave = onWidgetsSaved;
}

// ---- Schedule ------------------------------------------------------------

// "HH:MM" -> minutes since midnight, -1 if it doesn't parse.
static int parseTime(const String &s) {
  if (s.length() != 5 || s[2] != ':' || !isDigit(s[0]) || !isDigit(s[1]) || !isDigit(s[3]) ||
      !isDigit(s[4])) {
    return -1;
  }
  int h = s.substring(0, 2).toInt();
  int m = s.substring(3).toInt();
  return h < 24 && m < 60 ? h * 60 + m : -1;
}

static bool validTime(const String &s) { return parseTime(s) >= 0; }

// Monday first, as calendars here are. Keys index tm_wday via DAY_WDAY.
static const char *const DAY_KEYS[7] = {"off-mon", "off-tue", "off-wed", "off-thu",
                                        "off-fri", "off-sat", "off-sun"};
static const int DAY_WDAY[7] = {1, 2, 3, 4, 5, 6, 0};

#define DAY_FIELD(i, label, def) gsWithShowIf(gsToggle(DAY_KEYS[i], label, def), "enabled")
#define TIME_FIELD(key, label, def)                                                          \
  gsWithShowIf(gsWithPlaceholder(gsWithValidate(gsText(key, label, def, 5), validTime,        \
                                                "use HH:MM, e.g. 07:30"),                     \
                                 def),                                                        \
               "enabled")

static const GsField SCHEDULE_FIELDS[] = {
    gsWithHelp(gsToggle("enabled", "Use schedule", true),
               "Outside these hours and on days off the panel shows art"),
    TIME_FIELD("from", "Dashboard from", "07:30"),
    TIME_FIELD("until", "Dashboard until", "18:00"),
    DAY_FIELD(0, "Monday off", false),
    DAY_FIELD(1, "Tuesday off", false),
    DAY_FIELD(2, "Wednesday off", true),
    DAY_FIELD(3, "Thursday off", false),
    DAY_FIELD(4, "Friday off", false),
    DAY_FIELD(5, "Saturday off", true),
    DAY_FIELD(6, "Sunday off", true),
};

static void applySchedule() {
  ScheduleSettings &s = scheduleSettings();
  s.enabled = gsGetBool(SCHEDULE_GROUP, "enabled");
  int from = parseTime(gsGetString(SCHEDULE_GROUP, "from"));
  int until = parseTime(gsGetString(SCHEDULE_GROUP, "until"));
  if (from >= 0) {
    s.onMin = from;
  }
  if (until >= 0) {
    s.offMin = until;
  }
  s.offDays = 0;
  for (int i = 0; i < 7; i++) {
    if (gsGetBool(SCHEDULE_GROUP, DAY_KEYS[i])) {
      s.offDays |= 1 << DAY_WDAY[i];
    }
  }
}

static void onScheduleSaved(const GsGroup &) {
  applySchedule();
  refreshRequested = true;
}

static const GsGroup SCHEDULE_GROUP_DEF = gsWithOnSave(
    gsGroup(SCHEDULE_GROUP, "Schedule", "clock", SCHEDULE_FIELDS, "Dashboard"), onScheduleSaved);

// ---- Public --------------------------------------------------------------

void webUiBegin(WidgetScheduler &scheduler) {
  sched = &scheduler;
  buildWidgetsGroup();
  gsRegister(PANEL_GROUP);
  gsRegister(widgetsGroup);
  gsRegister(SCHEDULE_GROUP_DEF);
  applyPanel();
  applyWidgets();
  applySchedule();
}

int webUiRefreshMinutes() { return refreshMinutes; }

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
