#pragma once

#include "schedule.h"
#include "widget_scheduler.h"

// Small settings page at http://<OTA_HOSTNAME>.local/ : switch widgets on
// or off, pin a widget to a slot, and trigger a refresh. Settings are kept
// in NVS so they survive a reboot. No authentication -- anyone on the
// local network can change what the panel shows (nothing more).
void webUiBegin(WidgetScheduler &scheduler);

// Call from loop().
void webUiHandle();

// True once after the page asked for a refresh (Save / Refresh now).
bool webUiTakeRefreshRequest();

// OVERRIDE_ART / OVERRIDE_DASHBOARD once after the page asked for one,
// else OVERRIDE_NONE.
ScheduleOverride webUiTakeOverrideRequest();

// Lets the page show what's on the panel and since when. artIndex < 0
// means the dashboard.
void webUiNoteRefreshed(int artIndex);
