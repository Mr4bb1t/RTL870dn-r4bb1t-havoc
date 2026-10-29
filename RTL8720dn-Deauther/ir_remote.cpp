// STL MUST come first
#undef max
#undef min
#undef abs
#include <vector>
#include <string.h>
#include <stdio.h>

#include "ir_remote.h"
#include "ui_display.h"
#include "ui_state.h"
#include "debug.h"
#include <FlashMemory.h>

// =============================================================================
// STATE
// =============================================================================
int ir_menu_cur_x    = 0;
int ir_menu_target_x = 0;

static const char *proto_name(uint8_t p);

// Capture state (synchronous polling)
static uint16_t raw_scratch[IR_MAX_RAW];
static volatile uint32_t cap_edges = 0;      // edges seen (diagnostics)
static bool     capture_on = false;
static uint32_t ir_last_status_ms = 0;

static IRSavedSignal cur_sig;
static bool          have_sig = false;

static IRSavedSignal saved_sigs[IR_MAX_SAVED];
static uint8_t       saved_n = 0;
static bool          store_loaded = false;

static int  ir_record_sel = 0;
static int  ir_list_sel = 0;
static int  ir_list_scroll = 0;
static int  ir_detail_sel = 0;
static int  ir_detail_idx = 0;
static bool ir_delete_confirm = false;

static bool     spam_running = false;
static int      spam_idx = 0;
static bool     spam_vol = true;
static uint32_t spam_last_ms = 0;
static uint32_t spam_sent = 0;

static char     ir_toast[24] = "";
static uint32_t ir_toast_ms = 0;

// =============================================================================
// FLASH PERSISTENCE
// =============================================================================
#define IR_STORE_MAGIC   0x49523031UL  // "IR01"
#define IR_STORE_VERSION 1
#define IR_FLASH_BASE    FLASH_MEMORY_APP_BASE
#define IR_FLASH_SIZE    4096

struct IRStoreHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
};

static void ir_store_load() {
  if (store_loaded) return;
  store_loaded = true;

  FlashMemory.begin(IR_FLASH_BASE, IR_FLASH_SIZE);
  FlashMemory.read();

  IRStoreHeader h;
  memcpy(&h, FlashMemory.buf, sizeof(h));
  if (h.magic != IR_STORE_MAGIC || h.version != IR_STORE_VERSION ||
      h.count > IR_MAX_SAVED) {
    saved_n = 0;
    return;
  }

  saved_n = (uint8_t)h.count;
  memcpy(saved_sigs, FlashMemory.buf + sizeof(IRStoreHeader),
         saved_n * sizeof(IRSavedSignal));

  for (int i = 0; i < saved_n; i++) {
    if (saved_sigs[i].count > IR_MAX_RAW) saved_sigs[i].count = 0;
    if (saved_sigs[i].protocol > IR_PROTO_SONY) saved_sigs[i].protocol = IR_PROTO_RAW;
  }
}

static void ir_store_save() {
  FlashMemory.begin(IR_FLASH_BASE, IR_FLASH_SIZE);
  memset(FlashMemory.buf, 0, IR_FLASH_SIZE);

  IRStoreHeader h;
  h.magic   = IR_STORE_MAGIC;
  h.version = IR_STORE_VERSION;
  h.count   = saved_n;
  memcpy(FlashMemory.buf, &h, sizeof(h));
  memcpy(FlashMemory.buf + sizeof(IRStoreHeader), saved_sigs,
         saved_n * sizeof(IRSavedSignal));

  FlashMemory.update(true);
  DEBUG_SER_PRINT("[IR] Saved signals: ");
  DEBUG_SER_PRINT((int)saved_n);
  DEBUG_SER_PRINT("\n");
}

// =============================================================================
// CAPTURE (synchronous polling)
//
// The AmebaD core enables a hardware debounce filter inside gpio_irq_set()
// (gpio_irq_api.c line 192 passes GPIO_INT_DEBOUNCE_ENABLE). That filter eats
// the 560 us pulses of an IR frame, so attachInterrupt() never fires for a
// remote. Polling the pin directly avoids that path completely; the shortest
// pulse of the supported protocols is 560 us, which a tight loop catches
// easily.
// =============================================================================
static void decode_current();
static void ir_debug_dump();

int ir_pin_level() { return digitalRead(IR_RX_PIN) ? 1 : 0; }

