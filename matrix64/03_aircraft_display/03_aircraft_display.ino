// Aircraft Overhead Display -- production firmware for the Waveshare
// ESP32-S3-RGB-Matrix board driving a 64x64 HUB75 panel.
//
// Run Waveshare's own stock panel examples first (see README "Firmware
// setup") to confirm the panel itself lights up correctly before this --
// this sketch adds a richer JSON payload/layout on top of the same panel
// config and double-buffer/flip pattern validated there.
//
// Dumb display: the board stores no airline icons and has no SD card
// dependency. Every /api/display push from n8n carries the icon's pixels
// inline (iconData: 24x24 little-endian RGB565, base64 -- the n8n
// airline_icons data table's icon24 column), decoded once per push into a
// single buffer. A new or changed logo is just a new table row upstream; the
// firmware never needs to know which airlines exist. (Up to 1.2.x, icons were
// preloaded from SD into PSRAM and synced from a remote manifest -- all of
// that is gone.)
//
// Payload from the n8n workflow's Build Payload (Matrix64) node:
//   {
//     "flight": "SQ123", "airline": "Singapore Airlines",
//     "altitudeFt": 35000, "speedKt": 480, "verticalRateFpm": -650,
//     "originCode": "SIN", "destCode": "KUL", "progress": 0.42,
//     "originCity": "Singapore Changi Airport",
//     "destCity": "Kuala Lumpur International Airport",
//     "icon": "sq_logo", "iconData": "<1536 base64 chars>",
//     "make": "Airbus", "modelShort": "A388", "tail": "9V-SKA"
//   }
// speedKt/verticalRateFpm/progress/iconData are optional -- null (or
// absent) just omits that element, doesn't break the display. "icon" (the
// logo code) is ignored; n8n still sends it for older firmware.
//
// Rendering: everything is drawn into an off-screen GFXcanvas16 (same
// draw calls as the real panel -- setCursor/print/drawRGBBitmap all work
// on it identically), then blitted onto the physical display each frame.
// This also means the exact same frame can be served over HTTP as a BMP
// for debugging (see /debug/screenshot.bmp below) instead of needing to
// photograph the panel.
//
// Scroll policy: every line scrolls ONLY if it doesn't fit its available
// width statically (see scrollIfNeeded() below) -- except the city-name
// line, which always scrolls (full airport names routinely exceed 64px
// even alone, so there's no meaningful "fits" case for it).
//
// Five sections, each 6x8px/char size-1 text, separated by a 2px SECTION_GAP
// (see the layout #defines) so the leftover space on a 64px panel is spent
// as even breathing room instead of one dead gap or an oversized last row:
//
//  y  0- 7: airline name, full width, e.g. "Singapore Airlines"
//  y 10-17: flight number + altitude + climb/descend + speed, full width,
//           e.g. "SQ123   35000ft▲ 480kt" (green ▲ climbing, amber ▼
//           descending -- CP437 glyphs 0x1E/0x1F of the built-in GFX font --
//           omitted within +-200ft/min of level)
//  y 20-43: icon (24x24, left-aligned) with, to its right:
//             y 20: make (manufacturer), e.g. "Airbus"
//             y 28: modelShort (short ICAO type code), e.g. "A320"
//             y 36: tail (registration), e.g. "9V-SMA" -- three 8px rows
//                   exactly fill the icon's 24px; a typical 6-char
//                   registration exactly fills the column's width
//           (airframe age would go here too, but no data source has it --
//           adsbdb's /v0/aircraft/{icao24} has no build-date field)
//  y 46-53: route: origin code flush left, destination flush right, and a
//           track between them with a ► marker at "progress" (share of
//           the trip flown; centered when absent), e.g. "SIN --►.... KUL"
//  y 56-63: origin -> destination city names (ALWAYS scrolling) -- ends
//           exactly at the panel's bottom edge
//
// Libraries required (Arduino Library Manager):
//   - "ESP32 HUB75 LED MATRIX PANEL DMA Display" by mrfaptastic
//   - "ArduinoJson" by Benoit Blanchon
// (WebServer.h, FS.h, FFat.h, Preferences.h all ship with the esp32 core
// -- no install)
//
// Copy secrets.h.example to secrets.h and fill in your WiFi credentials
// before building -- secrets.h is gitignored, never commit real creds.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <time.h>
#include <mbedtls/base64.h>
#include "FS.h"
#include "FFat.h"
#include <Update.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "secrets.h"

// Explicit forward declaration: Arduino's automatic function-prototype
// generation (ctags-based) appears to stop working correctly for the rest
// of the file once it hits setupWatchdog()'s designated-initializer struct
// literal (esp_task_wdt_config_t wdtConfig = { .timeout_ms = ..., ... }) --
// a known ctags limitation with that syntax, confirmed by this exact
// symptom (a "not declared in this scope" error for a function that's
// defined later in the file, the normal case auto-prototyping exists to
// handle). applyBrightnessForTime() is called from handleConfig(), which
// is defined earlier in the file than it is -- hence needing this. If a
// similar error shows up for some other function added after this point,
// the fix is the same: add its prototype here too.
void applyBrightnessForTime();

// Every Serial.print*() call in this file goes through consoleLog instead
// (see the class below) -- same output over USB Serial, plus a ring buffer
// so the admin page's log viewer (GET /api/log) can show the tail of it
// without a separate UART reader. Print's own printf/print/println are
// implemented in terms of write(), so overriding just write() here gets
// every existing call site working via a simple s/Serial\./consoleLog./
// rename, no call-site rewrites needed.
// Bump this on every change you flash (major.minor.patch) -- shown in the
// admin console's footer (see handleStatus()) so it's obvious at a glance
// which build a board is running, and used by /api/ota's downgrade guard
// below (see parseVersion()/isVersionOlder()). Not read from a git tag or
// any build-time codegen -- this is a plain Arduino sketch with no such
// step -- so it's a plain literal you edit by hand alongside the change.
#define FIRMWARE_VERSION "1.3.1"

#define LOG_BUFFER_SIZE 4096
class Logger : public Print {
public:
  void begin(unsigned long baud) { Serial.begin(baud); }
  size_t write(uint8_t c) override {
    buf[head] = c;
    head = (head + 1) % LOG_BUFFER_SIZE;
    if (count < LOG_BUFFER_SIZE) count++;
    return Serial.write(c);
  }
  using Print::write; // pulls in the (const uint8_t*, size_t) overload, which
                       // Print implements as a loop over write(uint8_t) above

  // Returns the buffered log in chronological order (oldest first).
  String getTail() {
    String out;
    out.reserve(count);
    size_t start = (count < LOG_BUFFER_SIZE) ? 0 : head;
    for (size_t i = 0; i < count; i++) {
      out += buf[(start + i) % LOG_BUFFER_SIZE];
    }
    return out;
  }

private:
  char buf[LOG_BUFFER_SIZE];
  size_t head = 0;
  size_t count = 0;
};
Logger consoleLog;

// ESP32 Arduino core's built-in trusted-CA bundle (Mozilla's root store,
// baked into the firmware at build time) -- used instead of
// WiFiClientSecure::setInsecure() so the timezone lookup's HTTPS requests
// get real certificate validation. Requires arduino-esp32 core >=2.0 (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE,
// on by default). NetworkClientSecure::useBuiltinCACertBundle() attaches it
// directly -- no manual symbol/size plumbing needed (core >=3.x removed the
// single-arg setCACertBundle(bundle) overload this used to call).

// Gates POST /api/config and POST /api/ota -- see secrets.h.example for the
// full rationale. Falls back to a well-known default rather than failing
// to compile; a boot-time log line
// (see setupServer()) calls out when the fallback is actually in use so it
// doesn't go unnoticed on a network where that matters.
#ifndef ADMIN_USER
#define ADMIN_USER "admin"
#endif
#ifndef ADMIN_PASSWORD
#define ADMIN_PASSWORD "changeme"
#endif

// Timezone offset from UTC in seconds. This is now resolved automatically at
// boot via IP-geolocation (fetchTimezoneOffset(), called from setupNTP()) --
// no more reflashing just because the board moved to a new HOME_LAT/HOME_LON.
// SGT (UTC+8) is kept only as the fallback if that lookup fails (offline,
// API down, etc.) -- matches the HOME_LAT/HOME_LON in
// matrix64_aircraft_workflow.json as of this build.
#define TIMEZONE_OFFSET_SEC_FALLBACK 28800

#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

// 0 = no rotation, 1 = 90 deg, 2 = 180 deg, 3 = 270 deg
#define DISPLAY_ROTATION 0

#define ICON_SIZE 24
#define ICON_BYTES (ICON_SIZE * ICON_SIZE * 2)

// Layout constants (see header comment above for the row map). All text is
// size-1 (6x8px/char, 8px-tall rows) -- five sections (airline,
// flight/alt, icon block, IATA, city) total 56px of actual content on a
// 64px-tall panel, so the leftover 8px is spent as four 2px gaps between
// sections instead of one dead lump at the bottom (the original layout) or
// stretching the last row to fill it (tried, looked disproportionate next
// to the rest of the 6x8 text -- reverted).
#define SECTION_GAP 2
#define AIRLINE_Y 0
#define FLIGHTALT_Y (AIRLINE_Y + 8 + SECTION_GAP)              // 10
#define ICON_Y (FLIGHTALT_Y + 8 + SECTION_GAP)                 // 20
#define ICON_X 0
#define RIGHT_COL_X (ICON_SIZE + 2)
#define MAKE_Y ICON_Y
#define MODELSHORT_Y (ICON_Y + 8)
#define TAIL_Y (ICON_Y + 16)
#define IATA_Y (ICON_Y + ICON_SIZE + SECTION_GAP)              // 46
#define CITY_Y (IATA_Y + 8 + SECTION_GAP)                      // 56 -- 56+8=64, exactly the panel's bottom edge

