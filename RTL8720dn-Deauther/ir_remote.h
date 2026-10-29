#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include "config.h"
#include <U8g2lib.h>

// -----------------------------------------------------------------------------
// IR Remote / Universal Remote
//
// RX : D9  (PA_15) -> TSOP / VS1838 demodulator output
// TX : D12 (PA_12)  -> IR LED (through a transistor) driven by 38 kHz PWM
//
// Signals are captured as raw microsecond durations, decoded when possible
// (NEC / Samsung / Sony SIRC) and stored persistently in the vendor reserved
// flash sector at 0x00100000 (see FlashMemory library).
// -----------------------------------------------------------------------------
#define IR_MAX_RAW       200   // max edges stored per signal
#define IR_MAX_SAVED       8   // max saved signals
#define IR_FRAME_GAP_US 15000  // >15 ms of silence ends a frame

enum IRProtocol : uint8_t {
  IR_PROTO_RAW = 0,
  IR_PROTO_NEC,
  IR_PROTO_SAMSUNG,
  IR_PROTO_SONY,
};

struct IRSavedSignal {
  uint8_t  count;      // number of raw entries (0 = empty)
  uint8_t  protocol;   // IRProtocol
  uint16_t address;    // decoded address (Sony: 0)
  uint16_t command;    // decoded command (Sony: full 12-bit word)
  uint16_t raw[IR_MAX_RAW];
};

extern int ir_menu_cur_x;
extern int ir_menu_target_x;

void ir_init();
void ir_tick();

// Capture
void ir_start_capture();
void ir_stop_capture();
bool ir_capture_running();
bool ir_has_signal();
const IRSavedSignal *ir_current_signal();
uint32_t ir_edge_count();
void ir_clear_current();

// Saved signals
int  ir_saved_count();
const IRSavedSignal *ir_saved_at(int idx);
bool ir_save_current();
bool ir_delete_saved(int idx);
void ir_emit_saved(int idx);
void ir_replay_current();

// Spam
void ir_start_spam();
void ir_stop_spam();
bool ir_spam_active();

// UI
void ir_handle_input(bool up, bool dn, bool left, bool right, bool enter);
void render_ir_menu();
void render_ir_record();
void render_ir_list();
void render_ir_detail();
void render_ir_spam();

#endif // IR_REMOTE_H
