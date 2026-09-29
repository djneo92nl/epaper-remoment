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
| Indoor air | CO₂ / PM2.5 sensor | Urgent above threshold ("Open a window") |
| Energy price | Nord Pool | Mornings and evenings: price now, 24h bar chart, cheapest 3h window |
| Power | P1 meter | Grid import/export, usage and gas today |
| Solar | Inverters + Forecast.Solar | While producing: output now, progress vs. forecast, tomorrow |
| Heat pump | Quatt | More often while heating or on boiler assist |
| NOS / Tweakers | RSS | Latest headline; breakfast/lunch/evening, work hours |
| To-do | HA to-do list | Evenings and weekends, when there are open items |

A widget with nothing to show (failed fetch, empty list, night for solar)
simply sits out. Each widget stays up for at least two refreshes, and only
one slot changes per refresh. The rules are documented at the top of
[`src/widgets/widgets.h`](src/widgets/widgets.h); the scheduler lives in
[`src/widget_scheduler.cpp`](src/widget_scheduler.cpp).

## Hardware

- **Display:** Good Display DEPG0750RWU790F30HP, 800×480 BWR (driven with
  GxEPD2's `GxEPD2_750c_Z08`). Waveshare 7.5" (B) V2/V3 panels use the same
  driver family.
- **Board:** "e-Paper ESP32 Driver Board" (Waveshare design, Rev 3 clone).
  Pin mapping is in [`include/display_config.h`](include/display_config.h).

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Copy the example config files and fill them in:
   ```sh
   cp include/secrets.h.example include/secrets.h      # HA URL + token, OTA password
   cp include/ha_config.h.example include/ha_config.h  # your entity IDs
   ```
   Every widget's entity IDs live in `ha_config.h`. To drop a widget you
   don't have the hardware for, remove it from `allWidgets` in
   [`src/main.cpp`](src/main.cpp).
3. Flash over USB:
   ```sh
   pio run -e esp32dev -t upload
   ```
4. On first boot the board opens a WiFi access point `EPaper-Setup`
   (password `epaper1234`); join it and enter your WiFi credentials at
   `http://192.168.4.1`.

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
