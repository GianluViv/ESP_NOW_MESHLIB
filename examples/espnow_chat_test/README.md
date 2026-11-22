# ESPNowMesh Chat Test

Applicazione di test PlatformIO per verificare l'invio e la ricezione di messaggi di testo da tastiera (via Monitor Seriale) usando la libreria `ESPNowMesh`.

## Requisiti

- 2 dev board **ESP32** (`esp32dev`)
- PlatformIO (o `pioarduino`)

## 1. Compilazione e Upload

Il progetto definisce due environment in `platformio.ini` sullo stesso `src/main.cpp`:
- `master` (`-DIS_MASTER=1`)
- `slave` (`-DIS_MASTER=0`)

Carica i firmware sulle due schede (sostituisci `/dev/ttyUSB0` e `/dev/ttyUSB1` con le tue porte):

```bash
# Scheda 1: Master
pio run -e master -t upload --upload-port /dev/ttyUSB0

# Scheda 2: Slave
pio run -e slave -t upload --upload-port /dev/ttyUSB1
```

## 2. Avvio dei Monitor Seriali

Apri due terminali separati a `115200` baud (con `--echo` per vedere cosa digiti prima di premere Invio):

```bash
pio device monitor -p /dev/ttyUSB0 -b 115200 --echo
pio device monitor -p /dev/ttyUSB1 -b 115200 --echo
```

## 3. Pairing e Utilizzo

1. **Accendi prima il Master**, poi lo **Slave** (se lo Slave non rileva nessun Master durante la scansione dei canali WiFi, si riavvia automaticamente finché non ne trova uno).
2. Tieni premuto il pulsante **BOOT (GPIO 0)** per **3 secondi** su entrambe le schede.
3. Sul monitor vedrai `[PAIRING] Completato!` (lo Slave si riavvia automaticamente per applicare canale e `network_id` salvati in NVS).
4. Scrivi un messaggio in uno dei due terminali e premi **Invio** per inviarlo all'altra scheda (massimo 101 caratteri per messaggio).

## Note importanti

- **Inizializzazione WiFi sul Master**: quando il Master non si connette a un router WiFi, `main.cpp` chiama `WiFi.mode(WIFI_STA); WiFi.disconnect();` prima di `Mesh.begin(true, ...)` perché `ESPNowMesh::begin()` sul ramo Master presuppone che il driver WiFi sia già stato avviato.
- **Heartbeat dello Slave**: la libreria invia periodicamente pacchetti `PKT_STATUS` vuoti dal watchdog dello Slave; `main.cpp` li filtra controllando `msg[0] == '\0'`.
