# epaper-remoment

A Home Assistant dashboard for a 7.5" black/white/red e-paper panel, driven by
an ESP32. A fixed header shows the date and weather; below it, two slots
rotate between widgets based on time-of-day rules — and on what actually has
something to show.

## Widgets

| Widget | Source | When it shows |
|---|---|---|
| Calendar | HA calendar | Only when a meeting starts within 15 minutes (Google Calendar-style day timeline) |
| Home battery | Victron MultiPlus + BMS (Victron MQTT integration) | Urgent on any warning/alarm; more often while charging/discharging |
| Rain | Buienradar radar forecast (NL/BE) at HA's `zone.home` | Only when rain is expected in the next 2 hours; urgent when it starts within 30 minutes |
| Indoor air | CO₂ / PM2.5 sensor | Urgent above threshold ("Open a window") |
| Energy price | Nord Pool | Mornings and evenings: price now, 24h bar chart, cheapest 3h window |
| Power | P1 meter, Envoy, Victron | Grid import/export now; today's solar, grid used/returned, battery in/out and gas |
| Solar | Enphase Envoy + Forecast.Solar | While producing: output now, progress vs. forecast, tomorrow |
| Heat pump | Quatt | More often while heating or on boiler assist |
| NOS / Tweakers | RSS | Latest headline; breakfast/lunch/evening, work hours |
| To-do | HA to-do list | Evenings and weekends, when there are open items |

A widget with nothing to show (failed fetch, empty list, night for solar)
simply sits out. Each widget stays up for at least two refreshes, and only
one slot changes per refresh. The rules are documented at the top of
[`src/widgets/widgets.h`](src/widgets/widgets.h); the scheduler lives in
[`src/widget_scheduler.cpp`](src/widget_scheduler.cpp).

## Settings page

The device serves a settings UI at `http://epaper-dashboard.local/` (or its
IP address), built with [garnet_web](../garnet_web). Sign in with
`WEB_PASSWORD` from `secrets.h` (it defaults to `OTA_PASSWORD`); you can
change it later under System. Under *Dashboard* you can:

- **Panel:** see what's on screen, refresh, or switch to art or the
  dashboard right away,
- **Widgets:** switch widgets on or off, and pin a widget to the left or
  right slot (it falls back to automatic when it has nothing to show),
- **Schedule:** set the dashboard hours and days off (see below).

garnet_web also provides **Wi-Fi** (scan, up to 5 saved networks, static
IP) and **System** (hostname, chip info, password, settings
backup/restore).

Settings are stored on the device and survive reboots and firmware updates.

## Schedule and art mode

Outside the dashboard hours (default 07:30–18:00) and on days off (default
Wednesday, Saturday, Sunday) the panel shows a public-domain artwork instead
and stops talking to Home Assistant. E-paper keeps its image without power,
so the art just stays up; it moves to the next piece at every scheduled
switch.

<p>
  <img src="art/previews/red-fuji.png" width="49%" alt="Hokusai, Red Fuji, dithered to black/white/red">
  <img src="art/previews/great-wave.png" width="49%" alt="Hokusai, The Great Wave off Kanagawa, dithered to black/white/red">
</p>

The artworks are converted ahead of time by
[`tools/convert_art.py`](tools/convert_art.py): it downloads each work from
Wikimedia Commons, keeps red only for the parts of the painting that are
really red, renders everything else as black/white dithering, and writes
`art/art.bin` (embedded in the firmware) plus previews in `art/previews/`.
To change the selection, edit `ARTWORKS` in the script and run it:

```sh
pip install pillow numpy
python3 tools/convert_art.py
```

Included: Hokusai (*The Great Wave*, *Red Fuji*), Vermeer (*The
Milkmaid*), Rembrandt (*The Night Watch*), Van Gogh
(*The Starry Night*), Mondrian (*Composition II in Red, Blue, and Yellow*),
Munch (*The Scream*), Toulouse-Lautrec (*Moulin Rouge: La Goulue*),
Dürer (*Young Hare*) — all public domain, via
Wikimedia Commons.

## Hardware

- **Display:** Good Display DEPG0750RWU790F30HP, 800×480 BWR (driven with
  GxEPD2's `GxEPD2_750c_Z08`). Waveshare 7.5" (B) V2/V3 panels use the same
  driver family.
- **Board:** "e-Paper ESP32 Driver Board" (Waveshare design, Rev 3 clone).
  Pin mapping is in [`include/display_config.h`](include/display_config.h).

## Setup

1. Install [PlatformIO](https://platformio.org/), and check out
   [garnet_web](../garnet_web) and [garnet_settings](../garnet_settings)
   next to this repo (`platformio.ini` links them as `../garnet_web` and
   `../garnet_settings`). The build uses the
   [pioarduino](https://github.com/pioarduino/platform-espressif32)
   platform (Arduino-ESP32 3.x), which PlatformIO downloads on the first
   build.
2. Copy the example config files and fill them in:
   ```sh
   cp include/secrets.h.example include/secrets.h      # HA URL + token, OTA / web password
   cp include/ha_config.h.example include/ha_config.h  # your entity IDs
   ```
   Every widget's entity IDs live in `ha_config.h`. To drop a widget you
   don't have the hardware for, remove it from `allWidgets` in
   [`src/main.cpp`](src/main.cpp).
3. Flash over USB:
   ```sh
   pio run -e esp32dev -t upload
   ```
4. On first boot, and whenever none of its saved networks can be
   reached, the board opens an open WiFi access point
   `Dashboard-XXXX` (the panel shows its name). Join it, sign in with
   the web password, and pick your network under Wi-Fi. While the access
   point is up, the board keeps retrying the saved networks.

Later updates can go over WiFi:

```sh
OTA_PASSWORD=<your password> pio run -e esp32dev_ota -t upload
```

The serial log (115200 baud) shows, per refresh, which widgets had content
and which ones went into each slot.

## How data is fetched

Each widget fetches everything it needs with a single
[HA template](https://www.home-assistant.io/docs/configuration/templating/)
request (`POST /api/template`), so values are combined and formatted by Home
Assistant and the ESP32 only parses a small JSON object. RSS feeds are read
as a stream and only the first item is kept.

Note: HTTPS requests don't verify server certificates.

## License

[MIT](LICENSE)
