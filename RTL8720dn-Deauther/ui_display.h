#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "config.h"
#include <Wire.h>
#include <U8g2lib.h>

extern "C" uint8_t u8x8_byte_ameba_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

class U8G2_SH1106_AMEBA_HW_I2C : public U8G2 {
public:
  U8G2_SH1106_AMEBA_HW_I2C(const u8g2_cb_t *rotation) : U8G2() {
    u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, rotation, u8x8_byte_ameba_hw_i2c, u8x8_gpio_and_delay_arduino);
  }
};

extern U8G2_SH1106_AMEBA_HW_I2C display;

extern int      anim_frame;
extern uint32_t last_anim_ms;

void tick_anim();
void trigger_arrow_left();
void trigger_arrow_right();

extern int wifi_menu_cur_x;
extern int wifi_menu_target_x;

void draw_corner_brackets(int x, int y, int w, int h, int len = 6);
void draw_header(const char* title, const char* badge = nullptr);
void draw_hint(const char* hint, bool center = false);
void draw_btn(int x, int y, int w, int h, const char* text, bool is_selected);
void draw_str_centered(int y, const char* str, int color = 1);
void draw_signal_bars(int x, int y, int rssi, int color = 1);
void draw_rssi_bar(int x, int y, int rssi);
void draw_spinner(int x, int y);

void render_splash(int progress = -1);
void render_main_menu();
void render_wifi_menu();
void render_scanning();
void render_net_list();
void render_deauth_confirm();
void render_deauth_running();
void render_beacon_confirm();
void render_beacon_setup();
void render_beacon_keyboard();
void render_beacon_running();
void render_ble_menu();
void render_ble_running();
void render_about();

void render_screen();

#endif // UI_DISPLAY_H