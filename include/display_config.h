#pragma once

// ---------------------------------------------------------------------------
// Board: clone "e-Paper ESP32 Driver Board Rev 3"
// This pin mapping is confirmed to match Waveshare's official "e-Paper
// ESP32 Driver Board" reference design, which this clone copies. It also
// matches the pinout used in this project's earlier (non-working) ESPHome
// attempt, so the pin mapping was not the cause of that failure.
//
// Note these are NOT the ESP32's default VSPI pins (18/23) -- this board
// reroutes SCK/MOSI to 13/14.
// ---------------------------------------------------------------------------
#define EPD_SCK   13
#define EPD_MISO  -1   // not used by the panel (write-only SPI)
#define EPD_MOSI  14
#define EPD_CS    15
#define EPD_DC    27
#define EPD_RST   26
#define EPD_BUSY  25

// Reference design is active-HIGH busy. If the panel hangs waiting for
// "not busy" during refresh, this is the first thing to flip.
#define EPD_BUSY_INVERTED false

// ---------------------------------------------------------------------------
// Panel: Good Display DEPG0750RWU790F30HP, 800x480, black/white/red (BWR),
// FPC-8612 ribbon. Driver silicon is UC8179/GD7965-family, same family as
// Waveshare's 7.5" (B) V2/V3 panels.
//
// GxEPD2_750c_Z08 is the closest stock match (resolution, connector
// standard, driver family). A near-identical panel (DEPG0750RWF86BF30,
// also FPC-8612) needed a custom init sequence instead of stock Z08 --
// see https://github.com/ZinggJM/GxEPD2/discussions/127 and the fix at
// https://github.com/lumixen/gxepd2-rwf86bf
//
// If the first full refresh comes out garbled/incorrect (not just "no red
// showing", which could be a separate issue) rather than a clean image,
// swap EPD_DRIVER_CLASS below for a custom class modeled on that fix.
// ---------------------------------------------------------------------------
#include <GxEPD2_3C.h>
typedef GxEPD2_750c_Z08 EPD_DRIVER_CLASS;

// A full 800x480 buffer for both the black and red planes needs
// 2 * (800/8) * 480 = 96000 bytes of static RAM, which overflows ESP32
// DRAM once the WiFi/WiFiManager stack is linked in. GxEPD2's own example
// sketches work around this with a MAX_HEIGHT(EPD) macro that caps the
// per-refresh page buffer to a size that fits, letting firstPage()/
// nextPage() paginate internally across the full panel -- that macro
// lives in the example sketches, not the library, so it's defined here.
#define EPD_MAX_BUFFER_SIZE 24000ul // combined budget for both color planes
#define EPD_DRIVER_MAX_HEIGHT \
  (EPD_DRIVER_CLASS::HEIGHT <= (EPD_MAX_BUFFER_SIZE / 2 / (uint32_t(EPD_DRIVER_CLASS::WIDTH) / 8)) \
     ? EPD_DRIVER_CLASS::HEIGHT \
     : (EPD_MAX_BUFFER_SIZE / 2 / (uint32_t(EPD_DRIVER_CLASS::WIDTH) / 8)))