#define FULL_WIDTH_AVAIL (PANEL_RES_X - 2)     // full-width rows, 1px margin each side
#define RIGHT_COL_AVAIL (PANEL_RES_X - RIGHT_COL_X - 1) // beside-icon rows

// The one icon currently on screen, decoded from the last push's iconData.
// Plain internal RAM -- it's 1152 bytes, no need for PSRAM now that there's
// no library of hundreds of icons to hold.
uint16_t iconBuf[ICON_SIZE * ICON_SIZE];
bool hasIcon = false;

// Accent colors (RGB565). Shape carries the meaning, color reinforces it --
// the arrows still read at a glance on a dimmed night-mode panel or for a
// color-blind viewer.
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define COLOR_CLIMB RGB565(0x3E, 0xE0, 0x8A)   // green -- same as the TC002 route text
#define COLOR_DESCEND RGB565(0xFF, 0xB0, 0x00) // amber
#define COLOR_ROUTE_MARKER RGB565(0x3E, 0xE0, 0x8A)
#define COLOR_TRACK_FLOWN RGB565(0x60, 0x60, 0x60)
#define COLOR_TRACK_AHEAD RGB565(0x38, 0x38, 0x38)

// CP437 glyphs in Adafruit GFX's built-in 6x8 font (glcdfont.c) -- one byte
// and one 6px cell each, so the length*6 width math everywhere still holds.
#define GLYPH_ROUTE_MARKER '\x10' // right-pointing filled triangle
#define GLYPH_CLIMB '\x1E'        // up-pointing filled triangle
#define GLYPH_DESCEND '\x1F'      // down-pointing filled triangle

MatrixPanel_I2S_DMA *dma_display = nullptr;
GFXcanvas16 canvas(PANEL_RES_X, PANEL_RES_Y);
// Scratch canvas for the beside-icon column (make/modelShort): scrolling
// long text (e.g. "Avions de Transport Régional" for ATR) is drawn into
// this narrower buffer first, then blitted -- so it can never sweep across
// the icon area the way a full-width scroll would.
GFXcanvas16 rightColCanvas(RIGHT_COL_AVAIL, 8);
WebServer server(80);
uint16_t textColor;

// Runtime-configurable settings (Preferences/NVS namespace "mf") -- separate
// from secrets.h, which only ever holds WiFi + admin creds and is
// meant to be edited/reflashed by hand. These are meant to be changed live
// via the admin page (GET/POST /) without a reflash: seeded from sensible
// defaults on first boot, then read back from NVS on every boot after.
Preferences prefs;
String cfgHostname;          // WiFi.setHostname() -- takes effect on next boot
uint8_t cfgBrightness;       // dma_display->setBrightness8() -- live immediately (day/default value)
bool cfgTzOverrideEnabled;   // true = skip fetchTimezoneOffset(), use cfgTzOverrideOffsetSec
long cfgTzOverrideOffsetSec; // manual UTC offset in seconds, only used if the above is true
bool cfgNightModeEnabled;    // true = dim to cfgNightBrightness between cfgNightStartHour/EndHour
uint8_t cfgNightBrightness;  // brightness during the night window
uint8_t cfgNightStartHour;   // 0-23, local time (per the resolved/overridden timezone)
uint8_t cfgNightEndHour;     // 0-23 -- start > end means the window spans midnight (e.g. 23 -> 6)
String cfgNtpServer;         // setupNTP()'s primary NTP server (hostname or bare IP) -- editable
                              // from the Config tab; the secondary ("time.nist.gov") stays fixed as
                              // a hardcoded fallback. Useful when pool.ntp.org is blocked/slow on a
                              // given network, or to point at a LAN NTP server instead.

#define DEFAULT_HOSTNAME "miniflightwall"
#define DEFAULT_BRIGHTNESS 60
#define DEFAULT_NIGHT_BRIGHTNESS 10
#define DEFAULT_NIGHT_START_HOUR 23
#define DEFAULT_NIGHT_END_HOUR 6
#define DEFAULT_NTP_SERVER "pool.ntp.org"

void loadConfig() {
  prefs.begin("mf", false); // false = read/write
  cfgHostname = prefs.getString("hostname", DEFAULT_HOSTNAME);
  cfgBrightness = prefs.getUChar("brightness", DEFAULT_BRIGHTNESS);
  cfgTzOverrideEnabled = prefs.getBool("tzOverride", false);
  cfgTzOverrideOffsetSec = prefs.getLong("tzOffsetSec", TIMEZONE_OFFSET_SEC_FALLBACK);
  cfgNightModeEnabled = prefs.getBool("nightEnabled", false);
  cfgNightBrightness = prefs.getUChar("nightBright", DEFAULT_NIGHT_BRIGHTNESS);
  cfgNightStartHour = prefs.getUChar("nightStart", DEFAULT_NIGHT_START_HOUR);
  cfgNightEndHour = prefs.getUChar("nightEnd", DEFAULT_NIGHT_END_HOUR);
  cfgNtpServer = prefs.getString("ntpServer", DEFAULT_NTP_SERVER);
  // Left behind by 1.2.x's icon sync (base URL, queued SD deletes) -- dead
  // now, cleared so they don't sit in NVS forever. No-ops once gone.
  if (prefs.isKey("iconBaseUrl")) prefs.remove("iconBaseUrl");
  if (prefs.isKey("pendingDel")) prefs.remove("pendingDel");
}

// Conditionally-scrolling lines: static if they fit their row's available
// width, scroll only if they don't. Each needs its own scrollX so an
// in-progress scroll doesn't reset just because another line redraws.
String lineAirline = "";
String lineFlightAlt = "";
String lineMake = "";
String lineModelShort = "";
String lineTail = "";
String lineIata = ""; // "SIN>KUL" -- /api/status, and the route row's fallback if the codes don't leave room for a track
String routeOrigin = "";
String routeDest = "";
float routeProgress = -1; // 0..1 from the payload's "progress"; <0 = unknown (marker centered)
int scrollXAirline = PANEL_RES_X;
int scrollXFlightAlt = PANEL_RES_X;
int scrollXMake = PANEL_RES_X;
int scrollXModelShort = PANEL_RES_X;
int scrollXTail = PANEL_RES_X;
int scrollXIata = PANEL_RES_X;

// Always-scrolling line (city names)
String lineCity = "";
int scrollXCity = PANEL_RES_X;

unsigned long lastScroll = 0;

// Set on every /api/display push; checked in loop() (see STALE_TIMEOUT_MS)
// so a dead n8n workflow, a downed host, or a network blip doesn't leave
// the panel silently showing an aircraft that's long gone with no
// indication anything's wrong. 0 means "no push received yet this boot" --
// deliberately not stale-checked (there's nothing to time out from), the
// idle clock screen just shows normally until the first real push.
unsigned long lastDisplayPush = 0;
#define STALE_TIMEOUT_MS (5UL * 60 * 1000) // 5 minutes -- comfortably longer
                                            // than the workflow's 30s poll
                                            // interval, short enough that a
                                            // real outage doesn't sit
                                            // unnoticed for long

// Parses a "major.minor.patch" string (e.g. FIRMWARE_VERSION, or the
// "version" field /api/ota's uploader declares) into three ints. Returns
// false on anything that doesn't look like exactly that shape -- a missing
// part or non-digit content -- rather than defaulting unparseable parts to
// 0, so isVersionOlder() below can tell "not a version string" apart from
// "an actual older version" and the OTA handler can choose not to block on
// the former (an unparseable declared version isn't evidence of anything).
bool parseVersion(const String &v, int *major, int *minor, int *patch) {
  int firstDot = v.indexOf('.');
  int secondDot = firstDot < 0 ? -1 : v.indexOf('.', firstDot + 1);
  if (firstDot < 0 || secondDot < 0) return false;
  String a = v.substring(0, firstDot);
  String b = v.substring(firstDot + 1, secondDot);
  String c = v.substring(secondDot + 1);
  if (a.length() == 0 || b.length() == 0 || c.length() == 0) return false;
  for (size_t i = 0; i < a.length(); i++) if (!isdigit((unsigned char)a[i])) return false;
  for (size_t i = 0; i < b.length(); i++) if (!isdigit((unsigned char)b[i])) return false;
  for (size_t i = 0; i < c.length(); i++) if (!isdigit((unsigned char)c[i])) return false;
  *major = a.toInt();
  *minor = b.toInt();
  *patch = c.toInt();
  return true;
}

// True only if `candidate` parses as strictly older than `current`. Used by
// /api/ota (see handleOtaData()) to flag a likely-accidental downgrade --
// deliberately fails open (returns false, i.e. "not older, don't block") on
// anything it can't parse, since this is a convenience guard for the
// legitimate admin, not a security boundary (whoever's calling /api/ota
// already has the admin credentials).
bool isVersionOlder(const String &candidate, const String &current) {
  int cMaj, cMin, cPat, curMaj, curMin, curPat;
  if (!parseVersion(candidate, &cMaj, &cMin, &cPat) || !parseVersion(current, &curMaj, &curMin, &curPat)) {
    return false;
  }
  if (cMaj != curMaj) return cMaj < curMaj;
  if (cMin != curMin) return cMin < curMin;
  return cPat < curPat;
}

