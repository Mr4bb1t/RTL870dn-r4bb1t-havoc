#ifndef PACKET_INJECTION_H
#define PACKET_INJECTION_H

#include <Arduino.h>

typedef struct __attribute__((packed)) {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t  destination[6];
  uint8_t  source[6];
  uint8_t  access_point[6];
  uint16_t sequence_number;
  uint16_t reason;
} DeauthFrame;

static_assert(sizeof(DeauthFrame) == 26, "DeauthFrame size must be 26 bytes");

typedef struct __attribute__((packed)) {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t  destination[6];
  uint8_t  source[6];
  uint8_t  access_point[6];
  uint16_t sequence_number;
  uint64_t timestamp;
  uint16_t beacon_interval;
  uint16_t ap_capabilities;
  uint8_t  ssid_tag;
  uint8_t  ssid_length;
  uint8_t  ssid[255]; // Max 802.11 SSID length + IEs
} BeaconFrame;

static_assert(sizeof(BeaconFrame) == 293, "BeaconFrame size must be 293 bytes (38 header + 255 payload)");

/*
 * Import the needed c functions from the closed-source libraries
 * The function definitions might not be 100% accurate with the arguments as the types get lost during compilation and cannot be retrieved back during decompilation
 * However, these argument types seem to work perfect
*/
extern uint8_t* rltk_wlan_info;
extern "C" void* alloc_mgtxmitframe(void* ptr);
extern "C" void update_mgntframe_attrib(void* ptr, void* frame_control);
extern "C" int dump_mgntframe(void* ptr, void* frame_control);

void wifi_tx_raw_frame(void* frame, size_t length);
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason = 0x06);
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid);

#endif
