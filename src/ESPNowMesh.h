#ifndef ESPNOW_MESH_H
#define ESPNOW_MESH_H

#include <Arduino.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include "ESPNowMeshPacket.h"

// ---- Costanti configurabili via #define prima dell'include ------------------

#ifndef ENMESH_BEACON_INTERVAL_MS
#define ENMESH_BEACON_INTERVAL_MS   1000   // intervallo beacon master (ms, ±50 jitter)
#endif

#ifndef ENMESH_SLAVE_HB_INTERVAL_S
#define ENMESH_SLAVE_HB_INTERVAL_S  300    // intervallo heartbeat slave→master (secondi)
#endif

#ifndef ENMESH_SLAVE_TIMEOUT_S
#define ENMESH_SLAVE_TIMEOUT_S      (ENMESH_SLAVE_HB_INTERVAL_S * 5)  // timeout slave (s)
#endif

#ifndef ENMESH_CHANNEL_SCAN_WAIT_MS
#define ENMESH_CHANNEL_SCAN_WAIT_MS 3000   // attesa per canale durante scan (ms)
#endif

#ifndef ENMESH_MAX_SLAVES
#define ENMESH_MAX_SLAVES           8      // slave tracciati dal master
#endif

// ---- Info slave (lato master) -----------------------------------------------
typedef struct {
  uint8_t  mac[6];
  uint32_t last_seen_ms;
  bool     connected;
} ENMeshSlaveInfo;

// ---- Classe principale -------------------------------------------------------
class ESPNowMesh {
public:
  ESPNowMesh();

  /**
   * Inizializza la mesh.
   * @param isMaster   true = questo nodo è master, false = slave
   * @param networkId  stringa identificativa della rete (max 15 char)
   * @param ttl        TTL iniziale dei pacchetti (default: ENMESH_TTL_DEFAULT)
   * @return true se l'init ha avuto successo
   */
  bool begin(bool isMaster, const char *networkId, uint8_t ttl = ENMESH_TTL_DEFAULT);

  // ---- Pairing ---------------------------------------------------------------

  /** Attiva la modalità pairing sul nodo corrente.
   *  Master: inizia a trasmettere PKT_PAIRING_REQ.
   *  Slave:  si mette in ascolto e risponde al primo PKT_PAIRING_REQ valido. */
  void startPairing();

  /** Ritorna true se il nodo è attualmente in modalità pairing. */
  bool isPairing() const;

  // ---- Invio payload ---------------------------------------------------------

  /**
   * Invia un beacon con payload applicativo (solo master).
   * @param payload  puntatore ai dati applicativi
   * @param len      dimensione in byte (max ENMESH_PAYLOAD_LEN)
   * @return true se esp_now_send() ha accettato il pacchetto
   */
  bool sendBeacon(const void *payload, size_t len);

  /**
   * Invia lo stato dello slave al master (solo slave).
   * @param payload  puntatore ai dati applicativi
   * @param len      dimensione in byte (max ENMESH_PAYLOAD_LEN)
   * @return true se esp_now_send() ha accettato il pacchetto
   */
  bool sendStatus(const void *payload, size_t len);

  /**
   * Invia un comando a tutti gli slave (solo master).
   * @param payload  puntatore ai dati applicativi
   * @param len      dimensione in byte (max ENMESH_PAYLOAD_LEN)
   * @return true se esp_now_send() ha accettato il pacchetto
   */
  bool sendCommand(const void *payload, size_t len);

  // ---- Callbacks -------------------------------------------------------------

  /** Chiamata sullo slave quando arriva un beacon dal master.
   *  @param cb  funzione(payload, len) */
  void onBeacon(void (*cb)(const uint8_t *payload, size_t len));

  /** Chiamata sul master quando arriva un PKT_STATUS da uno slave.
   *  @param cb  funzione(senderMac[6], payload, len) */
  void onStatus(void (*cb)(const uint8_t senderMac[6], const uint8_t *payload, size_t len));

  /** Chiamata sul master quando arriva un PKT_COMMAND.
   *  @param cb  funzione(payload, len) */
  void onCommand(void (*cb)(const uint8_t *payload, size_t len));

  /** Chiamata al completamento del pairing.
   *  @param cb  funzione(isMaster): true se chiamata sul master, false sullo slave */
  void onPaired(void (*cb)(bool isMaster));

