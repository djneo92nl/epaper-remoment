#include "art.h"

#include <GxEPD2.h>
#include <esp_partition.h>

#include "text_util.h"

static const uint32_t PLANE_BYTES = (uint32_t)ART_WIDTH / 8 * ART_HEIGHT;
static const uint32_t ART_BYTES = ART_COUNT * 2 * PLANE_BYTES;

// art/art.bin lives in the "art" partition (partitions.csv), outside the
// app so OTA updates stay small. Mapped into the address space once, it
// reads like the embedded array it used to be. Null if the partition is
// missing or too small for the catalog (art.bin regenerated with more
// works than fit).
static const uint8_t *artData() {
  static const uint8_t *data = nullptr;
  static bool tried = false;
  if (tried) {
    return data;
  }
  tried = true;
  const esp_partition_t *part =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "art");
  if (part == nullptr || part->size < ART_BYTES) {
    Serial.printf("[art] no art partition big enough for %u bytes\n", (unsigned)ART_BYTES);
    return nullptr;
  }
  const void *ptr;
  esp_partition_mmap_handle_t handle;
  if (esp_partition_mmap(part, 0, ART_BYTES, ESP_PARTITION_MMAP_DATA, &ptr, &handle) != ESP_OK) {
    Serial.println("[art] mapping the art partition failed");
    return nullptr;
  }
  data = static_cast<const uint8_t *>(ptr);
  return data;
}

void drawArt(Adafruit_GFX &gfx, int index) {
  const ArtInfo &art = ART_CATALOG[index % ART_COUNT];

  gfx.fillScreen(GxEPD_WHITE);
  const uint8_t *bin = artData();
  if (bin != nullptr) {
    const uint8_t *black = bin + art.offset;
    gfx.drawBitmap(0, 0, black, ART_WIDTH, ART_HEIGHT, GxEPD_BLACK);
    gfx.drawBitmap(0, 0, black + PLANE_BYTES, ART_WIDTH, ART_HEIGHT, GxEPD_RED);
  }

  String caption = String(art.artist) + " - " + art.title + ", " + art.year;
  caption = fitText(caption, gfx.width() - 20, 2);
  gfx.setTextWrap(false);
  gfx.setTextSize(2);
  gfx.setTextColor(GxEPD_BLACK);
  gfx.setCursor((gfx.width() - caption.length() * 12) / 2, ART_HEIGHT + 6);
  gfx.print(caption);
}