static bool capture_once() {
  const uint32_t WAIT_US = 40000;   // keep the UI alive between attempts
  const uint32_t HARD_US = 400000;  // absolute cap for one frame

  int last = ir_pin_level();
  uint32_t t_wait = micros();

  // Phase 1: wait for the line to change at all.
  while (capture_on) {
    int v = ir_pin_level();
    if (v != last) {
      last = v;
      break;
    }
    if ((uint32_t)(micros() - t_wait) > WAIT_US) return false;
  }
  if (!capture_on) return false;

  DEBUG_SER_PRINT("[IR] first edge, line now ");
  DEBUG_SER_PRINT(last ? "HIGH" : "LOW");
  DEBUG_SER_PRINT("\n");

  // Phase 2: record the interval between every edge.
  int n = 0;
  uint32_t t_start = micros();
  uint32_t last_edge = t_start;
  uint32_t spin = 0;

  while (capture_on) {
    int v = ir_pin_level();
    if (v != last) {
      last = v;
      uint32_t now = micros();
      uint32_t d = now - last_edge;
      last_edge = now;
      if (n < IR_MAX_RAW) {
        raw_scratch[n++] = (uint16_t)(d > 60000 ? 60000 : d);
      }
      cap_edges++;
      spin = 0;
      continue;
    }
    if ((++spin & 0x3F) == 0) {
      uint32_t now = micros();
      if ((uint32_t)(now - last_edge) > IR_FRAME_GAP_US) break;
      if ((uint32_t)(now - t_start) > HARD_US) break;
    }
  }

  if (n < 6) {
    DEBUG_SER_PRINT("[IR] discarded, only ");
    DEBUG_SER_PRINT(n);
    DEBUG_SER_PRINT(" edges\n");
    return false;
  }

  memcpy(cur_sig.raw, raw_scratch, n * sizeof(uint16_t));
  for (int i = n; i < IR_MAX_RAW; i++) cur_sig.raw[i] = 0;
  cur_sig.count = (uint8_t)n;
  decode_current();
  have_sig = true;
  ir_debug_dump();
  return true;
}

void ir_start_capture() {
  pinMode(IR_RX_PIN, INPUT_PULLUP);
  capture_on = true;
  cap_edges = 0;
  ir_last_status_ms = millis();

  delay(2);
  DEBUG_SER_PRINT("[IR] capture armed: pin D");
  DEBUG_SER_PRINT(IR_RX_PIN);
  DEBUG_SER_PRINT(", idle level=");
  DEBUG_SER_PRINT(ir_pin_level() ? "HIGH" : "LOW");
  DEBUG_SER_PRINT("\n");
}

void ir_stop_capture() {
  if (capture_on) {
    capture_on = false;
    DEBUG_SER_PRINT("[IR] capture stopped. edges=");
    DEBUG_SER_PRINT((unsigned)cap_edges);
    DEBUG_SER_PRINT("\n");
  }
}

bool ir_capture_running() { return capture_on; }
bool ir_has_signal()      { return have_sig; }
const IRSavedSignal *ir_current_signal() { return &cur_sig; }
uint32_t ir_edge_count()  { return cap_edges; }

void ir_clear_current() {
  have_sig = false;
  memset(&cur_sig, 0, sizeof(cur_sig));
}

// =============================================================================
// DECODERS
// =============================================================================
static bool close_to(uint16_t v, uint16_t target, uint16_t tol) {
  return (v + tol >= target) && (v <= target + tol);
}

static bool decode_nec(const uint16_t *r, int n, uint16_t &addr,
                       uint16_t &cmd, bool &samsung) {
  if (n < 66) return false;

  bool nec_hdr = close_to(r[0], 9000, 2200) && close_to(r[1], 4500, 1300);
  bool sam_hdr = close_to(r[0], 4500, 1300) && close_to(r[1], 4500, 1300);
  if (!nec_hdr && !sam_hdr) return false;

  uint32_t data = 0;
  int i = 2;
  for (int b = 0; b < 32; b++) {
    if (i + 1 >= n) return false;
    uint16_t mark  = r[i++];
    uint16_t space = r[i++];
    if (!close_to(mark, 560, 280)) return false;
    if (space > 1000) {
      data |= (1UL << b);
    } else if (space < 300) {
      return false;
    }
  }

  addr    = (uint16_t)(data & 0xFFFF);
  cmd     = (uint16_t)((data >> 16) & 0xFFFF);
  samsung = sam_hdr;
  return true;
}

static bool decode_sony(const uint16_t *r, int n, uint16_t &word) {
  if (n < 24) return false;
  if (!close_to(r[0], 2400, 700) || !close_to(r[1], 600, 350)) return false;

  uint16_t v = 0;
  int i = 2;
  for (int b = 0; b < 12; b++) {
    if (i >= n) return false;
    uint16_t mark = r[i];
    i += 2;
    v <<= 1;
    if (mark > 900) {
      v |= 1;
    } else if (mark < 420) {
      return false;
    }
  }
  word = v;
  return true;
}

