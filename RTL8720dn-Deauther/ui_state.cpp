// STL MUST come first
#undef max
#include <vector>

#include "ui_state.h"
#include "ui_display.h"
#include "wifi_scanner.h"
#include "wifi_attacks.h"
#include "ble_spam.h"
#include "debug.h"
#include "system_mode.h"

const char* MAIN_MENU[] = {"WiFi Attacks", "Bluetooth Spam", "Device Info"};
const char* WIFI_MENU[] = {"Deauth Attack", "Beacon Flood", "Rescan WiFi", "< Back"};

const AboutItem ABOUT_ITEMS[] = {
  {"SOC",   "RTL8720DN", false},
  {"MOD",   "Ai-Thinker BW16", false},
  {"CPU",   "KM4 200M+KM0 20M", false},
  {"WIFI",  "Dual 2.4G+5G", false},
  {"BLE",   "Bluetooth 5.0", false},
  {"RAM",   "512KB SRAM", false},
  {"FLASH", "2MB Embedded", false},
  {"DISP",  "SH1106 128x64", false},
  {"FW",    "v1.0.0 (HAVOC)", false},
  {"DEV",   "Mr4bb1t", false},
  {"FLASH", "[ENTRAR FLASH MODE]", true},
  {"BACK",  "[VOLTAR AO MENU]", true}
};
const int ABOUT_ITEMS_COUNT = sizeof(ABOUT_ITEMS) / sizeof(ABOUT_ITEMS[0]);

int about_sel    = 0;
int about_scroll = 0;

Screen current_screen = SCR_SPLASH;
int    menu_sel       = 0;

// ── Beacon Setup state ────────────────────────────────────
int  beacon_setup_sel = 0;

// ── Phone-style On-screen Keyboard ───────────────────────
// 3 pages: 0=lowercase, 1=UPPERCASE, 2=123/symbols
// Each page: 3 rows × 10 cols of characters
// Row index 3 is the virtual action row (page/shift, space, del, ok)
#define KB_COLS       10
#define KB_CHAR_ROWS   3   // character rows per page
#define KB_PAGE_COUNT  3
// For header compatibility keep this alias
#define KB_ROWS_N KB_CHAR_ROWS

const char KB_DATA[KB_PAGE_COUNT][KB_CHAR_ROWS][KB_COLS + 1] = {
  { // Page 0: lowercase
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz. -_",
  },
  { // Page 1: UPPERCASE
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ. -_",
  },
  { // Page 2: numbers + symbols
    "1234567890",
    "!@#$%&*()-",
    ".,_:;?/+= ",
  },
};

int  kb_page = 0;
int  kb_row  = 0, kb_col = 0;
int  kb_len  = 0;

static uint32_t btn_up_last    = 0;
static uint32_t btn_dn_last    = 0;
static uint32_t btn_left_last  = 0;
static uint32_t btn_right_last = 0;
static uint32_t btn_enter_last = 0;

static bool last_up_state    = HIGH;
static bool last_dn_state    = HIGH;
static bool last_left_state  = HIGH;
static bool last_right_state = HIGH;
static bool last_enter_state = HIGH;

void flush_input() {
  uint32_t now = millis();
  btn_up_last    = now;
  btn_dn_last    = now;
  btn_left_last  = now;
  btn_right_last = now;
  btn_enter_last = now;

  last_up_state    = digitalRead(BTN_UP);
  last_dn_state    = digitalRead(BTN_DOWN);
  last_left_state  = digitalRead(BTN_LEFT);
  last_right_state = digitalRead(BTN_RIGHT);
  last_enter_state = digitalRead(BTN_ENTER);
}

bool check_btn_up() {
  bool cur = digitalRead(BTN_UP);
  if (cur == LOW && last_up_state == HIGH && (millis() - btn_up_last > BTN_DEBOUNCE_MS)) {
    btn_up_last = millis();
    last_up_state = LOW;
    DEBUG_SER_PRINT("[BTN] UP pressionado\n");
    return true;
  }
  if (cur == HIGH) last_up_state = HIGH;
  return false;
}

bool check_btn_dn() {
  bool cur = digitalRead(BTN_DOWN);
  if (cur == LOW && last_dn_state == HIGH && (millis() - btn_dn_last > BTN_DEBOUNCE_MS)) {
    btn_dn_last = millis();
    last_dn_state = LOW;
    DEBUG_SER_PRINT("[BTN] DOWN pressionado\n");
    return true;
  }
  if (cur == HIGH) last_dn_state = HIGH;
  return false;
}

