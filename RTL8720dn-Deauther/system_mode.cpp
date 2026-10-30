#include "system_mode.h"
#include "ui_display.h"
#include "debug.h"
#include "config.h"
#include "ir_remote.h"

extern "C" {
  void BKUP_Set(uint32_t DwordIdx, uint32_t BitMask);
  void sys_reset(void);
}

void enterDownloadMode() {
  DEBUG_SER_PRINT("\n=========================================\n");
  DEBUG_SER_PRINT("[SYSTEM] Entrando em UART Download Mode...\n");
  DEBUG_SER_PRINT("[SYSTEM] Liberando pino PB_2 (RX1) do receptor IR...\n");
  DEBUG_SER_PRINT("[SYSTEM] Aguardando gravacao via UART (PA7 TX / PB2 RX1)...\n");
  DEBUG_SER_PRINT("=========================================\n");

  // CRÍTICO: Desliga qualquer captura ativa e libera PB_2 de volta para
  // INPUT_PULLNONE + pinRemoveMode() para que o bootloader do RTL8720DN
  // utilize o pino PB_2 como RX1 sem qualquer conflito de GPIO ou pull-down!
  ir_stop_capture();
  pinMode(IR_RX_PIN, INPUT_PULLNONE);
  pinRemoveMode(IR_RX_PIN);

  display.clearBuffer();
  display.setFont(u8g2_font_7x14B_tf);
  draw_str_centered(18, "DOWNLOAD MODE", 1);
  display.setFont(u8g2_font_5x7_tf);
  draw_str_centered(32, "Waiting for UART...", 1);
  draw_str_centered(42, "PA7 (TX) / PB2 (RX1)", 1);
  draw_str_centered(52, "Do NOT send IR signals", 1);
  draw_str_centered(62, "Ready to Flash", 1);
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
        } else if (serial_cmd_buf == "ir export" || serial_cmd_buf == "ir dump" || 
                   serial_cmd_buf == "ir flipper" || serial_cmd_buf == "export") {
          ir_export_flipper();
        } else if (serial_cmd_buf == "help") {
          Serial.println("[CMD] Comandos: 'flash'/'download', 'reboot', 'ir export' (Formato Flipper .ir)");
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
