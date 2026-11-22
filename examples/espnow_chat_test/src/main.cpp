/**
 * ESPNowMesh — Esempio di Chat Seriale (Master <-> Slave)
 *
 * Questo esempio mostra come realizzare un semplice sistema di messaggistica
 * bidirezionale tra due ESP32 usando la libreria ESPNowMesh.
 *
 * Come funziona la comunicazione nella libreria:
 *  - La libreria usa un'architettura Master / Slave:
 *      * Master -> Slave : invia con Mesh.sendCommand(), riceve con onCommand()
 *      * Slave  -> Master: invia con Mesh.sendStatus(),  riceve con onStatus()
 *  - Ogni pacchetto trasporta un payload grezzo fino a ENMESH_PAYLOAD_LEN (102 byte).
 *    In questo esempio inviamo semplicemente una stringa C terminata da '\0'.
 *
 * Flusso operativo passo-passo:
 *  1. Carica l'environment "master" sulla prima ESP32 e "slave" sulla seconda.
 *  2. Accendi prima il Master, poi lo Slave (se lo Slave non trova nessun
 *     Master durante la scansione iniziale dei canali, si riavvia da solo).
 *  3. Tieni premuto il pulsante BOOT (GPIO 0) per 3 secondi su entrambe le
 *     schede per avviare il pairing. Al termine, lo Slave salva canale e
 *     network_id in NVS e si riavvia automaticamente.
 *  4. Scrivi un messaggio sul Monitor Seriale (115200 baud) e premi Invio.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPNowMesh.h>

// ---- 1. Configurazione ------------------------------------------------------

// Il ruolo viene impostato in platformio.ini tramite -DIS_MASTER=1 o -DIS_MASTER=0
#ifndef IS_MASTER
#define IS_MASTER 1
#endif

// Nome logico della rete (massimo 15 caratteri).
// Lo Slave lo usa solo se non ha già dati di pairing salvati in NVS.
#define NETWORK_ID        "CHAT_MESH_NET"

// Pulsante BOOT integrato sulla maggior parte delle dev board ESP32 (attivo basso)
#define BTN_PAIRING       0
#define PAIRING_HOLD_MS   3000

// Lunghezza massima del testo inviabile: lasciamo 1 byte per il terminatore '\0'
#define MAX_MSG_LEN       (ENMESH_PAYLOAD_LEN - 1)

// ---- 2. Variabili globali ---------------------------------------------------

static char     g_serialBuf[MAX_MSG_LEN + 1];  // buffer per comporre la riga digitata
static size_t   g_serialLen    = 0;            // caratteri attualmente nel buffer
static uint32_t g_btnPressedAt = 0;            // timestamp inizio pressione BOOT

// ---- 3. Callback di ricezione ESPNowMesh ------------------------------------
// Nota: le callback vengono registrate PRIMA di chiamare Mesh.begin().

#if IS_MASTER
// Sul Master, tutti i pacchetti inviati dagli Slave arrivano qui (PKT_STATUS)
static void onSlaveMessage(const uint8_t senderMac[6], const uint8_t *payload, size_t len) {
  if (payload == nullptr || len == 0) return;

  // Copiamo il payload in un buffer locale garantendo il terminatore '\0'
  char msg[ENMESH_PAYLOAD_LEN + 1];
  size_t copyLen = (len > ENMESH_PAYLOAD_LEN) ? ENMESH_PAYLOAD_LEN : len;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  // IMPORTANTE: il watchdog interno dello Slave invia periodicamente un
  // PKT_STATUS vuoto (heartbeat) per segnalare che è vivo. Lo ignoriamo per
  // non stampare righe vuote nella chat.
  if (msg[0] == '\0') return;

  // Stampiamo gli ultimi 3 byte del MAC del mittente + il messaggio ricevuto
  Serial.printf("[SLAVE %02X:%02X:%02X] %s\n",
                senderMac[3], senderMac[4], senderMac[5], msg);
}
#else
// Sullo Slave, i messaggi inviati dal Master arrivano qui (PKT_COMMAND)
static void onMasterMessage(const uint8_t *payload, size_t len) {
  if (payload == nullptr || len == 0) return;

  char msg[ENMESH_PAYLOAD_LEN + 1];
  size_t copyLen = (len > ENMESH_PAYLOAD_LEN) ? ENMESH_PAYLOAD_LEN : len;
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  if (msg[0] == '\0') return;

  Serial.printf("[MASTER] %s\n", msg);
}

// Chiamata sullo Slave se il Master non invia più beacon per 30 minuti
static void onMasterLost() {
  Serial.println("[SISTEMA] Attenzione: connessione con il master persa.");
}
#endif

// Chiamata su entrambe le schede quando il pairing va a buon fine
static void onPairingDone(bool isMaster) {
  if (isMaster) {
    Serial.println("[PAIRING] Completato! Slave accoppiato.");
  } else {
    Serial.println("[PAIRING] Completato! Riavvio automatico in corso...");
  }
}

// ---- 4. Funzioni di invio e gestione input ----------------------------------

// Invia una stringa C al nodo remoto usando il canale corretto per il ruolo
static void sendChatMessage(const char *text) {
  size_t len = strlen(text);
  if (len == 0) return;

  // Includiamo il terminatore '\0' nel pacchetto così il ricevente ha già una C-string valida
  size_t payloadLen = len + 1;
  if (payloadLen > ENMESH_PAYLOAD_LEN) {
    payloadLen = ENMESH_PAYLOAD_LEN;
  }

#if IS_MASTER
  bool ok = Mesh.sendCommand(text, payloadLen);
#else
  bool ok = Mesh.sendStatus(text, payloadLen);
#endif

  if (ok) {
    Serial.printf("[TU] %s\n", text);
  } else {
    Serial.println("[ERRORE] Invio fallito (ESP-NOW non pronto).");
  }
}

// Legge i caratteri disponibili da Serial senza bloccare il loop.
// Quando l'utente preme Invio (\n o \r), invia il messaggio accumulato.
static void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    // Tasto Invio: invia il messaggio se il buffer non è vuoto
    if (c == '\n' || c == '\r') {
      if (g_serialLen > 0) {
        g_serialBuf[g_serialLen] = '\0';
        sendChatMessage(g_serialBuf);
        g_serialLen = 0;
      }
      continue;
    }

    // Backspace / Cancella: rimuove l'ultimo carattere digitato
    if (c == '\b' || c == 0x7F) {
      if (g_serialLen > 0) {
        g_serialLen--;
      }
      continue;
    }

    // Scarta altri caratteri di controllo non stampabili
    if ((uint8_t)c < 0x20) {
      continue;
    }

    // Accoda il carattere fino al limite massimo consentito dal pacchetto
    if (g_serialLen < MAX_MSG_LEN) {
      g_serialBuf[g_serialLen++] = c;
    }
  }
}

// Controlla la pressione prolungata (3 secondi) del pulsante BOOT per avviare il pairing
static void handlePairingButton() {
  if (digitalRead(BTN_PAIRING) == LOW) {
    if (g_btnPressedAt == 0) {
      g_btnPressedAt = millis();
    }
    if (!Mesh.isPairing() && (millis() - g_btnPressedAt >= PAIRING_HOLD_MS)) {
      Mesh.startPairing();
      Serial.println("[PAIRING] Modalità pairing attiva...");
      g_btnPressedAt = 0;
    }
  } else {
    g_btnPressedAt = 0;
  }
}

// ---- 5. Setup e Loop --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_PAIRING, INPUT_PULLUP);

  // 1) Registra sempre tutte le callback PRIMA di Mesh.begin()
  Mesh.onPaired(onPairingDone);
#if IS_MASTER
  Mesh.onStatus(onSlaveMessage);
#else
  Mesh.onCommand(onMasterMessage);
  Mesh.onMasterLost(onMasterLost);
#endif

  Serial.println();
  Serial.println("=== ESPNowMesh Chat Test ===");
  Serial.printf("Ruolo: %s\n", IS_MASTER ? "MASTER" : "SLAVE");

#if IS_MASTER
  // 2) PASSAGGIO FONDAMENTALE SUL MASTER (quando non si usa un router WiFi):
  //    ESPNowMesh::begin(true, ...) avvia il WiFi solo se è già stata chiamata
  //    WiFi.begin(). Se usi solo ESP-NOW senza router, devi inizializzare
  //    esplicitamente il driver WiFi in modalità Station prima di Mesh.begin(),
  //    altrimenti esp_now_init() fallisce.
  //    (Sul ramo Slave non serve perché la libreria chiama WiFi.mode(WIFI_AP_STA) da sola.)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
#endif

  // 3) Inizializza la mesh.
  //    Nota sullo Slave: se non trova nessun Master attivo durante la scansione
  //    dei canali, Mesh.begin(false, ...) esegue automaticamente esp_restart().
  if (!Mesh.begin(IS_MASTER, NETWORK_ID)) {
    Serial.println("[ERRORE] Inizializzazione ESPNowMesh fallita!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("Pronto sul canale %d.\n", Mesh.activeChannel());
  Serial.println("Scrivi un messaggio e premi Invio per inviare.");
  Serial.println("Tieni premuto BOOT (GPIO 0) per 3s per avviare il pairing.");
  Serial.println();
}

void loop() {
  handleSerialInput();
  handlePairingButton();
  delay(10);
}
