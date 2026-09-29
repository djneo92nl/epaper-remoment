#pragma once

#include <Arduino.h>

struct RssHeadline {
  String title;
  String description;
};

// Fetches just the latest (first) item's title + description from the RSS
// feed at `url`. Returns 1 on success, 0 if the feed had no items, or -1
// on request failure.
int rssFetchLatest(const char *url, RssHeadline &out);
