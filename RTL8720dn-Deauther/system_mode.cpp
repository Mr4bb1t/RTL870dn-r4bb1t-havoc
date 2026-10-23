#include "system_mode.h"
#include "ui_display.h"
#include "debug.h"

extern "C" {
  void BKUP_Set(uint32_t DwordIdx, uint32_t BitMask);
  void sys_reset(void);
}

void enterDownloadMode() {
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
