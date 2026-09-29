#include "weather_icons.h"

#include <GxEPD2_3C.h>

static void drawSun(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t r) {
  gfx.fillCircle(cx, cy, r, GxEPD_RED);
  for (int i = 0; i < 8; i++) {
    float a = i * (PI / 4.0);
    int16_t x1 = cx + (int16_t)((r + 3) * cos(a));
    int16_t y1 = cy + (int16_t)((r + 3) * sin(a));
    int16_t x2 = cx + (int16_t)((r + 10) * cos(a));
    int16_t y2 = cy + (int16_t)((r + 10) * sin(a));
    gfx.drawLine(x1, y1, x2, y2, GxEPD_RED);
  }
}

static void drawMoon(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t r) {
  gfx.fillCircle(cx, cy, r, GxEPD_BLACK);
  gfx.fillCircle(cx + r / 2, cy - r / 3, r, GxEPD_WHITE);
  gfx.fillCircle(cx + (int16_t)(r * 1.6), cy - (int16_t)(r * 1.3), 2, GxEPD_RED);
}

// Where a cloud's silhouette actually sits, so rain/snow/lightning below it
// can anchor to real edges instead of guessed offsets.
struct CloudBounds {
  int16_t bottom;
  int16_t left;
  int16_t right;
};

// Fills the union of the cloud's rounded-rect body + two overlapping
// "bumps" in a single color, with no per-component outline -- drawing this
// at two sizes (see drawCloud) is what gives the cloud one clean seamless
// outline instead of each component's edge showing separately.
static CloudBounds fillCloudShape(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t cloudSize,
                                   uint16_t color) {
  int16_t bodyW = (int16_t)(cloudSize * 0.9);
  int16_t bodyH = (int16_t)(cloudSize * 0.38);
  int16_t bodyX = cx - bodyW / 2;
  int16_t bodyY = cy - bodyH / 3;
  int16_t bodyR = bodyH / 2;

  int16_t bigR = (int16_t)(cloudSize * 0.30);
  int16_t bigCx = cx - (int16_t)(cloudSize * 0.05);
  int16_t bigCy = bodyY + bigR / 3;

  int16_t smallR = (int16_t)(cloudSize * 0.20);
  int16_t smallCx = bodyX + bodyW - smallR - (int16_t)(cloudSize * 0.08);
  int16_t smallCy = bodyY + smallR / 3;

  gfx.fillRoundRect(bodyX, bodyY, bodyW, bodyH, bodyR, color);
  gfx.fillCircle(bigCx, bigCy, bigR, color);
  gfx.fillCircle(smallCx, smallCy, smallR, color);

  CloudBounds b;
  b.bottom = bodyY + bodyH;
  b.left = bodyX + bodyR / 2;
  b.right = bodyX + bodyW - bodyR / 2;
  return b;
}

// Draws the cloud twice at the same center: full size in black, then a
// slightly smaller version in white on top. What's left is one uniform
// black outline around the whole silhouette, with no seams between the
// body/bump components -- an outline of the union shape, not of each part.
static CloudBounds drawCloud(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t cloudSize) {
  int16_t stroke = cloudSize / 22;
  if (stroke < 2) {
    stroke = 2;
  }

  CloudBounds outer = fillCloudShape(gfx, cx, cy, cloudSize, GxEPD_BLACK);
  fillCloudShape(gfx, cx, cy, cloudSize - 2 * stroke - 2, GxEPD_WHITE);

  return outer;
}

static void drawRainLines(Adafruit_GFX &gfx, const CloudBounds &cb, int count) {
  int16_t span = cb.right - cb.left;
  int16_t spacing = span / (count + 1);
  for (int i = 0; i < count; i++) {
    int16_t lx = cb.left + spacing * (i + 1);
    int16_t topY = cb.bottom + 4;
    gfx.drawLine(lx, topY, lx - 5, topY + 14, GxEPD_BLACK);
  }
}

