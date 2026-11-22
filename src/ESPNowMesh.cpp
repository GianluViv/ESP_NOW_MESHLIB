#include "ESPNowMesh.h"
#include "ESPNowMeshStorage.h"

#include <WiFi.h>
#include <esp_wifi.h>

// ---- Singleton & costanti ---------------------------------------------------

ESPNowMesh Mesh;

const uint8_t ESPNowMesh::_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Timeout spegnimento slave (30 minuti senza beacon)
static const uint32_t SLAVE_OFFLINE_MS = 30UL * 60UL * 1000UL;

// ============================================================================
// COSTRUTTORE
// ============================================================================

ESPNowMesh::ESPNowMesh()
  : _isMaster(false), _initialized(false), _pairing(false),
    _ttl(ENMESH_TTL_DEFAULT), _activeChannel(1), _seqId(0),
    _lastBeaconMs(0), _seenIdx(0), _slaveCount(0),
    _cbBeacon(nullptr), _cbStatus(nullptr), _cbCommand(nullptr),
    _cbPaired(nullptr), _cbMasterLost(nullptr),
    _scanning(false), _scanFound(false),
    _hTaskBeacon(nullptr), _hTaskWatchdog(nullptr)
{
  memset(_myMac,    0, sizeof(_myMac));
  memset(_networkId, 0, sizeof(_networkId));
  memset(_seen,     0, sizeof(_seen));
  memset(_slaves,   0, sizeof(_slaves));
}

// ============================================================================
// REGISTRAZIONE CALLBACKS
// ============================================================================

void ESPNowMesh::onBeacon(void (*cb)(const uint8_t *payload, size_t len)) {
  _cbBeacon = cb;
}

void ESPNowMesh::onStatus(void (*cb)(const uint8_t senderMac[6], const uint8_t *payload, size_t len)) {
  _cbStatus = cb;
}

void ESPNowMesh::onCommand(void (*cb)(const uint8_t *payload, size_t len)) {
  _cbCommand = cb;
}

void ESPNowMesh::onPaired(void (*cb)(bool isMaster)) {
  _cbPaired = cb;
}

void ESPNowMesh::onMasterLost(void (*cb)()) {
  _cbMasterLost = cb;
}

// ============================================================================
// STATO / DIAGNOSTICA
// ============================================================================

bool    ESPNowMesh::isMaster()      const { return _isMaster; }
bool    ESPNowMesh::isInitialized() const { return _initialized; }
bool    ESPNowMesh::isPairing()     const { return _pairing; }
uint8_t ESPNowMesh::activeChannel() const { return _activeChannel; }
uint32_t ESPNowMesh::lastBeaconMs() const { return _lastBeaconMs; }
uint8_t ESPNowMesh::slaveCount()    const { return _slaveCount; }

bool ESPNowMesh::isConnected() const {
  if (_isMaster) return true;
  if (_lastBeaconMs == 0) return false;
  return (millis() - _lastBeaconMs) < (uint32_t)ENMESH_SLAVE_TIMEOUT_S * 1000UL;
}

bool ESPNowMesh::getSlaveInfo(uint8_t index, ENMeshSlaveInfo &out) const {
  if (index >= _slaveCount) return false;
  out = _slaves[index];
  return true;
}

void ESPNowMesh::clearPairingData() {
  ENMeshStorage::clear();
}

// ============================================================================
// TABELLA ANTI-LOOP
// ============================================================================

bool ESPNowMesh::isSeen(const uint8_t *mac, uint16_t seq) {
  for (uint8_t i = 0; i < ENMESH_SEEN_TABLE_SIZE; i++) {
    if (_seen[i].seq_id == seq &&
        memcmp(_seen[i].origin_mac, mac, 6) == 0) {
      return true;
    }
  }
  return false;
}

void ESPNowMesh::markSeen(const uint8_t *mac, uint16_t seq) {
  memcpy(_seen[_seenIdx].origin_mac, mac, 6);
  _seen[_seenIdx].seq_id = seq;
  _seenIdx = (_seenIdx + 1) % ENMESH_SEEN_TABLE_SIZE;
}

// ============================================================================
// GESTIONE PEER
// ============================================================================

void ESPNowMesh::ensurePeer(const uint8_t *mac) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;      // segue il canale WiFi corrente
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
}

// ============================================================================
// COSTRUZIONE E INVIO PACCHETTI
// ============================================================================

