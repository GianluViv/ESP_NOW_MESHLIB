# ESPNowMesh

An ESP-NOW mesh library for ESP32 (Arduino framework) with automatic pairing,
multi-hop relay, and a fully user-defined application payload.

The library takes care of the network plumbing — beacons, pairing handshake,
loop-free relay, channel discovery, NVS persistence and slave watchdog — while
leaving the payload format entirely up to the application.

## Features

- **Master / slave roles**, selectable at runtime (`begin(isMaster, ...)`).
- **Automatic pairing**: press a button (or call `startPairing()`) on both
  nodes; the slave receives the master's channel and network ID and stores
  them in NVS so it can reconnect after a reboot without re-pairing.
- **Multi-hop relay** with a TTL field and a "seen" table to prevent routing
  loops and duplicate delivery.
- **Multiple independent networks** on the same channel via a `network_id`
  string filter — packets from a foreign network are still relayed
  transparently (so they don't get blocked), but are not delivered to the
  application.
- **Automatic channel discovery**: a slave with no saved channel scans the
  common WiFi channels (1, 6, 11, then the rest) listening for a beacon or a
  pairing request.
- **NVS persistence** of the paired channel and network ID (slave side), so
  a power cycle doesn't require re-pairing.
- **Slave watchdog**: periodic heartbeat (`PKT_STATUS`) to the master, and an
  `onMasterLost()` callback if the master goes silent for 30 minutes.
- **Opaque application payload** (up to `ENMESH_PAYLOAD_LEN` = 102 bytes) —
  the library never interprets payload contents; you define your own struct.
- **WiFi power-save automatically disabled** — see
  [WiFi power save and ESP-NOW reliability](#wifi-power-save-and-esp-now-reliability) below, this is important.

## Installation

Copy this repository into your Arduino `libraries/` folder (or add it as a
PlatformIO library dependency), then:

```cpp
#include <ESPNowMesh.h>
```

The global singleton instance `Mesh` is available immediately — there is no
need to instantiate the class yourself.

Target: ESP32 (any variant with WiFi/ESP-NOW support). No external
dependencies beyond the ESP32 Arduino core (`WiFi.h`, `esp_now.h`, `esp_wifi.h`,
`Preferences.h`).

## Quick start

**Master:**

```cpp
#include <ESPNowMesh.h>

void onStatus(const uint8_t mac[6], const uint8_t *payload, size_t len) { /* ... */ }

void setup() {
  Mesh.onStatus(onStatus);
  Mesh.begin(true, "MY_MESH_NET");
}

void loop() {
  MyPayload p = { /* ... */ };
  Mesh.sendBeacon(&p, sizeof(p));
  delay(1000);
}
```

**Slave:**

```cpp
#include <ESPNowMesh.h>

void onBeacon(const uint8_t *payload, size_t len) { /* ... */ }

void setup() {
  Mesh.onBeacon(onBeacon);
  Mesh.begin(false, "MY_MESH_NET");
}

void loop() {
  // Mesh handles heartbeats and watchdog internally.
}
```

See [`examples/master`](examples/master/master.ino) and
[`examples/slave`](examples/slave/slave.ino) for complete, runnable examples
(button-triggered pairing, payload structs, callbacks).

## Concepts

### Packet format

Every packet is a fixed-size, packed 128-byte struct
(`ENMeshPacket`, see [`src/ESPNowMeshPacket.h`](src/ESPNowMeshPacket.h)):

| Field         | Size     | Purpose                                    |
|---------------|----------|---------------------------------------------|
| `network_id`  | 16 bytes | Logical network name (C-string)             |
| `sender_mac`  | 6 bytes  | STA MAC of the originating node             |
| `packet_type` | 1 byte   | `ENMeshPktType` (see below)                 |
| `seq_id`      | 2 bytes  | Per-sender sequence number (loop prevention)|
| `ttl`         | 1 byte   | Remaining relay hops                        |
| `payload`     | 102 bytes| Opaque application data                     |

Packet types (`ENMeshPktType`):

- `PKT_BEACON` — master → slaves, periodic heartbeat/state.
- `PKT_PAIRING_REQ` — master → broadcast, pairing request.
- `PKT_PAIRING_ACK` — slave → master, pairing confirmation.
- `PKT_STATUS` — slave → master, periodic status/alarm.
- `PKT_COMMAND` — master → slaves, direct command.

### Roles

- **Master**: sends a beacon roughly once per second (jittered ±50 ms) via
  `sendBeacon()`/the internal auto-beacon task, tracks slaves that have sent
  `PKT_STATUS`, and can broadcast commands (`sendCommand()`).
- **Slave**: listens for beacons, sends periodic heartbeats
  (`ENMESH_SLAVE_HB_INTERVAL_S`, default 300 s), and considers itself
  disconnected if no beacon arrives within `ENMESH_SLAVE_TIMEOUT_S`
  (default 5× the heartbeat interval).

### Pairing

Calling `startPairing()` on both nodes starts a handshake:

1. Master broadcasts `PKT_PAIRING_REQ` on its current channel.
2. A slave in pairing mode replies with `PKT_PAIRING_ACK`, saves the
   master's `network_id` and channel to NVS, then restarts to apply them.
3. The master's `onPaired(true)` callback fires on ACK; the slave's
   `onPaired(false)` callback fires just before it restarts.

A slave that already paired with this master, but isn't in pairing mode,
will still re-send its ACK if it sees a `PKT_PAIRING_REQ` for its own
network — this covers the case where the original ACK was lost.

### Relay / anti-loop

Every node relays packets with `ttl > 1` (broadcast, with 0–8 ms jitter),
decrementing the TTL. A small ring buffer of `(sender_mac, seq_id)` pairs
(`ENMESH_SEEN_TABLE_SIZE`, default 32 entries) prevents re-processing or
re-relaying a packet already seen.

### Channel discovery & persistence

- **Master**: if connected to a WiFi router, uses the router's channel;
  otherwise defaults to channel 1 (or whatever is set). Saves the channel to
  NVS.
- **Slave**: on boot, loads a saved `(channel, network_id)` pair from NVS.
  If found, it listens on that channel for up to 10 s; if no beacon/pairing
  request appears, it falls back to a full scan across channels
  1, 6, 11, 2–5, 7–10, 13, waiting `ENMESH_CHANNEL_SCAN_WAIT_MS` (default
  3 s) on each. If no master is found at all, the device restarts and tries
  again.

## API reference

```cpp
bool begin(bool isMaster, const char *networkId, uint8_t ttl = ENMESH_TTL_DEFAULT);

void startPairing();
bool isPairing() const;

bool sendBeacon(const void *payload, size_t len);   // master only
bool sendStatus(const void *payload, size_t len);   // slave only
bool sendCommand(const void *payload, size_t len);  // master only

void onBeacon(void (*cb)(const uint8_t *payload, size_t len));
void onStatus(void (*cb)(const uint8_t senderMac[6], const uint8_t *payload, size_t len));
void onCommand(void (*cb)(const uint8_t *payload, size_t len));
void onPaired(void (*cb)(bool isMaster));
void onMasterLost(void (*cb)());

bool     isMaster()      const;
bool     isInitialized() const;
bool     isConnected()   const;  // slave: recent beacon received
uint8_t  activeChannel() const;
uint32_t lastBeaconMs()  const;
uint8_t  slaveCount()    const;  // master only
bool     getSlaveInfo(uint8_t index, ENMeshSlaveInfo &out) const;

void clearPairingData(); // wipes NVS (channel + network_id)
```

Register callbacks **before** calling `begin()`.

## Configuration (`#define` before `#include <ESPNowMesh.h>`)

| Define                        | Default | Meaning                                   |
|--------------------------------|---------|--------------------------------------------|
| `ENMESH_BEACON_INTERVAL_MS`   | 1000    | Master beacon interval (±50 ms jitter)     |
| `ENMESH_SLAVE_HB_INTERVAL_S`  | 300     | Slave → master heartbeat interval (s)      |
| `ENMESH_SLAVE_TIMEOUT_S`      | HB × 5  | Slave considered disconnected after (s)    |
| `ENMESH_CHANNEL_SCAN_WAIT_MS` | 3000    | Wait time per channel during scan (ms)     |
| `ENMESH_MAX_SLAVES`           | 8       | Max slaves tracked by the master           |
| `ENMESH_NO_AUTO_BEACON`       | (unset) | Disable the automatic empty beacon; call `sendBeacon()` yourself |

## WiFi power save and ESP-NOW reliability

**This is the single most common cause of "flaky" ESP-NOW behavior on ESP32,
and it's worth understanding even if you never touch the setting yourself.**

By default, the ESP32 WiFi driver enables **modem sleep** (`WIFI_PS_MIN_MODEM`)
whenever the station interface is not associated to an access point — which
is exactly the situation ESP-NOW-only nodes are in. With modem sleep active,
the radio periodically powers down and only wakes up around DTIM beacon
intervals to save energy. Since ESP-NOW frames are not synchronized to that
schedule, incoming packets can arrive while the radio is asleep and are
simply **missed**, or are only processed after a delay of tens to hundreds
of milliseconds. In practice this shows up as:

- Intermittent, hard-to-reproduce packet loss.
- Slaves that appear to "disconnect" even though the master is transmitting
  fine.
- Random, highly variable latency on otherwise simple point-to-point sends.
- The problem getting *worse* the longer the radio has been idle, and
  seemingly "fixed" by unrelated activity that happens to keep the radio
  awake (e.g. Serial prints, WiFi scans).

This is a known characteristic of the ESP32 WiFi/ESP-NOW stack (not a bug
specific to this library), and it affects both master and slave roles.

### Mitigation implemented in this library

`ESPNowMesh::begin()` calls:

```cpp
esp_wifi_set_ps(WIFI_PS_NONE);
```

immediately after `esp_now_init()` succeeds, for **both** the master and the
slave role. This disables WiFi modem sleep entirely, keeping the radio
continuously active so ESP-NOW frames are received promptly and reliably.
You do not need to do anything to benefit from this — it is applied
automatically inside `begin()`.

**Do not call `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` or `WIFI_PS_MAX_MODEM`
after `Mesh.begin()`** — doing so re-enables modem sleep and reintroduces the
packet loss/latency problem described above.

### Trade-off: power consumption

Keeping the radio always on draws meaningfully more current than letting it
sleep — this matters for battery-powered slaves. Because ESP-NOW timing is
incompatible with modem sleep, you cannot have both "always-responsive
ESP-NOW" and "WiFi power save" at the same time. If a battery-powered node
needs real energy savings, the recommended approach is **duty-cycling at the
application level** instead of relying on modem sleep:

- Use `esp_deep_sleep()` (or light sleep) between operational cycles, waking
  on a timer or an external interrupt.
- On each wake-up, call `Mesh.begin()` again (pairing data is already
  persisted in NVS, so no re-pairing is needed), do your work — send a
  status, read a beacon if you need the master's latest state — and go back
  to sleep.
- Size the cycle so it's compatible with the master's beacon interval
  (`ENMESH_BEACON_INTERVAL_MS`) and the slave's own heartbeat interval
  (`ENMESH_SLAVE_HB_INTERVAL_S`), so the slave doesn't get flagged as timed
  out (`ENMESH_SLAVE_TIMEOUT_S`) between wake-ups.

This trades continuous connectivity for a much lower average current draw,
without fighting the WiFi driver's power-save/ESP-NOW timing conflict.

## Limitations

- Payload is capped at `ENMESH_PAYLOAD_LEN` (102 bytes); larger payloads are
  silently truncated by `sendBeacon()`/`sendStatus()`/`sendCommand()`.
- `network_id` is limited to 15 characters (+ null terminator).
- ESP-NOW's own hard limits apply: broadcast/unicast without encryption in
  this library, and the usual ESP-NOW peer-count ceiling.
- Multi-hop relay increases exposure to collisions on congested channels;
  the small random jitter on relay helps but doesn't eliminate this.

## License

See repository metadata / author information in
[`library.properties`](library.properties).
