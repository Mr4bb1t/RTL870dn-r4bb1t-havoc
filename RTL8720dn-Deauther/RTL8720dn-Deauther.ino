/*
 * RTL8720dn Deauther + BLE Spammer -- OLED Edition v2.0
 * ======================================================
 * OLED SDA   -> PA_26  |  OLED SCL   -> PA_25
 * BTN_UP     -> PA_12  |  BTN_DOWN   -> PA_13
 * BTN_LEFT   -> PA_15  |  BTN_RIGHT  -> PA_27
 * BTN_ENTER  -> PA_14 (ou BTN_SEL)
 */

// STL MUST come before any Ameba/Arduino headers
#undef max
#include <vector>

#include "config.h"
#include "wifi_scanner.h"
#include "wifi_attacks.h"
#include "ble_spam.h"
#include "ui_display.h"
#include "ui_state.h"
#include "debug.h"
#include <Wire.h>

static uint32_t last_beacon_ms   = 0;
static uint32_t last_ble_spam_ms = 0;
static uint32_t last_render_ms   = 0;

void setup() {
  DEBUG_SER_INIT();
  delay(200);

  DEBUG_SER_PRINT("\n\n====================================\n");
  DEBUG_SER_PRINT(">>> R4BB1T HAVOC INICIALIZADO! <<<\n");
  DEBUG_SER_PRINT("====================================\n");

  DEBUG_SER_PRINT("[1/4] Configurando pinos dos botoes...\n");
  DEBUG_SER_PRINT("  -> UP (D12)... ");
  pinMode(BTN_UP,    INPUT_PULLUP);
  DEBUG_SER_PRINT("OK\n  -> DOWN (D11)... ");
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  DEBUG_SER_PRINT("OK\n  -> LEFT (D9)... ");
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(3,         INPUT_PULLUP); // D3 (PA_30) reserva ativo simultaneamente
  DEBUG_SER_PRINT("OK (D9 + D3)\n  -> RIGHT (D2)... ");
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  DEBUG_SER_PRINT("OK\n  -> ENTER (D10)... ");
  pinMode(BTN_ENTER, INPUT_PULLUP);
  DEBUG_SER_PRINT("OK\n[2/4] Todos os botoes configurados!\n");

  DEBUG_SER_PRINT("[DEBUG] Leituras dos botoes: UP=");
  DEBUG_SER_PRINT(digitalRead(BTN_UP));
  DEBUG_SER_PRINT(" DOWN=");
  DEBUG_SER_PRINT(digitalRead(BTN_DOWN));
  DEBUG_SER_PRINT(" LEFT_D9=");
  DEBUG_SER_PRINT(digitalRead(BTN_LEFT));
  DEBUG_SER_PRINT(" LEFT_D3=");
  DEBUG_SER_PRINT(digitalRead(3));
  DEBUG_SER_PRINT(" RIGHT=");
  DEBUG_SER_PRINT(digitalRead(BTN_RIGHT));
  DEBUG_SER_PRINT(" ENTER=");
  DEBUG_SER_PRINT(digitalRead(BTN_ENTER));
  DEBUG_SER_PRINT("\n");

  DEBUG_SER_PRINT("[3/3] Inicializando Display SH1106 (SCL=D7, SDA=D8)...\n");
  display.begin();
  display.setContrast(220);
  DEBUG_SER_PRINT("[OK] Display.begin() concluido com sucesso!\n");

  current_screen = SCR_SPLASH;
  DEBUG_SER_PRINT("[OK] Splash animado iniciando...\n");
  uint32_t splash_start = millis();
  bool wifi_ready_done = false;
  while (millis() - splash_start < SPLASH_DURATION_MS) {
    uint32_t elapsed = millis() - splash_start;
    int progress = (int)((elapsed * 64) / SPLASH_DURATION_MS);
    display.clearBuffer();
    render_splash(progress);
    display.sendBuffer();

    if (!wifi_ready_done && elapsed > 600) {
      DEBUG_SER_PRINT("[WiFi] Inicializando interface WiFi (netif UP)...\n");
      wifi_on(RTW_MODE_STA);
      DEBUG_SER_PRINT("[WiFi] Interface WiFi pronta!\n");
      wifi_ready_done = true;
    }
    delay(25);
  }

  current_screen = SCR_MAIN_MENU;
  menu_sel = 0;
  DEBUG_SER_PRINT("[OK] Menu principal carregado. Pronto!\n");
}

void loop() {
  tick_anim();
  handle_input();

  if (isBeaconing && (millis() - last_beacon_ms > BEACON_INTERVAL_MS)) {
    run_beacon_frame();
    last_beacon_ms = millis();
  }
  if (isDeauthing) {
    run_deauth_frame();
  }
  if (isBleSpamming && (millis() - last_ble_spam_ms > BLE_SPAM_INTERVAL_MS)) {
    ble_spam_next();
    last_ble_spam_ms = millis();
  }
  if (millis() - last_render_ms > 30) {
    render_screen();
    last_render_ms = millis();
  }
}