void ESPNowMesh::buildPkt(ENMeshPacket &pkt, uint8_t type) {
  memset(&pkt, 0, sizeof(pkt));
  strncpy(pkt.network_id, _networkId, ENMESH_NETWORK_ID_LEN - 1);
  memcpy(pkt.sender_mac, _myMac, 6);
  pkt.packet_type = type;
  pkt.seq_id      = ++_seqId;
  pkt.ttl         = _ttl;
}

void ESPNowMesh::relay(const ENMeshPacket *pkt) {
  ENMeshPacket relay_pkt = *pkt;
  relay_pkt.ttl--;
  const uint32_t jitter = esp_random() % 9;  // 0..8 ms anti-collisione
  if (jitter > 0) delay(jitter);
  esp_now_send(_broadcast, (const uint8_t *)&relay_pkt, sizeof(relay_pkt));
}

bool ESPNowMesh::sendBroadcast(ENMeshPacket &pkt) {
  markSeen(pkt.sender_mac, pkt.seq_id);  // self-relay prevention
  return esp_now_send(_broadcast, (const uint8_t *)&pkt, sizeof(pkt)) == ESP_OK;
}

bool ESPNowMesh::sendUnicast(const uint8_t *mac, ENMeshPacket &pkt) {
  ensurePeer(mac);
  return esp_now_send(mac, (const uint8_t *)&pkt, sizeof(pkt)) == ESP_OK;
}

// ---- API pubblica invio -----------------------------------------------------

bool ESPNowMesh::sendBeacon(const void *payload, size_t len) {
  if (!_isMaster || !_initialized) return false;
  ENMeshPacket pkt;
  buildPkt(pkt, ENMESH_PKT_BEACON);
  if (payload && len > 0) {
    const size_t copy_len = (len > ENMESH_PAYLOAD_LEN) ? ENMESH_PAYLOAD_LEN : len;
    memcpy(pkt.payload, payload, copy_len);
  }
  return sendBroadcast(pkt);
}

bool ESPNowMesh::sendStatus(const void *payload, size_t len) {
  if (_isMaster || !_initialized) return false;
  ENMeshPacket pkt;
  buildPkt(pkt, ENMESH_PKT_STATUS);
  if (payload && len > 0) {
    const size_t copy_len = (len > ENMESH_PAYLOAD_LEN) ? ENMESH_PAYLOAD_LEN : len;
    memcpy(pkt.payload, payload, copy_len);
  }
  return sendBroadcast(pkt);
}

bool ESPNowMesh::sendCommand(const void *payload, size_t len) {
  if (!_isMaster || !_initialized) return false;
  ENMeshPacket pkt;
  buildPkt(pkt, ENMESH_PKT_COMMAND);
  if (payload && len > 0) {
    const size_t copy_len = (len > ENMESH_PAYLOAD_LEN) ? ENMESH_PAYLOAD_LEN : len;
    memcpy(pkt.payload, payload, copy_len);
  }
  return sendBroadcast(pkt);
}

// ============================================================================
// PAIRING
// ============================================================================

void ESPNowMesh::startPairing() {
  _pairing = true;
}

// ============================================================================
// HANDLER PACCHETTI RICEVUTI
// ============================================================================

void ESPNowMesh::handleBeacon(const ENMeshPacket *pkt) {
  if (_isMaster) return;  // il master non elabora i propri beacon in eco

  _lastBeaconMs = millis();

  if (_cbBeacon) {
    _cbBeacon(pkt->payload, ENMESH_PAYLOAD_LEN);
  }
}

void ESPNowMesh::handlePairingReq(const ENMeshPacket *pkt, const uint8_t *srcMac) {
  // Slave già accoppiato con QUESTO master ma non in modalità pairing:
  // ri-invia l'ACK perché il precedente potrebbe essersi perso.
  if (!_pairing && strncmp(pkt->network_id, _networkId, ENMESH_NETWORK_ID_LEN) == 0) {
    ENMeshPacket ack;
    buildPkt(ack, ENMESH_PKT_PAIRING_ACK);
    markSeen(ack.sender_mac, ack.seq_id);
    sendUnicast(srcMac, ack);
    return;
  }

  if (!_pairing) return;  // PAIRING_REQ ignorato se slave non in modalità pairing

  // Salva network_id e canale del master in NVS
  strncpy(_networkId, pkt->network_id, ENMESH_NETWORK_ID_LEN - 1);
  ENMeshStorage::saveNetworkId(_networkId);
  ENMeshStorage::saveChannel(_activeChannel);

  // Invia ACK al master
  ENMeshPacket ack;
  buildPkt(ack, ENMESH_PKT_PAIRING_ACK);
  strncpy(ack.network_id, pkt->network_id, ENMESH_NETWORK_ID_LEN - 1);
  markSeen(ack.sender_mac, ack.seq_id);
  sendUnicast(srcMac, ack);

  _pairing      = false;
  _lastBeaconMs = 0;

  if (_cbPaired) _cbPaired(false);

  delay(500);     // attende la trasmissione dell'ACK
  esp_restart();  // riavvia per applicare il nuovo network_id
}