// ---- Boot diagnostics ----

const char *resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software (ESP.restart())";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout (low voltage)";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
  }
}

// Logged as early as possible, before anything else, so the *previous*
// boot's fate (crash, watchdog reset, brownout, normal restart, ...) is
// visible in /api/log even without physical USB access -- useful now that
// there's a remote log viewer at all.
void logResetReason() {
  consoleLog.printf("Boot: reset reason = %s\n", resetReasonToString(esp_reset_reason()));
}

// ---- OTA rollback safety ----

#define OTA_BOOT_ATTEMPTS_KEY "otaBootTries"
#define OTA_GOOD_PARTITION_KEY "otaGoodPart"
#define OTA_MAX_BOOT_ATTEMPTS 3

// Called as early as possible in setup() (right after loadConfig() opens
// Preferences), before anything that could hang or crash. Arduino/
// arduino-esp32's precompiled bootloader doesn't have ESP-IDF's own
// app-rollback feature enabled -- that's a bootloader/sdkconfig setting
// this build can't toggle from a sketch -- so this reimplements the same
// idea at the application level instead: every boot is assumed failed
// until confirmOtaBootSuccess() (called once setup() has gotten far enough
// to be confident this firmware actually works) says otherwise. If a
// firmware image fails to reach that point OTA_MAX_BOOT_ATTEMPTS times in
// a row -- crashing or hanging before then, every time -- this manually
// points the bootloader at whichever partition last booted successfully
// and reboots into it, recovering from a bad OTA flash with no physical
// access needed.
void checkOtaRollback() {
  int attempts = prefs.getInt(OTA_BOOT_ATTEMPTS_KEY, 0);
  if (attempts >= OTA_MAX_BOOT_ATTEMPTS) {
    String goodLabel = prefs.getString(OTA_GOOD_PARTITION_KEY, "");
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (goodLabel.length() && running && goodLabel != String(running->label)) {
      const esp_partition_t *target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, goodLabel.c_str());
      if (target) {
        consoleLog.printf("OTA rollback: %d failed boots on %s, reverting to %s\n",
                           attempts, running->label, goodLabel.c_str());
        esp_ota_set_boot_partition(target);
        prefs.putInt(OTA_BOOT_ATTEMPTS_KEY, 0);
        delay(500);
        ESP.restart();
      }
    }
  }
  prefs.putInt(OTA_BOOT_ATTEMPTS_KEY, attempts + 1); // assumed-failed until confirmed otherwise
}

// Called once setup() has reached a point that's a reasonable "this
// firmware actually works" signal (matrix initialized, NTP/server set up --
// see the end of setup()) -- resets the failed-boot counter and records
// the current partition as the rollback target for any future bad flash.
// Also called on every ordinary (non-OTA) successful boot, which is
// correct: it just means "otaGoodPart" always reflects whatever partition
// last booted successfully, which is exactly the rollback target wanted.
void confirmOtaBootSuccess() {
  prefs.putInt(OTA_BOOT_ATTEMPTS_KEY, 0);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) prefs.putString(OTA_GOOD_PARTITION_KEY, running->label);
}

// ---- Watchdog ----

#define WDT_TIMEOUT_SEC 30

// If loop() ever stops running for WDT_TIMEOUT_SEC straight -- a hung
// blocking call, a wedged HTTP request, anything -- this reboots the board
// automatically instead of it sitting frozen until someone notices and
// power-cycles it by hand. Fed once per loop() iteration (see loop()
// below); esp_task_wdt_config_t is the current arduino-esp32 3.x API
// (older two-argument esp_task_wdt_init(timeout, panic) was removed).
//
// Deliberately called at the *end* of setup() (see setup() below), not the
// start: setup() itself blocks for a while -- WiFi connect, the timezone/NTP
// HTTPS round-trips, a first-boot FATFS format -- and nothing feeds the
// watchdog until loop() starts. Watching from the top of setup() would
// treat that normal startup time as a hang, tripping a false reset (and,
// combined with checkOtaRollback()'s failed-boot counter, a false OTA
// rollback on nothing worse than a slow WiFi connect).
void setupWatchdog() {
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_err_t err = esp_task_wdt_init(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE) {
    // The core already initialized the task WDT itself (CONFIG_ESP_TASK_WDT_INIT)
    // before this ever ran -- esp_task_wdt_init() refuses to re-init an
    // already-running watchdog, which would otherwise silently leave
    // whatever timeout the core defaulted to (5s is a common default,
    // shorter than WDT_TIMEOUT_SEC) instead of the one configured here.
    // Reconfigure the existing instance instead.
    err = esp_task_wdt_reconfigure(&wdtConfig);
  }
  if (err != ESP_OK) {
    consoleLog.printf("Watchdog init/reconfigure failed (esp_err_t %d) -- no auto-recovery from a hang this boot\n", err);
  }
  esp_task_wdt_add(NULL); // watch the current (Arduino loop) task
}

// Mounts FATFS on the internal flash partition (see the 32MB partition
// scheme in README "Arduino IDE setup"). No DMA-unsafe window -- it's the
// same flash chip the firmware itself runs from -- so it's mounted early and
// can be written to anytime,
// including while the display is live. `true` formats it automatically on
// a mount failure, which is expected and harmless on first-ever boot (a
// fresh partition has no filesystem on it yet).
void setupFatFS() {
  // On a virgin ffat partition (first boot after flashing the 32MB
  // partition scheme, or after any full-chip erase), the mount fails once
  // and FFat.begin(true) formats it -- ~22MB, which takes long enough that
  // without this line the board looks hung rather than just busy.
  consoleLog.println("Mounting FATFS (formatting if this is the first boot on this partition -- may take a while)...");
  if (!FFat.begin(true)) {
    consoleLog.println("FATFS mount failed");
  } else {
    consoleLog.println("FATFS mounted");
  }
}

void setupMatrix() {
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  // Confirmed working config for this board+panel, arrived at via Waveshare's
  // stock panel examples (gpio.e is the row-address line; without it, content
  // straddling the row-32 boundary splits into two bands).
  mxconfig.gpio.e = 9;
  mxconfig.clkphase = false;
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;
  mxconfig.double_buff = true;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setRotation(DISPLAY_ROTATION);
  dma_display->setBrightness8(cfgBrightness);
  dma_display->clearScreen();
  textColor = 0xFFFF; // white, RGB565
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfgHostname.c_str()); // must be set before begin() to take effect
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  consoleLog.printf("Connecting to %s", WIFI_SSID);
  // Bounded, not infinite: setupWatchdog() runs before this (see setup()),
  // and an AP that's simply down would otherwise spin here forever with
  // nothing feeding the watchdog -- exactly the kind of hang it exists to
  // catch. 30s (matches WDT_TIMEOUT_SEC) gives WiFi a real chance to
  // connect while still guaranteeing this function returns either way;
  // the periodic reconnect check in loop() keeps trying afterward if
  // this attempt timed out.
  unsigned long wifiDeadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) {
    delay(300);
    consoleLog.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    consoleLog.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
    lineCity = "READY " + WiFi.localIP().toString();
    scrollXCity = PANEL_RES_X;
  } else {
    consoleLog.println("\nWiFi connect timed out after 30s -- continuing boot, loop() will keep retrying");
  }
}

// Resolves the UTC offset for wherever the board's WAN IP currently
// geolocates to -- no lat/lon needed, no API key, and it means moving the
// board to a new city no longer means editing a #define and reflashing.
// TLS validated against the ESP32 core's built-in CA bundle (see
// useBuiltinCACertBundle() above), not setInsecure().
// Returns false (leaving *offsetSec untouched) on any failure -- network
// down, an API down/rate-limited, unexpected JSON shape -- so the caller
// can fall back to TIMEZONE_OFFSET_SEC_FALLBACK.
//
// Two hops, both to timeapi.io (previously worldtimeapi.org/api/ip -- a
// single hop, but a small hobby-run service with a documented history of
// extended outages; confirmed down, TLS handshake hanging, while building
// this):
//   1. api.ipify.org -- this board's own public IP as a bare text body (no
//      JSON, no key). timeapi.io's IP-lookup endpoint, unlike
//      worldtimeapi.org's, doesn't auto-detect the caller's IP -- it needs
//      one passed explicitly, hence this first hop.
//   2. timeapi.io/api/timezone/ip?ipAddress=<that ip> -- currentUtcOffset
//      already has any active DST shift folded in (distinct from
//      standardUtcOffset, which doesn't), so this is one field, not two to
//      sum, and configTime() below doesn't need Timezone.h's POSIX-TZ DST
//      rule parsing.
//
// Note: this only runs once at boot, same as the rest of setupNTP() before
// it. A board left running for months across a DST transition (most of the
// world outside Singapore) won't pick that transition up until its next
// reboot -- add a periodic re-call from loop() (e.g. once/day, same
// millis()-interval pattern as lastBrightnessCheck) if that matters for your
// location.
bool fetchTimezoneOffset(long *offsetSec) {
  String publicIp;
  {
    WiFiClientSecure client;
    client.useBuiltinCACertBundle();
    HTTPClient http;
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, "https://api.ipify.org")) {
      consoleLog.println("Timezone lookup: failed to start ipify request");
      return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      consoleLog.printf("Timezone lookup: ipify HTTP %d\n", code);
      http.end();
      return false;
    }
    publicIp = http.getString();
    http.end();
    publicIp.trim();
    if (publicIp.length() == 0) {
      consoleLog.println("Timezone lookup: empty IP from ipify");
      return false;
    }
  }

  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  String url = "https://timeapi.io/api/timezone/ip?ipAddress=" + publicIp;
  if (!http.begin(client, url)) {
    consoleLog.println("Timezone lookup: failed to start timeapi.io request");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    consoleLog.printf("Timezone lookup: timeapi.io HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    consoleLog.printf("Timezone lookup: bad JSON (%s)\n", err.c_str());
    return false;
  }

  if (!doc["currentUtcOffset"]["seconds"].is<long>()) {
    consoleLog.println("Timezone lookup: response missing currentUtcOffset.seconds");
    return false;
  }
  *offsetSec = doc["currentUtcOffset"]["seconds"];
  const char *tzName = doc["timeZone"] | "unknown";
  consoleLog.printf("Timezone lookup: %s, UTC%+ld:%02ld\n", tzName, *offsetSec / 3600, labs(*offsetSec / 60) % 60);
  return true;
}

