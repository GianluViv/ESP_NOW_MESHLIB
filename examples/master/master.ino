/**
 * ESPNowMesh — esempio MASTER
 *
 * Il master:
 *  - Si connette al WiFi (opzionale; se presente la lib usa il canale del router)
 *  - Inizializza la mesh come master con network ID "MY_MESH_NET"
 *  - Ogni secondo invia un beacon con il proprio stato (es. velocità ventilatori)
 *  - Riceve lo stato degli slave tramite callback onStatus()
 *  - Attiva il pairing premendo il pulsante su GPIO0 per 3 secondi
 *
 * Payload applicativo: struct MyBeaconPayload (definita dall'utente).
 * Il payload viene copiato nel campo opaque del pacchetto beacon.
 *
 * NOTA — risparmio energetico WiFi: Mesh.begin() disabilita automaticamente
 * il modem sleep (esp_wifi_set_ps(WIFI_PS_NONE)) perché altrimenti il radio
 * ESP-NOW perde o ritarda pacchetti quando va in sleep tra un beacon DTIM e
 * l'altro. Questo tiene il radio sempre attivo: se il master è alimentato
 * da rete non è un problema, ma non richiamare esp_wifi_set_ps(WIFI_PS_MIN_MODEM)
 * o WIFI_PS_MAX_MODEM dopo begin(), altrimenti si reintroduce il problema.
 * Vedi il README, sezione "WiFi power save", per i dettagli.
 */

#include <WiFi.h>
#include <ESPNowMesh.h>

// ---- Configurazione ---------------------------------------------------------

#define NETWORK_ID    "MY_MESH_NET"   // max 15 caratteri
#define WIFI_SSID     ""              // lascia vuoto per usare solo ESP-NOW senza WiFi
#define WIFI_PASS     ""
#define BTN_PAIRING   0              // GPIO per avvio pairing (active low, es. BOOT button)

// ---- Payload applicativo (definito dall'utente) -----------------------------

struct MyBeaconPayload {
  uint8_t  speed_pct;    // velocità ventilatore 0-100%
  uint8_t  state;        // stato macchina (enum applicativo)
  int16_t  temp_x10;     // temperatura ×10 (es. 215 = 21.5°C)
};

struct MyStatusPayload {
  uint8_t  alarm_code;
  uint8_t  slave_state;
};

// ---- Variabili applicative --------------------------------------------------

static uint8_t  g_speed   = 50;
static uint8_t  g_state   = 1;
static int16_t  g_temp    = 215;

static uint32_t g_lastBeaconSend = 0;
static uint32_t g_btnPressedAt  = 0;

// ---- Callbacks --------------------------------------------------------------

void onSlaveStatus(const uint8_t senderMac[6], const uint8_t *payload, size_t len) {
  if (len < sizeof(MyStatusPayload)) return;

  MyStatusPayload sp;
  memcpy(&sp, payload, sizeof(sp));

  Serial.printf("[MASTER] STATUS da %02X:%02X:%02X:%02X:%02X:%02X  alarm=%d state=%d\n",
    senderMac[0], senderMac[1], senderMac[2],
    senderMac[3], senderMac[4], senderMac[5],
    sp.alarm_code, sp.slave_state);
}

void onPairingDone(bool isMaster) {
  Serial.println("[MASTER] Pairing completato — slave accoppiato.");
  // Aggiungi qui feedback LED/buzzer
}

// ---- Setup ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PAIRING, INPUT_PULLUP);

  // Connessione WiFi opzionale — se configurata, la lib usa il canale del router
  if (strlen(WIFI_SSID) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi...");
    // La libreria attende autonomamente la connessione (max 15 s) dentro begin()
  }

  // Registra le callback prima di begin()
  Mesh.onStatus(onSlaveStatus);
  Mesh.onPaired(onPairingDone);

  if (!Mesh.begin(true, NETWORK_ID)) {
    Serial.println("ESPNowMesh init FALLITA");
    while (true) delay(1000);
  }

  Serial.printf("ESPNowMesh MASTER pronto — canale %d\n", Mesh.activeChannel());
}

// ---- Loop -------------------------------------------------------------------

void loop() {
  // ---- Invio beacon ogni secondo -------------------------------------------
  if (millis() - g_lastBeaconSend >= 1000) {
    MyBeaconPayload bp;
    bp.speed_pct = g_speed;
    bp.state     = g_state;
    bp.temp_x10  = g_temp;

    Mesh.sendBeacon(&bp, sizeof(bp));
    g_lastBeaconSend = millis();

    Serial.printf("[MASTER] Beacon inviato — speed=%d%% state=%d temp=%.1f°C  slave=%d\n",
      g_speed, g_state, g_temp / 10.0f, Mesh.slaveCount());
  }

  // ---- Gestione pulsante pairing -------------------------------------------
  // Long press 3 s su BTN_PAIRING → avvia pairing
  if (digitalRead(BTN_PAIRING) == LOW) {
    if (g_btnPressedAt == 0) g_btnPressedAt = millis();
    if (!Mesh.isPairing() && millis() - g_btnPressedAt >= 3000) {
      Mesh.startPairing();
      Serial.println("[MASTER] Modalità pairing avviata — in attesa slave...");
      g_btnPressedAt = 0;
    }
  } else {
    g_btnPressedAt = 0;
  }
}
