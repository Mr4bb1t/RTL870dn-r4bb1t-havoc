#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\ui_display.cpp"
// STL MUST come first
#undef max
#include <vector>

#include "animations/animations.h"
#include "ble_spam.h"
#include "ui_display.h"
#include "ui_state.h"
#include "wifi_attacks.h"
#include "wifi_scanner.h"

// Hardware I2C (HW_I2C) acelerado a 400kHz nativo do RTL8720DN (PA_25=SCL /
// PA_26=SDA) O callback dedicado u8x8_byte_ameba_hw_i2c garante que
// Wire.begin() seja chamado exatamente uma vez, evitando o bug
// amb_ard_pin_check_fun (Incorrect pin: 26) e acelerando os frames em ate 8x!
static bool wire_ready = false;

extern "C" uint8_t u8x8_byte_ameba_hw_i2c(u8x8_t *u8x8, uint8_t msg,
                                          uint8_t arg_int, void *arg_ptr) {
  switch (msg) {
  case U8X8_MSG_BYTE_INIT:
    if (!wire_ready) {
      Wire.begin();
      Wire.setClock(400000); // 400 kHz Fast I2C Hardware
      wire_ready = true;
    }
    break;
  case U8X8_MSG_BYTE_START_TRANSFER:
    Wire.beginTransmission(u8x8_GetI2CAddress(u8x8) >> 1);
    break;
  case U8X8_MSG_BYTE_SEND:
    Wire.write((const uint8_t *)arg_ptr, (size_t)arg_int);
    break;
  case U8X8_MSG_BYTE_END_TRANSFER:
    Wire.endTransmission();
    break;
  case U8X8_MSG_BYTE_SET_DC:
    break;
  default:
    return 0;
  }
  return 1;
}

U8G2_SH1106_AMEBA_HW_I2C display(U8G2_R0);

int anim_frame = 0;
uint32_t last_anim_ms = 0;

int center_anim_frame = 0;
uint32_t last_center_anim_ms = 0;

bool arrow_left_anim = false;
int arrow_left_frame = 0;
uint32_t last_arrow_left_ms = 0;

bool arrow_right_anim = false;
int arrow_right_frame = 0;
uint32_t last_arrow_right_ms = 0;

int wifi_menu_cur_x = 0;
int wifi_menu_target_x = 0;

void trigger_arrow_left() {
  arrow_left_anim = true;
  arrow_left_frame = 0;
  last_arrow_left_ms = millis();
}

void trigger_arrow_right() {
  arrow_right_anim = true;
  arrow_right_frame = 0;
  last_arrow_right_ms = millis();
}

void tick_anim() {
  uint32_t now = millis();

  // Submenu spinners
  if (now - last_anim_ms > 200) {
    anim_frame = (anim_frame + 1) % 4;
    last_anim_ms = now;
  }

  // Cycle center 48x48 icon animation
  if (now - last_center_anim_ms > 80) {
    center_anim_frame = (center_anim_frame + 1) % 14;
    last_center_anim_ms = now;
  }

  // Left arrow movement animation (0 -> 1 -> ... -> 9 -> 0)
  if (arrow_left_anim) {
    if (now - last_arrow_left_ms > 28) {
      last_arrow_left_ms = now;
      arrow_left_frame++;
      if (arrow_left_frame >= 10) {
        arrow_left_frame = 0;
        arrow_left_anim = false;
      }
    }
  }

  // Right arrow movement animation (0 -> 1 -> ... -> 9 -> 0)
  if (arrow_right_anim) {
    if (now - last_arrow_right_ms > 28) {
      last_arrow_right_ms = now;
      arrow_right_frame++;
      if (arrow_right_frame >= 10) {
        arrow_right_frame = 0;
        arrow_right_anim = false;
      }
    }
  }

  // Slide animation for lateral WiFi menu
  if (current_screen == SCR_WIFI_MENU) {
    wifi_menu_target_x = menu_sel * 128;
    if (wifi_menu_cur_x != wifi_menu_target_x) {
      int diff = wifi_menu_target_x - wifi_menu_cur_x;
      int step = diff / 3;
      if (step == 0)
        step = (diff > 0) ? 1 : -1;
      wifi_menu_cur_x += step;
    }
  }

  // Slide animation for lateral Games menu
  if (current_screen == SCR_GAMES_MENU) {
    games_menu_target_x = menu_sel * 128;
    if (games_menu_cur_x != games_menu_target_x) {
      int diff = games_menu_target_x - games_menu_cur_x;
      int step = diff / 3;
      if (step == 0)
        step = (diff > 0) ? 1 : -1;
      games_menu_cur_x += step;
    }
  }
}

void draw_corner_brackets(int x, int y, int w, int h, int len) {
  display.drawHLine(x, y, len);
  display.drawVLine(x, y, len);
  display.drawHLine(x + w - len, y, len);
  display.drawVLine(x + w - 1, y, len);
  display.drawHLine(x, y + h - 1, len);
  display.drawVLine(x, y + h - len, len);
  display.drawHLine(x + w - len, y + h - 1, len);
  display.drawVLine(x + w - 1, y + h - len, len);
}

void draw_header(const char *title, const char *badge) {
  display.setFont(u8g2_font_5x7_tf);
  display.drawBox(0, 0, 128, 10);
  display.setDrawColor(0);
  display.drawStr(3, 8, title);
  if (badge && badge[0] != '\0') {
    int bw = display.getStrWidth(badge);
    display.drawStr(125 - bw, 8, badge);
  }
  display.setDrawColor(1);
}

void draw_hint(const char *hint, bool center) {
  display.setFont(u8g2_font_5x7_tf);
  display.drawHLine(0, 53, 128);
  if (center) {
    int tw = display.getStrWidth(hint);
    int tx = (128 - tw) / 2;
    if (tx < 0)
      tx = 0;
    display.drawStr(tx, 62, hint);
  } else {
    display.drawStr(3, 62, hint);
  }
}