// Called once at boot, after WiFi is up. NTP sync happens in the background;
// the clock screen (drawClock) just shows "SYNCING..." until getLocalTime()
// first succeeds, so we don't need to block setup() waiting for it.
void setupNTP() {
  long offsetSec = TIMEZONE_OFFSET_SEC_FALLBACK;
  if (cfgTzOverrideEnabled) {
    // Manual override set via the admin page (POST /api/config) -- skip the
    // IP-geolocation lookup entirely rather than have it silently win a race
    // against a value the user explicitly chose.
    offsetSec = cfgTzOverrideOffsetSec;
    consoleLog.printf("Timezone: manual override, UTC%+ld:%02ld\n", offsetSec / 3600, labs(offsetSec / 60) % 60);
  } else if (!fetchTimezoneOffset(&offsetSec)) {
    consoleLog.printf("Timezone lookup failed -- falling back to UTC%+ld:%02ld\n",
                   TIMEZONE_OFFSET_SEC_FALLBACK / 3600, (TIMEZONE_OFFSET_SEC_FALLBACK / 60) % 60);
    offsetSec = TIMEZONE_OFFSET_SEC_FALLBACK;
  }
  // dst_offset is already folded into offsetSec above (or n/a on the
  // fallback path), so daylightOffset is always 0 here.
  // cfgNtpServer is the admin-configurable primary server (Config tab,
  // validated by isPlausibleNtpServer()); "time.nist.gov" stays a fixed
  // secondary regardless, so a bad/unreachable primary still has a
  // fallback instead of leaving the clock unsynced entirely.
  configTime(offsetSec, 0, cfgNtpServer.c_str(), "time.nist.gov");
  // configTime() only starts the SNTP exchange -- it returns immediately,
  // before any reply has actually arrived over UDP. Anything TLS-validated
  // after this (WiFiClientSecure's cert-validity check reads the system
  // clock) would "fail" with a generic HTTP -1 while it's still stuck at
  // the Unix epoch. Block
  // briefly for the clock to actually cross a sane recent date -- 10s is
  // comfortably more than NTP normally takes on a healthy connection --
  // rather than just logging that configTime() was *called*.
  time_t now = time(nullptr);
  unsigned long waitStart = millis();
  const time_t SANE_EPOCH = 1700000000; // 2023-11-14 -- just needs to be well past 1970
  while (now < SANE_EPOCH && millis() - waitStart < 10000) {
    delay(200);
    now = time(nullptr);
  }
  if (now < SANE_EPOCH) {
    consoleLog.println("NTP: time did not sync within 10s -- the clock screen will show SYNCING... until it does");
  } else {
    consoleLog.println("NTP configured");
  }
}

void resetAllScrollPositions() {
  scrollXAirline = PANEL_RES_X;
  scrollXFlightAlt = PANEL_RES_X;
  // These two scroll within the narrower rightColCanvas, not the full
  // panel, so they should re-enter from just off ITS right edge.
  scrollXMake = RIGHT_COL_AVAIL;
  scrollXModelShort = RIGHT_COL_AVAIL;
  scrollXTail = RIGHT_COL_AVAIL;
  scrollXIata = PANEL_RES_X;
  scrollXCity = PANEL_RES_X;
}

// Decodes the push's iconData (24x24 little-endian RGB565, base64) straight
// into iconBuf. mbedtls refuses to write past ICON_BYTES (anything that
// decodes larger is MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL), and hasIcon is
// only set for an exact ICON_BYTES result -- so truncated, oversized or
// garbage data blanks the icon slot instead of showing part of one.
void loadIcon(const char *b64) {
  hasIcon = false;
  if (!b64 || !*b64) return;
  size_t outLen = 0;
  int rc = mbedtls_base64_decode((unsigned char *)iconBuf, ICON_BYTES, &outLen,
                                 (const unsigned char *)b64, strlen(b64));
  if (rc == 0 && outLen == ICON_BYTES) {
    hasIcon = true;
  } else {
    consoleLog.printf("Push: bad iconData (mbedtls rc %d, %u bytes, expected %d) -- icon area will be blank\n",
                      rc, (unsigned)outLen, ICON_BYTES);
  }
}

void handleDisplay() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"use POST\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }

  String flight = doc["flight"] | "";
  loadIcon(doc["iconData"].as<const char *>());
  lineAirline = doc["airline"] | "";
  lineMake = doc["make"] | "";
  lineModelShort = doc["modelShort"] | "";
  lineTail = doc["tail"] | "";
  String originCode = doc["originCode"] | "";
  String destCode = doc["destCode"] | "";
  String originCity = doc["originCity"] | "";
  String destCity = doc["destCity"] | "";
  long altitudeFt = doc["altitudeFt"] | -1;
  long speedKt = doc["speedKt"] | -1;
  // A real vertical rate can legitimately be 0 (level flight), so unlike
  // altitudeFt/speedKt above this can't use a "-1 means missing" sentinel --
  // check the JSON type directly instead.
  bool hasVerticalRate = doc["verticalRateFpm"].is<long>();
  long verticalRateFpm = hasVerticalRate ? (long)doc["verticalRateFpm"] : 0;

  lineFlightAlt = flight;
  if (altitudeFt >= 0) {
    if (lineFlightAlt.length()) lineFlightAlt += "   ";
    lineFlightAlt += String(altitudeFt) + "ft";
    // Climb/descend indicator: a filled triangle glyph (drawn green/amber
    // by printColored()), omitted within a +-200ft/min band of level
    // (cruise-noise threshold, not literally 0 -- a level cruise still
    // jitters a little).
    if (hasVerticalRate) {
      if (verticalRateFpm > 200) lineFlightAlt += GLYPH_CLIMB;
      else if (verticalRateFpm < -200) lineFlightAlt += GLYPH_DESCEND;
    }
  }
  if (speedKt >= 0) {
    if (lineFlightAlt.length()) lineFlightAlt += " ";
    lineFlightAlt += String(speedKt) + "kt";
  }

  bool haveRoute = originCode.length() && destCode.length();
  lineIata = haveRoute ? (originCode + ">" + destCode) : "";
  routeOrigin = haveRoute ? originCode : "";
  routeDest = haveRoute ? destCode : "";
  routeProgress = doc["progress"].is<float>() ? constrain(doc["progress"].as<float>(), 0.0f, 1.0f) : -1;
  lineCity = (originCity.length() && destCity.length()) ? (originCity + " -> " + destCity) : "";

  if (lineAirline.length() == 0 && lineFlightAlt.length() == 0 && lineCity.length() == 0) {
    lineFlightAlt = flight; // fallback if the payload was nearly empty
  }

  lastDisplayPush = millis(); // see STALE_TIMEOUT_MS in loop()
  resetAllScrollPositions();
  server.send(200, "application/json", "{\"ok\":true}");
}

// Resets aircraft-display state back to "nothing to show" (falls back to
// the clock). Shared by handleClear() (explicit POST /api/clear) and the
// staleness check in loop() (no push received in STALE_TIMEOUT_MS) -- same
// end state, two different triggers.
void clearAircraftState() {
  hasIcon = false;
  routeOrigin = "";
  routeDest = "";
  routeProgress = -1;
  lineAirline = "";
  lineFlightAlt = "";
  lineMake = "";
  lineModelShort = "";
  lineTail = "";
  lineIata = "";
  lineCity = "";
}

void handleClear() {
  // A clear is proof n8n is alive and reachable, same as a real /api/display
  // push -- the field's actual meaning is "last contact with the workflow,"
  // not "last aircraft shown." Without this, a routine quiet period (no
  // aircraft in range -> n8n calls /api/clear, not /api/display) would
  // falsely trip the staleness check in loop() after STALE_TIMEOUT_MS even
  // though the pipeline is working exactly as intended, and then
  // handleStatus() would misreport "none yet this boot" on a healthy board.
  lastDisplayPush = millis();
  clearAircraftState();
  canvas.fillScreen(0);
  dma_display->flipDMABuffer();
  delay(1000 / dma_display->calculated_refresh_rate);
  dma_display->drawRGBBitmap(0, 0, canvas.getBuffer(), PANEL_RES_X, PANEL_RES_Y);
  server.send(200, "application/json", "{\"ok\":true}");
}