void ESPNowMesh::handlePairingAck(const ENMeshPacket *pkt, const uint8_t *srcMac) {
  (void)pkt;
  (void)srcMac;
  _pairing = false;
  if (_cbPaired) _cbPaired(true);
}

void ESPNowMesh::handleStatus(const ENMeshPacket *pkt) {
  if (!_isMaster) return;
  updateSlave(pkt->sender_mac, millis());
  if (_cbStatus) {
    _cbStatus(pkt->sender_mac, pkt->payload, ENMESH_PAYLOAD_LEN);
  }
}

void ESPNowMesh::handleCommand(const ENMeshPacket *pkt) {
  if (_isMaster) return;
  if (_cbCommand) {
    _cbCommand(pkt->payload, ENMESH_PAYLOAD_LEN);
  }
}

void ESPNowMesh::updateSlave(const uint8_t *mac, uint32_t nowMs) {
  for (uint8_t i = 0; i < _slaveCount; i++) {
    if (memcmp(_slaves[i].mac, mac, 6) == 0) {
      _slaves[i].last_seen_ms = nowMs;
      _slaves[i].connected    = true;
      return;
    }
  }
  if (_slaveCount < ENMESH_MAX_SLAVES) {
    memcpy(_slaves[_slaveCount].mac, mac, 6);
    _slaves[_slaveCount].last_seen_ms = nowMs;
    _slaves[_slaveCount].connected    = true;
    _slaveCount++;
  }
}

// ============================================================================
// CALLBACK ESP-NOW
// ============================================================================

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowMesh::recvCb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *srcMac = info->src_addr;
#else
void ESPNowMesh::recvCb(const uint8_t *srcMac, const uint8_t *data, int len) {
#endif
  if (len != static_cast<int>(sizeof(ENMeshPacket))) return;

  const ENMeshPacket *pkt = reinterpret_cast<const ENMeshPacket *>(data);

  // Fase di scanning: registra il primo beacon valido trovato
  if (Mesh._scanning) {
    if (pkt->packet_type == ENMESH_PKT_BEACON ||
        pkt->packet_type == ENMESH_PKT_PAIRING_REQ) {
      Mesh._scanFound = true;
    }
    return;
  }

  // Anti-loop
  if (Mesh.isSeen(pkt->sender_mac, pkt->seq_id)) return;
  Mesh.markSeen(pkt->sender_mac, pkt->seq_id);

  // Filtro network_id: accetta pacchetti della propria rete o se non ancora accoppiato
  const bool hasNetId    = (Mesh._networkId[0] != '\0');
  const bool netIdMatch  = !hasNetId ||
    (strncmp(pkt->network_id, Mesh._networkId, ENMESH_NETWORK_ID_LEN) == 0);

  if (!netIdMatch) {
    // Relay trasparente per non bloccare reti vicine
    if (pkt->ttl > 1) Mesh.relay(pkt);
    return;
  }

  // Dispatch
  switch (pkt->packet_type) {
    case ENMESH_PKT_BEACON:
      Mesh.handleBeacon(pkt);
      break;
    case ENMESH_PKT_PAIRING_REQ:
      if (!Mesh._isMaster) {
        Mesh.handlePairingReq(pkt, srcMac);
      }
      break;
    case ENMESH_PKT_PAIRING_ACK:
      if (Mesh._isMaster) {
        Mesh.handlePairingAck(pkt, srcMac);
      }
      break;
    case ENMESH_PKT_STATUS:
      Mesh.handleStatus(pkt);
      break;
    case ENMESH_PKT_COMMAND:
      Mesh.handleCommand(pkt);
      break;
    default:
      break;
  }

  // Relay se ci sono ancora hop disponibili
  if (pkt->ttl > 1) Mesh.relay(pkt);
}

