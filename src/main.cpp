#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#ifndef WIFI_SSID
#define WIFI_SSID "YourSSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YourPassword"
#endif

// ESP32-C3 Super Mini onboard LED is on GPIO 8 and is active-low.
#define LED_PIN 8
#define LED_ON LOW
#define LED_OFF HIGH

#define AP_SSID "ESP32C3-Bench"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define MAX_DL_SIZE 524288UL

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static bool apMode = false;

static void addCors(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
}

static void connectWiFi() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to WiFi SSID \"%s\"", WIFI_SSID);

  unsigned long start = millis();
  bool ledState = false;
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    digitalWrite(LED_PIN, LED_ON);
    Serial.print("Connected. IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    digitalWrite(LED_PIN, LED_ON);
    Serial.println("WiFi connect failed; started SoftAP fallback.");
    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  }
}

static void handleInfo(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["chip"] = "ESP32-C3";
  doc["freq_mhz"] = 160;
  doc["flash_mb"] = 4;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_mode"] = apMode ? "AP" : "STA";
  if (apMode) {
    doc["ip"] = WiFi.softAPIP().toString();
    doc["rssi"] = nullptr;
    doc["ssid"] = AP_SSID;
  } else {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["ssid"] = WiFi.SSID();
  }

  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", out);
  addCors(response);
  request->send(response);
}

static void handlePing(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["ok"] = true;
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["ts"] = millis();

  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", out);
  addCors(response);
  request->send(response);
}

static void handleDownload(AsyncWebServerRequest *request) {
  String path = request->url();
  long requested = path.substring(String("/dl/").length()).toInt();
  if (requested < 0) requested = 0;
  size_t total = (size_t)requested;
  if (total > MAX_DL_SIZE) total = MAX_DL_SIZE;

  AsyncWebServerResponse *response = request->beginResponse(
      "application/octet-stream", total,
      [total](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        size_t remaining = total - index;
        size_t chunk = remaining < maxLen ? remaining : maxLen;
        memset(buffer, 0xAA, chunk);
        return chunk;
      });
  addCors(response);
  request->send(response);
}

static void handleUpload(AsyncWebServerRequest *request) {
  // Final response after the full body has been absorbed via the body handler.
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
  AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", out);
  addCors(response);
  request->send(response);
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
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, "/index.html", "text/html");
      request->send(response);
    } else {
      request->send(404, "text/plain", "index.html not found in LittleFS");
    }
  });

  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/ping", HTTP_GET, handlePing);

  server.on("/ul", HTTP_POST, handleUpload, nullptr, handleUploadBody);

  // Match any /dl/<bytes> request.
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_GET && request->url().startsWith("/dl/")) {
      handleDownload(request);
      return;
    }
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(204);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "*");
      request->send(response);
      return;
    }
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  ws.cleanupClients();
  delay(10);
}