// Encodes a w x h RGB565 buffer as a 24bpp BMP (caller free()s the result).
// Returns nullptr on a malloc failure or a row width that isn't a multiple
// of 4 bytes (BMP's row-padding requirement; the 64px canvas already is, so
// this never actually trips, but it's checked rather than assumed).
uint8_t *encodeBmp(const uint16_t *pixels, int w, int h, size_t *outSize) {
  const int rowBytes = w * 3; // 24bpp
  if (rowBytes % 4 != 0) return nullptr;
  const int pixelDataSize = rowBytes * h;
  const int fileSize = 54 + pixelDataSize;

  uint8_t *bmp = (uint8_t *)malloc(fileSize);
  if (!bmp) return nullptr;

  auto putLE16 = [](uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; };
  auto putLE32 = [](uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
  };

  memset(bmp, 0, 54);
  bmp[0] = 'B'; bmp[1] = 'M';
  putLE32(bmp + 2, fileSize);
  putLE32(bmp + 10, 54); // pixel data offset
  putLE32(bmp + 14, 40); // DIB header size
  putLE32(bmp + 18, w);
  putLE32(bmp + 22, h); // positive height = bottom-up row order
  putLE16(bmp + 26, 1); // planes
  putLE16(bmp + 28, 24); // bits per pixel
  putLE32(bmp + 34, pixelDataSize);

  uint8_t *px = bmp + 54;
  for (int y = h - 1; y >= 0; y--) { // BMP rows are stored bottom-up
    for (int x = 0; x < w; x++) {
      uint16_t c = pixels[y * w + x];
      uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (c & 0x1F) * 255 / 31;
      *px++ = b; *px++ = g; *px++ = r; // BMP is BGR
    }
  }

  *outSize = fileSize;
  return bmp;
}

void sendBmp(const uint16_t *pixels, int w, int h) {
  size_t fileSize;
  uint8_t *bmp = encodeBmp(pixels, w, h, &fileSize);
  if (!bmp) {
    server.send(500, "text/plain", "out of memory");
    return;
  }
  server.sendHeader("Content-Type", "image/bmp");
  server.setContentLength(fileSize);
  server.send(200);
  server.sendContent((const char *)bmp, fileSize);
  free(bmp);
}

// Serves the exact same frame currently drawn into `canvas` as a BMP --
// open http://<board-ip>/debug/screenshot.bmp in a browser to see exactly
// what's on the panel, no photographing needed.
void handleScreenshot() {
  sendBmp(canvas.getBuffer(), PANEL_RES_X, PANEL_RES_Y);
}

// POST /api/ota (multipart/form-data, one file field: a compiled .bin from
// Arduino IDE's Sketch > Export Compiled Binary, or `arduino-cli compile
// --output-dir`) -- flashes it to the inactive ota_0/ota_1 app slot (see
// README "Arduino IDE setup" for the 32MB partition scheme this needs) and
// reboots into it on success. Gated by requireAuth() (see above) -- the
// one endpoint on this board where "anyone on the LAN can hit it" was
// judged too risky even under this project's usual no-auth convention (a
// bad /api/display push shows a wrong flight number; a bad /api/ota push
// replaces the firmware).
bool otaHasError = false;
// Auth is checked in UPLOAD_FILE_START below (headers, including
// Authorization, always arrive before the multipart body), but the actual
// 401 response has to be sent from handleOtaDone() instead of from here --
// calling server.send()/requestAuthentication() mid-multipart-parse (while
// this upload callback is still being invoked) isn't a state the WebServer
// library's request lifecycle supports safely. So this just remembers the
// verdict and lets the (harmlessly discarded) body finish streaming through.
bool otaUnauthorized = false;
// Set instead of a generic otaHasError when the block was specifically a
// downgrade guard trip (see below) -- handleOtaDone() uses this to send a
// distinct 409 the admin console's FirmwareUpdate.jsx can recognize and
// offer a "yes, downgrade anyway" resubmit for, instead of a flat failure.
bool otaDowngradeBlocked = false;
// Overrides the generic "update failed, see /api/log" message in
// handleOtaDone() when set -- used for the downgrade message specifically,
// which needs to say what it's actually blocking.
String otaErrorMessage = "";

void handleOtaData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaHasError = false;
    otaDowngradeBlocked = false;
    otaErrorMessage = "";
    otaUnauthorized = !server.authenticate(ADMIN_USER, ADMIN_PASSWORD);
    if (otaUnauthorized) {
      otaHasError = true; // also short-circuits the WRITE/END branches below
      consoleLog.println("OTA: rejected (bad or missing credentials)");
      return;
    }

    // Downgrade guard: FirmwareUpdate.jsx sends the .bin's own declared
    // version as a "version" form field, appended to the FormData *before*
    // the file field so it's already parsed into server.arg() by the time
    // this file part starts arriving (ESP32 WebServer's multipart parser
    // processes parts in request order). A missing or unparseable version
    // doesn't block -- a bare curl upload or an older UI build simply won't
    // send one, and that's not evidence of anything -- but a version that
    // parses as strictly older than what's currently running does, unless
    // the request also sets confirmDowngrade=1 (the UI's second, explicit
    // "yes, really downgrade" dialog). This is a convenience guard against
    // an accidental stale-.bin upload, not a security boundary: whoever's
    // calling this endpoint at all already has the admin credentials.
    String declaredVersion = server.arg("version");
    bool confirmDowngrade = server.arg("confirmDowngrade") == "1";
    if (declaredVersion.length() && isVersionOlder(declaredVersion, FIRMWARE_VERSION) && !confirmDowngrade) {
      otaHasError = true;
      otaDowngradeBlocked = true;
      otaErrorMessage = "downgrade blocked: board is on " + String(FIRMWARE_VERSION) +
                         ", upload is " + declaredVersion;
      consoleLog.printf("OTA: rejected downgrade attempt (%s -> %s)\n", FIRMWARE_VERSION, declaredVersion.c_str());
      return;
    }

    consoleLog.printf("OTA: starting update (%s, declared version %s)\n", upload.filename.c_str(),
                       declaredVersion.length() ? declaredVersion.c_str() : "unknown");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(consoleLog);
      otaHasError = true;
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaHasError && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(consoleLog);
      otaHasError = true;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaHasError) {
      if (Update.end(true)) {
        consoleLog.printf("OTA: wrote %u bytes, update complete\n", (unsigned)upload.totalSize);
      } else {
        Update.printError(consoleLog);
        otaHasError = true;
      }
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaHasError = true;
  }
}

void handleOtaDone() {
  if (otaUnauthorized) {
    server.requestAuthentication();
    return;
  }
  if (otaDowngradeBlocked) {
    server.send(409, "application/json", "{\"ok\":false,\"downgrade\":true,\"error\":\"" + otaErrorMessage + "\"}");
    return;
  }
  if (otaHasError) {
    String msg = otaErrorMessage.length() ? otaErrorMessage : "update failed, see /api/log";
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + msg + "\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true,\"note\":\"flashed -- rebooting into new firmware\"}");
  delay(500); // let the response actually flush before the restart
  ESP.restart();
}

// GET /api/log -- the consoleLog ring buffer's contents (plain text, tail
// end is most recent).
void handleLog() {
  server.send(200, "text/plain", consoleLog.getTail());
}

