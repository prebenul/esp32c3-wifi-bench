# ESP32-C3 Super Mini — WiFi Benchmark Tool

A self-contained WiFi performance test suite that runs directly on the ESP32-C3 Super Mini.
The device serves a web dashboard (stored in LittleFS flash) that lets you measure:

| Test | Method | What it measures |
|---|---|---|
| **Latency / Ping** | HTTP GET `/api/ping` × 20 | RTT, min/avg/max, jitter |
| **Download throughput** | HTTP GET `/dl/{size}` | Mbps from device → browser |
| **Upload throughput** | HTTP POST `/ul` | Mbps from browser → device |
| **WebSocket jitter** | WS echo × 50 | Round-trip std deviation |
| **RSSI** | Inline in ping response | Signal strength in dBm |
| **Auto-ping** | Repeating every 5 s | Live sparkline chart |

---

## Hardware

- **Board:** ESP32-C3 Super Mini (RISC-V, 160 MHz, 4 MB flash)
- **Power:** USB-C (flashing + serial monitor)
- **LED:** GPIO 8 — blinks during WiFi connect, solid ON when connected

---

## Quick Start

### 1. Install PlatformIO

Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code.

### 2. Set WiFi credentials

Edit `platformio.ini` and add your SSID + password in `build_flags`:

```ini
build_flags =
  -DWIFI_SSID=\"YourSSID\"
  -DWIFI_PASS=\"YourPassword\"
```

Or edit `src/main.cpp` directly and change the `#define` values.

### 3. Flash firmware

```bash
pio run -t upload
```

### 4. Flash the web UI (LittleFS)

```bash
pio run -t uploadfs
```

> **Important:** You must flash the filesystem at least once. After that, firmware updates (`pio run -t upload`) do not erase LittleFS.

### 5. Open the dashboard

Check the serial monitor for the IP address:

```bash
pio device monitor
```

Then open `http://<ip>` in your browser (same WiFi network).

---

## Project Structure

```
esp32c3-wifi-bench/
├── platformio.ini          ← PlatformIO config (board, libs, FS)
├── src/
│   └── main.cpp            ← Firmware (AsyncWebServer + endpoints)
├── data/
│   └── index.html          ← Web dashboard (flashed to LittleFS)
├── scripts/
│   └── generate_index.py   ← Pre-build hook placeholder
├── .vscode/
│   ├── extensions.json     ← Recommended extensions
│   ├── settings.json       ← Editor settings
│   └── c_cpp_properties.json ← IntelliSense config
└── README.md
```

---

## API Reference

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Web dashboard (index.html) |
| `GET` | `/api/info` | Device + network info (JSON) |
| `GET` | `/api/ping` | Latency probe (JSON + RSSI) |
| `GET` | `/dl/{bytes}` | Download benchmark (raw octet-stream) |
| `POST` | `/ul` | Upload benchmark (absorbs body, returns count JSON) |
| `WS` | `/ws` | WebSocket echo server |

Max download size: **512 KB** (configurable via `MAX_DL_SIZE` in main.cpp).

---

## Dependencies

Managed by PlatformIO (see `platformio.ini`):

- `ESP Async WebServer` ^3.7.6 — non-blocking HTTP + WebSocket server
- `AsyncTCP` ^3.3.2 — async TCP layer for ESP32
- `ArduinoJson` ^7.4.1 — JSON serialization

---

## Tips

- Run on **2.4 GHz only** — the ESP32-C3 does not support 5 GHz.
- For best throughput numbers, test close to the access point.
- Upload is usually slower than download because the ESP32-C3 TCP stack buffers uploads more conservatively.
- The sparkline chart stores up to 200 samples — start auto-ping for a continuous view.

---

## License

MIT — do whatever you want with it.
