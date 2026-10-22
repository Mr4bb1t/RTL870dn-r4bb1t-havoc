#ifndef BLE_SPAM_H
#define BLE_SPAM_H

#include "config.h"
#include <BLEDevice.h>

// =========================================================
// Apple Device Types (port of EvilAppleJuice-ESP32)
// =========================================================
enum ApplePacketType { APPLE_AUDIO, APPLE_SETUP };

struct AppleDevice {
  const char*    name;
  uint8_t        modelId;
  ApplePacketType type;
};

enum DeviceIndex : uint8_t {
  // Audio devices – 31-byte ADV payload (ID at index 7)
  AIRPODS = 0,
  POWER_BEATS,
  BEATS_X,
  BEATS_SOLO_3,
  BEATS_STUDIO_3,
  AIRPODS_MAX,
  POWER_BEATS_PRO,
  BEATS_SOLO_PRO,
  AIRPODS_PRO,
  AIRPODS_GEN_2,
  BEATS_FLEX,
  BEATS_STUDIO_BUDS,
  BEATS_FIT_PRO,
  AIRPODS_GEN_3,
  AIRPODS_PRO_GEN_2,
  BEATS_STUDIO_BUDS_PLUS,
  BEATS_STUDIO_PRO,
  AIRPODS_PRO_GEN_2_USB_C,
  BEATS_SOLO_4,
  BEATS_SOLO_BUDS,
  SOFTWARE_UPDATE,
  POWERBEATS_FIT,
  // Setup devices – 23-byte ADV payload (ID at index 13)
  APPLETV_SETUP,
  TRANSFER_NUMBER,
  APPLETV_PAIR,
  SETUP_NEW_PHONE,
  HOMEPOD_SETUP,
  APPLETV_HOMEKIT_SETUP,
  APPLETV_KEYBOARD_SETUP,
  TV_COLOR_BALANCE,
  APPLETV_NEW_USER,
  VISION_PRO,
  APPLETV_CONNECTING_TO_NETWORK,
  APPLETV_APPLEID_SETUP,
  APPLETV_WIRELESS_AUDIO_SYNC,
  NUM_APPLE_DEVICES
};

extern const AppleDevice ALL_APPLE_DEVICES[];

// =========================================================
// Runtime state
// =========================================================
extern bool     isBleSpamming;
extern uint32_t ble_pkt_sent;
extern uint8_t  ble_spam_idx;

// =========================================================
// API
// =========================================================
void start_ble_spam();
void stop_ble_spam();
void ble_spam_next();

// Returns the display name for the current device being broadcast
inline const char* ble_current_name() {
  return ALL_APPLE_DEVICES[ble_spam_idx % (uint8_t)NUM_APPLE_DEVICES].name;
}

#endif // BLE_SPAM_H