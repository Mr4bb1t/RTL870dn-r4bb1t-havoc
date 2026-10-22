#ifndef UI_STATE_H
#define UI_STATE_H

#include "config.h"

extern Screen current_screen;
extern int    menu_sel;

// Beacon setup
extern int  beacon_setup_sel;

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

void handle_input();

#endif // UI_STATE_H