  /** Chiamata sullo slave quando il master non si manifesta da TIMEOUT_SPEGNIMENTO_MS. */
  void onMasterLost(void (*cb)());

  // ---- Stato / diagnostica ---------------------------------------------------

  bool    isMaster()       const;
  bool    isInitialized()  const;

  /** Slave: true se ha ricevuto almeno un beacon e l'ultimo è recente (<ENMESH_SLAVE_TIMEOUT_S). */
  bool    isConnected()    const;

  uint8_t activeChannel()  const;
  uint32_t lastBeaconMs()  const;

  /** Master: numero di slave che hanno inviato almeno un PKT_STATUS. */
  uint8_t slaveCount()     const;

  /** Master: restituisce le info dello slave all'indice dato (0..slaveCount()-1). */
  bool    getSlaveInfo(uint8_t index, ENMeshSlaveInfo &out) const;

  // ---- Utilità ----------------------------------------------------------------

  /** Cancella i dati di pairing salvati in NVS (canale + network_id salvato dallo slave). */
  void clearPairingData();

private:
  // ---- Stato interno ---------------------------------------------------------
  bool     _isMaster;
  bool     _initialized;
  bool     _pairing;
  uint8_t  _ttl;
  uint8_t  _activeChannel;
  uint16_t _seqId;
  uint8_t  _myMac[6];
  char     _networkId[ENMESH_NETWORK_ID_LEN];

  uint32_t _lastBeaconMs;     // slave: millis() dell'ultimo beacon ricevuto (0 = mai)

  // Tabella anti-loop
  ENMeshSeenEntry _seen[ENMESH_SEEN_TABLE_SIZE];
  uint8_t         _seenIdx;

  // Slave list (solo master)
  ENMeshSlaveInfo _slaves[ENMESH_MAX_SLAVES];
  uint8_t         _slaveCount;

  // Callbacks registrate dall'utente
  void (*_cbBeacon) (const uint8_t *payload, size_t len);
  void (*_cbStatus) (const uint8_t senderMac[6], const uint8_t *payload, size_t len);
  void (*_cbCommand)(const uint8_t *payload, size_t len);
  void (*_cbPaired) (bool isMaster);
  void (*_cbMasterLost)();

  // Flag scanning canali
  volatile bool _scanning;
  volatile bool _scanFound;

  // ---- Handle task FreeRTOS --------------------------------------------------
  TaskHandle_t _hTaskBeacon;
  TaskHandle_t _hTaskWatchdog;

  // ---- Metodi privati --------------------------------------------------------
  void    buildPkt(ENMeshPacket &pkt, uint8_t type);
  bool    isSeen(const uint8_t *mac, uint16_t seq);
  void    markSeen(const uint8_t *mac, uint16_t seq);
  void    relay(const ENMeshPacket *pkt);
  void    ensurePeer(const uint8_t *mac);
  bool    sendBroadcast(ENMeshPacket &pkt);
  bool    sendUnicast(const uint8_t *mac, ENMeshPacket &pkt);

  void    handleBeacon(const ENMeshPacket *pkt);
  void    handlePairingReq(const ENMeshPacket *pkt, const uint8_t *srcMac);
  void    handlePairingAck(const ENMeshPacket *pkt, const uint8_t *srcMac);
  void    handleStatus(const ENMeshPacket *pkt);
  void    handleCommand(const ENMeshPacket *pkt);
  void    updateSlave(const uint8_t *mac, uint32_t nowMs);

  uint8_t scanChannels();
  bool    initEspNow(uint8_t channel);

  // Task FreeRTOS (statici — usano il singleton per accedere allo stato)
  static void taskBeacon(void *pv);
  static void taskWatchdog(void *pv);

  // Callback ESP-NOW (IDF 4.x / 5.x)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  static void recvCb(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
  static void recvCb(const uint8_t *mac, const uint8_t *data, int len);
#endif
  template <typename T>
  static void sendCb(const T *info, esp_now_send_status_t status) {
    (void)info;
    (void)status;
  }

  static const uint8_t _broadcast[6];
};

// Istanza globale accessibile ovunque (definita in ESPNowMesh.cpp)
extern ESPNowMesh Mesh;

#endif // ESPNOW_MESH_H
