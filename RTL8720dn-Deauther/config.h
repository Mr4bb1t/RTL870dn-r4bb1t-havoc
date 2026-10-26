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
// Timing / Attack Parameters
// =========================================================
#define BTN_DEBOUNCE_MS        150
#define BTN_HOLD_DELAY_MS      350   // Tempo segurando o botão antes de iniciar auto-repeat (350ms)
#define BTN_REPEAT_INTERVAL_MS 100   // Taxa de repetição contínua enquanto segurado (100ms = 10x/segundo)
#define BLE_SPAM_INTERVAL_MS    150    // 150ms por ciclo: ~7 pacotes/dispositivo (20ms adv interval)
#define BLE_FOCUSED_INTERVAL_MS 100    // 100ms modo AirPods-only: iguala o original ESP32
#define BEACON_INTERVAL_MS 130      // 130ms — intervalo 802.11 padrão, funciona no RTL8720dn
#define DEAUTH_INTERVAL_MS 70       // 70ms entre envios
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
#define MAIN_MENU_LEN 4
#define WIFI_MENU_LEN 4
#define GAMES_MENU_LEN 5

extern const char *MAIN_MENU[];
extern const char *WIFI_MENU[];
extern const char *GAMES_MENU[];

#endif // CONFIG_H