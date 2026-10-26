#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\ui_state.h"
#ifndef UI_STATE_H
#define UI_STATE_H

#include "config.h"

extern Screen current_screen;
extern int    menu_sel;

// Beacon setup
extern int  beacon_setup_sel;
extern bool beacon_net_list_picking; // True when SCR_NET_LIST was opened from BEACON CLONE mode

// BLE mode select
extern int  ble_select_sel; // 0=ALL DEVICES, 1=AIRPODS FOCUSED

// About (Device Info) scroll state
extern int  about_sel;
extern int  about_scroll;

struct AboutItem {
  const char* label;
  const char* value;
  bool isAction;
};

extern const AboutItem ABOUT_ITEMS[];
extern const int ABOUT_ITEMS_COUNT;

// On-screen keyboard (phone-style, 3 pages)
#define KB_COLS       10
#define KB_CHAR_ROWS   3
#define KB_PAGE_COUNT  3
#define KB_ROWS_N  KB_CHAR_ROWS   // alias for render code
extern int  kb_page, kb_row, kb_col, kb_len;
// KB_DATA[page][row][col+1]  — defined in ui_state.cpp
extern const char KB_DATA[KB_PAGE_COUNT][KB_CHAR_ROWS][KB_COLS + 1];

bool check_btn_up();
bool check_btn_dn();
bool check_btn_left();
bool check_btn_right();
bool check_btn_enter();
bool check_btn_sel_short();
bool check_btn_sel_long();

void flush_input();
void handle_input();

#endif // UI_STATE_H