// GET /api/wifi/scan -- JSON array of nearby SSIDs. WiFi.scanNetworks()
// blocks for ~1-2s, which will visibly stall the panel's scroll animation
// for that long (loop() can't run while this handler is blocked) -- an
// acceptable tradeoff for an admin action you trigger manually and
// infrequently, not something to run on a timer. Gated like /api/config and
// /api/reboot: unlike /api/display or /api/clear (worst case, a wrong
// flight number shows), an unauthenticated version of this is a free,
// repeatable panel-stalling DoS for anyone on the LAN.
void handleWifiScan() {
  if (!requireAuth()) return;
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject net = arr.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// GET /api/status -- JSON status snapshot, polled by the admin page's JS to
// keep the numbers live without a full page reload.
void handleStatus() {
  JsonDocument doc;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["uptimeSec"] = millis() / 1000;
  doc["secondsSinceLastPush"] = lastDisplayPush == 0 ? -1 : (long)((millis() - lastDisplayPush) / 1000);
  doc["freeHeapKB"] = ESP.getFreeHeap() / 1024;
  doc["freePsramKB"] = ESP.getFreePsram() / 1024;
  doc["wifiSsid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["iconShown"] = hasIcon;
  doc["brightness"] = cfgBrightness;
  doc["hostname"] = cfgHostname;
  doc["timezone"] = cfgTzOverrideEnabled ? "manual override" : "auto (IP-geolocation at boot)";
  // Raw config values (as opposed to the display-formatted "timezone" field
  // above) -- the admin console's Config tab uses these to pre-fill its
  // form on load (see ConfigForm.jsx's one-shot hydration).
  doc["tzOverrideEnabled"] = cfgTzOverrideEnabled;
  doc["tzOffsetSec"] = cfgTzOverrideEnabled ? cfgTzOverrideOffsetSec : TIMEZONE_OFFSET_SEC_FALLBACK;
  doc["nightModeEnabled"] = cfgNightModeEnabled;
  doc["nightBrightness"] = cfgNightBrightness;
  doc["nightStartHour"] = cfgNightStartHour;
  doc["nightEndHour"] = cfgNightEndHour;
  doc["ntpServer"] = cfgNtpServer;
  // Surfaced in the admin console so the default-credentials warning isn't
  // serial-log-only (see setupServer()'s boot-time log line) -- most people
  // never look at USB serial once a board is deployed, so that log line
  // alone is easy to miss forever.
  doc["usingDefaultAdminCreds"] = (strcmp(ADMIN_PASSWORD, "changeme") == 0);
  // The climb/descend glyphs are raw control bytes (0x1E/0x1F) -- mapped
  // back to ASCII here so the admin console doesn't render them as boxes.
  String nowShowing = lineFlightAlt;
  nowShowing.replace(String(GLYPH_CLIMB), "^");
  nowShowing.replace(String(GLYPH_DESCEND), "v");
  doc["nowShowing"] = nowShowing.length() ? nowShowing : "(clock)";
  doc["route"] = lineIata.length() ? lineIata : "";
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Parses a UTC offset string like "+8:00", "-5:30", "8", "+05:45" into
// seconds. Returns false (leaving *outSec untouched) on anything that
// doesn't look like an offset -- used by handleConfig() to validate the
// admin page's manual timezone-override field before writing it to NVS.
bool parseUtcOffset(const String &s, long *outSec) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;

  bool negative = false;
  int i = 0;
  if (t[0] == '+' || t[0] == '-') {
    negative = (t[0] == '-');
    i = 1;
  }

  int colon = t.indexOf(':', i);
  String hourStr = colon >= 0 ? t.substring(i, colon) : t.substring(i);
  String minStr = colon >= 0 ? t.substring(colon + 1) : "0";
  if (hourStr.length() == 0) return false;
  for (size_t k = 0; k < hourStr.length(); k++) if (!isdigit((unsigned char)hourStr[k])) return false;
  for (size_t k = 0; k < minStr.length(); k++) if (!isdigit((unsigned char)minStr[k])) return false;

  long hours = hourStr.toInt();
  long mins = minStr.toInt();
  if (hours > 14 || mins > 59) return false; // UTC-12 .. UTC+14 covers every real timezone

  long total = hours * 3600 + mins * 60;
  *outSec = negative ? -total : total;
  return true;
}

// True if `s` looks like a bare hostname or IP literal suitable for
// configTime()'s server argument -- NOT a URL. configTime() does no
// validation of its own, so a pasted "https://pool.ntp.org/" or an empty
// string would silently become a bogus NTP server that just never
// resolves/replies, with no error until you notice the clock never syncs.
// Used by handleConfig() to validate the admin page's NTP server field
// before writing it to NVS, same pattern as parseUtcOffset() above.
bool isPlausibleNtpServer(const String &s) {
  if (s.length() == 0 || s.length() > 63) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':';
    if (!ok) return false;
  }
  return true;
}

// ---- Admin console static assets (React/MUI SPA, built separately under
// web/ and pushed to the board with tools/deploy_web.py) ----
//
// Unlike v1/v2 (a template string baked into the firmware image), the
// admin console now lives as pre-gzipped static files on the FATFS
// partition under /web/ (see setupFatFS() -- this partition has no
// DMA-unsafe window, so it's safe to read on every request even while the
// display is running). Keeping the UI off the firmware image means it can
// be redeployed without a full OTA reflash, and there's no meaningful
// firmware-size budget concern with 22MB of FATFS to work with.
//
// Every request this handler serves is looked up as its own path with a
// literal ".gz" suffix on FATFS (deploy_web.py gzips the whole build
// output before uploading) -- e.g. GET / -> /web/index.html.gz,
// GET /assets/index-XYZ.js -> /web/assets/index-XYZ.js.gz. There's no SPA
// client-side routing to fall back to index.html for: this is a single
// page with no route changes, so a missing file is just a 404.
const char *mimeTypeFor(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".json")) return "application/json";
  // Deliberately not "application/octet-stream": _streamFileCore() (the
  // ESP32 WebServer core) skips the Content-Encoding: gzip header for that
  // exact content type, and every file this handler serves is gzipped on
  // disk -- an unmapped extension falling back to octet-stream would be
  // served as raw gzip bytes with no header saying so, which the browser
  // can't render. "text/plain" keeps the gzip header without needing a
  // real guess at the type.
  return "text/plain";
}

void handleWebStatic() {
  String path = server.uri();
  if (path == "/") path = "/index.html";
  String fatPath = "/web" + path + ".gz";

  if (!FFat.exists(fatPath)) {
    server.send(404, "text/plain", "not found -- has the admin console been deployed? see tools/deploy_web.py");
    return;
  }

  File f = FFat.open(fatPath, FILE_READ);
  if (!f) {
    // FFat.exists() said yes, but the open still failed (e.g. a zero-length
    // file left behind by a deploy that was interrupted mid-upload) --
    // don't hand a File in an unknown state to streamFile().
    server.send(500, "text/plain", "failed to open " + fatPath);
    return;
  }
  // streamFile() (WebServer.cpp's _streamFileCore()) already adds
  // Content-Encoding: gzip itself whenever the File's name ends in ".gz"
  // and the given contentType isn't gzip/octet-stream -- which is every
  // request this handler serves. Don't also sendHeader() it here: headers
  // aren't deduped, so the response would carry "gzip, gzip" and the
  // browser would fail to decode it (ERR_CONTENT_DECODING_FAILED).
  // mimeTypeFor() is passed the un-suffixed `path`, not `fatPath`, so the
  // Content-Type itself still comes out correct (e.g. "application/javascript").
  server.streamFile(f, mimeTypeFor(path));
  f.close();
}

// Recursively deletes everything under dirPath (FATFS has real
// subdirectories, unlike SPIFFS, so this has to recurse rather than just
// listing one level -- web/assets/ is one level deep in practice, but this
// doesn't assume that). Collects paths up front rather than deleting while
// the directory handle from openNextFile() is still walking it: removing an
// entry mid-iteration can skip the next one or corrupt the FAT directory
// cursor.
//
// The 64/16 arrays cap how much one pass can hold (kept small deliberately
// -- each stack frame here is ~1.3KB against the ~8KB loop-task stack). If
// a directory has
// more entries than that, this recurses on itself to sweep the remainder
// instead of silently leaving them behind.
void deleteRecursive(const String &dirPath) {
  File dir = FFat.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  String files[64];
  String dirs[16];
  int nFiles = 0, nDirs = 0;
  bool truncated = false;

  File f = dir.openNextFile();
  while (f) {
    if (f.isDirectory()) {
      if (nDirs < 16) dirs[nDirs++] = String(f.path());
      else truncated = true;
    } else {
      if (nFiles < 64) files[nFiles++] = String(f.path());
      else truncated = true;
    }
    f = dir.openNextFile();
  }
  dir.close();

  for (int i = 0; i < nFiles; i++) FFat.remove(files[i]);
  for (int i = 0; i < nDirs; i++) {
    deleteRecursive(dirs[i]);
    FFat.rmdir(dirs[i]);
  }

  // Only recurse if this pass actually made progress -- otherwise a
  // FFat.remove() that keeps failing on the same entries would recurse
  // forever (the watchdog would eventually catch it, but there's no reason
  // to rely on that).
  if (truncated && nFiles > 0) deleteRecursive(dirPath);
}

// POST /api/web/clear -- wipes /web/ before a fresh deploy. Needed because
// Vite hashes output filenames per build (index-XYZ123.js); without this,
// every redeploy would leave the previous build's assets behind forever.
// Gated the same as /api/ota -- arbitrary flash writes are exactly the
// kind of endpoint this board's usual no-auth convention excludes.
void handleWebClear() {
  if (!requireAuth()) return;
  deleteRecursive("/web");
  server.send(200, "application/json", "{\"ok\":true}");
}

// State shared between handleWebUploadData()/handleWebUploadDone(), same
// pattern as the OTA upload above -- the WebServer upload API has no
// per-request context object.
File webUploadFile;
String webUploadError;
bool webUploadStarted = false;
bool webUploadUnauthorized = false;

// POST /api/web/upload (multipart, field "file", uploaded with its
// filename set to the relative target path, e.g.
// "assets/index-XYZ.js.gz") -- deploy_web.py's per-file upload call. The
// resulting /web/<filename> path must stay under /web/ (checked below) so
// this can't be used to overwrite anything else on the partition, e.g. the
// FAT filesystem's own internals.
void handleWebUploadData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    webUploadStarted = true;
    webUploadError = "";
    webUploadUnauthorized = !server.authenticate(ADMIN_USER, ADMIN_PASSWORD);
    if (webUploadUnauthorized) return;

    // The target path travels as the multipart field's filename (e.g.
    // "assets/index-XYZ.js.gz"), not a ?path= query arg -- during a
    // multipart upload, server.arg() reads the multipart form fields, not
    // the URL query string.
    String path = "/web/" + upload.filename;
    // The "/web/" prefix is guaranteed by construction above -- ".." is the
    // actual containment check, blocking a filename like "../../secrets.h"
    // from escaping this directory.
    if (path.indexOf("..") >= 0) {
      webUploadError = "invalid path";
      return;
    }

    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
      String dir = path.substring(0, lastSlash);
      // mkdir one level at a time -- FFat.mkdir() (like most FAT
      // implementations) doesn't create intermediate directories.
      int from = 1;
      while (true) {
        int slash = dir.indexOf('/', from);
        String prefix = slash < 0 ? dir : dir.substring(0, slash);
        if (!FFat.exists(prefix)) FFat.mkdir(prefix);
        if (slash < 0) break;
        from = slash + 1;
      }
    }

    webUploadFile = FFat.open(path, FILE_WRITE);
    if (!webUploadFile) webUploadError = "failed to open " + path;

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!webUploadUnauthorized && webUploadFile) webUploadFile.write(upload.buf, upload.currentSize);

  } else if (upload.status == UPLOAD_FILE_END) {
    if (webUploadFile) webUploadFile.close();

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (webUploadFile) webUploadFile.close();
    webUploadError = "upload aborted";
  }
}

