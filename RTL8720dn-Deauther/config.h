#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================================
// Hardware Pins (5-Button Tactical D-Pad + Action)
// Mapeados para os pinos digitais reais do BW16 (0 a 12)
// =========================================================
#define BTN_UP     6  // D6  -> PB_3 (Pino livre sem LED, vizinho ao D12)
#define BTN_DOWN  11  // D11 -> PA_13
#define BTN_LEFT   3  // D3  -> PA_30
#define BTN_RIGHT  2  // D2  -> PA_27
#define BTN_ENTER 10  // D10 -> PA_14
#define BTN_SEL   BTN_ENTER // Alias de compatibilidade

// =========================================================
// OLED Display Pins (SH1106 1.3" I2C)
// =========================================================
#define OLED_SCL   7  // D7 -> PA_25
#define OLED_SDA   8  // D8 -> PA_26

// =========================================================
// IR Remote Pins
// =========================================================
// PB_2 (D4) is also the boot/download strap, BUT we never configure it as
// GPIO outside the IR menu. At boot/RESET the pin is in high-Z so the BURN
// button can pull it LOW without fighting the MCU. pinMode(INPUT_PULLUP) is
// called only inside ir_start_capture() (on entering the IR menu) and
// released back to INPUT (tristate) in ir_stop_capture() when leaving.
//
// Pins that CANNOT be used as IR_RX on this board:
//   D5  / PB_2  -- original strap number in some core variants (same physical pin)
//   D9  / PA_15 -- UART0-RX of Serial: UART peripheral holds it LOW after Serial.begin()
//   D4  / PA_29 -- UART-bridge RX: conflicts with USB firmware upload in some core variants
#define IR_RX_PIN  4   // D4  -> PB_2 (TSOP / VS1838 demodulated output, late-armed)
#define IR_TX_PIN 12   // D12 -> PA_12 (IR LED via transistor, 38 kHz PWM)

// =========================================================
// Timing / Attack Parameters
// =========================================================
#define BTN_DEBOUNCE_MS        150
#define BTN_HOLD_DELAY_MS      350   // Tempo segurando o botão antes de iniciar auto-repeat (350ms)
#define BTN_REPEAT_INTERVAL_MS 100   // Taxa de repetição contínua enquanto segurado (100ms = 10x/segundo)
// Cadencia: tempo entre chamadas de ble_spam_next(). Cada troca de
// identidade custa ~15-25ms (stop + set_rand_addr + params + start), entao
// o periodo real de uma identidade = INTERVAL * BLE_SPAM_REPEAT.
// Com 110ms x 2 = ~220ms por MAC -> ~4-5 MACs novos por segundo no ar.
#define BLE_SPAM_INTERVAL_MS    110    // 110ms por ciclo no modo ALL
#define BLE_FOCUSED_INTERVAL_MS  80    // 80ms no modo FOCUSED (AirPods only)

// ---------------------------------------------------------
// Potencia de transmissao do advertising BLE.
//
// O core expoe o comando vendor le_adv_set_tx_power(0, tx_gain).
// Tabela documentada em vendor_cmd_bt.h:
//     0x06 = -10.0 dBm   (padrao conservador)
//     0x1A =   0.0 dBm
//     0x23 =  +4.5 dBm   (maximo documentado)
//
// O BW16 tem PA de ~7 dBm; indices acima de 0x23 existem no driver mas
// NAO sao documentados e podem estourar o limite regulatorio / distorcer
// a modulacao. 0 desliga o ajuste (mantem o default do driver).
// ---------------------------------------------------------
#define BLE_TX_GAIN             0x23
// BLE spam – identidade (MAC) do advertising
//
// O iPhone deduplica advertising por endereco (AdvA). Se todos os
// pacotes saem do mesmo MAC, ele mostra UM popup e ignora o resto.
// Para repetir o popup e preciso trocar o endereco aleatorio do
// controller entre um ciclo e outro.
//
// BLE_MAC_ROTATE      1 = troca o MAC estatico aleatorio (recomendado)
//                     0 = usa o MAC publico fixo do chip (fallback)
// BLE_SPAM_REPEAT     quantos ciclos a MESMA identidade (MAC+payload)
//                     fica no ar antes de trocar. O advertising fica
//                     rodando continuamente nesse periodo (20 ms), entao
//                     o iOS ve ~5 pacotes por ciclo. 1 = troca sempre.
// BLE_MAC_LOG_EVERY   1 = loga o MAC a cada rotacao no serial.
// BLE_TX_GAIN         indice de potencia do advertising (comando vendor).
//                     0 = nao mexe (default do driver, ~ -10 dBm).
//                     0x23 = +4.5 dBm (maximo documentado).
// ---------------------------------------------------------
#define BLE_MAC_ROTATE          1
#define BLE_SPAM_REPEAT         2
#define BLE_MAC_LOG_EVERY       1
#define DEAUTH_INTERVAL_MS      10    // 10ms entre envios de deauth

