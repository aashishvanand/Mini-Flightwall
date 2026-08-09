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

To add or regenerate icons from source logo images (`.webp`, `.png`, `.jpg`):

```bash
pip install pillow
python3 tools/convert_tiles.py --input logos/ --size 24 --format raw565 --output icons
```

This resizes each source image to fit a 24×24 canvas (letterboxed on a black background — the panel's "off" color), converts it to RGB565, and writes `<name>_logo.bin` per source file. See `tools/convert_tiles.py --help` for options.

## n8n workflow setup

1. Import `matrix64_aircraft_workflow.json` into n8n.
2. Open the **Config (edit per location)** node and fill in:
   - `HOME_LAT` / `HOME_LON` — your location's coordinates (placeholders are `0.0` — replace before running)
   - `RADIUS_DEG` — search radius in degrees (default `0.15` ≈ ~16km)
   - `MATRIX_IP` — your ESP32's LAN IP (placeholder `192.168.1.50`)
   - `OPENSKY_CLIENT_ID` / `OPENSKY_CLIENT_SECRET` — from your [OpenSky API client credentials](https://opensky-network.org/apidoc/rest.html#authentication)
3. Activate the workflow. It polls every 30 seconds, finds the nearest airborne aircraft, looks up its route and aircraft type, and pushes a display payload to the matrix.

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