static void decode_current() {
  cur_sig.protocol = IR_PROTO_RAW;
  cur_sig.address  = 0;
  cur_sig.command  = 0;

  uint16_t a = 0, c = 0;
  bool samsung = false;
  if (decode_nec(cur_sig.raw, cur_sig.count, a, c, samsung)) {
    cur_sig.protocol = samsung ? IR_PROTO_SAMSUNG : IR_PROTO_NEC;
    cur_sig.address  = a;
    cur_sig.command  = c;
    return;
  }

  uint16_t w = 0;
  if (decode_sony(cur_sig.raw, cur_sig.count, w)) {
    cur_sig.protocol = IR_PROTO_SONY;
    cur_sig.address  = 0;
    cur_sig.command  = w;
  }
}

// =============================================================================
// TRANSMIT (38 kHz carrier on IR_TX_PIN)
// =============================================================================
static void ir_mark(uint32_t us) {
  analogWrite(IR_TX_PIN, 128);   // 50% duty -> 38 kHz carrier
  delayMicroseconds(us);
  analogWrite(IR_TX_PIN, 0);     // carrier off
}

static void ir_space(uint32_t us) {
  delayMicroseconds(us);
}

static void send_nec_word(uint32_t data, bool samsung) {
  for (int rep = 0; rep < 5; rep++) {
    ir_mark(samsung ? 4500 : 9000);
    ir_space(4500);

    for (int b = 0; b < 32; b++) {
      ir_mark(560);
      ir_space((data & (1UL << b)) ? 1690 : 560);
    }

    ir_mark(560);
    ir_space(samsung ? 45000 : 40000);
  }
}

static void send_sony_word(uint16_t word) {
  for (int rep = 0; rep < 3; rep++) {
    ir_mark(2400);
    ir_space(600);

    for (int b = 11; b >= 0; b--) {
      ir_mark((word & (1 << b)) ? 1200 : 600);
      ir_space(600);
    }

    ir_space(20000);
  }
}

static void send_raw_signal(const IRSavedSignal &s) {
  for (int i = 0; i < s.count; i++) {
    if (i & 1) ir_space(s.raw[i]);
    else       ir_mark(s.raw[i]);
  }
  ir_space(40000);
}

static void ir_send_signal(const IRSavedSignal &s) {
  if (s.count < 4 && s.protocol == IR_PROTO_RAW) {
    DEBUG_SER_PRINT("[IR] tx aborted: empty raw signal\n");
    return;
  }

  DEBUG_SER_PRINT("[IR] tx start proto=");
  DEBUG_SER_PRINT(proto_name(s.protocol));
  DEBUG_SER_PRINT(" addr=0x");
  DEBUG_SER_PRINT((unsigned)s.address);
  DEBUG_SER_PRINT(" cmd=0x");
  DEBUG_SER_PRINT((unsigned)s.command);
  DEBUG_SER_PRINT(" edges=");
  DEBUG_SER_PRINT((int)s.count);
  DEBUG_SER_PRINT("\n");

  bool was_capturing = capture_on;
  if (was_capturing) ir_stop_capture();

  switch (s.protocol) {
    case IR_PROTO_NEC:
      send_nec_word((uint32_t)s.address | ((uint32_t)s.command << 16), false);
      break;
    case IR_PROTO_SAMSUNG:
      send_nec_word((uint32_t)s.address | ((uint32_t)s.command << 16), true);
      break;
    case IR_PROTO_SONY:
      send_sony_word(s.command);
      break;
    default:
      send_raw_signal(s);
      break;
  }

  DEBUG_SER_PRINT("[IR] tx done\n");

  if (was_capturing) {
    // Let the receiver AGC recover before re-arming, otherwise it can capture
    // the tail of our own transmission.
    delay(60);
    ir_start_capture();
  }
}

// =============================================================================
// SAVED SIGNAL API
// =============================================================================
int ir_saved_count() { ir_store_load(); return saved_n; }

const IRSavedSignal *ir_saved_at(int idx) {
  ir_store_load();
  if (idx < 0 || idx >= saved_n) return nullptr;
  return &saved_sigs[idx];
}

bool ir_save_current() {
  ir_store_load();
  if (!have_sig || cur_sig.count < 4 || saved_n >= IR_MAX_SAVED) {
    DEBUG_SER_PRINT("[IR] save refused: have_sig=");
    DEBUG_SER_PRINT((int)have_sig);
    DEBUG_SER_PRINT(" count=");
    DEBUG_SER_PRINT((int)cur_sig.count);
    DEBUG_SER_PRINT(" used=");
    DEBUG_SER_PRINT((int)saved_n);
    DEBUG_SER_PRINT("\n");
    return false;
  }

  saved_sigs[saved_n] = cur_sig;
  saved_n++;
  ir_store_save();
  DEBUG_SER_PRINT("[IR] saved slot ");
  DEBUG_SER_PRINT((int)saved_n);
  DEBUG_SER_PRINT("\n");
  return true;
}

