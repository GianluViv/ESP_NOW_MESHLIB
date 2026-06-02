#ifndef ESPNOW_MESH_STORAGE_H
#define ESPNOW_MESH_STORAGE_H

#include <Preferences.h>
#include "ESPNowMeshPacket.h"

// Namespace NVS usato dalla libreria — non collide con namespace applicativi
#define ENMESH_NVS_NAMESPACE "enmesh"
#define ENMESH_NVS_KEY_CHANNEL "channel"
#define ENMESH_NVS_KEY_NETID   "net_id"

class ENMeshStorage {
public:
  // Carica canale e network_id da NVS. Ritorna true se i dati erano presenti.
  static bool load(uint8_t &channel, char *network_id_out) {
    Preferences prefs;
    if (!prefs.begin(ENMESH_NVS_NAMESPACE, true)) return false;
    channel = prefs.getUChar(ENMESH_NVS_KEY_CHANNEL, 0);
    String nid = prefs.getString(ENMESH_NVS_KEY_NETID, "");
    prefs.end();
    if (nid.length() == 0) return false;
    strncpy(network_id_out, nid.c_str(), ENMESH_NETWORK_ID_LEN - 1);
    network_id_out[ENMESH_NETWORK_ID_LEN - 1] = '\0';
    return true;
  }

  // Salva canale in NVS.
  static void saveChannel(uint8_t channel) {
    Preferences prefs;
    if (!prefs.begin(ENMESH_NVS_NAMESPACE, false)) return;
    prefs.putUChar(ENMESH_NVS_KEY_CHANNEL, channel);
    prefs.end();
  }

  // Salva network_id in NVS.
  static void saveNetworkId(const char *network_id) {
    Preferences prefs;
    if (!prefs.begin(ENMESH_NVS_NAMESPACE, false)) return;
    prefs.putString(ENMESH_NVS_KEY_NETID, network_id);
    prefs.end();
  }

  // Cancella tutti i dati salvati dalla libreria.
  static void clear() {
    Preferences prefs;
    if (!prefs.begin(ENMESH_NVS_NAMESPACE, false)) return;
    prefs.clear();
    prefs.end();
  }
};

#endif // ESPNOW_MESH_STORAGE_H
