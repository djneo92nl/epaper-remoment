#pragma once

#include "schedule.h"
#include "widget_scheduler.h"

// The dashboard's settings as garnet_settings groups, shown by garnet_web
// (which also handles WiFi, the setup access point and mDNS):
//   Panel     what's on screen now + Refresh / Show art / Show dashboard
//   Widgets   switch widgets on or off, pin a widget to a slot
//   Schedule  dashboard hours and days off (art mode otherwise)
// Values live in NVS (one namespace per group), so they survive reboots
// and firmware updates.
//
// Registers the groups and applies the stored values to the scheduler and
// the schedule. Call before gwBegin().
void webUiBegin(WidgetScheduler &scheduler);

// True once after the page asked for a refresh (a save / Refresh now).
bool webUiTakeRefreshRequest();

// OVERRIDE_ART / OVERRIDE_DASHBOARD once after the page asked for one,
// else OVERRIDE_NONE.
ScheduleOverride webUiTakeOverrideRequest();

// Lets the page show what's on the panel and since when. artIndex < 0
// means the dashboard.
void webUiNoteRefreshed(int artIndex);