bool check_btn_left() {
  bool cur = digitalRead(BTN_LEFT);
  // LEFT/RIGHT usam debounce maior (220ms) pois os pinos PA_30/PA_27 do BW16
  // ficam propensos a bounce na linha de baixo do teclado e menus
  if (cur == LOW && last_left_state == HIGH && (millis() - btn_left_last > 220)) {
    btn_left_last = millis();
    last_left_state = LOW;
    DEBUG_SER_PRINT("[BTN] LEFT pressionado\n");
    return true;
  }
  if (cur == HIGH) last_left_state = HIGH;
  return false;
}

bool check_btn_right() {
  bool cur = digitalRead(BTN_RIGHT);
  // LEFT/RIGHT usam debounce maior (220ms) - veja comentário em check_btn_left()
  if (cur == LOW && last_right_state == HIGH && (millis() - btn_right_last > 220)) {
    btn_right_last = millis();
    last_right_state = LOW;
    DEBUG_SER_PRINT("[BTN] RIGHT pressionado\n");
    return true;
  }
  if (cur == HIGH) last_right_state = HIGH;
  return false;
}

bool check_btn_enter() {
  bool cur = digitalRead(BTN_ENTER);
  if (cur == LOW && last_enter_state == HIGH && (millis() - btn_enter_last > BTN_DEBOUNCE_MS)) {
    btn_enter_last = millis();
    last_enter_state = LOW;
    DEBUG_SER_PRINT("[BTN] ENTER pressionado\n");
    return true;
  }
  if (cur == HIGH) last_enter_state = HIGH;
  return false;
}

bool check_btn_sel_short() {
  return false;
}

bool check_btn_sel_long() {
  return false;
}

static void do_scan() {
  current_screen = SCR_SCANNING;
  DEBUG_SER_PRINT("[SCAN] Iniciando varredura WiFi 2.4G + 5GHz...\n");
  startScan();
  uint32_t start_ms = millis();
  while (millis() - start_ms < 3500) {
    tick_anim();
    render_screen();
    delay(40);
  }
  sortScanResults();
  DEBUG_SER_PRINT("[SCAN] Varredura finalizada. Redes encontradas: ");
  DEBUG_SER_PRINT((int)scan_results.size());
  DEBUG_SER_PRINT("\n");
  net_sel    = 0;
  net_scroll = 0;
}