// ============================================================================
// SCAN CANALI
// ============================================================================

uint8_t ESPNowMesh::scanChannels() {
  if (WiFi.status() == WL_CONNECTED) {
    return static_cast<uint8_t>(WiFi.channel());
  }

  const uint8_t order[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 13};
  const uint8_t n       = sizeof(order) / sizeof(order[0]);

  _scanning = true;

  for (uint8_t i = 0; i < n; i++) {
    const uint8_t ch = order[i];
    _scanFound = false;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    const uint32_t t0 = millis();
    while (millis() - t0 < ENMESH_CHANNEL_SCAN_WAIT_MS) {
      if (_scanFound) {
        _scanning = false;
        return ch;
      }
      delay(10);
    }
  }

  _scanning = false;
  return 0;
}

// ============================================================================
// INIT ESP-NOW
// ============================================================================

bool ESPNowMesh::initEspNow(uint8_t channel) {
  _activeChannel = channel;
  esp_wifi_get_mac(WIFI_IF_STA, _myMac);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) return false;

  // Disabilita il risparmio energetico del modem WiFi: se attivo, il radio
  // va periodicamente in sleep e ritarda/perde i pacchetti ESP-NOW in arrivo
  // (vedi sezione "WiFi power save" nel README).
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(recvCb));
  esp_now_register_send_cb(sendCb);
  ensurePeer(_broadcast);
  return true;
}

// ============================================================================
// BEGIN
// ============================================================================

bool ESPNowMesh::begin(bool isMaster, const char *networkId, uint8_t ttl) {
  _isMaster = isMaster;
  _ttl      = ttl;
  strncpy(_networkId, networkId, ENMESH_NETWORK_ID_LEN - 1);

  memset(_seen,   0, sizeof(_seen));
  memset(_slaves, 0, sizeof(_slaves));
  _seenIdx    = 0;
  _slaveCount = 0;
  _seqId      = 0;

  if (_isMaster) {
    // ---- MASTER ----
    uint8_t channel    = 1;
    bool    useSetCh   = true;

    if (WiFi.status() == WL_CONNECTED) {
      // WiFi già connesso: usa il canale imposto dal router
      channel  = static_cast<uint8_t>(WiFi.channel());
      useSetCh = false;
    } else if (WiFi.getMode() != WIFI_OFF) {
      // WiFi avviato ma non ancora connesso: attendi fino a 15 s
      const uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(100);
      }
      if (WiFi.status() == WL_CONNECTED) {
        channel  = static_cast<uint8_t>(WiFi.channel());
        useSetCh = false;
      }
    }

    esp_wifi_get_mac(WIFI_IF_STA, _myMac);
    _activeChannel = channel;

    if (useSetCh) {
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    if (esp_now_init() != ESP_OK) return false;

    // Disabilita il risparmio energetico del modem WiFi (vedi README, sezione
    // "WiFi power save"): senza questa chiamata il radio va periodicamente in
    // sleep e i pacchetti ESP-NOW in arrivo vengono ritardati o persi.
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(recvCb));
    esp_now_register_send_cb(sendCb);
    ensurePeer(_broadcast);

    ENMeshStorage::saveChannel(channel);

    xTaskCreate(taskBeacon, "ENMESH_TX", 3 * 1024, this, 2, &_hTaskBeacon);

  } else {
    // ---- SLAVE ----

    // Modalità AP_STA per coesistenza ESP-NOW + WiFi client
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();

    // Init ESP-NOW sul canale 1 temporaneo (serve prima della scan)
    esp_wifi_get_mac(WIFI_IF_STA, _myMac);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) return false;

    // Disabilita il risparmio energetico del modem WiFi (vedi README, sezione
    // "WiFi power save"): senza questa chiamata il radio va periodicamente in
    // sleep e i pacchetti ESP-NOW in arrivo vengono ritardati o persi.
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(recvCb));
    esp_now_register_send_cb(sendCb);
    ensurePeer(_broadcast);

    // Legge canale e network_id da NVS (se presenti)
    uint8_t savedChannel = 0;
    char    savedNetId[ENMESH_NETWORK_ID_LEN] = {};
    const bool hadData = ENMeshStorage::load(savedChannel, savedNetId);

    if (hadData && savedNetId[0] != '\0') {
      strncpy(_networkId, savedNetId, ENMESH_NETWORK_ID_LEN - 1);
    }

    uint8_t channel = savedChannel;

    if (channel != 0) {
      // Canale memorizzato: attendi 10 s su quel canale prima di scan completa
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      _scanning  = true;
      _scanFound = false;
      const uint32_t t0 = millis();
      while (millis() - t0 < 10000) {
        if (_scanFound) break;
        delay(10);
      }
      _scanning = false;

      if (!_scanFound) {
        channel = 0;  // forza scan completa
      }
    }

    if (channel == 0) {
      channel = scanChannels();
      if (channel == 0) {
        delay(500);
        esp_restart();
      }
      // Salva il canale solo se già accoppiato
      if (_networkId[0] != '\0') {
        ENMeshStorage::saveChannel(channel);
      }
    }

    _activeChannel = channel;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    _lastBeaconMs = 0;  // sentinella: nessun beacon ancora ricevuto

    xTaskCreate(taskWatchdog, "ENMESH_WDG", 3 * 1024, this, 2, &_hTaskWatchdog);
  }

  _initialized = true;
  return true;
}