bool ir_delete_saved(int idx) {
  ir_store_load();
  if (idx < 0 || idx >= saved_n) return false;

  for (int i = idx; i < saved_n - 1; i++) {
    saved_sigs[i] = saved_sigs[i + 1];
  }
  saved_n--;
  memset(&saved_sigs[saved_n], 0, sizeof(IRSavedSignal));
  ir_store_save();
  DEBUG_SER_PRINT("[IR] deleted slot ");
  DEBUG_SER_PRINT(idx + 1);
  DEBUG_SER_PRINT(", now ");
  DEBUG_SER_PRINT((int)saved_n);
  DEBUG_SER_PRINT("\n");
  return true;
}

void ir_emit_saved(int idx) {
  const IRSavedSignal *s = ir_saved_at(idx);
  if (!s) {
    DEBUG_SER_PRINT("[IR] emit: invalid slot\n");
    return;
  }
  DEBUG_SER_PRINT("[IR] emit slot ");
  DEBUG_SER_PRINT(idx + 1);
  DEBUG_SER_PRINT(" proto=");
  DEBUG_SER_PRINT(proto_name(s->protocol));
  DEBUG_SER_PRINT("\n");
  ir_send_signal(*s);
}

void ir_replay_current() {
  if (have_sig) {
    DEBUG_SER_PRINT("[IR] replay captured frame\n");
    ir_send_signal(cur_sig);
  } else {
    DEBUG_SER_PRINT("[IR] replay: no signal\n");
  }
}

// =============================================================================
// SPAM
// =============================================================================
struct IRSpamEntry {
  const char *brand;
  uint8_t     protocol;
  uint16_t    address;
  uint16_t    power;
  uint16_t    volup;
};

static const IRSpamEntry IR_SPAM[] = {
  {"Samsung",   IR_PROTO_SAMSUNG, 0xE0E0, 0x40BF, 0xE01F},
  {"LG",        IR_PROTO_NEC,     0x20DF, 0x10EF, 0x40BF},
  {"Sony",      IR_PROTO_SONY,    0x0000, 0x0A90, 0x0490},
  {"Toshiba",   IR_PROTO_NEC,     0x02FD, 0x48B7, 0x40BF},
  {"NEC TV",    IR_PROTO_NEC,     0x00FF, 0x40BF, 0xE01F},
};
#define IR_SPAM_COUNT ((int)(sizeof(IR_SPAM) / sizeof(IR_SPAM[0])))

void ir_start_spam() {
  spam_running = true;
  spam_idx     = 0;
  spam_vol     = true;
  spam_sent    = 0;
  spam_last_ms = millis();
  ir_toast[0]  = '\0';
  DEBUG_SER_PRINT("[IR] spam started\n");
}

void ir_stop_spam() { spam_running = false; }
bool ir_spam_active() { return spam_running; }

// =============================================================================
// TICK
// =============================================================================
static const char *proto_name(uint8_t p);

static void ir_debug_dump() {
#ifdef DEBUG
  DEBUG_SER_PRINT("[IR] frame edges=");
  DEBUG_SER_PRINT((unsigned)cur_sig.count);
  DEBUG_SER_PRINT(" proto=");
  DEBUG_SER_PRINT(proto_name(cur_sig.protocol));
  DEBUG_SER_PRINT(" us:");
  for (int i = 0; i < cur_sig.count && i < 16; i++) {
    DEBUG_SER_PRINT(" ");
    DEBUG_SER_PRINT((unsigned)cur_sig.raw[i]);
  }
  DEBUG_SER_PRINT("\n");
#endif
}

