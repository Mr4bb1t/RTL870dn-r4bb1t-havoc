#include "system_mode.h"
#include "config.h"
#include "ui_display.h"
#include "wifi_attacks.h"
#include "ble_spam.h"
#include "debug.h"

bool rgb_attack_mode = false;
bool led_red_state   = false;

void set_rgb_raw(uint8_t r, uint8_t g, uint8_t b) {
  // Common Anode: 255 - valor (0 = apagado / 255 no pino; 255 = aceso / 0 no pino)
  analogWrite(LED_PIN_RED,   255 - r);
  analogWrite(LED_PIN_GREEN, 255 - g);
  analogWrite(LED_PIN_BLUE,  255 - b);
}

void set_rgb_hsv(uint16_t h, uint8_t s, uint8_t v) {
  uint8_t r, g, b;
  h = h % 360;
  uint8_t region = h / 60;
  uint8_t remainder = (h - (region * 60)) * 6;

  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  set_rgb_raw(r, g, b);
}

void rgb_led_init() {
  pinMode(LED_PIN_RED, OUTPUT);
  digitalWrite(LED_PIN_RED, HIGH);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  rgb_attack_mode = false;
  led_red_state   = false;
}

void enable_rgb_attack_mode() {
  rgb_attack_mode = true;
  pinMode(LED_PIN_RED,   OUTPUT);
  pinMode(LED_PIN_GREEN, OUTPUT);
  pinMode(LED_PIN_BLUE,  OUTPUT);
  set_rgb_raw(0, 0, 0); // inicia desligado
}

void disable_rgb_attack_mode() {
  rgb_attack_mode = false;
  // Desliga todos os canais de LED
  analogWrite(LED_PIN_RED,   255);
  analogWrite(LED_PIN_GREEN, 255);
  analogWrite(LED_PIN_BLUE,  255);
  digitalWrite(LED_PIN_RED,   HIGH);
  digitalWrite(LED_PIN_GREEN, HIGH);
  digitalWrite(LED_PIN_BLUE,  HIGH);

  // Restaura pinos dos botoes DOWN e ENTER para navegacao normal no menu
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_ENTER, INPUT_PULLUP);
  pinMode(LED_PIN_RED, OUTPUT);
  digitalWrite(LED_PIN_RED, HIGH);
  led_red_state = false;
}

void tick_rgb_attack_effects() {
  if (!rgb_attack_mode) return;

  uint32_t now = millis();

  if (isBeaconing) {
    // BEACON FLOOD: Rainbow Fade continuo por todas as cores do arco-iris
    uint16_t hue = (now / 8) % 360;
    set_rgb_hsv(hue, 255, 255);
  } else if (isDeauthing) {
    // DEAUTH ATTACK: Pulso de combate tatico de alta energia
    uint16_t hue = (now / 6) % 360;
    int phase = (now / 60) % 8;
    uint8_t brightness = (phase < 6) ? 255 : 60;
    set_rgb_hsv(hue, 255, brightness);
  } else if (isBleSpamming) {
    // BLE SPAM: Cyberpunk Wave (Ciano Neon -> Azul -> Magenta -> Roxo)
    uint16_t hue = 160 + ((now / 7) % 170); // 160 (Ciano) ate 330 (Magenta)
    set_rgb_hsv(hue, 255, 255);
  } else {
    // Rainbow Fade padrao
    uint16_t hue = (now / 8) % 360;
    set_rgb_hsv(hue, 255, 255);
  }
}

// Funcoes de compatibilidade para controle simples
void led_red_init() {
  rgb_led_init();
}

void led_red_on() {
  if (!rgb_attack_mode) {
    digitalWrite(LED_PIN_RED, LOW);
    led_red_state = true;
  }
}

void led_red_off() {
  if (!rgb_attack_mode) {
    digitalWrite(LED_PIN_RED, HIGH);
    led_red_state = false;
  }
}

void led_red_toggle() {
  if (!rgb_attack_mode) {
    if (led_red_state) led_red_off();
    else led_red_on();
  }
}

extern "C" {
  void BKUP_Set(uint32_t DwordIdx, uint32_t BitMask);
  void sys_reset(void);
}

void enterDownloadMode() {
  led_red_on();
  DEBUG_SER_PRINT("\n=========================================\n");
  DEBUG_SER_PRINT("[SYSTEM] Entrando em UART Download Mode...\n");
  DEBUG_SER_PRINT("[SYSTEM] Aguardando gravacao via UART (LOG_TX)...\n");
  DEBUG_SER_PRINT("=========================================\n");

  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  draw_str_centered(20, "DOWNLOAD MODE", 1);
  display.setFont(u8g2_font_5x7_tf);
  draw_str_centered(34, "Waiting for UART...", 1);
  draw_str_centered(44, "PA7 (LOG_TX) Active", 1);
  draw_str_centered(56, "Ready to Flash", 1);
  display.sendBuffer();

  delay(300);

  // Bit 9 (BIT_UARTBURN_BOOT) no registrador de backup 0
  BKUP_Set(0, (1 << 9));

  // Aciona o reset do SoC para o bootloader entrar no modo de gravação
  sys_reset();

  while (true) {
    yield();
  }
}

static String serial_cmd_buf = "";

void check_serial_commands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      serial_cmd_buf.trim();
      if (serial_cmd_buf.length() > 0) {
        serial_cmd_buf.toLowerCase();
        DEBUG_SER_PRINT("[CMD] Recebido via Serial: ");
        DEBUG_SER_PRINT(serial_cmd_buf);
        DEBUG_SER_PRINT("\n");

        if (serial_cmd_buf == "flash" || serial_cmd_buf == "download" || 
            serial_cmd_buf == "bootloader" || serial_cmd_buf == "uartburn") {
          enterDownloadMode();
        } else if (serial_cmd_buf == "reboot" || serial_cmd_buf == "reset") {
          DEBUG_SER_PRINT("[CMD] Reiniciando...\n");
          delay(100);
          sys_reset();
        } else if (serial_cmd_buf == "help") {
          Serial.println("[CMD] Comandos: 'flash' / 'download' (Download Mode), 'reboot' (Reiniciar)");
        }
        serial_cmd_buf = "";
      }
    } else {
      if (serial_cmd_buf.length() < 32) {
        serial_cmd_buf += c;
      }
    }
  }
}