// ============================================================================
// TASK BEACON MASTER
// ============================================================================

void ESPNowMesh::taskBeacon(void *pv) {
  ESPNowMesh *self = static_cast<ESPNowMesh *>(pv);

  while (true) {
    delay(ENMESH_BEACON_INTERVAL_MS - 50 + (esp_random() % 100));  // ~1 Hz ±50 ms

    // Timeout slave: marca disconnected quelli silenziosi
    const uint32_t now = millis();
    for (uint8_t i = 0; i < self->_slaveCount; i++) {
      ENMeshSlaveInfo &s = self->_slaves[i];
      if (s.connected &&
          (now - s.last_seen_ms) > (uint32_t)ENMESH_SLAVE_TIMEOUT_S * 1000UL) {
        s.connected = false;
      }
    }

    ENMeshPacket pkt;

    if (self->_pairing) {
      self->buildPkt(pkt, ENMESH_PKT_PAIRING_REQ);
    } else {
      // In modalità normale chiama sendBeacon() con payload nullo se l'utente
      // non ha ancora inviato dati — il beacon tiene vivi gli slave.
      // L'utente può chiamare sendBeacon() autonomamente nel proprio task/loop
      // e disabilitare questo beacon automatico definendo ENMESH_NO_AUTO_BEACON.
#ifndef ENMESH_NO_AUTO_BEACON
      self->buildPkt(pkt, ENMESH_PKT_BEACON);
      // payload lasciato azzerato (nessun dato applicativo automatico)
      self->sendBroadcast(pkt);
      continue;
#else
      delay(ENMESH_BEACON_INTERVAL_MS);
      continue;
#endif
    }

    self->markSeen(pkt.sender_mac, pkt.seq_id);
    esp_now_send(self->_broadcast, (const uint8_t *)&pkt, sizeof(pkt));
  }
}

// ============================================================================
// TASK WATCHDOG SLAVE
// ============================================================================

void ESPNowMesh::taskWatchdog(void *pv) {
  ESPNowMesh *self = static_cast<ESPNowMesh *>(pv);

  bool     pairingScanDone = false;
  bool     firstHbSent     = false;
  uint32_t hbCounter       = 0;

  while (true) {
    delay(1000);

    // Modalità pairing slave: scan canali una sola volta per sessione
    if (self->_pairing) {
      if (!pairingScanDone) {
        const uint8_t ch = self->scanChannels();
        if (ch != 0) {
          self->_activeChannel = ch;
          esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        }
        pairingScanDone = true;
      }
      continue;
    }

    pairingScanDone = false;

    // Nessun beacon ancora ricevuto: standby indefinito
    if (self->_lastBeaconMs == 0) continue;

    // Primo beacon: invia subito HB al master
    if (!firstHbSent) {
      self->sendStatus(nullptr, 0);
      firstHbSent = true;
      hbCounter   = 0;
    }

    // HB periodico
    hbCounter++;
    if (hbCounter >= ENMESH_SLAVE_HB_INTERVAL_S) {
      self->sendStatus(nullptr, 0);
      hbCounter = 0;
    }

    // Timeout beacon → notifica master perso
    const uint32_t elapsed = millis() - self->_lastBeaconMs;
    if (elapsed >= SLAVE_OFFLINE_MS) {
      if (self->_cbMasterLost) self->_cbMasterLost();
    }
  }
}
