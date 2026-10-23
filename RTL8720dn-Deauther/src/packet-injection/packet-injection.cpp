#include "packet-injection.h"
#include "../../debug.h"

/*
 * Transmits a raw 802.11 frame with a given length.
 * The frame must be valid and have a sequence number of 0 as it will be set automatically.
 * The frame check sequence is added automatically and must not be included in the length.
 * @param frame A pointer to the raw frame
 * @param size The size of the frame
*/
void wifi_tx_raw_frame(void* frame, size_t length) {
  // Limitador de taxa: descarta frame se chamado antes de 8ms desde o ultimo envio.
  // Evita saturar a fila TX do driver RTL8720dn.
  static uint32_t last_tx_ms = 0;
  static uint32_t dropped_rate_limit = 0;
  static uint32_t dropped_no_mem = 0;
  static uint32_t success_tx = 0;
  static uint32_t last_diag_ms = 0;

  uint32_t now_ms = millis();
  if (last_tx_ms != 0 && (now_ms - last_tx_ms) < 8) {
    dropped_rate_limit++;
    return;
  }
  last_tx_ms = now_ms;

  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xa80);

  if (frame_control != 0) {
    update_mgntframe_attrib(ptr, frame_control + 8);
    memset((void *)*(uint32_t *)(frame_control + 0x80), 0, 0x68);
    uint8_t *frame_data = (uint8_t *)*(uint32_t *)(frame_control + 0x80) + 0x28;
    memcpy(frame_data, frame, length);
    *(uint32_t *)(frame_control + 0x14) = length;
    *(uint32_t *)(frame_control + 0x18) = length;
    dump_mgntframe(ptr, frame_control);
    success_tx++;
  } else {
    // Alerta crítico de sistema: fila TX esgotada no hardware/driver
    dropped_no_mem++;
    DEBUG_SER_PRINT("[WIFI_TX] WARN: alloc_mgtxmitframe sem memoria! Fila TX cheia (dropped=");
    DEBUG_SER_PRINT(dropped_no_mem);
    DEBUG_SER_PRINT(")\n");
  }

  // Telemetria periódica do driver (a cada 2.5s) se houver atividade
  if (now_ms - last_diag_ms >= 2500) {
    last_diag_ms = now_ms;
    DEBUG_SER_PRINT("[WIFI_TX] TX OK: ");
    DEBUG_SER_PRINT(success_tx);
    DEBUG_SER_PRINT(" | RateDrop: ");
    DEBUG_SER_PRINT(dropped_rate_limit);
    DEBUG_SER_PRINT(" | QDrop: ");
    DEBUG_SER_PRINT(dropped_no_mem);
    DEBUG_SER_PRINT("\n");
  }
}

/*
 * Transmits a 802.11 deauth frame on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the deauth
 * @param reason A reason code according to the 802.11 spec. Optional 
*/
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason) {
  DeauthFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.reason = reason;
  wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

/*
 * Transmits a very basic 802.11 beacon with the given ssid on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the beacon
 * @param ssid '\0' terminated array of characters representing the SSID
*/
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
  BeaconFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.ssid_length = 0; // Inicializa corretamente — original deixava como lixo de pilha
  for (int i = 0; ssid[i] != '\0' && i < 32; i++) {
    frame.ssid[i] = ssid[i];
    frame.ssid_length++;
  }
  wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
}