void ir_tick() {
  // Capture runs synchronously here: waits up to 40 ms for the first edge and
  // then collects the whole frame (max ~400 ms).
  if (capture_on && !have_sig) {
    capture_once();
  }

  // Heartbeat so the serial log shows whether the receiver is alive.
  if (capture_on && !have_sig &&
      (uint32_t)(millis() - ir_last_status_ms) > 1000) {
    ir_last_status_ms = millis();
    DEBUG_SER_PRINT("[IR] waiting... edges=");
    DEBUG_SER_PRINT((unsigned)cap_edges);
    DEBUG_SER_PRINT(" level=");
    DEBUG_SER_PRINT(ir_pin_level() ? "HIGH" : "LOW");
    DEBUG_SER_PRINT("\n");
  }

  // Spam scheduler
  if (spam_running && (millis() - spam_last_ms > 850)) {
    spam_last_ms = millis();

    const IRSpamEntry &e = IR_SPAM[spam_idx];
    IRSavedSignal tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.protocol = e.protocol;
    tmp.address  = e.address;
    tmp.command  = spam_vol ? e.volup : e.power;

    ir_send_signal(tmp);
    spam_sent++;

    DEBUG_SER_PRINT("[IR] spam #");
    DEBUG_SER_PRINT((unsigned long)spam_sent);
    DEBUG_SER_PRINT(" ");
    DEBUG_SER_PRINT(e.brand);
    DEBUG_SER_PRINT(" ");
    DEBUG_SER_PRINT(spam_vol ? "VOL+" : "POWER");
    DEBUG_SER_PRINT("\n");

    if (spam_vol) {
      spam_vol = false;
    } else {
      spam_vol = true;
      spam_idx = (spam_idx + 1) % IR_SPAM_COUNT;
    }
  }

  // Toast timeout
  if (ir_toast[0] != '\0' && millis() - ir_toast_ms > 1400) {
    ir_toast[0] = '\0';
  }
}

// =============================================================================
// INIT
// =============================================================================
// Tells apart "line actively driven low" (wiring/short) from "line floating"
// (receiver OUT released, which is the healthy idle state).
static void ir_pin_selftest() {
  pinMode(IR_RX_PIN, INPUT_PULLUP);
  delay(5);
  int up = digitalRead(IR_RX_PIN) ? 1 : 0;

  pinMode(IR_RX_PIN, INPUT);   // this core maps INPUT to a pull-down
  delay(5);
  int dn = digitalRead(IR_RX_PIN) ? 1 : 0;

  pinMode(IR_RX_PIN, INPUT_PULLUP);
  delay(5);

  DEBUG_SER_PRINT("[IR] pin test: pullup=");
  DEBUG_SER_PRINT(up);
  DEBUG_SER_PRINT(" pulldown=");
  DEBUG_SER_PRINT(dn);
  DEBUG_SER_PRINT(" -> ");

  if (!up && !dn) {
    DEBUG_SER_PRINT("LINE HELD LOW: pino em curto com GND ou OUT na perna GND\n");
  } else if (up && dn) {
    DEBUG_SER_PRINT("LINE HELD HIGH: OUT na perna VCC\n");
  } else if (up && !dn) {
    DEBUG_SER_PRINT("high-Z normal (OUT em repouso = HIGH)\n");
  } else {
    DEBUG_SER_PRINT("estado estranho, confira a fiacao\n");
  }

  // NOTE: D5 (PB_2) is the boot/download strap on this board. Never configure
  // or probe it here: a wire on that pad keeps the ROM from entering UART
  // download mode. Only the configured RX pin is touched.
  pinMode(IR_RX_PIN, INPUT_PULLUP);
}

void ir_init() {
  pinMode(IR_TX_PIN, OUTPUT);
  analogWriteResolution(8);
  analogWritePeriod(26);          // ~38.4 kHz
  analogWrite(IR_TX_PIN, 0);

  pinMode(IR_RX_PIN, INPUT_PULLUP);

  ir_store_load();
  DEBUG_SER_PRINT("[IR] TX pin D");
  DEBUG_SER_PRINT(IR_TX_PIN);
  DEBUG_SER_PRINT(" (PA_12) 38kHz carrier ready, RX pin D");
  DEBUG_SER_PRINT(IR_RX_PIN);
  DEBUG_SER_PRINT(" (PA_19)\n");
  ir_pin_selftest();
  DEBUG_SER_PRINT("[IR] Ready. Saved signals: ");
  DEBUG_SER_PRINT((int)saved_n);
  DEBUG_SER_PRINT("\n");
}

// =============================================================================
// UI HELPERS
// =============================================================================
static const char *proto_name(uint8_t p) {
  switch (p) {
    case IR_PROTO_NEC:     return "NEC";
    case IR_PROTO_SAMSUNG: return "SAMSUNG";
    case IR_PROTO_SONY:    return "SONY";
    default:               return "RAW";
  }
}

static void code_string(const IRSavedSignal &s, char *out, size_t out_sz) {
  if (s.protocol == IR_PROTO_SONY) {
    snprintf(out, out_sz, "0x%03X", (unsigned)s.command);
  } else if (s.protocol == IR_PROTO_RAW) {
    snprintf(out, out_sz, "%u edges", (unsigned)s.count);
  } else {
    snprintf(out, out_sz, "0x%04X/0x%04X", (unsigned)s.address,
             (unsigned)s.command);
  }
}