static void drawSnowFlakes(Adafruit_GFX &gfx, const CloudBounds &cb, int count) {
  int16_t span = cb.right - cb.left;
  int16_t spacing = span / (count + 1);
  for (int i = 0; i < count; i++) {
    int16_t fx = cb.left + spacing * (i + 1);
    int16_t fy = cb.bottom + 12;
    gfx.drawLine(fx - 5, fy, fx + 5, fy, GxEPD_BLACK);
    gfx.drawLine(fx, fy - 5, fx, fy + 5, GxEPD_BLACK);
    gfx.drawLine(fx - 4, fy - 4, fx + 4, fy + 4, GxEPD_BLACK);
    gfx.drawLine(fx - 4, fy + 4, fx + 4, fy - 4, GxEPD_BLACK);
  }
}

static void drawLightningBolt(Adafruit_GFX &gfx, const CloudBounds &cb) {
  int16_t x0 = (cb.left + cb.right) / 2 + 2, y0 = cb.bottom;
  gfx.fillTriangle(x0, y0, x0 - 8, y0 + 14, x0, y0 + 10, GxEPD_RED);
  gfx.fillTriangle(x0, y0 + 10, x0 + 8, y0 + 10, x0 - 4, y0 + 24, GxEPD_RED);
}

static void drawFogLines(Adafruit_GFX &gfx, const CloudBounds &cb) {
  for (int i = 0; i < 3; i++) {
    int16_t ly = cb.bottom + 6 + i * 6;
    gfx.drawLine(cb.left, ly, cb.right, ly, GxEPD_BLACK);
  }
}

static void drawWindLines(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t r) {
  for (int i = 0; i < 3; i++) {
    int16_t ly = cy - r + i * r;
    int16_t len = r * 2 - i * (r / 2);
    gfx.drawLine(cx - len / 2, ly, cx + len / 2, ly, GxEPD_BLACK);
    gfx.drawLine(cx + len / 2, ly, cx + len / 2 - 6, ly - 4, GxEPD_BLACK);
  }
}

void drawWeatherIcon(Adafruit_GFX &gfx, const String &condition, int16_t x, int16_t y, int16_t size) {
  int16_t cx = x + size / 2;
  int16_t cy = y + size / 2;
  int16_t r = size / 6; // base radius used by non-cloud helpers (sun, wind)

  if (condition == "sunny") {
    drawSun(gfx, cx, cy, size / 4);
  } else if (condition == "clear-night") {
    drawMoon(gfx, cx, cy, size / 5);
  } else if (condition == "partlycloudy") {
    drawSun(gfx, cx - size / 6, cy - size / 8, size / 5);
    drawCloud(gfx, cx + size / 10, cy + size / 8, (int16_t)(size * 0.75));
  } else if (condition == "cloudy") {
    drawCloud(gfx, cx, cy, size);
  } else if (condition == "fog") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawFogLines(gfx, cb);
  } else if (condition == "windy" || condition == "windy-variant") {
    drawWindLines(gfx, cx, cy, r);
  } else if (condition == "rainy") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawRainLines(gfx, cb, 3);
  } else if (condition == "pouring") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawRainLines(gfx, cb, 5);
  } else if (condition == "lightning") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawLightningBolt(gfx, cb);
  } else if (condition == "lightning-rainy") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawLightningBolt(gfx, cb);
    drawRainLines(gfx, cb, 2);
  } else if (condition == "hail") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawSnowFlakes(gfx, cb, 3);
  } else if (condition == "snowy") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawSnowFlakes(gfx, cb, 4);
  } else if (condition == "snowy-rainy") {
    CloudBounds cb = drawCloud(gfx, cx, cy - size / 8, (int16_t)(size * 0.85));
    drawSnowFlakes(gfx, cb, 2);
    drawRainLines(gfx, cb, 2);
  } else if (condition == "exceptional") {
    gfx.drawTriangle(cx, cy - size / 3, cx - size / 3, cy + size / 3, cx + size / 3, cy + size / 3,
                      GxEPD_RED);
    gfx.setTextColor(GxEPD_RED);
    gfx.setTextSize(2);
    gfx.setCursor(cx - 4, cy - 4);
    gfx.print("!");
  } else {
    // Unknown condition: fall back to a plain outlined circle so a new or
    // unmapped HA condition string still renders *something* rather than
    // nothing.
    gfx.drawCircle(cx, cy, size / 3, GxEPD_BLACK);
  }
}
