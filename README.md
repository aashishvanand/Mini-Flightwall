# Mini Flightwall

A 64×64 RGB LED matrix that shows whatever aircraft is currently flying overhead — flight number, altitude, airline logo, aircraft type and registration, and route with a flight-progress marker — updated automatically every 30 seconds. When there's nothing overhead, it falls back to a clock.

An n8n workflow polls [OpenSky Network](https://opensky-network.org/) and [FlightRadar24](https://www.flightradar24.com/)'s live feed for aircraft near a configured home location, enriches the match with route/aircraft-type data from [adsbdb](https://www.adsbdb.com/) (cross-checked against FR24's live data, since adsbdb's callsign→route table can be stale), and POSTs a JSON payload to a small HTTP server running on an ESP32-S3.

| ![Singapore Airlines](Singapore%20Airlines.bmp) | ![Cathay Pacific](Cathay%20Pacific.bmp) | ![Malaysian Airlines](Malaysian%20Airlines.bmp) | ![Air India](Air%20India.bmp) |
|---|---|---|---|

*Frames pulled straight off the panel's own `/debug/screenshot.bmp` endpoint — no camera needed.*

## How it works

```
n8n (every 30s)
  -> OpenSky Network + FlightRadar24  (nearest aircraft near HOME_LAT/HOME_LON,
                                        merged/de-duplicated by icao24)
  -> adsbdb           (route + aircraft type lookup by callsign / icao24)
  -> airline_icons    (n8n data table: the airline's 24x24 logo, sent inline)
  -> FlightRadar24    (cross-checks adsbdb's route; falls back to FR24's live
                        route/aircraft type when adsbdb has nothing or looks stale)
  -> HTTP POST /api/display  -> ESP32-S3 -> 64x64 HUB75 panel
```

If nothing is found nearby, n8n instead calls `POST /api/clear` and the panel falls back to an NTP-synced clock.

## Hardware

| Product | Qty | Unit Price (INR) | Total (INR) | Total (SGD, approx.) |
|---|---|---|---|---|
| [Waveshare RGB Full-Color LED Matrix Panel](https://www.waveshare.com/rgb-matrix-p2-64x64.htm), 2mm pitch, 64×64 pixels, adjustable brightness | 1 | ₹2,809 | ₹2,809 | ~S$37.70 |
| [Waveshare ESP32-S3 RGB Matrix Driver Board](https://www.waveshare.com/esp32-s3-matrix.htm), dual mic array, Wi-Fi + BLE 5, AI voice interaction support | 1 | ₹2,669 | ₹2,669 | ~S$35.85 |
| **Total** | | | **₹5,478** | **~S$73.55** |

*SGD figures are a rough conversion at ~74.5 INR/SGD (August 2026) for reference only — check a live rate before budgeting.*

You'll also need:
- A 5V power supply sized for your panel's brightness setting (the sketches default to `setBrightness8(60)` to keep current draw modest — raise with care)
- A machine running [n8n](https://n8n.io/) (self-hosted or cloud) to run the workflow
- A free [OpenSky Network](https://opensky-network.org/) API client ID/secret (OAuth2 client credentials)

## Repo layout

```
case/                     3D-printable enclosure (STEP): front bezel, back shell, and the assembly
matrix64/
  03_aircraft_display/    Aircraft Overhead Display -- production firmware
    web/                  Admin console SPA (React + MUI) -- deployed to the board's
                          FATFS partition, not baked into the firmware, see
                          "Admin web console" below
icons/                    Legacy 24x24 raw RGB565 icon set from firmware <=1.2.x's SD card -- unused since 1.3.0
n8n/
  matrix64_aircraft_workflow.json   n8n workflow: OpenSky+FR24 -> adsbdb (FR24-verified) -> matrix
  airline_icons.csv       345 airline logos + the generic fallback, for the airline_icons data table
tools/
  import_icons_n8n.py     Creates the airline_icons table in your n8n and loads a CSV into it
  convert_tiles.py        Converts source logos into airline_icons CSV rows (--csv), or legacy .bin files
  deploy_web.py           Builds web/ and pushes it to a board's FATFS partition
```

## Assembly / wiring

The Waveshare ESP32-S3 RGB Matrix Driver Board is built to plug directly onto the back of the panel — there's no point-to-point jumper wiring to do:

1. **Panel <-> driver board**: connect the driver board to the panel's HUB75 input using the IDC ribbon cable that ships with the driver board. The connector is keyed (notched), so it only seats one way — don't force it.
2. **Power**: feed 5V into the panel's power input (screw terminal or barrel jack, depending on your panel), sized for your brightness setting. The firmware defaults to `setBrightness8(60)` (roughly 25% duty) specifically to keep current draw modest until you've confirmed your supply can handle full brightness — a 64×64 panel at full white, full brightness can pull several amps.
3. **USB-C**: connect the driver board to your computer for flashing and for Serial Monitor output (WiFi connection status, the assigned IP, etc.).

No SD card is needed (since firmware 1.3.0 -- icons arrive inline with every push). The driver board's GPIO wiring to the panel is fixed by Waveshare and already baked into the sketches — you don't need to configure it, but it's documented here since it explains a couple of the config lines in the code:

| Signal | GPIO | Where it's set |
|---|---|---|
| HUB75 row-address line E (needed for 64-row panels) | 9 | `mxconfig.gpio.e` in `setupMatrix()` |

### Case

`case/` has a two-piece printable enclosure as STEP files, commissioned by Aashish Vivekanand for this project. Most slicers (Bambu Studio, OrcaSlicer, PrusaSlicer) import STEP directly.

| File | Part | Size (mm) |
|---|---|---|
| `mini_flightwall_case_front.step` | Front bezel: panel window, 6 counterbored panel screws | 143.9 × 143.9 × 20 |
| `mini_flightwall_case_back.step` | Back shell: room for the driver board and wiring, keyhole wall-mount slot | 143.9 × 143.9 × 38.4 |
| `mini_flightwall_case_assembly.step` | Both parts in position, for reference only (don't print this one) | 143.9 × 143.9 × 58.4 |

Four corner screws join the two halves. A 35 mm slot on the side, cut across the seam, lets the USB-C cable out sideways so the case can sit flush against a wall.

## Arduino IDE setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (2.x).
2. Add the ESP32 board package: **File > Preferences > Additional Boards Manager URLs**, add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`, then install **esp32** via **Tools > Board > Boards Manager**.
3. Select the board: **Tools > Board > esp32 > ESP32S3 Dev Module**.
4. Under **Tools**, set:
   - **USB CDC On Boot**: Enabled (so Serial Monitor works over USB-C without a separate UART adapter)
   - **PSRAM**: OPI PSRAM (large allocations such as the TLS buffers and JSON documents land there — required for Aircraft Overhead Display; flashing without it leaves the board heap-starved). This board (Waveshare's ESP32-S3-N32R16 driver board) has 16MB of Octal PSRAM, which is what "OPI" refers to.
   - **Flash Size**: **32MB (256Mb)** — this board's chip is the N32R16 variant (32MB flash, 16MB PSRAM). The IDE's default is 4MB regardless of board choice; leaving it on the default doesn't break anything (the firmware still fits fine either way), it just leaves ~28MB of the chip's actual flash completely unpartitioned and unusable.
   - **Partition Scheme**: **32M Flash (4.8MB APP/22MB FATFS)** — pick this only after Flash Size above is set to 32MB (a 32MB-sized partition table on a board set to 4MB will fail to flash). Gives 4.8MB of app space instead of ~1.2MB, comfortable headroom for the admin web console and anything added to it later. The 22MB FATFS partition holds the admin web console (see "Admin web console"); config lives in NVS/Preferences.
   - **Port**: whichever `/dev/cu.usbmodem*` (macOS) or `COM*` (Windows) appears when the board is plugged in
5. Install libraries via **Tools > Manage Libraries**:
   - `ESP32 HUB75 LED MATRIX PANEL DMA Display` by mrfaptastic
   - `ArduinoJson` by Benoit Blanchon
   - `Adafruit GFX Library` (Aircraft Overhead Display only)
6. Open the `.ino` file for the sketch you're building (Arduino IDE will open the whole sketch folder, including `secrets.h`), hit **Upload**, then open **Tools > Serial Monitor** at 115200 baud to watch it connect to WiFi and print its IP.

Double-check board-specific settings (exact partition scheme, PSRAM mode) against Waveshare's own wiki page for this exact driver board — defaults above worked for this build, but Waveshare occasionally revises recommended settings between firmware/SDK versions.

## Firmware setup

1. **Panel bring-up test** (not included here): run Waveshare's own stock examples (`01_SimpleTestShapes` / `07_Pixel_Mapping_Test`) with `PANEL_RES_Y` changed to 64, to confirm the panel lights up correctly before writing any custom code. An earlier bring-up sketch of our own (`matrix64/02_http_display` -- WiFi + a bare HTTP endpoint, validating networking and the double-buffer/flip drawing pattern) has since been removed now that the production firmware is working end-to-end; those config decisions (panel driver, GPIO wiring, buffer-flip timing) are documented inline in `03_aircraft_display.ino` instead.
2. **Aircraft Overhead Display** — `matrix64/03_aircraft_display`: the production firmware — renders the aircraft layout from each n8n push (airline logo included inline), and falls back to an NTP clock when idle. Also serves `/debug/screenshot.bmp` so you can see exactly what's on the panel from a browser, no camera needed.

For each sketch:

```bash
cp secrets.h.example secrets.h
# edit secrets.h with your real WiFi SSID/password
```

`secrets.h` is gitignored — never commit it.

### Icons (Aircraft Overhead Display only)

The board stores no icons. The n8n workflow's **Build Payload (Matrix64)** node resolves which logo to show from the `airline_icons` data table (see "n8n workflow setup" for loading it): the airline's ICAO designator first, then its IATA code, then the generic `00` row (a blank tail) — so a missing logo reads as "no art yet" instead of a stale or wrong one. Every push carries the pixels inline as `iconData`: the row's `icon24` column, 24×24 raw little-endian RGB565 (1,152 bytes, no header — the simplest thing the ESP32 can draw straight from a `uint16_t` buffer), base64-encoded to 1,536 chars. The firmware decodes it once per push; anything that isn't exactly 1,152 bytes after decoding blanks the icon slot and logs why.

Adding or changing a logo means adding or updating a table row. Neither the workflow nor the firmware changes, and the new logo shows on the very next push:

```bash
python3 -m venv venv && venv/bin/pip install pillow numpy
# name each source logo after the airline code: sq.png (IATA) or asy.webp (ICAO-only operator)
venv/bin/python tools/convert_tiles.py --input logos/ --csv new_icons.csv
# fill in the icao/iata/name columns the filename didn't cover, then:
python3 tools/import_icons_n8n.py --csv new_icons.csv
```

`convert_tiles.py` uses emblem-focus detection: it crops tightly around the logo's actual mark (bird, crane, flag, flower) so it fills the 24×24 tile instead of sitting small in a letterboxed square. Rows are upserted by `code`, so re-importing replaces an existing logo. Each `icao` must belong to only one row; a wrong ICAO code puts the wrong logo on screen, so leave it blank if unsure and the row matches on IATA alone.

Airline logos are trademarks of their respective owners. They're included for personal, non-commercial display only.

Firmware up to 1.2.x instead preloaded `icons/*.bin` from an SD card and synced them from a remote manifest (`tools/sync_icons_r2.sh`); that path is gone as of 1.3.0. `icons/` is kept for reference only.

### Display layout

Five sections of 6×8 px text (size 1), separated by 2 px gaps. The logo sits at the left edge of the middle section, and the text beside it is clipped to its own column so a scrolling name never runs over the logo:

```
 y  0-7    Singapore Airlines                  airline name
 y 10-17   SQ962   1950ft▲ 174kt               flight, altitude, climb/descend, speed
 y 20-43   ┌──────┐ Boeing                     make          (y 20)
           │ logo │ B38M                       ICAO type     (y 28)
           └──────┘ 9V-MBL                     registration  (y 36)
 y 46-53   SIN ━━━►┈┈┈┈┈┈┈ CGK                 route + progress marker
 y 56-63   Singapore Changi -> Soekarno-Hatta  city names (always scrolls)
```

- **Logo**: 24×24 px at x 0–23, y 20–43, drawn from the push's `iconData` (see "Icons" above). If a push has no valid icon, that space stays blank and the text column doesn't move.
- **Beside the logo**: make, ICAO type and registration on three 8px rows starting at x 26, which exactly fill the logo's 24px height (a 6-character registration like `9V-SMA` exactly fills the column; anything longer scrolls within it). Before 1.3.1 there were only make and type, spaced out at y 24 and y 34.
- **Other rows** stay still when the text fits, and scroll when it doesn't. The flight/altitude row is usually long enough to scroll.
- **Climb/descend**: a green ▲ (climbing) or amber ▼ (descending) after the altitude, omitted within ±200 ft/min of level. Both are CP437 glyphs (`0x1E`/`0x1F`) from Adafruit GFX's built-in font — no custom bitmaps.
- **Route row**: origin code flush left, destination flush right, and a track between them with a green ► (`0x10`) at the payload's `progress` — solid over the part already flown, dotted over what's left. Without `progress` (an FR24-only route, which has no airport coordinates) the marker sits centered on an all-dotted track.

## n8n workflow setup

`n8n/matrix64_aircraft_workflow.json` here is a standalone, self-contained
template — just the matrix panel's branch, no Ulanzi AWTRIX/TC002 clock
nodes. It's the same logic this build's actual feeder runs for the matrix
(the real feeder's workflow additionally drives two Ulanzi clocks off the
same position/route data — see the sibling `Ulanzi Feeder` repo's
`n8n_aircraft_workflow.json` if you want that combined setup instead).

Nodes, in order:

```
Every 30s
  -> Config (edit per location)
  -> Get/Refresh OpenSky Token -> Fetch Nearby States (OpenSky)   -\
     Fetch Nearby States (FR24)                                    >-- Merge Position Data -> Nearest Aircraft
  -> Aircraft Found?
       true  -> Lookup Route (adsbdb)  -\
                Verify Route (FR24)      >-- Merge Route Data -> Compare Routes -> Lookup Icon -\
                Lookup Aircraft (adsbdb) ---------------------------------------------------- >-- Merge Matrix Data -> Build Payload (Matrix64) -> Push to Matrix
       false -> Clear Matrix
```

- **Position**: OpenSky and FlightRadar24's live feed are merged and
  de-duplicated by `icao24` — the two networks' ADS-B ground receivers
  overlap but aren't identical, so combining them catches aircraft one
  network's receivers missed but the other's caught.
- **Route**: adsbdb resolves a callsign to an airline/origin/destination,
  but that table is static and can go stale (flight numbers get reused
  across seasons/codeshares). `Compare Routes` cross-checks it against
  FlightRadar24's live feed for the same callsign and trusts the FR24
  match when one exists; adsbdb's route is only used when FR24 has
  nothing *and* the aircraft's current position is geographically
  plausible for adsbdb's claimed origin/destination.
- **Aircraft type**: adsbdb's `/v0/aircraft/{icao24}` lookup is the
  richer source (full manufacturer name + ICAO type), but doesn't have
  every hex (private/GA aircraft, newly registered, or a hex reassigned
  since adsbdb's table was last refreshed). When it has nothing, `Build
  Payload (Matrix64)` falls back to the bare type code FlightRadar24's
  live feed reports for that `icao24` (no manufacturer name, just
  e.g. `B738`).
- **Logo**: `Lookup Icon` reads the `airline_icons` data table, matching
  the airline's ICAO code, its IATA code, or the generic `00` row, and
  `Build Payload (Matrix64)` picks the best match (see "Icons" above).

Setup (needs an n8n version with Data tables, and the n8n API enabled for step 1):

1. **Load the logos.** Create an API key under **Settings > n8n API**, then:
   ```bash
   export N8N_URL=http://<your-n8n-host>:5678
   export N8N_API_KEY=<your key>
   python3 tools/import_icons_n8n.py
   ```
   This creates the `airline_icons` table and loads 345 airline logos plus the generic fallback tail from `n8n/airline_icons.csv`. It's safe to re-run.
2. **Import** `n8n/matrix64_aircraft_workflow.json` (**Workflows > Import from File**).
3. **Open the `Lookup Icon` node and select the `airline_icons` table.** Table IDs are different on every n8n instance, so the template ships with none selected. Without this step, no logo appears on the panel.
4. **Fill in `Config (edit per location)`:**
   - `HOME_LAT` / `HOME_LON` — your location's coordinates (placeholders are `0.0` — replace before running)
   - `RADIUS_DEG` — search radius in degrees (default `0.15` ≈ ~16km)
   - `MATRIX_IP` — your ESP32's LAN IP (placeholder `192.168.1.50`)
   - `OPENSKY_CLIENT_ID` / `OPENSKY_CLIENT_SECRET` — from your [OpenSky API client credentials](https://opensky-network.org/apidoc/rest.html#authentication)

Then activate it. It polls every 30 seconds, finds the nearest airborne aircraft, looks up and verifies its route and aircraft type, and pushes a display payload to the matrix. FlightRadar24's feed here is its unofficial public `data-cloud.flightradar24.com/zones/fcgi/feed.js` endpoint. It isn't an official or documented API: FR24 can change, rate-limit or block it at any time, and using it may go against their terms of service. Keep it to personal use and proceed with caution, or use [FR24's official API](https://fr24api.flightradar24.com/) for anything dependable or commercial. If it's ever unreachable, the workflow just falls back to OpenSky-only positioning and adsbdb-only routing (via the plausibility check above), same as before FR24 was added.

### Payload shape

```json
{
  "flight": "SQ123",
  "altitudeFt": 35000,
  "speedKt": 460, "verticalRateFpm": 1200,
  "airline": "Singapore Airlines",
  "originCode": "SIN", "destCode": "KUL", "progress": 0.42,
  "originCity": "Singapore Changi Airport",
  "destCity": "Kuala Lumpur International Airport",
  "icon": "sq_logo", "iconData": "<1536 base64 chars>",
  "make": "Airbus", "modelShort": "A388", "tail": "9V-SKA"
}
```

`tail` is the registration (FR24 first, then adsbdb), shown under the type
beside the logo; omitted when neither source has it (typically private/GA).
`progress` (0–1, share of the trip flown) is distance-from-origin over
origin + remaining distance, and is only sent when adsbdb supplied both
airports' coordinates. `iconData` is described under "Icons" above; `icon`
(the logo code) is ignored by firmware 1.3.0+ and kept for older builds.

`speedKt` / `verticalRateFpm` are `null` when the source feed didn't report
them for that aircraft (firmware omits the line rather than showing `0`).
`originCity`/`destCity`/`airline` are only present when adsbdb's route data
was trusted (see "n8n workflow setup" above) — a FR24-sourced route still
sets `originCode`/`destCode`/`iconData`, just without the city names or airline
name adsbdb would have added.

## API reference (firmware)

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/display` | POST | Push a new display payload (JSON) -- this is what the n8n workflow calls every 30s |
| `/api/clear` | POST | Clear the display, fall back to the NTP clock |
| `/debug/screenshot.bmp` | GET | Returns the current frame as a BMP -- no camera needed to see what's on the panel |

The endpoints below back the admin web console at `http://<board-ip>/` (see
"Admin web console" below) -- none of them need to be called directly for
normal operation, they exist for the page's own JS to call.

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | Admin dashboard -- live preview, status, WiFi scan, config, log, reboot, firmware update |
| `/api/status` | GET | JSON status snapshot (uptime, heap/PSRAM, WiFi, whether an icon is showing, current config, what's showing) |
| `/api/config` | POST | Update hostname / brightness / timezone override / night mode / NTP server (form-encoded, persisted to NVS). **Requires HTTP Basic Auth** |
| `/api/reboot` | POST | Reboot the board |
| `/api/log` | GET | Tail of the in-memory log ring buffer (plain text) |
| `/api/wifi/scan` | GET | JSON list of nearby SSIDs (blocks ~1-2s; the panel's scroll will visibly pause) |
| `/api/ota` | POST | Flash a new firmware `.bin` (multipart/form-data) and reboot into it. **Requires HTTP Basic Auth** -- see "OTA firmware updates" below |
| `/api/web/upload` | POST | Write one file (multipart/form-data, field `file`, uploaded with its filename set to the relative target path, e.g. `assets/index-XYZ.js.gz`) to `/web/<filename>` on the FATFS partition. **Requires HTTP Basic Auth**. Used by `tools/deploy_web.py`, not meant to be called by hand |
| `/api/web/clear` | POST | Recursively delete everything under `/web/` on FATFS, ahead of a fresh deploy. **Requires HTTP Basic Auth** |

## Admin web console

`http://<board-ip>/` -- a React + MUI single-page app (source under
`matrix64/03_aircraft_display/web/`), not a status page: the panel preview
and status numbers auto-refresh via JS polling `/api/status` and
`/debug/screenshot.bmp` (no need to reload the page). From it you can scan
for WiFi networks, watch the
live log, edit hostname/brightness/timezone-override, reboot, and flash new
firmware -- all documented in the API table above.

Unlike the previous plain-HTML version, the built app isn't baked into the
firmware image at all -- it's deployed separately as static files on the
board's internal FATFS partition (`/web/`, the same 22MB FATFS partition
described in "Arduino IDE setup"), served by `handleWebStatic()` in the
firmware. That partition has no DMA-unsafe window, so
it's safe to read/write at any time, including while the display is
running. This means UI changes can be redeployed without a full firmware
reflash, and there's no realistic firmware-size pressure from the UI's
bundle size (React + MUI, gzipped, is well under 1% of the partition).

To build and deploy it:

```bash
python3 tools/deploy_web.py <board-ip>
```

`deploy_web.py` runs `npm install`/`npm run build` in `web/` (skip with
`--skip-build` if you've already built it), gzips every output file (the
firmware only ever looks up `<path>.gz` on FATFS), clears `/web/` on the board first
(Vite hashes output filenames per build, so stale files from a previous
deploy would otherwise accumulate forever), then uploads everything via
`/api/web/upload`. It prompts for the admin credentials the same way the
OTA/config endpoints do -- see below.

Like every other endpoint on this board, most of the console has **no
authentication** -- it trusts the LAN the same way `/api/display` always
has. That's an explicit, accepted tradeoff for a board that's meant to
never be exposed to the WAN, not an oversight. Changing config, flashing
firmware, and deploying the web console itself are the exceptions, gated
behind HTTP Basic Auth; see "OTA firmware updates" below for why.

### OTA firmware updates

The Firmware update section of `/` accepts a compiled `.bin` (Arduino IDE:
**Sketch > Export Compiled Binary**, or `arduino-cli compile --output-dir
<dir>`) and flashes it to the board's spare `ota_0`/`ota_1` app partition
(the 32MB scheme's partitions both being ~4.5MB is what makes this
practical -- see "Arduino IDE setup"), rebooting into it automatically on
success.

Unlike every other endpoint on this board, `/api/ota`, `/api/web/upload`,
`/api/web/clear` (all three: arbitrary writes to internal flash), and
`/api/config` (which can change the hostname or force a timezone) require
HTTP Basic Auth -- your browser will prompt once and cache the credentials
for the origin. Set `ADMIN_USER`/`ADMIN_PASSWORD` in `secrets.h` (see
`secrets.h.example`); left unset, both fall back to `admin`/`changeme`,
which is fine for a private LAN but the firmware logs a warning at boot if
you're still on it. Every other endpoint stays unauthenticated, same
LAN-only trust model as always -- these specifically were judged too risky
to leave open (a bad `/api/display` push shows a wrong flight number; a bad
`/api/ota` or `/api/web/upload` push replaces the firmware or the console).

**Automatic rollback:** if a flashed firmware image fails to reach the end
of `setup()` three boots in a row -- crashing or hanging every time, before
WiFi/matrix/NTP/the web server are all confirmed up -- the board reverts to
whichever partition last booted successfully and reboots into that instead,
recovering from a bad flash without needing physical USB access. This is a
software-level reimplementation of the idea (a boot-attempt counter and a
"last known good partition" label, both in NVS), not ESP-IDF's own
bootloader app-rollback feature -- the precompiled Arduino bootloader
doesn't have that enabled, and toggling it isn't exposed through the IDE.

### Reliability

A few things exist specifically to keep this board running unattended for
months, not weeks:

- **Watchdog**: if `loop()` ever stops running for 30s straight (a hung
  blocking call, a wedged request), the board reboots itself automatically
  instead of sitting frozen until someone notices.
- **Stale-data fallback**: if n8n stops pushing entirely -- the workflow
  deactivated, the n8n host down, a sustained network problem -- the panel
  falls back to the clock after 5 minutes of silence instead of showing an
  aircraft that flew off long ago with no indication anything's wrong. A
  routine `/api/clear` (no aircraft currently in range) counts as contact
  and doesn't trigger this -- only *nothing at all* arriving does. Visible
  in the admin page as "Last display push."
- **Boot diagnostics**: the reset reason (power-on, brownout, panic, task
  watchdog, ...) is logged at the top of every boot, visible in `/api/log`
  -- useful for diagnosing an unexpected reboot after the fact without
  physical USB access.
- **Night mode**: optional brightness schedule (Config section of `/`) --
  dims to a lower brightness during a configured local-time window (default
  23:00-06:00) instead of running at full brightness in a dark room all
  night. Applies live; the window can wrap past midnight.

## License

GPL-3.0 — see [LICENSE](LICENSE).