static void toast_set(const char *msg) {
  strncpy(ir_toast, msg, sizeof(ir_toast) - 1);
  ir_toast[sizeof(ir_toast) - 1] = '\0';
  ir_toast_ms = millis();
}

// =============================================================================
// RENDER: CAROUSEL MENU
// =============================================================================
void render_ir_menu() {
  const char *badges[] = {"RECORD", "EMIT", "SPAM", "NAVIGATION"};
  const char *titles[] = {"RECORD", "EMIT", "SPAM", "RETURN"};

  for (int i = 0; i < IR_MENU_LEN; i++) {
    int cx = (i * 128) - ir_menu_cur_x;
    if (cx < -124 || cx > 124) continue;

    display.drawRFrame(cx + 8, 4, 112, 50, 3);

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

    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(cx + 14, 33, titles[i]);

    display.setFont(u8g2_font_5x7_tf);
    if (i == 0) {
      display.drawStr(cx + 14, 46, "Learn a code >");
    } else if (i == 1) {
      int n = ir_saved_count();
      char buf[24];
      snprintf(buf, sizeof(buf), "%d saved >", n);
      display.drawStr(cx + 14, 46, buf);
    } else if (i == 2) {
      display.drawStr(cx + 14, 46, "Blast all TVs >");
    } else {
      display.drawStr(cx + 14, 46, "< Back to Menu");
    }

    // Right side icons
    if (i == 0) {
      // Record: IR LED + waves
      display.drawDisc(cx + 91, 32, 3);
      display.drawCircle(cx + 91, 32, 7);
      display.drawCircle(cx + 99, 32, 4);
      display.drawCircle(cx + 99, 32, 8);
    } else if (i == 1) {
      // Emit: remote with signal waves
      display.drawRFrame(cx + 88, 24, 10, 18, 2);
      display.drawBox(cx + 91, 27, 4, 2);
      display.drawBox(cx + 91, 31, 4, 2);
      display.drawBox(cx + 91, 35, 4, 2);
      display.drawCircle(cx + 104, 32, 3);
      display.drawCircle(cx + 104, 32, 7);
      display.drawCircle(cx + 104, 32, 11);
    } else if (i == 2) {
      // Spam: lightning bolt
      display.drawTriangle(cx + 98, 22, cx + 88, 35, cx + 96, 35);
      display.drawTriangle(cx + 96, 35, cx + 106, 29, cx + 96, 43);
      display.drawBox(cx + 90, 42, 14, 2);
    } else {
      display.drawTriangle(cx + 89, 32, cx + 97, 25, cx + 97, 39);
      display.drawBox(cx + 97, 30, 12, 5);
      display.drawBox(cx + 106, 24, 3, 11);
    }
  }

  display.setFont(u8g2_font_5x7_tf);
  if (menu_sel > 0) display.drawStr(2, 61, "<");
  if (menu_sel < IR_MENU_LEN - 1) display.drawStr(122, 61, ">");

  for (int p = 0; p < IR_MENU_LEN; p++) {
    int dot_x = 46 + p * 10;
    if (p == menu_sel) {
      display.drawRBox(dot_x - 1, 58, 8, 3, 1);
    } else {
      display.drawDisc(dot_x + 2, 59, 1);
    }
  }
}

// =============================================================================
// RENDER: RECORD
// =============================================================================
static void render_wait_icon(int x, int y) {
  // Small TV with IR waves
  display.drawRFrame(x, y, 24, 16, 2);
  display.drawFrame(x + 4, y + 4, 10, 7);
  display.drawLine(x + 8, y, x + 5, y - 4);
  display.drawLine(x + 16, y, x + 19, y - 4);
  display.drawCircle(x + 28, y + 8, 3);
  display.drawCircle(x + 28, y + 8, 7);
  display.drawCircle(x + 28, y + 8, 11);
}

void render_ir_record() {
  draw_header("IR RECORD", have_sig ? "CAPTURED" : "LEARNING");

  if (!have_sig) {
    render_wait_icon(30, 18);
    display.setFont(u8g2_font_5x7_tf);
    draw_str_centered(40, "Waiting for IR signal...", 1);

    // Live counter + pin level: 0 edges / LOW means the receiver is not
    // reaching the pin at all.
    uint32_t e = ir_edge_count();
    int lvl = ir_pin_level();
    char buf[26];
    if (e == 0) {
      snprintf(buf, sizeof(buf), "EDGES 0 L:%d check OUT", lvl);
    } else {
      snprintf(buf, sizeof(buf), "EDGES %lu L:%d rx ok", (unsigned long)e, lvl);
    }
    draw_str_centered(49, buf, 1);

    // Blinking capture dot
    if ((millis() / 300) & 1) display.drawDisc(6, 46, 2);
  } else {
    display.setFont(u8g2_font_7x14B_tf);
    draw_str_centered(22, proto_name(cur_sig.protocol), 1);

    char code[28];
    code_string(cur_sig, code, sizeof(code));
    display.setFont(u8g2_font_5x7_tf);
    draw_str_centered(33, code, 1);

    char info[28];
    if (ir_toast[0]) {
      snprintf(info, sizeof(info), "%s", ir_toast);
    } else {
      snprintf(info, sizeof(info), "%u edges captured", (unsigned)cur_sig.count);
    }
    draw_str_centered(41, info, 1);
  }

  // Buttons row
  draw_btn(4, 51, 38, 12, "SAVE",   ir_record_sel == 0);
  draw_btn(45, 51, 38, 12, "REPLAY", ir_record_sel == 1);
  draw_btn(86, 51, 38, 12, "BACK",   ir_record_sel == 2);
}