// Beacon flood: replica a mecânica de canais do deauth (grupo fantasma).
// O rádio nunca fica preso em um canal só, então o anel de TX de
// gerenciamento do RTL8720 é drenado periodicamente.
#define BEACON_CHANNEL           1   // canal primário do flood
#define BEACON_CHANNEL_HOP       1   // 1 = alterna com canal de escape; 0 = só canal primário
#define BEACON_MAX_CONSEC_FAIL 120   // auto-stop se o anel de TX não drenar

// ---------------------------------------------------------
// Agenda do flood: manter TODOS os SSIDs no ar ao mesmo tempo.
//
// O scheduler envia 1 beacon por vez, em round-robin estrito
// (0,1,2,...,N-1,0,1,...). O espaçamento entre beacons é calculado para
// fechar uma rodada completa em BEACON_REFRESH_MS:
//
//     gap = clamp(BEACON_REFRESH_MS / N, MIN_PACING, MAX_PACING)
//
//   N=1    -> gap 100ms -> 10 beacons/s (igual ao beacon interval real)
//   N=20   -> gap  10ms -> rodada ~200-250ms -> as 20 redes sempre visíveis
//   N=25   -> gap  10ms -> rodada ~250-300ms
//   N=100  -> gap  10ms -> rodada ~1-1,2s (limite do driver/rate limiter)
//
// MIN_PACING nunca pode ficar abaixo de 8ms: wifi_tx_raw_frame() descarta
// quadros mais próximos que isso (proteção contra esgotar o anel de TX).
// A troca de canal acontece a cada BEACON_HOP_EVERY_FRAMES beacons, que é
// frequente o suficiente para o anel (~33 descritores) nunca encher.
// ---------------------------------------------------------
#define BEACON_REFRESH_MS        200   // meta de tempo para 1 rodada completa
#define BEACON_MIN_PACING_MS      10   // piso por beacon (rate limiter do driver)
#define BEACON_MAX_PACING_MS     100   // teto (N pequeno não precisa ser mais lento)
#define BEACON_HOP_EVERY_FRAMES   16   // troca de canal a cada N beacons
#define SPLASH_DURATION_MS 2500
#define SCAN_DURATION_MS 5000

// =========================================================
// WiFi Scan Result (forward declared without std::string)
// =========================================================
typedef struct {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  short rssi;
  uint8_t channel;
  bool is5GHz;
} WiFiScanResult;

// =========================================================
// UI Screen Enum
// =========================================================
enum Screen {
  SCR_SPLASH = 0,
  SCR_MAIN_MENU,
  SCR_WIFI_MENU,
  SCR_SCANNING,
  SCR_NET_LIST,
  SCR_DEAUTH_CONFIRM,
  SCR_DEAUTH_RUNNING,
  SCR_BEACON_CONFIRM,
  SCR_BEACON_SETUP,      // NEW: type + quantity picker
  SCR_BEACON_KEYBOARD,   // NEW: custom SSID keyboard
  SCR_BEACON_RUNNING,
  SCR_BLE_MENU,
  SCR_BLE_SELECT,        // Novo: seleciona modo ALL / AIRPODS FOCUSED
  SCR_BLE_RUNNING,
  SCR_GAMES_MENU,        // Hub / Menu de Jogos Retro
  SCR_GAME_TETRIS,       // Jogo Tetris
  SCR_GAME_INVADERS,     // Jogo Space Invaders
  SCR_GAME_DOOM,         // Jogo DOOM 3D Raycaster
  SCR_GAME_SNAKE,        // Jogo Snake Arcade
  SCR_IR_MENU,           // Hub do Controle IR
  SCR_IR_RECORD,         // Gravar sinal IR
  SCR_IR_LIST,           // Lista de sinais salvos
  SCR_IR_DETAIL,         // Detalhe / emitir / excluir
  SCR_IR_SPAM,           // Spam de codigos IR
  SCR_ABOUT,
};

// =========================================================
// Beacon Flood Mode
// =========================================================
enum BeaconMode {
  BEACON_MODE_RANDOM = 0,  // Built-in 25 funny SSIDs
  BEACON_MODE_CLONE,       // Clone scanned networks (unique names)
  BEACON_MODE_CUSTOM,      // User-typed custom SSID repeated
};
#define BEACON_MODE_COUNT 3

// =========================================================
// Menu Labels (defined in ui_state.cpp)
// =========================================================
#define MAIN_MENU_LEN 5
#define WIFI_MENU_LEN 4
#define GAMES_MENU_LEN 5
#define IR_MENU_LEN 4

extern const char *MAIN_MENU[];
extern const char *WIFI_MENU[];
extern const char *GAMES_MENU[];

#endif // CONFIG_H