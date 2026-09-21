# Mini Flightwall

A 64×64 RGB LED matrix that shows whatever aircraft is currently flying overhead — flight number, altitude, airline logo, aircraft type, and route — updated automatically every 30 seconds. When there's nothing overhead, it falls back to a clock.

An n8n workflow polls [OpenSky Network](https://opensky-network.org/) for aircraft near a configured home location, enriches the match with route/aircraft-type data from [adsbdb](https://www.adsbdb.com/), and POSTs a JSON payload to a small HTTP server running on an ESP32-S3.

| ![Singapore Airlines](Singapore%20Airlines.bmp) | ![Cathay Pacific](Cathay%20Pacific.bmp) | ![Malaysian Airlines](Malaysian%20Airlines.bmp) | ![Air India](Air%20India.bmp) |
|---|---|---|---|

*Frames pulled straight off the panel's own `/debug/screenshot.bmp` endpoint — no camera needed.*

## How it works

```
n8n (every 30s)
  -> OpenSky Network  (nearest aircraft near HOME_LAT/HOME_LON)
  -> adsbdb           (route + aircraft type lookup by callsign / icao24)
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
- A microSD card (FAT32) for storing preloaded airline-logo icons
- A 5V power supply sized for your panel's brightness setting (the sketches default to `setBrightness8(60)` to keep current draw modest — raise with care)
- A machine running [n8n](https://n8n.io/) (self-hosted or cloud) to run the workflow
- A free [OpenSky Network](https://opensky-network.org/) API client ID/secret (OAuth2 client credentials)

## Repo layout

```
matrix64/
  03_aircraft_display/    Aircraft Overhead Display -- production firmware
icons/                    Preprocessed 24x24 raw RGB565 airline-logo icons (copy to SD card /icons)
tools/
  convert_tiles.py        Converts source logo images (webp/png/jpg) to the icons/*.bin format
matrix64_aircraft_workflow.json   n8n workflow: OpenSky -> adsbdb -> matrix
```

## Assembly / wiring

The Waveshare ESP32-S3 RGB Matrix Driver Board is built to plug directly onto the back of the panel — there's no point-to-point jumper wiring to do:

1. **Panel <-> driver board**: connect the driver board to the panel's HUB75 input using the IDC ribbon cable that ships with the driver board. The connector is keyed (notched), so it only seats one way — don't force it.
2. **Power**: feed 5V into the panel's power input (screw terminal or barrel jack, depending on your panel), sized for your brightness setting. The firmware defaults to `setBrightness8(60)` (roughly 25% duty) specifically to keep current draw modest until you've confirmed your supply can handle full brightness — a 64×64 panel at full white, full brightness can pull several amps.
3. **SD card**: format a microSD card FAT32, copy `icons/*.bin` onto it under `/icons/` (Aircraft Overhead Display firmware only), and insert it into the driver board's SD slot **before** powering on — the board reads it once at boot and never touches it again while running.
4. **USB-C**: connect the driver board to your computer for flashing and for Serial Monitor output (WiFi connection status, the assigned IP, icon-preload count, etc.).

The driver board's GPIO wiring to the panel and SD card is fixed by Waveshare and already baked into the sketches — you don't need to configure it, but it's documented here since it explains a couple of the config lines in the code:

| Signal | GPIO | Where it's set |
|---|---|---|
| HUB75 row-address line E (needed for 64-row panels) | 9 | `mxconfig.gpio.e` in `setupMatrix()` |
| SD_MMC data line (D0) | 17 | `BSP_SD_D0` |
| SD_MMC command line (CMD) | 44 | `BSP_SD_CMD` |
| SD_MMC clock line (CLK) | 1 | `BSP_SD_CLK` |

## Arduino IDE setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (2.x).
2. Add the ESP32 board package: **File > Preferences > Additional Boards Manager URLs**, add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`, then install **esp32** via **Tools > Board > Boards Manager**.
3. Select the board: **Tools > Board > esp32 > ESP32S3 Dev Module**.
4. Under **Tools**, set:
   - **USB CDC On Boot**: Enabled (so Serial Monitor works over USB-C without a separate UART adapter)
   - **PSRAM**: OPI PSRAM (the icon cache and the off-screen canvas live in PSRAM — required for Aircraft Overhead Display). This board (Waveshare's ESP32-S3-N32R16 driver board) has 16MB of Octal PSRAM, which is what "OPI" refers to.
   - **Flash Size**: **32MB (256Mb)** — this board's chip is the N32R16 variant (32MB flash, 16MB PSRAM). The IDE's default is 4MB regardless of board choice; leaving it on the default doesn't break anything (the firmware still fits fine either way), it just leaves ~28MB of the chip's actual flash completely unpartitioned and unusable.
   - **Partition Scheme**: **32M Flash (4.8MB APP/22MB FATFS)** — pick this only after Flash Size above is set to 32MB (a 32MB-sized partition table on a board set to 4MB will fail to flash). Gives 4.8MB of app space instead of ~1.2MB, comfortable headroom for the admin web console and anything added to it later. The 22MB FATFS partition goes unused (icons live on the SD card via SD_MMC, config in NVS/Preferences) — it's just what this scheme bundles; a "no FS" 32MB scheme would be marginally more correct but isn't offered by the IDE's ESP32S3 board definitions as of this writing.
   - **Port**: whichever `/dev/cu.usbmodem*` (macOS) or `COM*` (Windows) appears when the board is plugged in
5. Install libraries via **Tools > Manage Libraries**:
   - `ESP32 HUB75 LED MATRIX PANEL DMA Display` by mrfaptastic
   - `ArduinoJson` by Benoit Blanchon
   - `Adafruit GFX Library` (Aircraft Overhead Display only)
6. Open the `.ino` file for the sketch you're building (Arduino IDE will open the whole sketch folder, including `secrets.h`), hit **Upload**, then open **Tools > Serial Monitor** at 115200 baud to watch it connect to WiFi and print its IP.

Double-check board-specific settings (exact partition scheme, PSRAM mode) against Waveshare's own wiki page for this exact driver board — defaults above worked for this build, but Waveshare occasionally revises recommended settings between firmware/SDK versions.

## Firmware setup

1. **Panel bring-up test** (not included here): run Waveshare's own stock examples (`01_SimpleTestShapes` / `07_Pixel_Mapping_Test`) with `PANEL_RES_Y` changed to 64, to confirm the panel lights up correctly before writing any custom code. An earlier bring-up sketch of our own (`matrix64/02_http_display` -- WiFi + a bare HTTP endpoint, validating networking and the double-buffer/flip drawing pattern) has since been removed now that the production firmware is working end-to-end; those config decisions (panel driver, GPIO wiring, buffer-flip timing) are documented inline in `03_aircraft_display.ino` instead.
2. **Aircraft Overhead Display** — `matrix64/03_aircraft_display`: the production firmware — preloads airline icons from SD into PSRAM at boot, renders the aircraft layout, and falls back to an NTP clock when idle. Also serves `/debug/screenshot.bmp` so you can see exactly what's on the panel from a browser, no camera needed.

For each sketch:

```bash
cp secrets.h.example secrets.h
# edit secrets.h with your real WiFi SSID/password
```

`secrets.h` is gitignored — never commit it.

### Icons (Aircraft Overhead Display only)

Icons are 24×24 raw RGB565 pixel dumps — no file header, 1,152 bytes each — because that's the simplest format the ESP32 can load straight into a `uint16_t` buffer with no decoding at runtime. `icons/` in this repo already has the pre-converted `.bin` files for a couple hundred airline codes; copy the ones you need onto the SD card under `/icons/`. The filename (minus `.bin`) is the `icon` key sent in the JSON payload, e.g. `sq_logo.bin` for Singapore Airlines.

The n8n workflow's **Build Payload** node resolves which icon to send: it tries the airline's IATA code first, falls back to its ICAO code if there's no icon for the IATA (or no IATA at all, e.g. military/cargo callsigns), and falls back to `00_logo` (a blank tail) if neither has one — so a missing icon reads as "no art yet" instead of a stale or wrong logo. That node keeps a hardcoded `AVAILABLE_ICONS` set of every code currently in `icons/`; regenerate it (`ls icons | sed 's/_logo\.bin$//' | sort`) and paste it back in whenever you add or remove icons.

To add or regenerate icons from source logo images (`.webp`, `.png`, `.jpg`):

```bash
python3 -m venv venv && venv/bin/pip install pillow numpy
venv/bin/python tools/convert_tiles.py --input logos/ --size 24 --format raw565 --output icons
```

Uses emblem-focus detection (crops tightly around the actual logo mark -- birds, cranes, flags, kapok flowers -- so it fills most of the 24x24 tile instead of sitting small in a plain letterboxed square), unsharp masking, and a two-pass downscale before writing RGB565, `<name>_logo.bin` per source file. Ported from the [Ulanzi Feeder](../Ulanzi%20Feeder) repo's 8x8 AWTRIX converter -- see `tools/convert_tiles.py --help` for options.

Three ways a new or changed icon reaches the board, in increasing order of how "permanent" the change is meant to be:

1. **Admin console upload** (`http://<board-ip>/`, Icons section) -- fastest for a one-off fix, shows on the panel immediately. See "Icon upload via the admin console" further down.
2. **Remote sync** (below) -- the way to roll out a whole icon-set update (e.g. after a `convert_tiles.py` regeneration) without pulling the SD card by hand.
3. **Pull the SD card and copy `icons/*.bin` onto it directly** -- always works, no network dependency, but means physically accessing the board.

All three end up in the same place: SD's `/icons/` is the durable source of truth `preloadIcons()` reads at boot, regardless of which path got a file there.

#### Remote icon sync

Icons are hosted as static files on any S3-compatible object storage (or really, anything served over plain HTTPS) — the firmware just does a `GET`, no vendor SDK or API involved:

- **At boot**, right after WiFi connects and before the HUB75 DMA starts (the SD card is unreliable once it's running), `syncIcons(true)` GETs `manifest.json` from `ICON_BASE_URL`, downloads whatever's missing or changed, writes it to `/icons/` on the SD card, and loads it into PSRAM.
- **Every `ICON_SYNC_INTERVAL_MS`** (3 days by default) while running, `loop()` calls `syncIcons(false)` — same manifest diff, but PSRAM-only, since SD can't be touched once the display's DMA is active. Those updates get persisted to SD on the next reboot regardless.

`manifest.json` is a JSON array of `{"name", "sha256"}` — the SHA-256 lets the firmware tell "already have this" apart from "have a file by this name but the art changed," and download only what's actually different.

**Security against a MITM or malicious proxy:** every request validates the server's TLS certificate against the ESP32 core's built-in CA bundle (`useBuiltinCACertBundle()`, not `setInsecure()`) and disables HTTP redirects, so a network attacker can't substitute a different host or downgrade the connection. On top of that, every downloaded icon's SHA-256 is checked against the hash the manifest declared for it — fetched over that same validated connection — before it's written to SD or shown on the panel; a mismatch is dropped silently rather than displayed. This also means a compromised object (even one served with a technically-valid cert) gets caught by the hash check as long as the manifest itself wasn't tampered with in the same request.

In `matrix64/03_aircraft_display/secrets.h`, set `ICON_BASE_URL` to `<base>/<prefix>` (no trailing slash) — `<base>/manifest.json` and `<base>/<name>.bin` must both resolve. Leaving it unset skips the sync entirely — the SD card's existing icons still work. This value is public info (it's just a URL your board fetches from over plain HTTPS) but stays out of the repo since it lives in the gitignored `secrets.h`, not `secrets.h.example`.

Any S3-compatible bucket with public HTTPS access works — Cloudflare R2, AWS S3, MinIO, Backblaze B2, etc. `tools/sync_icons_r2.sh` is the reference implementation, using R2 (chosen for free egress and an S3-compatible API):

1. `wrangler login`, then `wrangler r2 bucket create <bucket-name>`.
2. In the Cloudflare dashboard, open the bucket → **Settings** → **Public access** → **Allow Access**, and copy the `r2.dev` URL, or attach a custom domain (e.g. `data.airportdata.dev`).
3. Set `ICON_BASE_URL` as described above.

Whenever `icons/` changes:

```bash
./tools/sync_icons_r2.sh <bucket-name> [prefix]
```

`prefix` defaults to `24x24` to match the layout above; pass `""` to upload to the bucket root instead. This uploads every `icons/*.bin` plus a `manifest.json` with each one's SHA-256. New/changed icons show up within one sync interval; reboot to pick them up immediately. On a different S3-compatible provider, upload the same `icons/*.bin` files plus a matching `manifest.json` (name + sha256 per file) however that provider's tooling works (`aws s3 cp`, `mc mirror`, rclone, etc.) — the firmware doesn't care how the files got there, only that they're reachable over HTTPS at `ICON_BASE_URL`.

Object storage was chosen over Cloudflare Images because the `.bin` files are headerless raw RGB565 dumps, not a format Images can store or serve — Images is built for re-encoding/serving actual photos, not arbitrary binary blobs.

## n8n workflow setup

`matrix64_aircraft_workflow.json` here is a standalone template (OpenSky-only,
single-device push) for anyone running just the matrix panel on its own.

**This build's actual feeder doesn't run that file.** The matrix panel is one
push target on a shared workflow that also drives two Ulanzi AWTRIX/TC002
clocks — see the sibling `Ulanzi Feeder` repo's `n8n_aircraft_workflow.json`
for the full picture (dual-source OpenSky + FlightRadar24 positioning, route
verification, per-device payload builders). That workflow's **Config** node
holds one shared `HOME_LAT`/`HOME_LON`/`RADIUS_DEG` and OpenSky credentials
for all three devices, plus a `MATRIX_IP` for this panel; its **Compare
Routes** node feeds a `Build Payload (Matrix64)` branch that reconstructs
this repo's full payload shape (airline, altitude, city names, aircraft
type) using this repo's `Lookup Aircraft (adsbdb)` node and 24×24 icon set,
then `POST`s to `Push to Matrix` / `Clear Matrix` exactly as described below.

If you *are* running the matrix standalone (no Ulanzi clocks), import
`matrix64_aircraft_workflow.json` instead and fill in:
   - `HOME_LAT` / `HOME_LON` — your location's coordinates (placeholders are `0.0` — replace before running)
   - `RADIUS_DEG` — search radius in degrees (default `0.15` ≈ ~16km)
   - `MATRIX_IP` — your ESP32's LAN IP (placeholder `192.168.1.50`)
   - `OPENSKY_CLIENT_ID` / `OPENSKY_CLIENT_SECRET` — from your [OpenSky API client credentials](https://opensky-network.org/apidoc/rest.html#authentication)

Then activate it. It polls every 30 seconds, finds the nearest airborne aircraft, looks up its route and aircraft type, and pushes a display payload to the matrix.

### Payload shape

```json
{
  "flight": "SQ123",
  "airline": "Singapore Airlines",
  "altitudeFt": 35000,
  "originCode": "SIN", "destCode": "KUL",
  "originCity": "Singapore Changi Airport",
  "destCity": "Kuala Lumpur International Airport",
  "icon": "sq_logo", "make": "Airbus", "modelShort": "A388"
}
```

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
| `/` | GET | Admin dashboard -- live preview, status, icon browser, WiFi scan, config, log, reboot, firmware update |
| `/api/status` | GET | JSON status snapshot (uptime, heap/PSRAM, WiFi, icon count, current config, what's showing) |
| `/api/config` | POST | Update hostname / brightness / timezone override / night mode (form-encoded, persisted to NVS). **Requires HTTP Basic Auth** |
| `/api/reboot` | POST | Reboot the board |
| `/api/log` | GET | Tail of the in-memory log ring buffer (plain text) |
| `/api/icons` | GET | JSON array of every icon name currently loaded in PSRAM |
| `/api/icon.bmp?name=<n>` | GET | A single icon's pixels as a BMP (for the admin page's icon grid) |
| `/api/icons/delete?name=<n>` | POST | Remove an icon from PSRAM now; actually deleted from the SD card on next reboot (SD can't be touched while the display's DMA is running -- see "Icons" above) |
| `/api/icons/upload` | POST | Upload a 24x24 raw565 `.bin` (multipart/form-data) -- stages to internal flash (FATFS, no DMA-unsafe window), loads into PSRAM immediately, merged onto the SD card on next reboot. See "Icon upload via the admin console" below |
| `/api/wifi/scan` | GET | JSON list of nearby SSIDs (blocks ~1-2s; the panel's scroll will visibly pause) |
| `/api/ota` | POST | Flash a new firmware `.bin` (multipart/form-data) and reboot into it. **Requires HTTP Basic Auth** -- see "OTA firmware updates" below |

## Admin web console

`http://<board-ip>/` -- a live dashboard, not just a status page: the panel
preview and status numbers auto-refresh via JS polling `/api/status` and
`/debug/screenshot.bmp` (no need to reload the page). From it you can browse
and delete loaded icons, upload new ones, scan for WiFi networks, watch the
live log, edit hostname/brightness/timezone-override, reboot, and flash new
firmware -- all documented in the API table above.

Like every other endpoint on this board, most of the console has **no
authentication** -- it trusts the LAN the same way `/api/display` always
has. That's an explicit, accepted tradeoff for a board that's meant to
never be exposed to the WAN, not an oversight. The two exceptions --
changing config and flashing firmware -- are gated behind HTTP Basic Auth;
see "OTA firmware updates" below for why those two specifically.

### Icon upload via the admin console

Upload a `.bin` produced by `tools/convert_tiles.py` (exactly 1,152 bytes --
24x24 raw565, same format as everything on the SD card) through the Icons
section of `/`. It's staged to a `/pending` directory on the board's
internal flash (FATFS, mounted from the 32MB partition scheme's `ffat`
partition -- see "Arduino IDE setup") rather than the SD card, because
internal flash has no DMA-unsafe window the way SD_MMC does: it can be
written to at any time, including while the display is actively running.

The upload shows on the panel immediately (loaded straight into PSRAM), but
`/pending` is intentionally not the icon's permanent home -- on the *next*
boot, before `preloadIcons()` runs, every file in `/pending` is copied into
SD's `/icons/` (overwriting any existing file of the same name -- last
upload wins) and then deleted from `/pending`. SD stays the single durable
copy of record, same as it's always been for icons that arrive via the R2
remote-sync path; uploaded icons just take one extra boot to actually land
there instead of being written to SD directly.

### OTA firmware updates

The Firmware update section of `/` accepts a compiled `.bin` (Arduino IDE:
**Sketch > Export Compiled Binary**, or `arduino-cli compile --output-dir
<dir>`) and flashes it to the board's spare `ota_0`/`ota_1` app partition
(the 32MB scheme's partitions both being ~4.5MB is what makes this
practical -- see "Arduino IDE setup"), rebooting into it automatically on
success.

Unlike every other endpoint on this board, `/api/ota` (and `/api/config`,
which can change the hostname or force a timezone) require HTTP Basic Auth
-- your browser will prompt once and cache the credentials for the origin.
Set `ADMIN_USER`/`ADMIN_PASSWORD` in `secrets.h` (see `secrets.h.example`);
left unset, both fall back to `admin`/`changeme`, which is fine for a
private LAN but the firmware logs a warning at boot if you're still on it.
Every other endpoint stays unauthenticated, same LAN-only trust model as
always -- these two specifically were judged too risky to leave open (a bad
`/api/display` push shows a wrong flight number; a bad `/api/ota` push
replaces the firmware).

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