void handle_input() {
  bool up    = check_btn_up();
  bool dn    = check_btn_dn();
  bool left  = check_btn_left();
  bool right = check_btn_right();
  bool enter = check_btn_enter();
  bool lon   = false;

  if (up || dn || left || right || enter) {
    DEBUG_SER_PRINT("[UI] Tela=");
    DEBUG_SER_PRINT((int)current_screen);
    DEBUG_SER_PRINT(" Item=");
    DEBUG_SER_PRINT(menu_sel);
    DEBUG_SER_PRINT("\n");
  }

  switch (current_screen) {
    case SCR_MAIN_MENU:
      if (enter) {
        if (menu_sel == 0) {
          // Se já tiver resultados do scan automático da splash, vai direto.
          // Caso contrário faz o scan agora (primeira entrada sem splash scan).
          if (scan_results.empty()) do_scan();
          current_screen     = SCR_WIFI_MENU;
          menu_sel           = 0;
          wifi_menu_cur_x    = 0;
          wifi_menu_target_x = 0;
        }
        else if (menu_sel == 1) { current_screen = SCR_BLE_MENU; menu_sel = 0; }
        else if (menu_sel == 2) { 
          current_screen = SCR_ABOUT; 
          about_sel = 0; 
          about_scroll = 0; 
        }
      }
      else if (left || up) {
        menu_sel = (menu_sel - 1 + MAIN_MENU_LEN) % MAIN_MENU_LEN;
        trigger_arrow_left();
        DEBUG_SER_PRINT("  -> Menu Principal: ");
        DEBUG_SER_PRINT(MAIN_MENU[menu_sel]);
        DEBUG_SER_PRINT("\n");
      }
      else if (right || dn) {
        menu_sel = (menu_sel + 1) % MAIN_MENU_LEN;
        trigger_arrow_right();
        DEBUG_SER_PRINT("  -> Menu Principal: ");
        DEBUG_SER_PRINT(MAIN_MENU[menu_sel]);
        DEBUG_SER_PRINT("\n");
      }
      break;

    case SCR_WIFI_MENU:
      if (enter) {
        if (menu_sel == 0) {
          if (scan_results.empty()) do_scan();
          current_screen = SCR_NET_LIST;
          net_sel = 0;
          net_scroll = 0;
        }
        else if (menu_sel == 1) {
          current_screen = SCR_BEACON_CONFIRM;
          menu_sel = 0;
        }
        else if (menu_sel == 2) {
          do_scan();
          current_screen     = SCR_WIFI_MENU;
          menu_sel           = 0;
          wifi_menu_cur_x    = 0;
          wifi_menu_target_x = 0;
        }
        else if (menu_sel == 3) {
          current_screen = SCR_MAIN_MENU;
          menu_sel       = 0;
        }
      }
      else if (left || up)  menu_sel = (menu_sel - 1 + WIFI_MENU_LEN) % WIFI_MENU_LEN;
      else if (right || dn) menu_sel = (menu_sel + 1) % WIFI_MENU_LEN;
      break;

    case SCR_NET_LIST:
      if (up) {
        if (net_sel > 0) net_sel--;
        if (net_sel < net_scroll) net_scroll = net_sel;
      }
      if (dn) {
        if (scan_results.empty()) { current_screen = SCR_WIFI_MENU; menu_sel = 0; break; }
        if (net_sel < (int)scan_results.size() - 1) net_sel++;
        if (net_sel >= net_scroll + 4) net_scroll = net_sel - 3;
      }
      // ENTER: Alterna seleção da rede atual (Checkbox)
      if (enter && net_sel < (int)scan_results.size()) {
        net_marked[net_sel] = !net_marked[net_sel];
      }
      // RIGHT: Avanço direto para tela de confirmação do Deauth
      if (right || lon) {
        current_screen = SCR_DEAUTH_CONFIRM;
        menu_sel = 0;
      }
      // LEFT: Retorno rápido ao menu Wi-Fi
      if (left) {
        current_screen = SCR_WIFI_MENU;
        menu_sel = 0;
        wifi_menu_cur_x = 0;
      }
      break;

    case SCR_DEAUTH_CONFIRM:
      if (left)  menu_sel = 1; // CANCEL
      if (right) menu_sel = 0; // ATTACK
      if (up || dn) menu_sel = (menu_sel + 1) % 2;
      if (enter) {
        if (menu_sel == 0) { start_deauth(); current_screen = SCR_DEAUTH_RUNNING; }
        else               { current_screen = SCR_WIFI_MENU; menu_sel = 0; wifi_menu_cur_x = 0; }
      }
      break;

    case SCR_DEAUTH_RUNNING:
      // Qualquer tecla de parada (ENTER, LEFT, DOWN) desarma o ataque
      if (enter || left || dn) {
        stop_deauth();
        current_screen = SCR_WIFI_MENU;
        menu_sel = 0;
        wifi_menu_cur_x = 0;
      }
      break;

    case SCR_BEACON_CONFIRM:
      // Now just a pass-through to SETUP
      if (left)  menu_sel = 1;
      if (right) menu_sel = 0;
      if (up || dn) menu_sel = (menu_sel + 1) % 2;
      if (enter) {
        if (menu_sel == 0) {
          beacon_setup_sel = 0;
          current_screen   = SCR_BEACON_SETUP;
        } else {
          current_screen = SCR_WIFI_MENU;
          menu_sel = 1;
          wifi_menu_cur_x = 128;
        }
      }
      break;

    case SCR_BEACON_SETUP:
      // Rows: 0=mode, 1=count, 2=START, 3=CANCEL
      if (up)   beacon_setup_sel = (beacon_setup_sel - 1 + 4) % 4;
      if (dn)   beacon_setup_sel = (beacon_setup_sel + 1) % 4;
      if (beacon_setup_sel == 0) {
        // Mode row: LEFT/RIGHT cycles through modes
        if (left)  beacon_mode = (BeaconMode)((beacon_mode - 1 + BEACON_MODE_COUNT) % BEACON_MODE_COUNT);
        if (right) beacon_mode = (BeaconMode)((beacon_mode + 1) % BEACON_MODE_COUNT);
      } else if (beacon_setup_sel == 1) {
        // Count row: LEFT/RIGHT changes quantity
        if (left)  { beacon_count -= 25; if (beacon_count < BEACON_MIN_COUNT) beacon_count = BEACON_MIN_COUNT; }
        if (right) { beacon_count += 25; if (beacon_count > BEACON_MAX_COUNT) beacon_count = BEACON_MAX_COUNT; }
      } else if (beacon_setup_sel == 2) {
        if (enter) {
          // If CUSTOM mode, go to keyboard first
          if (beacon_mode == BEACON_MODE_CUSTOM) {
            kb_page = 0; // always start on lowercase
            kb_row  = 0;
            kb_col  = 0;
            kb_len  = strlen(beacon_custom_ssid);
            current_screen = SCR_BEACON_KEYBOARD;
          } else {
            start_beacon();
            current_screen = SCR_BEACON_RUNNING;
          }
        }
      } else { // beacon_setup_sel == 3 (CANCEL)
        if (enter || left) {
          current_screen = SCR_WIFI_MENU;
          menu_sel = 1;
          wifi_menu_cur_x = 128;
        }
      }
      // Also allow ENTER on count row to jump to START
      if (enter && beacon_setup_sel == 1) beacon_setup_sel = 2;
      break;

    case SCR_BEACON_KEYBOARD:
      // Rows 0-2: character grid  |  Row 3: action row
      // UP from row 0 → action row (3), DOWN from action row → row 0
      if (up)    kb_row = (kb_row <= 0)           ? KB_CHAR_ROWS : kb_row - 1;
      if (dn)    kb_row = (kb_row >= KB_CHAR_ROWS) ? 0            : kb_row + 1;
      if (left)  kb_col = (kb_col - 1 + KB_COLS) % KB_COLS;
      if (right) kb_col = (kb_col + 1) % KB_COLS;

      if (kb_row < KB_CHAR_ROWS) {
        // ── Character rows ──────────────────────────────
        if (enter) {
          char ch = KB_DATA[kb_page][kb_row][kb_col];
          if (ch != '\0' && kb_len < 32) {
            beacon_custom_ssid[kb_len++] = ch;
            beacon_custom_ssid[kb_len]   = '\0';
          }
        }
      } else {
        // ── Action row ──────────────────────────────────
        // col 0-1 : [⇧] toggle page  (lower → UPPER → 123 → lower)
        // col 2-6 : [   SPACE   ]
        // col 7-8 : [⌫ DEL]
        // col 9   : [✓ OK]
        if (enter) {
          if (kb_col <= 1) {
            kb_page = (kb_page + 1) % KB_PAGE_COUNT;
          } else if (kb_col <= 6) {
            if (kb_len < 32) {
              beacon_custom_ssid[kb_len++] = ' ';
              beacon_custom_ssid[kb_len]   = '\0';
            }
          } else if (kb_col <= 8) {
            if (kb_len > 0) {
              kb_len--;
              beacon_custom_ssid[kb_len] = '\0';
            }
          } else { // col 9 = OK/DONE
            if (kb_len == 0) {
              strncpy(beacon_custom_ssid, "R4BB1T", sizeof(beacon_custom_ssid));
              kb_len = 6;
            }
            start_beacon();
            current_screen = SCR_BEACON_RUNNING;
          }
        }
      }
      break;

    case SCR_BEACON_RUNNING:
      if (enter || dn) {
        stop_beacon();
        current_screen = SCR_WIFI_MENU;
        menu_sel = 1;
        wifi_menu_cur_x = 128;
      }
      break;

    case SCR_BLE_MENU:
      if (enter || right)        { start_ble_spam(); current_screen = SCR_BLE_RUNNING; }
      else if (left || dn || up) { current_screen = SCR_MAIN_MENU; menu_sel = 1; }
      break;

    case SCR_BLE_RUNNING:
      if (enter || left || dn) {
        stop_ble_spam();
        current_screen = SCR_MAIN_MENU;
        menu_sel = 1;
      }
      break;

    case SCR_ABOUT:
      if (enter) {
        if (about_sel == ABOUT_ITEMS_COUNT - 2) {
          // Opção [ENTRAR FLASH MODE]
          enterDownloadMode();
        } else if (about_sel == ABOUT_ITEMS_COUNT - 1) {
          // Opção [VOLTAR AO MENU]
          current_screen = SCR_MAIN_MENU;
          menu_sel = 2;
        }
      }
      else if (up) {
        if (about_sel > 0) {
          about_sel--;
          if (about_sel < about_scroll) {
            about_scroll = about_sel;
          }
        }
      }
      else if (dn) {
        if (about_sel < ABOUT_ITEMS_COUNT - 1) {
          about_sel++;
          if (about_sel >= about_scroll + 4) {
            about_scroll = about_sel - 3;
          }
        }
      }
      else if (left) {
        current_screen = SCR_MAIN_MENU;
        menu_sel = 2;
        DEBUG_SER_PRINT("[UI] Retornando de ABOUT ao Menu Principal\n");
      }
      break;

    default: break;
  }
}