#include "packet-injection.h"

#define MAX_RAW_FRAME_LEN 1500

/*
 * Transmits a raw 802.11 frame with a given length.
 * The frame must be valid and have a sequence number of 0 as it will be set automatically.
 * The frame check sequence is added automatically and must not be included in the length.
 * @param frame A pointer to the raw frame
 * @param length The size of the frame in bytes (must be <= MAX_RAW_FRAME_LEN)
*/
void wifi_tx_raw_frame(void* frame, size_t length) {
  if (frame == NULL || length == 0 || length > MAX_RAW_FRAME_LEN) return;
  if (rltk_wlan_info == NULL) return;
  
  uint32_t *p1 = *(uint32_t **)(rltk_wlan_info + 0x10);
  if (p1 == NULL) return;
  uint8_t *ptr = (uint8_t *)(*p1);
  if (ptr == NULL) return;

  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xa80);
  if (frame_control == NULL) return;

  uint32_t buf_addr = *(uint32_t *)(frame_control + 0x80);
  if (buf_addr == 0) return;

  update_mgntframe_attrib(ptr, frame_control + 8);
  memset((void *)buf_addr, 0, 0x68);
  uint8_t *frame_data = (uint8_t *)buf_addr + 0x28;
  memcpy(frame_data, frame, length);
  *(uint32_t *)(frame_control + 0x14) = length;
  *(uint32_t *)(frame_control + 0x18) = length;
  dump_mgntframe(ptr, frame_control);
}

/*
 * Transmits a 802.11 deauth frame on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the deauth
 * @param reason A reason code according to the 802.11 spec. Optional 
*/
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason) {
  if (src_mac == NULL || dst_mac == NULL) return;
  DeauthFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.frame_control = 0xC0;
  frame.duration = 0xFFFF;
  memcpy(frame.destination, dst_mac, 6);
  memcpy(frame.source, src_mac, 6);
  memcpy(frame.access_point, src_mac, 6);
  frame.sequence_number = 0;
  frame.reason = reason;
  wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

/*
 * Transmits a basic 802.11 beacon with the given ssid on the active channel
 * @param src_mac An array of bytes containing the mac address of the sender. The array has to be 6 bytes in size
 * @param dst_mac An array of bytes containing the destination mac address or FF:FF:FF:FF:FF:FF to broadcast the beacon
 * @param ssid '\0' terminated array of characters representing the SSID
*/
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
  if (src_mac == NULL || dst_mac == NULL || ssid == NULL) return;
  BeaconFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.frame_control = 0x80;
  frame.duration = 0;
  memcpy(frame.destination, dst_mac, 6);
  memcpy(frame.source, src_mac, 6);
  memcpy(frame.access_point, src_mac, 6);
  frame.sequence_number = 0;
  frame.timestamp = 0;
  frame.beacon_interval = 0x64;
  frame.ap_capabilities = 0x21;
  frame.ssid_tag = 0;

  uint8_t s_len = 0;
  for (int i = 0; ssid[i] != '\0' && i < 32; i++) {
    frame.ssid[s_len++] = ssid[i];
  }
  frame.ssid_length = s_len; // Tag 0 (SSID) length

  // Tag 1: Supported Rates (802.11 mandatory IE para smartphones reconhecerem o AP)
  frame.ssid[s_len++] = 0x01; // Element ID: Supported Rates
  frame.ssid[s_len++] = 0x08; // Length: 8
  frame.ssid[s_len++] = 0x82; // 1(B) Mbps
  frame.ssid[s_len++] = 0x84; // 2(B) Mbps
  frame.ssid[s_len++] = 0x8b; // 5.5(B) Mbps
  frame.ssid[s_len++] = 0x96; // 11(B) Mbps
  frame.ssid[s_len++] = 0x0c; // 6 Mbps
  frame.ssid[s_len++] = 0x12; // 9 Mbps
  frame.ssid[s_len++] = 0x18; // 12 Mbps
  frame.ssid[s_len++] = 0x24; // 18 Mbps

  // Tag 3: DS Parameter Set (Canal 1)
  frame.ssid[s_len++] = 0x03; // Element ID: DS Parameter Set
  frame.ssid[s_len++] = 0x01; // Length: 1
  frame.ssid[s_len++] = 0x01; // Channel 1

  wifi_tx_raw_frame(&frame, 38 + s_len);
}
