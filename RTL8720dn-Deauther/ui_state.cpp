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
bool beacon_net_list_picking = false;

// ── BLE Mode Select state ─────────────────────────────────
int ble_select_sel = 1; // 0=ALL DEVICES, 1=AIRPODS FOCUSED (default: mais efetivo)

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

struct ButtonTracker {
  uint8_t  pin;
  uint32_t last_trigger;
  uint32_t press_start;
  bool     last_state;
  bool     is_repeating;
  bool     lock_until_release;
};

static ButtonTracker btn_up_trk    = { (uint8_t)BTN_UP,    0, 0, HIGH, false, false };
static ButtonTracker btn_dn_trk    = { (uint8_t)BTN_DOWN,  0, 0, HIGH, false, false };
static ButtonTracker btn_left_trk  = { (uint8_t)BTN_LEFT,  0, 0, HIGH, false, false };
static ButtonTracker btn_right_trk = { (uint8_t)BTN_RIGHT, 0, 0, HIGH, false, false };
static ButtonTracker btn_enter_trk = { (uint8_t)BTN_ENTER, 0, 0, HIGH, false, false };

static bool check_button(ButtonTracker &btn, const char* name, bool allow_repeat = true) {
  bool cur = digitalRead(btn.pin);
  uint32_t now = millis();

  if (cur == HIGH) {
    btn.last_state = HIGH;
    btn.is_repeating = false;
    btn.lock_until_release = false;
    return false;
  }

  // cur == LOW (botão pressionado)
  if (btn.lock_until_release) {
    return false;
  }

  // Primeiro clique (borda de descida HIGH -> LOW)
  if (btn.last_state == HIGH) {
    if (now - btn.last_trigger > BTN_DEBOUNCE_MS) {
      btn.last_state   = LOW;
      btn.press_start  = now;
      btn.last_trigger = now;
      btn.is_repeating = false;
      return true;
    }
    return false;
  }

  // Botão continua segurado (last_state == LOW)
  if (!allow_repeat) {
    return false;
  }

  uint32_t held = now - btn.press_start;
  if (!btn.is_repeating) {
    if (held >= BTN_HOLD_DELAY_MS) {
      btn.is_repeating = true;
      btn.last_trigger = now;
      return true;
    }
  } else {
    // Repetição contínua a cada BTN_REPEAT_INTERVAL_MS
    if (now - btn.last_trigger >= BTN_REPEAT_INTERVAL_MS) {
      btn.last_trigger = now;
      return true;
    }
  }

  return false;
}

void flush_input() {
  uint32_t now = millis();
  ButtonTracker* btns[] = { &btn_up_trk, &btn_dn_trk, &btn_left_trk, &btn_right_trk, &btn_enter_trk };
  for (int i = 0; i < 5; i++) {
    btns[i]->last_trigger = now;
    btns[i]->is_repeating = false;
    if (digitalRead(btns[i]->pin) == LOW) {
      btns[i]->lock_until_release = true;
      btns[i]->last_state = LOW;
    } else {
      btns[i]->lock_until_release = false;
      btns[i]->last_state = HIGH;
    }
  }
}

bool check_btn_up() {
  return check_button(btn_up_trk, "UP", true);
}

bool check_btn_dn() {
  return check_button(btn_dn_trk, "DOWN", true);
}

bool check_btn_left() {
  return check_button(btn_left_trk, "LEFT", true);
}

bool check_btn_right() {
  return check_button(btn_right_trk, "RIGHT", true);
}

bool check_btn_enter() {
  return check_button(btn_enter_trk, "ENTER", true);
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
  while (!scan_done && (millis() - start_ms < 5000)) {
    check_serial_commands();
    tick_anim();
    render_screen();
    delay(20);
  }
  sortScanResults();
  DEBUG_SER_PRINT("[SCAN] Varredura finalizada. Redes encontradas: ");
  DEBUG_SER_PRINT((int)scan_results.size());
  DEBUG_SER_PRINT("\n");
  net_sel    = 0;
  net_scroll = 0;
}

