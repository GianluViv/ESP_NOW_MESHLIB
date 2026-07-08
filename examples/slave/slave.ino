/**
 * ESPNowMesh — esempio SLAVE
 *
 * Lo slave:
 *  - All'avvio cerca il master eseguendo la scan dei canali WiFi
 *  - Se non trova il master si riavvia automaticamente
 *  - Riceve beacon dal master e aggiorna il proprio stato locale
 *  - Ogni ENMESH_SLAVE_HB_INTERVAL_S secondi invia il proprio stato al master
 *  - Notifica onMasterLost() se il master scompare per 30 minuti
 *  - Attiva il pairing premendo il pulsante su GPIO0 per 3 secondi
 *
 * Payload applicativo: stesso MyBeaconPayload/MyStatusPayload del master.
 * Lo slave deve solo definire le stesse struct — nessuna dipendenza hardcoded
 * dalla libreria sul contenuto del payload.
 *
 * NOTA — risparmio energetico WiFi: Mesh.begin() disabilita automaticamente
 * il modem sleep (esp_wifi_set_ps(WIFI_PS_NONE)) perché altrimenti il radio
 * ESP-NOW perde o ritarda i beacon del master quando va in sleep tra un DTIM
 * e l'altro (causa tipica di slave che sembrano "disconnessi" a intermittenza).
 * Il rovescio della medaglia è un consumo più alto: se lo slave è a batteria
 * e serve risparmio energetico reale, non riattivare il power-save WiFi —
 * usa invece un duty-cycle applicativo (light/deep sleep tra un ciclo e
 * l'altro, re-inizializzando la mesh al risveglio). Vedi il README, sezione
 * "WiFi power save", per i dettagli e le alternative.
 */

#include <ESPNowMesh.h>

// ---- Configurazione ---------------------------------------------------------

#define NETWORK_ID    "MY_MESH_NET"  // deve corrispondere al master
#define BTN_PAIRING   0             // GPIO per avvio pairing (active low, es. BOOT button)

// ---- Payload applicativo (identico al master) -------------------------------

struct MyBeaconPayload {
  uint8_t  speed_pct;
  uint8_t  state;
  int16_t  temp_x10;
};

struct MyStatusPayload {
  uint8_t  alarm_code;
  uint8_t  slave_state;
};

// ---- Variabili applicative --------------------------------------------------

static uint8_t  g_masterSpeed = 0;
static uint8_t  g_masterState = 0;
static float    g_masterTemp  = 0.0f;

static uint8_t  g_myAlarm     = 0;
static uint8_t  g_myState     = 0;

static uint32_t g_btnPressedAt = 0;

// ---- Callbacks --------------------------------------------------------------

void onMasterBeacon(const uint8_t *payload, size_t len) {
  if (len < sizeof(MyBeaconPayload)) return;

  MyBeaconPayload bp;
  memcpy(&bp, payload, sizeof(bp));

  g_masterSpeed = bp.speed_pct;
  g_masterState = bp.state;
  g_masterTemp  = bp.temp_x10 / 10.0f;

  Serial.printf("[SLAVE] Beacon — speed=%d%% state=%d temp=%.1f°C\n",
    g_masterSpeed, g_masterState, g_masterTemp);

  // Applica qui la logica dello slave (es. imposta velocità motore)
}

void onMasterLost() {
  Serial.println("[SLAVE] Master perso! Spegni motori / vai in sicurezza.");
  g_myState = 0;
  // Aggiungi qui la logica di sicurezza (es. motori off)
}

void onPairingDone(bool isMaster) {
  Serial.println("[SLAVE] Pairing completato — in attesa di riavvio...");
  // Il riavvio avviene automaticamente nella libreria dopo l'ACK
}

// ---- Helper: invia il proprio stato al master ------------------------------

void sendMyStatus() {
  MyStatusPayload sp;
  sp.alarm_code  = g_myAlarm;
  sp.slave_state = g_myState;
  Mesh.sendStatus(&sp, sizeof(sp));
  Serial.printf("[SLAVE] Status inviato — alarm=%d state=%d\n",
    sp.alarm_code, sp.slave_state);
}

// ---- Setup ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PAIRING, INPUT_PULLUP);

  // Registra le callback prima di begin()
  Mesh.onBeacon(onMasterBeacon);
  Mesh.onMasterLost(onMasterLost);
  Mesh.onPaired(onPairingDone);

  // Lo slave usa il network_id salvato in NVS dopo il pairing.
  // Alla prima accensione (prima del pairing) il network_id passato qui
  // viene ignorato: lo slave accetta il PAIRING_REQ da qualsiasi master.
  // Dopo il pairing, il network_id del master viene salvato in NVS e
  // usato per filtrare i beacon nelle sessioni successive.
  if (!Mesh.begin(false, NETWORK_ID)) {
    Serial.println("ESPNowMesh init FALLITA (nessun master trovato — riavvio)");
    // begin() riavvia automaticamente se la scan fallisce; questo punto
    // non viene mai raggiunto in condizioni normali.
    while (true) delay(1000);
  }

  Serial.printf("ESPNowMesh SLAVE pronto — canale %d  connesso=%s\n",
    Mesh.activeChannel(), Mesh.isConnected() ? "si" : "no (attesa beacon)");
}

// ---- Loop -------------------------------------------------------------------

void loop() {
  // ---- Gestione pulsante pairing ------------------------------------------
  // Long press 3 s su BTN_PAIRING → avvia pairing
  if (digitalRead(BTN_PAIRING) == LOW) {
    if (g_btnPressedAt == 0) g_btnPressedAt = millis();
    if (!Mesh.isPairing() && millis() - g_btnPressedAt >= 3000) {
      Mesh.startPairing();
      Serial.println("[SLAVE] Modalità pairing avviata — in attesa di PAIRING_REQ...");
      g_btnPressedAt = 0;
    }
  } else {
    g_btnPressedAt = 0;
  }

  // ---- Logica applicativa slave -------------------------------------------
  // Qui l'applicazione usa g_masterSpeed / g_masterState / g_masterTemp
  // per controllare attuatori, LED, display, ecc.
  // Il watchdog e l'invio periodico di status sono gestiti internamente dalla lib.

  delay(10);
}
