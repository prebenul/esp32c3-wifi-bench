#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");

// ---------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------
static void addCors(AsyncWebServerResponse *r) {
  r->addHeader("Access-Control-Allow-Origin", "*");
}

static void printStatus() {
  Serial.println();
  Serial.println("=== ESP32-C3 WiFi Bench ===");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  SSID : %s\n", WiFi.SSID().c_str());
    Serial.printf("  IP   : http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI : %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("  WiFi : NOT CONNECTED");
  }
  Serial.println("===========================");
  Serial.println();
}

// ---------------------------------------------------------------
//  WiFi — STA only, keep retrying every 5 s indefinitely
// ---------------------------------------------------------------
static void connectWiFi() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to \"%s\"", WIFI_SSID);

  bool ledState = false;
  while (WiFi.status() != WL_CONNECTED) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
    delay(500);
    Serial.print(".");

    // If disconnected for more than 30 s, re-issue begin()
    static unsigned long lastBegin = 0;
    if (millis() - lastBegin > 30000) {
      lastBegin = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  digitalWrite(LED_PIN, LED_ON);
  Serial.println();
  printStatus();
}

// ---------------------------------------------------------------
//  Serial-connect detection — print IP whenever a new terminal
//  connects (CDC DTR line or any byte received).
// ---------------------------------------------------------------
static void checkSerialConnect() {
  // On USB-CDC ESP32-C3, Serial.available() spikes when a terminal
  // connects. We also detect DTR toggling via HardwareSerial-CDC.
  static bool prevConnected = false;
  bool connected = (bool)Serial;   // true when a CDC host is attached
  if (connected && !prevConnected) {
    delay(50);          // brief pause so the terminal is ready
    printStatus();
  }
  prevConnected = connected;

  // Also print if the user sends any byte (convenient "where am I?" key)
  if (Serial.available()) {
    while (Serial.available()) Serial.read();   // drain
    printStatus();
  }
}

// ---------------------------------------------------------------
//  HTTP handlers
// ---------------------------------------------------------------
static void handleInfo(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["chip"]      = "ESP32-C3";
  doc["freq_mhz"]  = 160;
  doc["flash_mb"]  = 4;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_mode"] = "STA";
  doc["ip"]        = WiFi.localIP().toString();
  doc["rssi"]      = WiFi.RSSI();
  doc["ssid"]      = WiFi.SSID();

  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *r = request->beginResponse(200, "application/json", out);
  addCors(r);
  request->send(r);
}

static void handlePing(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["ok"]   = true;
  doc["rssi"] = WiFi.RSSI();
  doc["ts"]   = millis();

  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *r = request->beginResponse(200, "application/json", out);
  addCors(r);
  request->send(r);
}

static void handleDownload(AsyncWebServerRequest *request) {
  String path      = request->url();
  long requested   = path.substring(String("/dl/").length()).toInt();
  if (requested < 0) requested = 0;
  size_t total     = min((size_t)requested, (size_t)MAX_DL_SIZE);

  AsyncWebServerResponse *r = request->beginResponse(
      "application/octet-stream", total,
      [total](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        size_t remaining = total - index;
        size_t chunk = min(remaining, maxLen);
        memset(buffer, 0xAA, chunk);
        return chunk;
      });
  addCors(r);
  request->send(r);
}

static void handleUpload(AsyncWebServerRequest *request) {
  size_t received = 0;
  if (request->_tempObject != nullptr) {
    received = *reinterpret_cast<size_t *>(request->_tempObject);
    delete reinterpret_cast<size_t *>(request->_tempObject);
    request->_tempObject = nullptr;
  }

  JsonDocument doc;
  doc["received"] = received;
  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *r = request->beginResponse(200, "application/json", out);
  addCors(r);
  request->send(r);
}

static void handleUploadBody(AsyncWebServerRequest *request, uint8_t *data,
                             size_t len, size_t index, size_t total) {
  if (request->_tempObject == nullptr) {
    request->_tempObject = new size_t(0);
  }
  *reinterpret_cast<size_t *>(request->_tempObject) += len;
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len) {
      if (info->opcode == WS_TEXT) {
        client->text(data, len);
      } else {
        client->binary(data, len);
      }
    }
  }
}

// ---------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed.");
  }

  connectWiFi();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      request->send(404, "text/plain", "index.html not found — run: pio run -t uploadfs");
    }
  });

  server.on("/api/info",  HTTP_GET, handleInfo);
  server.on("/api/ping",  HTTP_GET, handlePing);
  server.on("/ul", HTTP_POST, handleUpload, nullptr, handleUploadBody);

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_GET && request->url().startsWith("/dl/")) {
      handleDownload(request);
      return;
    }
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *r = request->beginResponse(204);
      r->addHeader("Access-Control-Allow-Origin", "*");
      r->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      r->addHeader("Access-Control-Allow-Headers", "*");
      request->send(r);
      return;
    }
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP server started.");
}

// ---------------------------------------------------------------
//  Loop
// ---------------------------------------------------------------
void loop() {
  ws.cleanupClients();
  checkSerialConnect();
  delay(10);
}
