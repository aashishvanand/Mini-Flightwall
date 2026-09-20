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
  02_http_display/        WiFi HTTP Text Display -- bring-up firmware
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
   - **PSRAM**: OPI PSRAM (the icon cache and the off-screen canvas live in PSRAM — required for Aircraft Overhead Display)
   - **Partition Scheme**: a scheme with enough app space for the libraries below (Default 4MB with spiffs is fine for both sketches)
   - **Port**: whichever `/dev/cu.usbmodem*` (macOS) or `COM*` (Windows) appears when the board is plugged in
5. Install libraries via **Tools > Manage Libraries**:
   - `ESP32 HUB75 LED MATRIX PANEL DMA Display` by mrfaptastic
   - `ArduinoJson` by Benoit Blanchon
   - `Adafruit GFX Library` (Aircraft Overhead Display only)
6. Open the `.ino` file for the sketch you're building (Arduino IDE will open the whole sketch folder, including `secrets.h`), hit **Upload**, then open **Tools > Serial Monitor** at 115200 baud to watch it connect to WiFi and print its IP.

Double-check board-specific settings (exact partition scheme, PSRAM mode) against Waveshare's own wiki page for this exact driver board — defaults above worked for this build, but Waveshare occasionally revises recommended settings between firmware/SDK versions.

## Firmware setup

Build in order — don't skip straight to the production firmware without first confirming the bring-up firmware works on your panel/board/power combo.

1. **Panel bring-up test** (not included here): run Waveshare's own stock examples (`01_SimpleTestShapes` / `07_Pixel_Mapping_Test`) with `PANEL_RES_Y` changed to 64, to confirm the panel lights up correctly before writing any custom code.
2. **WiFi HTTP Text Display** — `matrix64/02_http_display`: WiFi + a bare HTTP endpoint that scrolls posted text across the panel. Confirms networking and the double-buffer/flip drawing pattern.
3. **Aircraft Overhead Display** — `matrix64/03_aircraft_display`: the production firmware — preloads airline icons from SD into PSRAM at boot, renders the aircraft layout, and falls back to an NTP clock when idle. Also serves `/debug/screenshot.bmp` so you can see exactly what's on the panel from a browser, no camera needed.

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
pip install pillow
python3 tools/convert_tiles.py --input logos/ --size 24 --format raw565 --output icons
```

This resizes each source image to fit a 24×24 canvas (letterboxed on a black background — the panel's "off" color), converts it to RGB565, and writes `<name>_logo.bin` per source file. See `tools/convert_tiles.py --help` for options.

#### Remote icon sync

New or updated icons reach the board without pulling the SD card and re-flashing it by hand. Icons are hosted as static files on any S3-compatible object storage (or really, anything served over plain HTTPS) — the firmware just does a `GET`, no vendor SDK or API involved:

- **At boot**, right after WiFi connects and before the HUB75 DMA starts (the SD card is unreliable once it's running), `syncIcons(true)` GETs `manifest.json` from `ICON_BASE_URL`, downloads whatever's missing or changed, writes it to `/icons/` on the SD card, and loads it into PSRAM.
- **Every `ICON_SYNC_INTERVAL_MS`** (3 days by default) while running, `loop()` calls `syncIcons(false)` — same manifest diff, but PSRAM-only, since SD can't be touched once the display's DMA is active. Those updates get persisted to SD on the next reboot regardless.

`manifest.json` is a JSON array of `{"name", "sha256"}` — the SHA-256 lets the firmware tell "already have this" apart from "have a file by this name but the art changed," and download only what's actually different.

**Security against a MITM or malicious proxy:** every request validates the server's TLS certificate against the ESP32 core's built-in CA bundle (`setCACertBundle`, not `setInsecure()`) and disables HTTP redirects, so a network attacker can't substitute a different host or downgrade the connection. On top of that, every downloaded icon's SHA-256 is checked against the hash the manifest declared for it — fetched over that same validated connection — before it's written to SD or shown on the panel; a mismatch is dropped silently rather than displayed. This also means a compromised object (even one served with a technically-valid cert) gets caught by the hash check as long as the manifest itself wasn't tampered with in the same request.

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
| `/api/display` | POST | Push a new display payload (JSON) |
| `/api/clear` | POST | Clear the display, fall back to clock (Aircraft Overhead Display) or blank (WiFi HTTP Text Display) |
| `/debug/screenshot.bmp` | GET | Aircraft Overhead Display only — returns the current frame as a BMP |

## License

GPL-3.0 — see [LICENSE](LICENSE).
