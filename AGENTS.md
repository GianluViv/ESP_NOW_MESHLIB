# AGENTS.md — ESPNowMesh

Guidelines for coding agents working in `ESP_NOW_MESHLIB`.

## Overview

`ESPNowMesh` is an ESP32 Arduino/PlatformIO library providing an ESP-NOW mesh layer with master/slave roles, automatic pairing, NVS persistence, channel scanning, and loop-free multi-hop relay.

- **Target:** ESP32 (Arduino core, compatible with ESP-IDF 4.x and 5.x).
- **Dependencies:** Only ESP32 Arduino core headers (`Arduino.h`, `WiFi.h`, `esp_now.h`, `esp_wifi.h`, `Preferences.h`, `esp_idf_version.h`). Do not introduce external library dependencies.
- **Entry point:** Global singleton `extern ESPNowMesh Mesh;` declared in `src/ESPNowMesh.h`.

## Repository Structure

- `src/ESPNowMesh.h`, `src/ESPNowMesh.cpp` — Main `ESPNowMesh` class, FreeRTOS tasks (`taskBeacon`, `taskWatchdog`), ESP-NOW callbacks, channel scanner, and relay logic.
- `src/ESPNowMeshPacket.h` — Wire format (`ENMeshPacket`, `ENMeshPktType`, `ENMeshSeenEntry`) and packet constants.
- `src/ESPNowMeshStorage.h` — NVS persistence helper (`ENMeshStorage`) using `Preferences` under namespace `"enmesh"`.
- `examples/master/master.ino`, `examples/slave/slave.ino` — Arduino IDE examples.
- `examples/espnow_chat_test/` — PlatformIO test/example project (`[env:master]` and `[env:slave]`, referencing the library via `lib_deps = symlink://../..`).
- `library.properties` — Arduino Library Manager metadata.
- `README.md` — Public English documentation and API reference.

## Critical Invariants & Gotchas

1. **Fixed 128-byte packet layout:** `ENMeshPacket` in `src/ESPNowMeshPacket.h` uses `#pragma pack(push, 1)` and has a `static_assert(sizeof(ENMeshPacket) == ENMESH_PKT_SIZE)`. Any header change must keep `ENMESH_PAYLOAD_LEN` (`102`) consistent so total size stays `128` bytes.
2. **ESP-IDF 4.x / 5.x compatibility:** Keep `#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)` guards around `recvCb` and any IDF-version-specific ESP-NOW APIs.
3. **WiFi power save must stay disabled:** `begin()` calls `esp_wifi_set_ps(WIFI_PS_NONE)` for both master and slave to prevent missed ESP-NOW frames. Do not re-enable `WIFI_PS_MIN_MODEM` or `WIFI_PS_MAX_MODEM`.
4. **Master WiFi driver initialization:** When a master node does not connect to a WiFi router, the sketch must initialize the WiFi driver (`WiFi.mode(WIFI_STA); WiFi.disconnect();`) before calling `Mesh.begin(true, ...)`.
5. **Callback registration order:** User callbacks (`onBeacon`, `onStatus`, `onCommand`, `onPaired`, `onMasterLost`) are expected to be registered before `Mesh.begin(...)`.
6. **Slave heartbeat packets:** The slave watchdog periodically sends empty `ENMESH_PKT_STATUS` packets. Examples/handlers on the master side must guard against empty payloads.
7. **Pairing & NVS:** On the slave, `ENMeshStorage` stores `channel` and `net_id` in NVS (`"enmesh"`). Pairing completion and failed channel scans trigger `esp_restart()` on the slave.
8. **Compile-time `#define` overrides:** Keep `#ifndef` guards around `ENMESH_BEACON_INTERVAL_MS`, `ENMESH_SLAVE_HB_INTERVAL_S`, `ENMESH_SLAVE_TIMEOUT_S`, `ENMESH_CHANNEL_SCAN_WAIT_MS`, and `ENMESH_MAX_SLAVES` in `src/ESPNowMesh.h` so sketches can override them before `#include <ESPNowMesh.h>`.

## Conventions & Verification

- **Language:** Source comments and examples are written in Italian; `README.md` is in English. Match the existing language of the file you edit.
- **Docs sync:** When changing public methods, `#define` defaults, or packet behavior, update `README.md` (and `library.properties` version if releasing).
- **Build check:** When PlatformIO (`pio`) is available, verify changes by building `examples/espnow_chat_test`:
  ```bash
  cd examples/espnow_chat_test
  pio run -e master && pio run -e slave
  ```