void draw_str_centered(int y, const char *str, int color) {
  int tw = display.getStrWidth(str);
  int tx = (128 - tw) / 2;
  if (tx < 0)
    tx = 0;
  display.setDrawColor(color);
  display.drawStr(tx, y, str);
  display.setDrawColor(1);
}

void draw_btn(int x, int y, int w, int h, const char *text, bool is_selected) {
  display.setFont(u8g2_font_5x7_tf);
  int tw = display.getStrWidth(text);
  int tx = x + (w - tw) / 2;
  int ty = y + (h + 6) / 2;
  if (is_selected) {
    display.drawRBox(x, y, w, h, 2);
    display.setDrawColor(0);
    display.drawStr(tx, ty, text);
    display.setDrawColor(1);
  } else {
    display.drawRFrame(x, y, w, h, 2);
    display.drawStr(tx, ty, text);
  }
}

void draw_signal_bars(int x, int y, int rssi, int color) {
  int bars = 1;
  if (rssi > -65)
    bars = 3;
  else if (rssi > -80)
    bars = 2;

  display.setDrawColor(color);
  for (int b = 0; b < 3; b++) {
    int bx = x + b * 3;
    int bh = 3 + b * 2;
    int by = y + 7 - bh;
    if (b < bars) {
      display.drawBox(bx, by, 2, bh);
    } else {
      display.drawPixel(bx, y + 6);
    }
  }
  display.setDrawColor(1);
}

void draw_rssi_bar(int x, int y, int rssi) { draw_signal_bars(x, y, rssi, 1); }

void draw_spinner(int x, int y) {
  const char *frames[] = {"   ", ".  ", ".. ", "..."};
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(x, y, frames[anim_frame % 4]);
}

void render_splash(int progress) {
  // Esquerda: Logo Cyber-Rabbit (48x56 posicionado em X=2, Y=4 com 4px de
  // respiro no topo e na base)
  display.drawXBMP(2, 4, 48, 56, icon_rabbit_48x56);

  // Direita: Badge R4BB1T com cantos arredondados (X=54, Y=3, W=70, H=10)
  display.drawRBox(54, 3, 70, 10, 2);
  display.setFont(u8g2_font_5x7_tf);
  display.setDrawColor(0); // Texto escuro vazado no badge
  display.drawStr(74, 11, "R4BB1T");
  display.setDrawColor(1);

  // Titulo HAVOC em negrito (Y=25 -> ocupa Y=14..25, com 2px livres abaixo do
  // badge)
  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(54, 25, "HAVOC");

  // DEAUTHER (Y=35 -> ocupa Y=29..35, com 4px livres abaixo de HAVOC)
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(54, 35, "DEAUTHER");

  // Dual-Band (Y=45 -> ocupa Y=39..45, com 4px livres abaixo de DEAUTHER)
  display.drawStr(54, 45, "2.4G + 5GHz");

  // Barra de carregamento cyber (Frame em Y=52, altura 6 -> ocupa Y=52..57, 7px
  // livres acima e 6px na base)
  display.drawFrame(54, 52, 70, 6);
  int fill_w = (progress >= 0) ? (progress * 66 / 64)
                               : (int)(((millis() % 2000) * 66) / 2000);
  if (fill_w > 66)
    fill_w = 66;
  if (fill_w > 0) {
    display.drawBox(56, 54, fill_w, 2);
  }

  // Status do scan WiFi em tempo real, abaixo da barra de progresso
  // Exibe "Scanning..." enquanto aguarda, ou "X redes" quando já tem resultados
  display.setFont(u8g2_font_4x6_tf);
  if (scan_results.empty()) {
    if (scan_done) {
      display.drawStr(54, 63, "0 redes OK");
    } else {
      // Anima os pontos para indicar atividade: "Scanning" / "Scanning." / etc.
      int dots = (int)(millis() / 400) % 4;
      const char* dotStr[] = {"Scanning", "Scanning.", "Scanning..", "Scanning..."};
      display.drawStr(54, 63, dotStr[dots]);
    }
  } else {
    char buf[24];
    if (scan_done) {
      snprintf(buf, sizeof(buf), "%d redes OK", (int)scan_results.size());
    } else {
      snprintf(buf, sizeof(buf), "%d redes...", (int)scan_results.size());
    }
    display.drawStr(54, 63, buf);
  }
}

void render_main_menu() {
  // Left & Right animated navigation arrows (32x32) at Y=8 (shifted up 3px)
  display.drawXBMP(4, 8, 32, 32, anim_arrow_left_32x32[arrow_left_frame]);
  display.drawXBMP(92, 8, 32, 32, anim_arrow_right_32x32[arrow_right_frame]);

  // Center Animated Icon (48x48) at X=40, Y=0 (shifted up 3px)
  if (menu_sel == 0) {
    display.drawXBMP(40, 0, 48, 48, anim_wifi_48x48[center_anim_frame % 14]);
  } else if (menu_sel == 1) {
    display.drawXBMP(40, 0, 48, 48,
                     anim_bluetooth_48x48[center_anim_frame % 14]);
  } else if (menu_sel == 2) {
    display.drawXBMP(40, 0, 48, 48, icon_games_48x48);
  } else {
    display.drawXBMP(40, 0, 48, 48, icon_about_48x48);
  }

  // Centered Menu Title at Y=51
  display.setFont(u8g2_font_5x7_tf);
  draw_str_centered(51, MAIN_MENU[menu_sel], 1);

  // Balanced Pagination Pills at Y=58
  for (int i = 0; i < MAIN_MENU_LEN; i++) {
    int dot_x = 48 + i * 9;
    if (i == menu_sel) {
      display.drawRBox(dot_x - 1, 58, 8, 3, 1);
    } else {
      display.drawDisc(dot_x + 2, 59, 1);
    }
  }
}