// =============================================================================
// RENDER: SAVED LIST
// =============================================================================
void render_ir_list() {
  char badge[8];
  snprintf(badge, sizeof(badge), "%d/%d", (int)saved_n, IR_MAX_SAVED);
  draw_header("IR SIGNALS", badge);

  if (saved_n == 0) {
    display.setFont(u8g2_font_5x7_tf);
    draw_str_centered(30, "No saved signals", 1);
    draw_str_centered(41, "Record a code first", 1);
    draw_str_centered(56, "LEFT = back", 1);
    return;
  }

  const int visible = 4;
  if (ir_list_sel < ir_list_scroll) ir_list_scroll = ir_list_sel;
  if (ir_list_sel >= ir_list_scroll + visible) ir_list_scroll = ir_list_sel - visible + 1;

  display.setFont(u8g2_font_5x7_tf);
  for (int row = 0; row < visible; row++) {
    int idx = ir_list_scroll + row;
    if (idx >= saved_n) break;

    int y = 13 + row * 10;
    bool sel = (idx == ir_list_sel);

    if (sel) {
      display.drawRBox(2, y, 124, 9, 1);
      display.setDrawColor(0);
    }

    char line[32];
    char code[20];
    code_string(saved_sigs[idx], code, sizeof(code));
    snprintf(line, sizeof(line), "#%d %-7s %s", idx + 1,
             proto_name(saved_sigs[idx].protocol), code);
    display.drawStr(5, y + 7, line);

    if (sel) display.setDrawColor(1);
  }

  draw_hint("ENTER open   LEFT back");
}

// =============================================================================
// RENDER: DETAIL
// =============================================================================
void render_ir_detail() {
  const IRSavedSignal *s = ir_saved_at(ir_detail_idx);
  if (!s) {
    draw_header("IR SIGNAL", "EMPTY");
    draw_str_centered(33, "Signal not found", 1);
    draw_btn(35, 51, 58, 12, "BACK", true);
    return;
  }

  char badge[10];
  snprintf(badge, sizeof(badge), "#%d", ir_detail_idx + 1);
  draw_header("IR SIGNAL", badge);

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(5, 20, "PROTOCOL");
  display.drawStr(66, 20, proto_name(s->protocol));

  char code[24];
  code_string(*s, code, sizeof(code));
  display.drawStr(5, 30, "CODE");
  display.drawStr(40, 30, code);

  char info[28];
  if (ir_delete_confirm) {
    snprintf(info, sizeof(info), "ENTER again = delete");
  } else if (ir_toast[0]) {
    snprintf(info, sizeof(info), "%s", ir_toast);
  } else {
    snprintf(info, sizeof(info), "EDGES %u", (unsigned)s->count);
  }
  display.drawStr(5, 40, info);

  draw_btn(4, 51, 38, 12, "EMIT",   ir_detail_sel == 0);
  draw_btn(45, 51, 38, 12, "DELETE", ir_detail_sel == 1);
  draw_btn(86, 51, 38, 12, "BACK",   ir_detail_sel == 2);
}

// =============================================================================
// RENDER: SPAM
// =============================================================================
void render_ir_spam() {
  draw_header("IR SPAM", spam_running ? "LIVE" : "STOP");

  const IRSpamEntry &e = IR_SPAM[spam_idx];

  display.setFont(u8g2_font_7x14B_tf);
  draw_str_centered(25, e.brand, 1);

  char line[32];
  const char *action = spam_vol ? "VOL +" : "POWER";
  snprintf(line, sizeof(line), "%s  0x%04X", action,
           (unsigned)(spam_vol ? e.volup : e.power));
  display.setFont(u8g2_font_5x7_tf);
  draw_str_centered(36, line, 1);

  // Animated IR waves on the left
  for (int k = 0; k < 3; k++) {
    int r = 4 + ((anim_frame * 3 + k * 3) % 9);
    display.drawCircle(14, 32, r);
  }

  snprintf(line, sizeof(line), "SENT %lu", (unsigned long)spam_sent);
  display.drawStr(4, 47, line);

  // Brand progress pills
  for (int i = 0; i < IR_SPAM_COUNT; i++) {
    int dx = 60 + i * 8;
    if (i == spam_idx) display.drawRBox(dx - 1, 44, 6, 3, 1);
    else              display.drawDisc(dx + 1, 45, 1);
  }

  draw_hint("ENTER = STOP");
}

