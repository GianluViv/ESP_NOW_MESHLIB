#ifndef ESPNOW_MESH_PACKET_H
#define ESPNOW_MESH_PACKET_H

#include <Arduino.h>

// Dimensione totale pacchetto ESP-NOW (limite hardware: 250 byte)
#define ENMESH_PKT_SIZE         128
#define ENMESH_NETWORK_ID_LEN   16
#define ENMESH_PAYLOAD_LEN      102   // 128 - 26 byte header
#define ENMESH_TTL_DEFAULT      3
#define ENMESH_SEEN_TABLE_SIZE  32

// ---- Tipi di pacchetto -------------------------------------------------------
typedef enum : uint8_t {
  ENMESH_PKT_BEACON      = 0x01,  // master → slaves: heartbeat
  ENMESH_PKT_PAIRING_REQ = 0x02,  // master → broadcast: richiesta pairing
  ENMESH_PKT_PAIRING_ACK = 0x03,  // slave → master: conferma pairing
  ENMESH_PKT_STATUS      = 0x04,  // slave → master: stato / allarme
  ENMESH_PKT_COMMAND     = 0x05,  // master → slave: comando diretto (futuro)
} ENMeshPktType;

// ---- Struttura pacchetto (packed, 128 byte fissi) ----------------------------
#pragma pack(push, 1)
typedef struct {
  char     network_id[ENMESH_NETWORK_ID_LEN]; // ID rete (C-string, null-terminated)
  uint8_t  sender_mac[6];                      // MAC STA del nodo originatore
  uint8_t  packet_type;                        // ENMeshPktType
  uint16_t seq_id;                             // numero sequenza anti-loop
  uint8_t  ttl;                                // hop rimanenti
  uint8_t  payload[ENMESH_PAYLOAD_LEN];        // payload opaque definito dall'utente
} ENMeshPacket;
#pragma pack(pop)

static_assert(sizeof(ENMeshPacket) == ENMESH_PKT_SIZE,
              "ENMeshPacket deve essere esattamente 128 byte");

// ---- Entry tabella anti-loop -------------------------------------------------
typedef struct {
  uint8_t  origin_mac[6];
  uint16_t seq_id;
} ENMeshSeenEntry;

#endif // ESPNOW_MESH_PACKET_H
