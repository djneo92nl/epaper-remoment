#include "widget_scheduler.h"

#include "ha_config.h"

// Caps the "time off screen" multiplier so a widget that's been hidden for
// hours doesn't drown out the time-of-day weights entirely.
static const uint32_t MAX_AGE_REFRESHES = 8;

WidgetScheduler::WidgetScheduler(Widget **widgets, int count)
    : widgets_(widgets), count_(min(count, MAX_WIDGETS)) {
  for (int i = 0; i < MAX_WIDGETS; i++) {
    enabled_[i] = true;
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    slot_[s] = -1;
    pin_[s] = -1;
  }
}

int WidgetScheduler::indexOf(const String &name) const {
  for (int i = 0; i < count_; i++) {
    if (name == widgets_[i]->name()) {
      return i;
    }
  }
  return -1;
}

bool WidgetScheduler::refresh(const struct tm &now, Widget **slotsOut) {
  refreshNo_++;

  bool eligible[MAX_WIDGETS];
  bool urgent[MAX_WIDGETS];
  bool anyContent = false;
  for (int i = 0; i < count_; i++) {
    if (!enabled_[i]) {
      hasContent_[i] = urgent[i] = eligible[i] = false;
      Serial.printf("[widgets] %-12s switched off\n", widgets_[i]->name());
      continue;
    }
    hasContent_[i] = widgets_[i]->fetch();
    urgent[i] = hasContent_[i] && widgets_[i]->urgent(now);
    eligible[i] = hasContent_[i] && (urgent[i] || widgets_[i]->weight(now) > 0);
    anyContent |= hasContent_[i];
    Serial.printf("[widgets] %-12s %s%s\n", widgets_[i]->name(),
                  hasContent_[i] ? "has content" : "nothing to show",
                  urgent[i] ? ", URGENT" : "");
  }

  int chosen[WIDGET_SLOT_COUNT];
  bool locked[WIDGET_SLOT_COUNT];
  bool taken[MAX_WIDGETS] = {};

  // Pinned slots: the pinned widget, as long as it has something to show
  // (regardless of its weight -- e.g. a pinned calendar shows all day).
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    int p = pin_[s];
    locked[s] = p >= 0 && p < count_ && hasContent_[p] && !taken[p];
    chosen[s] = locked[s] ? p : -1;
    if (locked[s]) {
      taken[p] = true;
    }
  }

  // Keep what's still eligible, but release the longest-shown non-urgent
  // widget once it has had its minimum time -- only one per refresh.
  int release = -1;
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    int cur = slot_[s];
    if (!locked[s] && cur >= 0 && eligible[cur] && !urgent[cur] &&
        shownFor_[s] >= WIDGET_MIN_DWELL_REFRESHES &&
        (release < 0 || shownFor_[s] > shownFor_[release])) {
      release = s;
    }
  }
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    int cur = slot_[s];
    if (!locked[s] && cur >= 0 && eligible[cur] && s != release && !taken[cur]) {
      chosen[s] = cur;
      taken[cur] = true;
    }
  }

  // Urgent widgets not yet on screen: take a free automatic slot, else
  // bump the longest-shown non-urgent one.
  for (int i = 0; i < count_; i++) {
    if (!urgent[i] || taken[i]) {
      continue;
    }
    int target = -1;
    for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
      if (!locked[s] && chosen[s] < 0) {
        target = s;
        break;
      }
    }
    if (target < 0) {
      for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
        if (!locked[s] && !urgent[chosen[s]] &&
            (target < 0 || shownFor_[s] > shownFor_[target])) {
          target = s;
        }
      }
    }
    if (target < 0) {
      break; // every automatic slot already holds an urgent widget
    }
    if (chosen[target] >= 0) {
      taken[chosen[target]] = false;
    }
    chosen[target] = i;
    taken[i] = true;
  }

  // The released widget may only come back into its own slot (and only
  // if nothing else wants it), so it can't end up in both slots or jump
  // to the other one.
  if (release >= 0 && chosen[release] < 0) {
    taken[slot_[release]] = true;
  }

  // Fill what's left by weight x time off screen.
  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    if (chosen[s] >= 0) {
      continue;
    }
    int best = -1;
    uint32_t bestScore = 0;
    for (int i = 0; i < count_; i++) {
      if (!eligible[i] || taken[i]) {
        continue;
      }
      uint32_t age = min(refreshNo_ - lastShown_[i], MAX_AGE_REFRESHES);
      uint32_t score = (uint32_t)widgets_[i]->weight(now) * age;
      if (score > bestScore) {
        best = i;
        bestScore = score;
      }
    }
    // Nothing else wants the slot: let the released widget stay.
    if (best < 0 && s == release) {
      best = slot_[s];
    }
    chosen[s] = best;
    if (best >= 0) {
      taken[best] = true;
    }
  }

  for (int s = 0; s < WIDGET_SLOT_COUNT; s++) {
    shownFor_[s] = (chosen[s] >= 0 && chosen[s] == slot_[s]) ? shownFor_[s] + 1 : 1;
    slot_[s] = chosen[s];
    if (chosen[s] >= 0) {
      lastShown_[chosen[s]] = refreshNo_;
    }
    slotsOut[s] = chosen[s] >= 0 ? widgets_[chosen[s]] : nullptr;
    Serial.printf("[widgets] slot %d: %s%s\n", s,
                  chosen[s] >= 0 ? widgets_[chosen[s]]->name() : "(empty)",
                  locked[s] ? " (pinned)" : "");
  }

  return anyContent;
}