void handle_input() {
  Screen prev_screen = current_screen;

  bool up    = check_btn_up();
  bool dn    = check_btn_dn();
  bool left  = check_btn_left();
  bool right = check_btn_right();
  bool enter = check_btn_enter();
  bool lon   = false;

  switch (current_screen) {
    case SCR_MAIN_MENU:
      if (enter) {
        if (menu_sel == 0) {
          // Entra direto no menu de WiFi carregando os cards de wifi sem reescanear
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
      }
      else if (right || dn) {
        menu_sel = (menu_sel + 1) % MAIN_MENU_LEN;
        trigger_arrow_right();
      }
      break;

    case SCR_WIFI_MENU:
      if (enter) {
        if (menu_sel == 0) {
          // Entra direto na lista de redes escaneadas
          current_screen = SCR_NET_LIST;
          net_sel = 0;
          net_scroll = 0;
        }
        else if (menu_sel == 1) {
          current_screen = SCR_BEACON_CONFIRM;
          menu_sel = 0;
        }
        else if (menu_sel == 2) {
          // Rescan explicitamente solicitado pelo usuario
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
        if (scan_results.empty()) {
          current_screen = beacon_net_list_picking ? SCR_BEACON_SETUP : SCR_WIFI_MENU;
          menu_sel = 0;
          break;
        }
        if (net_sel < (int)scan_results.size() - 1) net_sel++;
        if (net_sel >= net_scroll + 4) net_scroll = net_sel - 3;
      }
      // ENTER: Alterna seleção da rede atual (Checkbox) ou rescan se lista vazia
      if (enter) {
        if (scan_results.empty()) {
          do_scan();
        } else if (!btn_enter_trk.is_repeating && net_sel < (int)scan_results.size()) {
          net_marked[net_sel] = !net_marked[net_sel];
        }
      }
      // RIGHT: Avanço para confirmação (Deauth ou Beacon Setup dependendo da origem)
      if (right || lon) {
        if (beacon_net_list_picking) {
          beacon_net_list_picking = false;
          beacon_setup_sel = 2; // foca no botão START
          current_screen   = SCR_BEACON_SETUP;
        } else {
          current_screen = SCR_DEAUTH_CONFIRM;
          menu_sel = 0;
        }
      }
      // LEFT: Retorno (ao Beacon Setup ou ao Menu Wi-Fi)
      if (left) {
        if (beacon_net_list_picking) {
          beacon_net_list_picking = false;
          current_screen   = SCR_BEACON_SETUP;
        } else {
          current_screen = SCR_WIFI_MENU;
          menu_sel = 0;
          wifi_menu_cur_x = 0;
        }
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
      // Rows: 0=mode, 1=count, 2=START/SELECT, 3=CANCEL
      if (up)   beacon_setup_sel = (beacon_setup_sel - 1 + 4) % 4;
      if (dn)   beacon_setup_sel = (beacon_setup_sel + 1) % 4;
      if (beacon_setup_sel == 0) {
        // Mode row: LEFT/RIGHT cycles through modes
        if (left)  beacon_mode = (BeaconMode)((beacon_mode - 1 + BEACON_MODE_COUNT) % BEACON_MODE_COUNT);
        if (right) beacon_mode = (BeaconMode)((beacon_mode + 1) % BEACON_MODE_COUNT);
        if (enter && beacon_mode == BEACON_MODE_CLONE) {
          // ENTER direto no modo CLONE abre a lista de redes para marcar os alvos
          beacon_net_list_picking = true;
          if (scan_results.empty()) do_scan();
          current_screen = SCR_NET_LIST;
          net_sel = 0;
          net_scroll = 0;
        }
      } else if (beacon_setup_sel == 1) {
        // Count row: LEFT/RIGHT changes quantity
        if (left)  { beacon_count -= 25; if (beacon_count < BEACON_MIN_COUNT) beacon_count = BEACON_MIN_COUNT; }
        if (right) { beacon_count += 25; if (beacon_count > BEACON_MAX_COUNT) beacon_count = BEACON_MAX_COUNT; }
      } else if (beacon_setup_sel == 2) {
        if (enter) {
          if (beacon_mode == BEACON_MODE_CLONE) {
            // No modo CLONE, se nenhuma rede foi marcada ou usuario deseja selecionar, vai para NET_LIST
            int marked = 0;
            for (size_t i = 0; i < scan_results.size(); i++) if (net_marked[i]) marked++;
            if (marked == 0) {
              beacon_net_list_picking = true;
              if (scan_results.empty()) do_scan();
              current_screen = SCR_NET_LIST;
              net_sel = 0;
              net_scroll = 0;
            } else {
              start_beacon();
              current_screen = SCR_BEACON_RUNNING;
            }
          } else if (beacon_mode == BEACON_MODE_CUSTOM) {
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
      // Rows 0-2: character grid (3 rows x 10 cols)
      // Row 3: action row ([ABC/123], [SPACE], [DEL], [OK])
      if (up) {
        if (kb_row == KB_CHAR_ROWS) {
          // Subindo da barra de acao para o grid de caracteres
          if (kb_col <= 1)      kb_col = 0;
          else if (kb_col <= 6) kb_col = 4;
          else if (kb_col <= 8) kb_col = 7;
          else                  kb_col = 9;
          kb_row = KB_CHAR_ROWS - 1;
        } else if (kb_row <= 0) {
          kb_row = KB_CHAR_ROWS;
          if (kb_col <= 1)      kb_col = 0;
          else if (kb_col <= 6) kb_col = 4;
          else if (kb_col <= 8) kb_col = 7;
          else                  kb_col = 9;
        } else {
          kb_row--;
        }
      }

      if (dn) {
        if (kb_row >= KB_CHAR_ROWS) {
          kb_row = 0;
        } else if (kb_row == KB_CHAR_ROWS - 1) {
          // Descendo para a barra de acao: ajusta direto para o botao correspondente
          kb_row = KB_CHAR_ROWS;
          if (kb_col <= 1)      kb_col = 0;
          else if (kb_col <= 6) kb_col = 4;
          else if (kb_col <= 8) kb_col = 7;
          else                  kb_col = 9;
        } else {
          kb_row++;
        }
      }

      if (kb_row < KB_CHAR_ROWS) {
        // Grid de caracteres de 10 colunas
        if (left)  kb_col = (kb_col - 1 + KB_COLS) % KB_COLS;
        if (right) kb_col = (kb_col + 1) % KB_COLS;
      } else {
        // Barra de acao inferior ([ABC], [SPACE], [DEL], [OK]):
        // Pula diretamente entre os 4 botoes a cada clique de LEFT ou RIGHT!
        if (right) {
          if (kb_col <= 1)      kb_col = 4; // ABC -> SPACE
          else if (kb_col <= 6) kb_col = 7; // SPACE -> DEL
          else if (kb_col <= 8) kb_col = 9; // DEL -> OK
          else                  kb_col = 0; // OK -> ABC
        }
        if (left) {
          if (kb_col <= 1)      kb_col = 9; // ABC -> OK
          else if (kb_col <= 6) kb_col = 0; // SPACE -> ABC
          else if (kb_col <= 8) kb_col = 4; // DEL -> SPACE
          else                  kb_col = 7; // OK -> DEL
        }
      }

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
            // [ABC] page toggle: apenas 1 por clique (não repete descontroladamente se segurado)
            if (!btn_enter_trk.is_repeating) {
              kb_page = (kb_page + 1) % KB_PAGE_COUNT;
            }
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
      // Redireciona direto para a tela de seleção de modo
      if (enter || right) {
        ble_select_sel = 1; // default FOCUSED
        current_screen = SCR_BLE_SELECT;
      } else if (left || dn || up) {
        current_screen = SCR_MAIN_MENU;
        menu_sel = 1;
      }
      break;

    case SCR_BLE_SELECT:
      if (left) {
        current_screen = SCR_BLE_MENU;
      } else if (up) {
        ble_select_sel = (ble_select_sel - 1 + 2) % 2;
      } else if (dn || right) {
        ble_select_sel = (ble_select_sel + 1) % 2;
      }
      if (enter) {
        // Configura modo antes de iniciar
        ble_focused_mode = (ble_select_sel == 1);
        start_ble_spam();
        current_screen = SCR_BLE_RUNNING;
      }
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
      }
      break;

    default: break;
  }

  // Se houve troca de tela, impede que qualquer tecla que ainda esteja pressionada
  // dispare na tela recém-carregada até que o usuário a solte
  if (current_screen != prev_screen) {
    flush_input();
  }
}