void render_wifi_menu() {
  const char *badges[] = {"TARGET ATTACK", "BROADCAST SPAM", "WIFI SCANNER",
                          "NAVIGATION"};
  const char *titles[] = {"DEAUTH", "BEACON", "SCAN", "RETURN"};

  for (int i = 0; i < WIFI_MENU_LEN; i++) {
    int cx = (i * 128) - wifi_menu_cur_x;
    if (cx < -124 || cx > 124)
      continue;

    // Card boundary (112 px wide, 50 px high, centered at cx + 8, Y=4..54)
    display.drawRFrame(cx + 8, 4, 112, 50, 3);

    // Top Header Badge
    display.setFont(u8g2_font_5x7_tf);
    int bw = display.getStrWidth(badges[i]);
    if (i == menu_sel) {
      display.drawBox(cx + 9, 5, 110, 11);
      display.setDrawColor(0);
      display.drawStr(cx + 8 + (112 - bw) / 2, 13, badges[i]);
      display.setDrawColor(1);
    } else {
      display.drawHLine(cx + 9, 16, 110);
      display.drawStr(cx + 8 + (112 - bw) / 2, 13, badges[i]);
    }

    // Title in bold
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(cx + 14, 33, titles[i]);

    // Subtitle below title
    display.setFont(u8g2_font_5x7_tf);
    if (i == 0) {
      char buf[20];
      snprintf(buf, sizeof(buf), "%d APs Ready >", (int)scan_results.size());
      display.drawStr(cx + 14, 46, buf);
    } else if (i == 1) {
      display.drawStr(cx + 14, 46, "25 Fake SSIDs >");
    } else if (i == 2) {
      display.drawStr(cx + 14, 46, "Dual-Band >");
    } else {
      display.drawStr(cx + 14, 46, "< Back to Menu");
    }

    // Right-side vector graphics for each card
    if (i == 0) {
      // Sniper Crosshair
      display.drawCircle(cx + 99, 32, 8);
      display.drawCircle(cx + 99, 32, 4);
      display.drawVLine(cx + 99, 21, 23);
      display.drawHLine(cx + 88, 32, 23);
      display.drawDisc(cx + 99, 32, 1);
    } else if (i == 1) {
      // Mini realistic lattice broadcast tower (scaled down)
      int bx = cx + 96, by = 31;
      display.drawLine(bx - 5, by + 10, bx, by - 6);
      display.drawLine(bx + 5, by + 10, bx, by - 6);
      display.drawHLine(bx - 5, by + 10, 11);
      display.drawHLine(bx - 3, by + 4, 7);
      display.drawHLine(bx - 2, by - 1, 5);
      display.drawLine(bx - 3, by + 4, bx + 2, by - 1);
      display.drawLine(bx + 3, by + 4, bx - 2, by - 1);
      display.drawVLine(bx, by - 8, 3);
      display.drawDisc(bx, by - 9, 1);
      display.drawCircle(bx, by - 9, 3,
                         U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
      display.drawCircle(bx, by - 9, 6,
                         U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    } else if (i == 2) {
      // Radar Screen shifted up & left (cx + 94, ry = 29, r = 8) to not overlap
      // "2G"
      display.drawCircle(cx + 94, 29, 8);
      display.drawCircle(cx + 94, 29, 4);
      display.drawLine(cx + 94, 29, cx + 100, 24);
      display.drawDisc(cx + 98, 26, 1);
    } else {
      // Return Arrow
      display.drawTriangle(cx + 89, 32, cx + 97, 25, cx + 97, 39);
      display.drawBox(cx + 97, 30, 12, 5);
      display.drawBox(cx + 106, 24, 3, 11);
    }
  }

  // Bottom Pagination Pills & Indicators
  display.setFont(u8g2_font_5x7_tf);
  if (menu_sel > 0)
    display.drawStr(2, 61, "<");
  if (menu_sel < WIFI_MENU_LEN - 1)
    display.drawStr(122, 61, ">");

  for (int p = 0; p < WIFI_MENU_LEN; p++) {
    int dot_x = 46 + p * 10;
    if (p == menu_sel) {
      display.drawRBox(dot_x - 1, 58, 8, 3, 1);
    } else {
      display.drawDisc(dot_x + 2, 59, 1);
    }
  }
}

void render_scanning() {
  draw_header("WIFI SCANNER", "2.4G+5G");

  // Left: Animated Radar Dish (cx=24, cy=36, r=18) - Centered in full height
  display.drawCircle(24, 36, 18);
  display.drawCircle(24, 36, 9);
  display.drawVLine(24, 18, 37);
  display.drawHLine(6, 36, 37);

  // Continuous smooth rotating radar sweep line (16 points around radius 17)
  static const int8_t sweep_x[16] = {0, 7,  12,  16,  17,  16,  12,  7,
                                     0, -7, -12, -16, -17, -16, -12, -7};
  static const int8_t sweep_y[16] = {-17, -16, -12, -7, 0, 7,  12,  16,
                                     17,  16,  12,  7,  0, -7, -12, -16};
  int sw_idx = (millis() / 100) % 16;
  display.drawLine(24, 36, 24 + sweep_x[sw_idx], 36 + sweep_y[sw_idx]);

  // Detected Blips
  display.drawDisc(17, 28, 1);
  display.drawDisc(32, 26, 1);
  display.drawDisc(29, 44, 1);

  // Right Side: Channel hopping & live telemetry across full height
  display.setFont(u8g2_font_5x7_tf);
  const int scan_channels[] = {1, 6, 11, 36, 44, 149, 157};
  int curCh = scan_channels[(millis() / 250) % 7];
  const char *band = (curCh > 14) ? "5G" : "2G";

  char ch_buf[24];
  snprintf(ch_buf, sizeof(ch_buf), "CH : %d [%s]", curCh, band);
  display.drawStr(48, 20, ch_buf);

  char f_buf[24];
  snprintf(f_buf, sizeof(f_buf), "APS: %d found", (int)scan_results.size());
  display.drawStr(48, 30, f_buf);

  int c2g = 0, c5g = 0;
  for (size_t s = 0; s < scan_results.size(); s++) {
    if (scan_results[s].is5GHz)
      c5g++;
    else
      c2g++;
  }
  char b_buf[24];
  snprintf(b_buf, sizeof(b_buf), "2G:%d | 5G:%d", c2g, c5g);
  display.drawStr(48, 40, b_buf);

  display.drawStr(48, 49, "SIG: -45 dBm");

  // Animated sweep progress bar
  display.drawFrame(48, 54, 76, 5);
  int pfill = (int)(((millis() % 2200) * 72) / 2200);
  display.drawBox(50, 56, pfill, 1);
}

void render_net_list() {
  int markedCount = 0;
  for (size_t i = 0; i < scan_results.size(); i++) {
    if (net_marked[i]) markedCount++;
  }

  char m_buf[16];
  snprintf(m_buf, sizeof(m_buf), "%d/%d Sel:%d", net_sel + 1,
           (int)scan_results.size(), markedCount);
  draw_header("NETWORKS", m_buf);

  if (scan_results.empty()) {
    draw_str_centered(32, "No networks found!");
    draw_str_centered(46, "ENTER to rescan");
    return;
  }

  display.setFont(u8g2_font_5x7_tf);
  const int visible = 4;
  for (int i = 0; i < visible && (net_scroll + i) < (int)scan_results.size(); i++) {
    int idx   = net_scroll + i;
    int y_top = 13 + i * 12; // 13, 25, 37, 49
    int y_txt = y_top + 8;   // Baseline for 5x7 font (height 7px)
    WiFiScanResult &r = scan_results[idx];
    bool isSel = (idx == net_sel);

    // Selected row highlight: rounded bar
    if (isSel) {
      display.drawRBox(0, y_top, 122, 11, 2);
    }
    int color = isSel ? 0 : 1;
    display.setDrawColor(color);

    // 7x7 Checkbox at X=2
    display.drawFrame(2, y_top + 2, 7, 7);
    if (net_marked[idx]) {
      display.drawBox(4, y_top + 4, 3, 3);
    }

    // SSID: up to 11 chars. Smooth pause & scroll if selected and > 11 chars
    String s = (r.ssid.length() > 0) ? r.ssid : String("*HIDDEN*");
    char ssid_buf[16];
    if (isSel && s.length() > 11) {
      int maxOffset = s.length() - 11;
      int period = maxOffset + 6;
      int step = (millis() / 450) % period;
      int offset = 0;
      if (step <= 2) {
        offset = 0;
      } else if (step < 2 + maxOffset) {
        offset = step - 2;
      } else {
        offset = maxOffset;
      }
      s.substring(offset, offset + 11).toCharArray(ssid_buf, sizeof(ssid_buf));
    } else {
      s.substring(0, 11).toCharArray(ssid_buf, sizeof(ssid_buf));
    }
    display.drawStr(12, y_txt, ssid_buf);

    // Band: 5G or 2G badge at X=72
    display.drawStr(72, y_txt, r.is5GHz ? "5G" : "2G");

    // Channel number at X=87
    char cbuf[8];
    snprintf(cbuf, sizeof(cbuf), "c%d", r.channel);
    display.drawStr(87, y_txt, cbuf);

    // Clean 3-bar signal meter at X=111
    draw_signal_bars(111, y_top + 2, r.rssi, color);

    display.setDrawColor(1);
  }

  // Right Edge Vertical Scrollbar
  if (scan_results.size() > (size_t)visible) {
    display.drawVLine(126, 13, 49);
    int trackHeight = 49;
    int thumbHeight = (int)((visible * trackHeight) / (int)scan_results.size());
    if (thumbHeight < 6) thumbHeight = 6;
    int maxScroll = (int)scan_results.size() - visible;
    int thumbY = 13 + (net_scroll * (trackHeight - thumbHeight)) / maxScroll;
    display.drawBox(125, thumbY, 3, thumbHeight);
  }
}

void render_deauth_confirm() {
  draw_header("CONFIRM ATTACK", "DEAUTH");

  int count = 0;
  for (size_t i = 0; i < scan_results.size(); i++)
    if (net_marked[i])
      count++;

  // Warning Triangle Icon on Left with mathematically centered exclamation mark
  // '!'
  display.drawTriangle(12, 18, 5, 32, 19, 32);
  display.setDrawColor(0);
  display.drawVLine(12, 22, 5);
  display.drawPixel(12, 29);
  display.setDrawColor(1);

  if (count == 0) {
    display.drawStr(25, 21, "NO TARGETS MARKED!");
    display.drawStr(25, 32, "Select APs in Net List.");
    display.drawStr(25, 42, "Dual-Band 2.4G / 5G");
  } else {
    char buf[28];
    snprintf(buf, sizeof(buf), "TARGETS: %d AP(s)", count);
    display.drawStr(25, 21, buf);

    String firstMarked = "";
    for (size_t i = 0; i < scan_results.size(); i++) {
      if (net_marked[i]) {
        firstMarked = scan_results[i].ssid;
        break;
      }
    }
    char sbuf[22];
    snprintf(sbuf, sizeof(sbuf), "\"%.14s\"", firstMarked.c_str());
    display.drawStr(25, 31, sbuf);
    display.drawStr(25, 41, "Deauth Disconnect Spam");
  }

  // Two balanced action buttons at Y=46..60 (height 15px)
  draw_btn(10, 46, 50, 15, "CANCEL", menu_sel == 1);
  draw_btn(68, 46, 50, 15, count == 0 ? "RETURN" : "ATTACK", menu_sel == 0);
}

void render_deauth_running() {
  draw_header("* DEAUTH ATTACK *", "RUN");

  // Left Column: Meaningful Wi-Fi Link Disruption Graphic (X=2..34, Y=14..62)
  // 1. Access Point (Router) at Top Left (X=5, Y=18)
  display.drawRFrame(5, 18, 18, 8, 1); // Router chassis
  display.drawVLine(8, 13, 5);         // Left Antenna
  display.drawPixel(8, 12);
  display.drawVLine(20, 13, 5); // Right Antenna
  display.drawPixel(20, 12);
  display.drawPixel(10, 22); // Status LED 1
  display.drawPixel(14, 22); // Status LED 2

  // 2. Client Smartphone at Bottom Left (X=8, Y=44)
  display.drawRFrame(8, 44, 12, 18, 2); // Phone body
  display.drawFrame(10, 46, 8, 12);     // Screen frame
  display.drawHLine(12, 60, 4);         // Home button indicator

  // 3. Wi-Fi Wireless Link & Deauth Disruption Strike (Y=26..43)
  // Normal Wi-Fi wave emitted from router downwards
  display.drawCircle(14, 26, 4, U8G2_DRAW_LOWER_RIGHT | U8G2_DRAW_LOWER_LEFT);

  // Deauth frame disruption in 4 alternating phases
  int strikePhase = (millis() / 90) % 4;
  if (strikePhase == 0) {
    // Attack packet injecting from right towards link
    display.drawHLine(24, 34, 6);
    display.drawLine(27, 32, 24, 34);
    display.drawLine(27, 36, 24, 34);
    display.drawDisc(14, 34, 2); // Radio interference cluster
  } else if (strikePhase == 1) {
    // High-voltage Deauth lightning bolt severing the wave
    display.drawLine(20, 27, 12, 33);
    display.drawLine(12, 33, 18, 36);
    display.drawLine(18, 36, 10, 43);
    display.drawPixel(7, 35);
    display.drawPixel(22, 35);
  } else if (strikePhase == 2) {
    // Broken Chain Link [---] / [---]
    display.drawHLine(7, 33, 4);
    display.drawVLine(7, 34, 3);
    display.drawHLine(7, 37, 4);

    display.drawHLine(17, 33, 4);
    display.drawVLine(20, 34, 3);
    display.drawHLine(17, 37, 4);

    // Slashing blade cutting the link
    display.drawLine(11, 39, 17, 31);
  } else {
    // Disconnected 'X' with burst spark particles
    display.drawLine(11, 32, 17, 38);
    display.drawLine(11, 38, 17, 32);
    display.drawPixel(8, 30);
    display.drawPixel(20, 30);
    display.drawPixel(8, 40);
    display.drawPixel(20, 40);
  }

  // Right Side: Attack Telemetry across full height (No bottom footer bar!)
  display.setFont(u8g2_font_5x7_tf);

  // Cycle through marked targets
  std::vector<int> marked_idx;
  for (size_t i = 0; i < scan_results.size(); i++) {
    if (net_marked[i])
      marked_idx.push_back(i);
  }
  int active_ch = 1;
  const char *active_band = "2G";
  String activeSSID = "Scanning...";

  if (!marked_idx.empty()) {
    int cur_idx = marked_idx[(millis() / 2000) % marked_idx.size()];
    WiFiScanResult &tgt = scan_results[cur_idx];
    active_ch = tgt.channel;
    active_band = tgt.is5GHz ? "5G" : "2G";
    activeSSID = (tgt.ssid.length() > 0) ? tgt.ssid : String("*HIDDEN*");
  }

  // Marquee scroll target SSID if length > 10
  char sbuf[24];
  if (activeSSID.length() > 10) {
    int maxOff = activeSSID.length() - 10;
    int off = (millis() / 250) % (maxOff + 4);
    if (off > maxOff)
      off = maxOff;
    snprintf(sbuf, sizeof(sbuf), "TGT : %.10s",
             activeSSID.substring(off, off + 10).c_str());
  } else {
    snprintf(sbuf, sizeof(sbuf), "TGT : %.10s", activeSSID.c_str());
  }

  // Right Side: Clean, spacious telemetry with 14px vertical spacing
  display.drawStr(36, 26, sbuf);

  char ch_buf[24];
  snprintf(ch_buf, sizeof(ch_buf), "CH  : %d [%s]", active_ch, active_band);
  display.drawStr(36, 40, ch_buf);

  char pkt_buf[24];
  if (deauth_fail_count > 0) {
    snprintf(pkt_buf, sizeof(pkt_buf), "PKT:%lu E:%lu", (unsigned long)sent_frames, (unsigned long)deauth_fail_count);
  } else {
    snprintf(pkt_buf, sizeof(pkt_buf), "PKTS: %lu", (unsigned long)sent_frames);
  }
  display.drawStr(36, 54, pkt_buf);
}

void render_beacon_confirm() {
  draw_header("CONFIRM BEACON", "25AP");

  // Modern Clean Broadcast / Wi-Fi Beacon Icon (X=4..24, Y=18..42)
  // Center radio source / antenna mast dot
  display.drawDisc(14, 38, 2);
  display.drawVLine(14, 35, 8); // antenna mast
  display.drawHLine(11, 43, 7); // mast base

  // Expanding Concentric Upper Radio Waves
  display.drawCircle(14, 34, 5, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  display.drawCircle(14, 34, 9, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  display.drawCircle(14, 34, 13, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);

  display.setFont(u8g2_font_5x7_tf);
  char buf[28];
  snprintf(buf, sizeof(buf), "SPAM %d FAKE APs", NUM_BEACON_SSIDS);
  display.drawStr(32, 22, buf);
  display.drawStr(32, 32, "Dual-Band 2.4G/5G");
  display.drawStr(32, 42, "SSID Broadcast");

  // Two balanced action buttons at Y=46..60 (height 15px)
  draw_btn(10, 46, 50, 15, "CANCEL", menu_sel == 1);
  draw_btn(68, 46, 50, 15, "START", menu_sel == 0);
}

void render_beacon_running() {
  // Header badge shows mode + count
  char hdrbuf[16];
  const char* modeTag = (beacon_mode == BEACON_MODE_RANDOM) ? "RND" :
                        (beacon_mode == BEACON_MODE_CLONE)  ? "CLN" : "CST";
  snprintf(hdrbuf, sizeof(hdrbuf), "%s %d", modeTag, beacon_count);
  draw_header("* BEACON FLOOD *", hdrbuf);

  // Animated Broadcast Transmitter Icon (X=4..26, Y=16..42)
  int tx = 14, ty = 36;
  display.drawDisc(tx, ty, 2);         // Transmitter node
  display.drawVLine(tx, ty + 1, 6);    // Stand
  display.drawHLine(tx - 3, ty + 7, 7);// Base

  // Pulsing / expanding circular broadcast waves (left and right + upper)
  int phase = (millis() / 200) % 3;
  if (phase >= 0) {
    display.drawCircle(tx, ty - 2, 5, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  }
  if (phase >= 1) {
    display.drawCircle(tx, ty - 2, 9, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  }
  if (phase >= 2) {
    display.drawCircle(tx, ty - 2, 13, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
  }

  // Right Side: Telemetry (X=32 to fit comfortably)
  display.setFont(u8g2_font_5x7_tf);
  char buf[28];
  if (beacon_fail_count > 0) {
    snprintf(buf, sizeof(buf), "PKT:%lu E:%lu", (unsigned long)beacon_frames, (unsigned long)beacon_fail_count);
  } else {
    snprintf(buf, sizeof(buf), "PKTS: %lu", (unsigned long)beacon_frames);
  }
  display.drawStr(32, 23, buf);

  char cntbuf[20];
  snprintf(cntbuf, sizeof(cntbuf), "SSIDs: %d", beacon_count);
  display.drawStr(32, 35, cntbuf);

  // Fake SSID Ticker in an inverted badge spanning lower area
  display.drawRBox(30, 47, 96, 12, 1);
  char ssid_buf[36];
  get_beacon_ssid(beacon_ssid_idx % beacon_count, ssid_buf, sizeof(ssid_buf));
  // Strip invisible NBSP padding for display
  char disp_buf[17];
  int dlen = 0;
  for (int c = 0; ssid_buf[c] && dlen < 14; c++) {
    unsigned char uc = (unsigned char)ssid_buf[c];
    if (uc == 0xC2 || uc == 0xA0) continue; // skip NBSP bytes
    disp_buf[dlen++] = ssid_buf[c];
  }
  disp_buf[dlen] = '\0';
  display.setDrawColor(0);
  display.drawStr(34, 56, disp_buf);
  display.setDrawColor(1);
}

// ─────────────────────────────────────────────────────────
// Beacon Setup Screen
// ─────────────────────────────────────────────────────────
void render_beacon_setup() {
  draw_header("BEACON SETUP", "CFG");

  display.setFont(u8g2_font_5x7_tf);

  // Row 0: Mode selector
  bool row0sel = (beacon_setup_sel == 0);
  if (row0sel) {
    display.drawBox(0, 11, 128, 11);
    display.setDrawColor(0);
  }
  display.drawStr(3, 20, "MODE:");
  const char* modeName = (beacon_mode == BEACON_MODE_RANDOM) ? "RANDOM" :
                         (beacon_mode == BEACON_MODE_CLONE)  ? "CLONE " : "CUSTOM";
  if (row0sel) {
    display.drawStr(35, 20, "<");
    display.drawStr(52, 20, modeName);
    display.drawStr(90, 20, ">");
  } else {
    display.drawStr(52, 20, modeName);
  }
  if (row0sel) display.setDrawColor(1);

  // Row 1: Count selector
  bool row1sel = (beacon_setup_sel == 1);
  if (row1sel) {
    display.drawBox(0, 22, 128, 11);
    display.setDrawColor(0);
  }
  display.drawStr(3, 31, "QTD: ");
  char cntbuf[12];
  snprintf(cntbuf, sizeof(cntbuf), "%d", beacon_count);
  if (row1sel) {
    display.drawStr(35, 31, "<");
    display.drawStr(52, 31, cntbuf);
    display.drawStr(90, 31, ">");
  } else {
    display.drawStr(52, 31, cntbuf);
  }
  if (row1sel) display.setDrawColor(1);

  // Row 2: Clone note (only shown in CLONE mode)
  int marked = 0;
  if (beacon_mode == BEACON_MODE_CLONE) {
    for (size_t i = 0; i < scan_results.size(); i++) if (net_marked[i]) marked++;
    char note[28];
    snprintf(note, sizeof(note), "Base: %d marked AP(s)", marked);
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(3, 41, marked > 0 ? note : "Mark APs in Net List");
  } else if (beacon_mode == BEACON_MODE_CUSTOM) {
    char note[20];
    snprintf(note, sizeof(note), "SSID: %.12s", beacon_custom_ssid);
    display.drawStr(3, 41, note);
  } else {
    display.drawStr(3, 41, "25 built-in SSIDs");
  }

  // Buttons row
  const char* actLabel = (beacon_mode == BEACON_MODE_CUSTOM) ? "KEYBOARD" :
                         (beacon_mode == BEACON_MODE_CLONE && marked == 0) ? "TARGETS" : "START";
  draw_btn(4,  48, 56, 13, actLabel, beacon_setup_sel == 2);
  draw_btn(68, 48, 56, 13, "CANCEL", beacon_setup_sel == 3);
}

// ─────────────────────────────────────────────────────────
// Beacon Keyboard Screen  (phone-style, 3 pages)
// Layout:
//   Y 0-9  : header "CUSTOM SSID | abc/ABC/123"
//   Y10-20 : preview bar (inverted, shows what has been typed)
//   Y21-48 : character grid  (3 rows × 10 cols, cell 12×9)
//   Y50-62 : action row  [⇧/page] [  SPACE  ] [⌫] [✓]
// ─────────────────────────────────────────────────────────
void render_beacon_keyboard() {
  display.setFont(u8g2_font_5x7_tf);

  // ── Preview do texto (fundo preto limpo, sem barra branca ofuscante) ──
  display.setDrawColor(1);
  {
    // Mostra os últimos 20 caracteres + cursor piscante
    char prev[26];
    int  show = (kb_len > 20) ? 20 : kb_len;
    if (show > 0) {
      memcpy(prev, beacon_custom_ssid + (kb_len - show), show);
    }
    // Cursor piscante a cada 400ms
    bool blink = ((millis() / 400) % 2) == 0;
    prev[show]   = blink ? '_' : ' ';
    prev[show+1] = '\0';

    display.drawStr(2, 9, "> ");
    display.drawStr(14, 9, prev);
  }

  // Linha divisória sutil
  display.drawHLine(0, 13, 128);

  // ── Character grid ──────────────────────────────────────
  // 10 cols × 12px wide = 120px, centered (4px margin each side)
  // 3 rows  ×  9px tall
  const int CELL_W = 12;
  const int CELL_H =  9;
  const int GRID_X =  4;   // left margin
  const int GRID_Y = 22;   // first row baseline

  bool inCharGrid = (kb_row < KB_CHAR_ROWS);

  for (int r = 0; r < KB_CHAR_ROWS; r++) {
    for (int c = 0; c < KB_COLS; c++) {
      int  cx = GRID_X + c * CELL_W;
      int  cy = GRID_Y + r * CELL_H;
      char ch = KB_DATA[kb_page][r][c];
      if (ch == '\0') continue;

      bool isCursor = inCharGrid && (kb_row == r) && (kb_col == c);
      if (isCursor) {
        display.drawBox(cx - 1, cy - 6, CELL_W - 1, CELL_H - 1);
        display.setDrawColor(0);
      }
      char buf[2] = { ch, '\0' };
      display.drawStr(cx + 2, cy, buf);
      if (isCursor) display.setDrawColor(1);
    }
  }

  // ── Action row ──────────────────────────────────────────
  // Row 3 = kb_row == KB_CHAR_ROWS
  // Cols: 0-1=page/shift, 2-6=space, 7-8=del, 9=ok
  bool actSel = (kb_row == KB_CHAR_ROWS);
  const int AY = GRID_Y + KB_CHAR_ROWS * CELL_H + 2; // top of action row

  // [⇧] Shift/Page pill  (cols 0-1, width ~22px)
  bool shSel = actSel && (kb_col <= 1);
  const char* shLbl = (kb_page == 0) ? "ABC" :
                      (kb_page == 1) ? "123" : "abc";
  if (shSel) display.drawRBox(0, AY, 22, 11, 2);
  else        display.drawRFrame(0, AY, 22, 11, 2);
  display.setDrawColor(shSel ? 0 : 1);
  display.drawStr(2, AY + 8, shLbl);
  display.setDrawColor(1);

  // [   SPACE   ] pill  (cols 2-6, width ~57px)
  bool spSel = actSel && (kb_col >= 2) && (kb_col <= 6);
  if (spSel) display.drawRBox(24, AY, 57, 11, 2);
  else        display.drawRFrame(24, AY, 57, 11, 2);
  display.setDrawColor(spSel ? 0 : 1);
  display.drawStr(40, AY + 8, "SPACE");
  display.setDrawColor(1);

  // [⌫ DEL] pill  (cols 7-8, width ~22px)
  bool dlSel = actSel && (kb_col >= 7) && (kb_col <= 8);
  if (dlSel) display.drawRBox(83, AY, 22, 11, 2);
  else        display.drawRFrame(83, AY, 22, 11, 2);
  display.setDrawColor(dlSel ? 0 : 1);
  display.drawStr(85, AY + 8, "DEL");
  display.setDrawColor(1);

  // [✓ OK] pill  (col 9, width ~20px)
  bool okSel = actSel && (kb_col == 9);
  if (okSel) display.drawRBox(107, AY, 20, 11, 2);
  else        display.drawRFrame(107, AY, 20, 11, 2);
  display.setDrawColor(okSel ? 0 : 1);
  display.drawStr(110, AY + 8, "OK");
  display.setDrawColor(1);
}

void render_ble_menu() {
  draw_header("BLUETOOTH SPAM", "APPLE");

  // Left: Animated Bluetooth icon
  display.drawXBMP(4, 16, 24, 24, anim_bluetooth_24x24[center_anim_frame % 14]);

  // Right: Feature overview
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(34, 24, "EvilAppleJuice");
  display.drawStr(34, 34, "22 audio devices");
  display.drawStr(34, 44, "Random MAC");

  // Prominent Action Button
  draw_btn(10, 50, 108, 13, "INICIAR SPAM", true);
}

// ─── BLE Mode Select ─────────────────────────────────────
// Tela de seleção: modo ALL (22 devices) vs FOCUSED (AirPods only)
void render_ble_select() {
  draw_header("BLE SPAM MODE", "");

  display.setFont(u8g2_font_5x7_tf);

  // Opção 1: ALL DEVICES
  bool sel0 = (ble_select_sel == 0);
  if (sel0) {
    display.drawRBox(2, 13, 124, 23, 2);
  } else {
    display.drawRFrame(2, 13, 124, 23, 2);
  }
  draw_str_centered(22, "ALL DEVICES", sel0 ? 0 : 1);
  draw_str_centered(32, "22 audio - cycle", sel0 ? 0 : 1);

  // Opção 2: AIRPODS FOCUSED
  bool sel1 = (ble_select_sel == 1);
  if (sel1) {
    display.drawRBox(2, 38, 124, 23, 2);
  } else {
    display.drawRFrame(2, 38, 124, 23, 2);
  }
  draw_str_centered(47, "AIRPODS FOCUSED", sel1 ? 0 : 1);
  draw_str_centered(57, "max popups iOS", sel1 ? 0 : 1);
}

void render_ble_running() {
  draw_header("* BLE SPAM *", ble_focused_mode ? "FOCUSED" : "ALL");

  // Left: Pulsing Bluetooth animation
  display.drawXBMP(4, 16, 24, 24, anim_bluetooth_24x24[center_anim_frame % 14]);

  // Right: Telemetry
  display.setFont(u8g2_font_5x7_tf);
  char buf[28];
  snprintf(buf, sizeof(buf), "PKTS: %lu", (unsigned long)ble_pkt_sent);
  display.drawStr(34, 24, buf);

  // Device index counter
  char idbuf[20];
  if (ble_focused_mode) {
    snprintf(idbuf, sizeof(idbuf), "DEV: AirPods x%lu", (unsigned long)ble_pkt_sent);
  } else {
    snprintf(idbuf, sizeof(idbuf), "DEV: %u/22", (unsigned)(ble_spam_idx + 1));
  }
  display.drawStr(34, 34, idbuf);

  display.drawStr(34, 44, "TGT: iPhone/iPad");

  // Inverted pill: current Apple device name (max 16 chars)
  display.drawRBox(2, 50, 124, 12, 2);
  char nbuf[17];
  String(ble_current_name()).substring(0, 16).toCharArray(nbuf, sizeof(nbuf));
  display.setDrawColor(0);
  draw_str_centered(59, nbuf, 0);
  display.setDrawColor(1);
}

void render_about() {
  char count_buf[14];
  snprintf(count_buf, sizeof(count_buf), "%d/%d", about_sel + 1, ABOUT_ITEMS_COUNT);
  draw_header("DEVICE INFO", count_buf);

  display.setFont(u8g2_font_5x7_tf);
  const int visible = 4;
  for (int i = 0; i < visible && (about_scroll + i) < ABOUT_ITEMS_COUNT; i++) {
    int idx   = about_scroll + i;
    int y_top = 13 + i * 12; // 13, 25, 37, 49
    int y_txt = y_top + 8;
    bool isSel = (idx == about_sel);

    if (isSel) {
      display.drawRBox(0, y_top, 122, 11, 2);
    }
    int color = isSel ? 0 : 1;
    display.setDrawColor(color);

    const AboutItem &item = ABOUT_ITEMS[idx];
    if (item.isAction) {
      if (idx == ABOUT_ITEMS_COUNT - 2) {
        display.drawStr(isSel ? 6 : 4, y_txt, "> ENTRAR FLASH MODE");
      } else {
        display.drawStr(isSel ? 6 : 4, y_txt, "< VOLTAR AO MENU");
      }
    } else {
      char row_buf[32];
      snprintf(row_buf, sizeof(row_buf), "%-5s: %s", item.label, item.value);
      display.drawStr(4, y_txt, row_buf);
    }

    display.setDrawColor(1);
  }

  // Right Edge Vertical Scrollbar
  if (ABOUT_ITEMS_COUNT > visible) {
    display.drawVLine(126, 13, 49);
    int trackHeight = 49;
    int thumbHeight = (int)((visible * trackHeight) / ABOUT_ITEMS_COUNT);
    if (thumbHeight < 6) thumbHeight = 6;
    int maxScroll = ABOUT_ITEMS_COUNT - visible;
    int thumbY = 13 + (about_scroll * (trackHeight - thumbHeight)) / maxScroll;
    display.drawBox(125, thumbY, 3, thumbHeight);
  }
}

void render_screen() {
  display.clearBuffer();
  switch (current_screen) {
  case SCR_SPLASH:
    render_splash();
    break;
  case SCR_MAIN_MENU:
    render_main_menu();
    break;
  case SCR_WIFI_MENU:
    render_wifi_menu();
    break;
  case SCR_SCANNING:
    render_scanning();
    break;
  case SCR_NET_LIST:
    render_net_list();
    break;
  case SCR_DEAUTH_CONFIRM:
    render_deauth_confirm();
    break;
  case SCR_DEAUTH_RUNNING:
    render_deauth_running();
    break;
  case SCR_BEACON_CONFIRM:
    render_beacon_confirm();
    break;
  case SCR_BEACON_SETUP:
    render_beacon_setup();
    break;
  case SCR_BEACON_KEYBOARD:
    render_beacon_keyboard();
    break;
  case SCR_BEACON_RUNNING:
    render_beacon_running();
    break;
  case SCR_BLE_MENU:
    render_ble_menu();
    break;
  case SCR_BLE_SELECT:
    render_ble_select();
    break;
  case SCR_BLE_RUNNING:
    render_ble_running();
    break;
  case SCR_GAMES_MENU:
    render_games_menu();
    break;
  case SCR_GAME_TETRIS:
    render_game_tetris();
    break;
  case SCR_GAME_INVADERS:
    render_game_invaders();
    break;
  case SCR_GAME_DOOM:
    render_game_doom();
    break;
  case SCR_GAME_SNAKE:
    render_game_snake();
    break;
  case SCR_ABOUT:
    render_about();
    break;
  default:
    break;
  }
  display.sendBuffer();
}