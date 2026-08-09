// WiFi HTTP Text Display -- bring-up firmware for the Waveshare
// ESP32-S3-RGB-Matrix board driving a 64x64 HUB75 panel.
//
// Only run this AFTER Waveshare's stock panel test examples
// (01_SimpleTestShapes / 07_Pixel_Mapping_Test, with PANEL_RES_Y changed
// to 64) have confirmed the panel lights up correctly on your specific
// board + power setup. Panel config below uses the ESP32-S3 library defaults, per
// Waveshare's own 05_AnimatedGIFPanel_SD example for this exact board --
// no gpio.e / driver overrides, since those risk colliding with this
// board's Flash/PSRAM pins.
//
// Uses the built-in WebServer.h (ships with the esp32 Arduino core --
// no extra install) instead of ESPAsyncWebServer, to avoid that library's
// messy/archived fork situation for what's just a single low-traffic
// endpoint.
//
// Libraries required (Arduino Library Manager):
//   - "ESP32 HUB75 LED MATRIX PANEL DMA Display" by mrfaptastic
//   - "ArduinoJson" by Benoit Blanchon
//
// Copy secrets.h.example to secrets.h and fill in your WiFi credentials
// before building -- secrets.h is gitignored, never commit real creds.

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "secrets.h"

#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

// 0 = no rotation, 1 = 90 deg, 2 = 180 deg, 3 = 270 deg
// (Adafruit_GFX-style rotation, inherited by MatrixPanel_I2S_DMA -- safe
// on a square 64x64 panel, no cropping either way)
#define DISPLAY_ROTATION 0

MatrixPanel_I2S_DMA *dma_display = nullptr;
WebServer server(80);

String currentText = "READY";
int scrollX = PANEL_RES_X;
uint16_t textColor;

void setupMatrix() {
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  // Restored from Waveshare's actual getting-started example for this
  // board (01_SimpleTestShapes) -- gpio.e is the row-address line this
  // library needs for a 64-tall panel (two lines updated 32 rows apart);
  // without it, content straddling the row-32 boundary splits into two
  // bands with a gap, which matches what showed up on the panel.
  // Previously removed based on misreading a different example's comment
  // (05_AnimatedGIFPanel_SD only questions clkphase/driver, not gpio.e).
  mxconfig.gpio.e = 9;
  mxconfig.clkphase = false;
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  mxconfig.double_buff = true; // draw off-screen, flip atomically -- avoids
                                // tearing (half-old/half-new frame) since the
                                // DMA hardware scans continuously while we draw

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setRotation(DISPLAY_ROTATION);
  dma_display->setBrightness8(60); // keep it modest until you know your power supply headroom
  dma_display->clearScreen();
  textColor = dma_display->color565(255, 255, 255);
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Show the IP on the panel itself so it's obvious the board is online
  // and ready to receive POSTs, without needing Serial Monitor or the router.
  currentText = "READY " + WiFi.localIP().toString();
  scrollX = PANEL_RES_X;
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
  currentText = doc["text"] | "";
  scrollX = PANEL_RES_X; // restart scroll from the right edge
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClear() {
  currentText = "";
  dma_display->clearScreen();
  dma_display->flipDMABuffer();
  dma_display->clearScreen(); // clear both buffers so it stays blank either way
  server.send(200, "application/json", "{\"ok\":true}");
}

void setupServer() {
  // POST /api/display  body: {"text": "SQ123 SIN-KUL"}
  server.on("/api/display", HTTP_POST, handleDisplay);
  // POST /api/clear
  server.on("/api/clear", HTTP_POST, handleClear);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  setupMatrix();
  setupWiFi();
  setupServer();
}

unsigned long lastScroll = 0;

void loop() {
  server.handleClient();

  if (currentText.length() == 0) return;

  unsigned long now = millis();
  if (now - lastScroll < 25) return; // ~40 updates/sec -- 60ms (~17/sec) read as choppy to the eye
  lastScroll = now;

  // Flip to the back buffer (currently showing the previous frame) and
  // draw the next frame into it off-screen, then loop() flips again next
  // tick -- keeps the panel always showing a fully-drawn frame, never a
  // half-drawn one.
  //
  // flipDMABuffer() only signals the swap -- it doesn't block until the
  // hardware has actually finished displaying it. Skipping this wait (per
  // Waveshare's own 03_DoubleBuffer example) let us start overwriting the
  // new back buffer before the flip took visual effect, which showed up
  // as a ghost layer of the previous frame.
  dma_display->flipDMABuffer();
  delay(1000 / dma_display->calculated_refresh_rate);
  dma_display->clearScreen();
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
  dma_display->setCursor(scrollX, 28); // vertically centered-ish for an 8px font on a 64px panel
  dma_display->setTextColor(textColor);
  dma_display->print(currentText);

  scrollX--;
  int textWidthPx = currentText.length() * 6; // 6px per char at textSize(1)
  if (scrollX < -textWidthPx) {
    scrollX = PANEL_RES_X;
  }
}