void handleWebUploadDone() {
  if (webUploadUnauthorized) {
    server.requestAuthentication();
    webUploadUnauthorized = false;
    webUploadStarted = false; // clear regardless of outcome -- a stale true would misreport the next request
    return;
  }
  if (!webUploadStarted) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no file received\"}");
    return;
  }
  webUploadStarted = false;
  if (webUploadError.length()) {
    String err = webUploadError;
    webUploadError = "";
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + err + "\"}");
  } else {
    server.send(200, "application/json", "{\"ok\":true}");
  }
}

// Gates POST /api/config, POST /api/reboot, GET /api/wifi/scan, and (via the
// two-phase check in handleOtaData(), since it's an upload) POST /api/ota.
// Every other endpoint on this board stays unauthenticated -- see
// secrets.h.example and the README for why specifically these. Reboot and
// WiFi-scan were added to this list alongside config/OTA because, unlike
// /api/display or /api/clear (worst case: a wrong flight number shows),
// leaving them open is a free, repeatable denial-of-service for anyone on
// the LAN (reboot knocks the panel offline; scan blocks loop() for ~1-2s
// each call). server.requestAuthentication() sends a real 401
// with WWW-Authenticate: Basic, which makes the browser show its native
// credential prompt (this works for a fetch()-initiated request too, not
// just a page navigation) and then cache the credentials for the origin,
// not just this one handler's response.
//
// That last part is also exactly the CSRF exposure worth guarding against
// here: once a browser has cached Basic Auth credentials for this origin,
// it attaches them automatically to *any* request to that origin --
// including one triggered by a form or fetch() on a completely different
// page the admin happens to have open at the same time. A cross-origin
// request can't forge the Origin header itself (the browser sets it, not
// the page's JS), so rejecting a mismatch here blocks that case without
// needing a CSRF token. A same-origin browser request always sends a
// matching Origin (or none, for older/simple requests, which this doesn't
// penalize), and script/CLI clients (curl, deploy_web.py) typically send no
// Origin at all -- so absence is allowed through; only a *mismatched*
// Origin is treated as suspicious.
bool requireAuth() {
  if (server.hasHeader("Origin")) {
    String expected = "http://" + server.hostHeader();
    if (server.header("Origin") != expected) {
      server.send(403, "application/json", "{\"error\":\"cross-origin request rejected\"}");
      return false;
    }
  }
  if (!server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// POST /api/config -- standard HTML form post (application/x-www-form-urlencoded),
// not JSON, since it's submitted by the plain <form> in handleAdmin() above.
// Persists to NVS via Preferences; brightness and night-mode settings apply
// immediately (via applyBrightnessForTime() at the end, not a direct
// setBrightness8() call here -- see that function's comment for why: a
// direct call would ignore whether it's currently inside the night window),
// hostname, a newly-enabled/changed TZ override, and the NTP server all
// need a reboot (WiFi.setHostname() only takes effect before WiFi.begin(),
// and re-running configTime() live is more moving parts than just asking
// for a reboot, same UX as hostname).
void handleConfig() {
  if (!requireAuth()) return;

  if (server.hasArg("hostname")) {
    String h = server.arg("hostname");
    h.trim();
    if (h.length() > 0 && h.length() < 32) {
      cfgHostname = h;
      prefs.putString("hostname", cfgHostname);
    }
  }

  if (server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b >= 1 && b <= 255) {
      cfgBrightness = (uint8_t)b;
      prefs.putUChar("brightness", cfgBrightness);
    }
  }

  bool tzEnabled = server.hasArg("tzOverrideEnabled"); // checkbox: present only if checked
  long tzOffsetSec;
  if (tzEnabled && server.hasArg("tzOffset") && parseUtcOffset(server.arg("tzOffset"), &tzOffsetSec)) {
    cfgTzOverrideEnabled = true;
    cfgTzOverrideOffsetSec = tzOffsetSec;
    prefs.putBool("tzOverride", true);
    prefs.putLong("tzOffsetSec", tzOffsetSec);
  } else if (!tzEnabled) {
    cfgTzOverrideEnabled = false;
    prefs.putBool("tzOverride", false);
  }
  // else: checkbox was on but the offset text didn't parse -- leave the
  // previous stored override alone rather than silently disabling it.

  cfgNightModeEnabled = server.hasArg("nightModeEnabled"); // checkbox
  prefs.putBool("nightEnabled", cfgNightModeEnabled);

  if (server.hasArg("nightBrightness")) {
    int nb = server.arg("nightBrightness").toInt();
    if (nb >= 1 && nb <= 255) {
      cfgNightBrightness = (uint8_t)nb;
      prefs.putUChar("nightBright", cfgNightBrightness);
    }
  }
  if (server.hasArg("nightStart")) {
    int h = server.arg("nightStart").toInt();
    if (h >= 0 && h <= 23) {
      cfgNightStartHour = (uint8_t)h;
      prefs.putUChar("nightStart", cfgNightStartHour);
    }
  }
  if (server.hasArg("nightEnd")) {
    int h = server.arg("nightEnd").toInt();
    if (h >= 0 && h <= 23) {
      cfgNightEndHour = (uint8_t)h;
      prefs.putUChar("nightEnd", cfgNightEndHour);
    }
  }

  if (server.hasArg("ntpServer")) {
    String s = server.arg("ntpServer");
    s.trim();
    if (isPlausibleNtpServer(s)) {
      cfgNtpServer = s;
      prefs.putString("ntpServer", cfgNtpServer);
    }
    // else: doesn't look like a bare hostname/IP -- leave the previous
    // value alone rather than saving something that'll just never resolve.
  }

  applyBrightnessForTime(); // pick up any brightness/night-mode change immediately, night-window-aware

  server.sendHeader("Location", "/");
  server.send(303); // redirect back to the admin page showing the new values
}

void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, "text/plain", "Rebooting...");
  delay(200); // let the response actually flush before the restart
  ESP.restart();
}

void setupServer() {
  if (strcmp(ADMIN_PASSWORD, "changeme") == 0) {
    consoleLog.println("WARNING: /api/config and /api/ota are using the default admin/changeme "
                        "credentials -- set ADMIN_USER/ADMIN_PASSWORD in secrets.h if that's not "
                        "acceptable for your network.");
  }
  server.on("/", HTTP_GET, handleWebStatic);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaData);
  server.on("/api/web/clear", HTTP_POST, handleWebClear);
  server.on("/api/web/upload", HTTP_POST, handleWebUploadDone, handleWebUploadData);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/display", HTTP_POST, handleDisplay);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.on("/debug/screenshot.bmp", HTTP_GET, handleScreenshot);
  // Everything else (the SPA's hashed JS/CSS bundle under /assets/, any
  // future static file) falls through to the same FATFS-backed handler.
  server.onNotFound(handleWebStatic);
  server.begin();
}

void setup() {
  consoleLog.begin(115200);
  logResetReason();
  loadConfig();           // opens Preferences -- needed before
                           // checkOtaRollback() right below, and before
                           // setupWiFi() (hostname) / setupMatrix()
                           // (brightness) further down
  checkOtaRollback();     // as early as possible, before anything that
                          // could hang or crash
  setupFatFS();           // internal flash -- serves the admin console (see
                           // handleWebStatic()); no DMA-unsafe window
  setupWiFi();
  setupNTP();
  setupMatrix();
  setupServer();
  setupWatchdog();          // deliberately last -- see that function's comment
                             // for why (everything above here can legitimately
                             // block for a while and shouldn't trip it)
  confirmOtaBootSuccess(); // reached the end of setup() without hanging or
                            // crashing -- this firmware is good; reset the
                            // rollback counter and record this partition
}

// canvas.print(), except the climb/descend glyphs are drawn in their accent
// colors; everything else in textColor.
void printColored(const String &text) {
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    canvas.setTextColor(c == GLYPH_CLIMB ? COLOR_CLIMB : c == GLYPH_DESCEND ? COLOR_DESCEND : textColor);
    canvas.print(c);
  }
  canvas.setTextColor(textColor);
}

// Draws `text` statically at `staticX` if it fits within `availWidth`;
// otherwise scrolls it across the full panel width using `scrollX`.
void drawScrollIfNeeded(const String &text, int y, int staticX, int availWidth, int &scrollX) {
  if (text.length() == 0) return;
  int textWidthPx = text.length() * 6;
  if (textWidthPx <= availWidth) {
    canvas.setCursor(staticX, y);
    printColored(text);
  } else {
    canvas.setCursor(scrollX, y);
    printColored(text);
    scrollX--;
    if (scrollX < -textWidthPx) scrollX = PANEL_RES_X;
  }
}

// Same idea as drawScrollIfNeeded(), but for the beside-icon column: text
// is drawn into rightColCanvas (exactly RIGHT_COL_AVAIL px wide) first,
// then blitted at (mainX, mainY) -- so a long scrolling name is clipped to
// the column and never overdraws the icon to its left.
void drawScrollClipped(const String &text, int mainX, int mainY, int availWidth, int &scrollX) {
  if (text.length() == 0) return;
  int textWidthPx = text.length() * 6;
  rightColCanvas.fillScreen(0);
  rightColCanvas.setTextSize(1);
  rightColCanvas.setTextWrap(false);
  rightColCanvas.setTextColor(textColor);
  if (textWidthPx <= availWidth) {
    rightColCanvas.setCursor(0, 0);
    rightColCanvas.print(text);
  } else {
    rightColCanvas.setCursor(scrollX, 0);
    rightColCanvas.print(text);
    scrollX--;
    if (scrollX < -textWidthPx) scrollX = availWidth;
  }
  canvas.drawRGBBitmap(mainX, mainY, rightColCanvas.getBuffer(), availWidth, 8);
}

