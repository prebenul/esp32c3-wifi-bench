#pragma once

// =============================================================
//  ESP32-C3 WiFi Bench — User Configuration
//  Edit this file to match your setup.
// =============================================================

// --- WiFi credentials ---
#define WIFI_SSID "YourSSID"
#define WIFI_PASS "YourPassword"

// --- Hardware ---
#define LED_PIN   8       // Onboard LED pin (active-low on ESP32-C3 Super Mini)
#define LED_ON    LOW
#define LED_OFF   HIGH

// --- Web server ---
#define HTTP_PORT 80

// --- Benchmark limits ---
#define MAX_DL_SIZE  524288UL   // Max download payload: 512 KB