// =============================================================================
// INPUT
// =============================================================================
void ir_handle_input(bool up, bool dn, bool left, bool right, bool enter) {
  switch (current_screen) {
    case SCR_IR_MENU:
      if (enter) {
        if (menu_sel == 0) {
          DEBUG_SER_PRINT("[IR] -> RECORD\n");
          ir_clear_current();
          ir_record_sel = 0;
          ir_start_capture();
          current_screen = SCR_IR_RECORD;
        } else if (menu_sel == 1) {
          DEBUG_SER_PRINT("[IR] -> LIST (");
          DEBUG_SER_PRINT((int)saved_n);
          DEBUG_SER_PRINT(" saved)\n");
          ir_list_sel = 0;
          ir_list_scroll = 0;
          current_screen = SCR_IR_LIST;
        } else if (menu_sel == 2) {
          DEBUG_SER_PRINT("[IR] -> SPAM\n");
          ir_start_spam();
          current_screen = SCR_IR_SPAM;
        } else {
          DEBUG_SER_PRINT("[IR] -> back to main menu\n");
          current_screen = SCR_MAIN_MENU;
          menu_sel = 3;
        }
      } else if (left || up) {
        menu_sel = (menu_sel - 1 + IR_MENU_LEN) % IR_MENU_LEN;
        trigger_arrow_left();
      } else if (right || dn) {
        menu_sel = (menu_sel + 1) % IR_MENU_LEN;
        trigger_arrow_right();
      }
      break;

    case SCR_IR_RECORD:
      if (left)  ir_record_sel = (ir_record_sel + 2) % 3;
      if (right) ir_record_sel = (ir_record_sel + 1) % 3;
      if (enter) {
        if (ir_record_sel == 0) {
          if (!have_sig) {
            toast_set("No signal yet");
          } else if (ir_save_current()) {
            toast_set("Saved!");
          } else {
            toast_set(saved_n >= IR_MAX_SAVED ? "Memory full" : "No signal yet");
          }
        } else if (ir_record_sel == 1) {
          if (have_sig) {
            ir_replay_current();
            toast_set("Replayed");
          } else {
            toast_set("No signal yet");
          }
        } else {
          ir_stop_capture();
          current_screen = SCR_IR_MENU;
          menu_sel = 0;
        }
      }
      break;

    case SCR_IR_LIST:
      ir_store_load();
      if (saved_n > 0) {
        if (up && ir_list_sel > 0) ir_list_sel--;
        if (dn && ir_list_sel < saved_n - 1) ir_list_sel++;
        if (enter || right) {
          ir_detail_idx    = ir_list_sel;
          ir_detail_sel    = 0;
          ir_delete_confirm = false;
          ir_toast[0]      = '\0';
          current_screen   = SCR_IR_DETAIL;
        }
      }
      if (left) {
        current_screen = SCR_IR_MENU;
        menu_sel = 1;
      }
      break;

    case SCR_IR_DETAIL:
      if (left)  { ir_detail_sel = (ir_detail_sel + 2) % 3; ir_delete_confirm = false; }
      if (right) { ir_detail_sel = (ir_detail_sel + 1) % 3; ir_delete_confirm = false; }
      if (enter) {
        if (ir_detail_sel == 0) {
          ir_emit_saved(ir_detail_idx);
          toast_set("Sent!");
          ir_delete_confirm = false;
        } else if (ir_detail_sel == 1) {
          if (!ir_delete_confirm) {
            ir_delete_confirm = true;
          } else {
            ir_delete_saved(ir_detail_idx);
            ir_delete_confirm = false;
            if (ir_detail_idx >= saved_n && ir_detail_idx > 0) ir_detail_idx--;
            ir_list_sel = ir_detail_idx;
            current_screen = SCR_IR_LIST;
          }
        } else {
          ir_delete_confirm = false;
          current_screen = SCR_IR_LIST;
        }
      }
      break;

    case SCR_IR_SPAM:
      if (enter || left || dn) {
        ir_stop_spam();
        current_screen = SCR_IR_MENU;
        menu_sel = 2;
      }
      break;

    default:
      break;
  }
}