// Route row: origin code flush left (1px margin), destination flush right,
// and between them a track with a marker at routeProgress -- solid over the
// part already flown, dotted over what's left. With no progress (FR24-only
// route, no coordinates) the marker sits centered and the track is all
// dotted. Codes that leave no room for a usable track (would need two
// 5+-letter codes) fall back to plain "SIN>KUL" text.
void drawRoute() {
  if (routeOrigin.length() == 0 || routeDest.length() == 0) return;
  const int markerW = 5;                             // glyph's inked columns
  const int originW = routeOrigin.length() * 6;
  const int destX = PANEL_RES_X - routeDest.length() * 6; // last inked column lands on x=62
  const int trackStart = 1 + originW + 1;            // one blank column past the glyph cell's own spacing
  const int trackEnd = destX - 2;
  const int trackLen = trackEnd - trackStart + 1;
  if (trackLen < markerW + 4) {
    drawScrollIfNeeded(lineIata, IATA_Y, 1, FULL_WIDTH_AVAIL, scrollXIata);
    return;
  }

  canvas.setCursor(1, IATA_Y);
  canvas.print(routeOrigin);
  canvas.setCursor(destX, IATA_Y);
  canvas.print(routeDest);

  float p = routeProgress >= 0 ? routeProgress : 0.5f;
  int markerX = trackStart + (int)(p * (trackLen - markerW) + 0.5f);
  int trackY = IATA_Y + 3; // the marker glyph's middle row
  for (int x = trackStart; x <= trackEnd; x++) {
    if (x >= markerX - 1 && x <= markerX + markerW) continue; // 1px breathing room around the marker
    if (routeProgress >= 0 && x < markerX) {
      canvas.drawPixel(x, trackY, COLOR_TRACK_FLOWN);
    } else if ((x - trackStart) % 2 == 0) {
      canvas.drawPixel(x, trackY, COLOR_TRACK_AHEAD);
    }
  }
  canvas.setTextColor(COLOR_ROUTE_MARKER);
  canvas.setCursor(markerX, IATA_Y);
  canvas.print(GLYPH_ROUTE_MARKER);
  canvas.setTextColor(textColor);
}

void drawAircraft() {
  canvas.fillScreen(0);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setTextColor(textColor);

  drawScrollIfNeeded(lineAirline, AIRLINE_Y, 1, FULL_WIDTH_AVAIL, scrollXAirline);
  drawScrollIfNeeded(lineFlightAlt, FLIGHTALT_Y, 1, FULL_WIDTH_AVAIL, scrollXFlightAlt);

  if (hasIcon) {
    canvas.drawRGBBitmap(ICON_X, ICON_Y, iconBuf, ICON_SIZE, ICON_SIZE);
  }
  drawScrollClipped(lineMake, RIGHT_COL_X, MAKE_Y, RIGHT_COL_AVAIL, scrollXMake);
  drawScrollClipped(lineModelShort, RIGHT_COL_X, MODELSHORT_Y, RIGHT_COL_AVAIL, scrollXModelShort);
  drawScrollClipped(lineTail, RIGHT_COL_X, TAIL_Y, RIGHT_COL_AVAIL, scrollXTail);

  drawRoute();

  // City names always scroll -- full airport names routinely exceed 64px
  // even alone, so there's no meaningful "fits statically" case here.
  // Same 6x8 size-1 text as every other row (tried 2x here to fill the old
  // layout's dead space at the bottom -- looked disproportionately large
  // next to the rest of the display, reverted; see the SECTION_GAP layout
  // above for how that space is used instead).
  if (lineCity.length()) {
    canvas.setCursor(scrollXCity, CITY_Y);
    canvas.print(lineCity);
    scrollXCity--;
    if (scrollXCity < -(int)(lineCity.length() * 6)) scrollXCity = PANEL_RES_X;
  }
}

// Night-mode brightness scheduling, checked periodically from loop() (see
// lastBrightnessCheck). 0 is never a real brightness (the config form's
// minimum is 1), so it doubles as an "not applied yet" sentinel -- avoids
// calling setBrightness8() every check when nothing's actually changed.
uint8_t lastAppliedBrightness = 0;

void applyBrightnessForTime() {
  if (!cfgNightModeEnabled) {
    if (lastAppliedBrightness != cfgBrightness) {
      dma_display->setBrightness8(cfgBrightness);
      lastAppliedBrightness = cfgBrightness;
    }
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return; // NTP not synced yet -- leave brightness as-is rather than guess

  int hour = timeinfo.tm_hour;
  bool isNight;
  if (cfgNightStartHour == cfgNightEndHour) {
    isNight = false; // degenerate zero-length window -- treat as always-day, not always-night
  } else if (cfgNightStartHour < cfgNightEndHour) {
    isNight = (hour >= cfgNightStartHour && hour < cfgNightEndHour);
  } else {
    // Window spans midnight, e.g. 23 -> 6.
    isNight = (hour >= cfgNightStartHour || hour < cfgNightEndHour);
  }

  uint8_t target = isNight ? cfgNightBrightness : cfgBrightness;
  if (lastAppliedBrightness != target) {
    dma_display->setBrightness8(target);
    lastAppliedBrightness = target;
  }
}

// Idle screen shown whenever there's no aircraft payload loaded (i.e. after
// /api/clear, or before the first /api/display). NTP sync runs in the
// background from setupNTP(); until getLocalTime() first succeeds, this
// just shows "SYNCING..." instead of a wrong/zeroed time.
void drawClock() {
  canvas.fillScreen(0);
  canvas.setTextWrap(false);
  canvas.setTextColor(textColor);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    canvas.setTextSize(1);
    canvas.setCursor(4, 28);
    canvas.print("SYNCING...");
    return;
  }

  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  canvas.setTextSize(2);
  int timeWidthPx = 5 * 12; // "HH:MM" = 5 chars * 12px/char at size 2
  canvas.setCursor((PANEL_RES_X - timeWidthPx) / 2, 24);
  canvas.print(timeBuf);

  char dateBuf[12];
  snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  canvas.setTextSize(1);
  int dateWidthPx = strlen(dateBuf) * 6;
  canvas.setCursor((PANEL_RES_X - dateWidthPx) / 2, 44);
  canvas.print(dateBuf);
}

unsigned long lastWiFiCheck = 0;
unsigned long lastBrightnessCheck = 0;

void loop() {
  esp_task_wdt_reset(); // fed every iteration -- see setupWatchdog()

  // Brightness/night-mode changes at most twice a day (entering/leaving the
  // night window), so a 30s check interval is cheap and still responsive.
  unsigned long nowBrightness = millis();
  if (nowBrightness - lastBrightnessCheck >= 30000) {
    lastBrightnessCheck = nowBrightness;
    applyBrightnessForTime();
  }

  // setupWiFi() only blocks-until-connected once at boot -- with no recovery
  // path, a dropped connection (router reboot, brief outage) would otherwise
  // leave the board silently unreachable until it's power-cycled by hand.
  // WiFi.reconnect() is non-blocking (unlike WiFi.begin() looped with
  // delay()), so this check costs nothing when already connected.
  unsigned long nowWiFi = millis();
  if (nowWiFi - lastWiFiCheck >= 5000) {
    lastWiFiCheck = nowWiFi;
    if (WiFi.status() != WL_CONNECTED) {
      consoleLog.println("WiFi disconnected -- reconnecting...");
      WiFi.reconnect();
    }
  }

  server.handleClient();

  // Staleness check: if n8n stops pushing -- workflow deactivated, the n8n
  // host down, a network blip -- the panel would otherwise keep showing
  // whatever aircraft was last pushed forever, with no indication the data
  // is old. lastDisplayPush == 0 means no push has landed yet this boot --
  // nothing to time out from, so that case is skipped (the idle clock just
  // shows normally, same as it always has).
  if (lastDisplayPush != 0 && (unsigned long)(millis() - lastDisplayPush) >= STALE_TIMEOUT_MS) {
    consoleLog.println("No display push in STALE_TIMEOUT_MS -- falling back to clock");
    clearAircraftState();
    lastDisplayPush = 0; // don't re-trigger every loop() until the next real push
  }

  bool haveContent = lineAirline.length() || lineFlightAlt.length() || lineMake.length() ||
                      lineModelShort.length() || lineTail.length() || lineIata.length() || lineCity.length();

  unsigned long now = millis();
  if (haveContent) {
    if (now - lastScroll < 25) return; // ~40 updates/sec, needed for smooth scrolling
    lastScroll = now;
    drawAircraft();
  } else {
    if (now - lastScroll < 500) return; // clock only needs to redraw once/sec
    lastScroll = now;
    drawClock();
  }

  // Flip to the back buffer, then wait one frame period for the swap to
  // actually take visual effect before blitting the freshly-drawn canvas --
  // flipDMABuffer() queues the swap for the next DMA scan pass rather than
  // applying it synchronously, so drawing immediately after it would race
  // the still-in-flight previous frame.
  dma_display->flipDMABuffer();
  delay(1000 / dma_display->calculated_refresh_rate);
  dma_display->drawRGBBitmap(0, 0, canvas.getBuffer(), PANEL_RES_X, PANEL_RES_Y);
}
