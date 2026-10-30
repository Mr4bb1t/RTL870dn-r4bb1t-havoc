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
static void ir_pin_selftest();

int ir_pin_level() { return digitalRead(IR_RX_PIN) ? 1 : 0; }

static bool capture_once() {
  const uint32_t WAIT_US = 40000;   // Janela de escuta contínua e rápida mantendo UI fluida
  const uint32_t HARD_US = 400000;  // Teto máximo para 1 frame completo

  // Garante linha em idle (HIGH). Se estiver presa em LOW, aguarda liberar
  uint32_t t_wait = micros();
  while (capture_on && ir_pin_level() == 0) {
    if ((uint32_t)(micros() - t_wait) > WAIT_US) return false;
  }

  // Fase 1: detecta a borda de descida (HIGH -> LOW) que inicia a portadora (mark)
  uint32_t t_start = 0;
  while (capture_on) {
    if (ir_pin_level() == 0) {
      t_start = micros(); // Medição imediata no instante exato da borda!
      break;
    }
    if ((uint32_t)(micros() - t_wait) > WAIT_US) return false;
  }
  if (!capture_on || t_start == 0) return false;

  // Fase 2: laço fechado medindo cada transição (SEM prints seriais no caminho crítico!)
  int n = 0;
  int last = 0; // A linha acabou de cair para LOW (mark do preâmbulo)
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

  // Filtro de ruído: frames de controle remoto válidos têm pelo menos 20 bordas
  if (n < 20) {
    return false;
  }

  DEBUG_SER_PRINT("[IR] Quadro capturado: bordas=");
  DEBUG_SER_PRINT(n);
  DEBUG_SER_PRINT(" header_mark=");
  DEBUG_SER_PRINT((unsigned)raw_scratch[0]);
  DEBUG_SER_PRINT(" us, header_space=");
  DEBUG_SER_PRINT((unsigned)raw_scratch[1]);
  DEBUG_SER_PRINT(" us\n");

  memcpy(cur_sig.raw, raw_scratch, n * sizeof(uint16_t));
  for (int i = n; i < IR_MAX_RAW; i++) cur_sig.raw[i] = 0;
  cur_sig.count = (uint8_t)n;
  decode_current();
  have_sig = true;
  ir_toast[0] = '\0'; // Limpa toast anterior para exibir o novo sinal capturado
  ir_debug_dump();
  return true;
}

void ir_start_capture() {
  // Arma o pino PB_2 (RX1) exclusivamente durante a gravacao.
  pinMode(IR_RX_PIN, INPUT_PULLUP);
  capture_on = true;
  cap_edges = 0;
  ir_last_status_ms = millis();

  delay(2);
  ir_pin_selftest();   // diagnostica ligacao com o pino armado
  DEBUG_SER_PRINT("[IR] capture armed: pino PB_2 (RX1/D5), nivel idle=");
  DEBUG_SER_PRINT(ir_pin_level() ? "HIGH" : "LOW");
  DEBUG_SER_PRINT("\n");
}

void ir_stop_capture() {
  if (capture_on) {
    capture_on = false;
    // Libera PB_2 de volta para alta impedancia (INPUT_PULLNONE) e remove
    // o modo GPIO para que o pino fique 100% livre para o bootloader/RX1
    // e botao BURN sem conflito com o MCU:
    pinMode(IR_RX_PIN, INPUT_PULLNONE);
    pinRemoveMode(IR_RX_PIN);
    DEBUG_SER_PRINT("[IR] capture stopped, PB_2 liberado (high-Z/RX1 livre). edges=");
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

  bool nec_hdr = close_to(r[0], 9000, 2500) && close_to(r[1], 4500, 1800);
  bool sam_hdr = close_to(r[0], 4500, 1800) && close_to(r[1], 4500, 1800);
  if (!nec_hdr && !sam_hdr) return false;

  uint32_t data = 0;
  int i = 2;
  for (int b = 0; b < 32; b++) {
    if (i + 1 >= n) return false;
    uint16_t mark  = r[i++];
    uint16_t space = r[i++];
    if (!close_to(mark, 560, 350)) return false;
    if (space >= 950 && space <= 2600) {
      data |= (1UL << b);
    } else if (space >= 200 && space < 950) {
      // Bit 0: espaço curto (560 us nominal)
    } else {
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
  // Transmite exatamente 1 quadro do protocolo (comando único, sem duplicar)
  ir_mark(samsung ? 4500 : 9000);
  ir_space(4500);

  for (int b = 0; b < 32; b++) {
    ir_mark(560);
    ir_space((data & (1UL << b)) ? 1690 : 560);
  }

  ir_mark(560);
  ir_space(samsung ? 45000 : 40000);
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
  // Reproduz exatamente 1 vez o sinal RAW capturado
  for (int i = 0; i < s.count; i++) {
    if (i & 1) ir_space(s.raw[i]);
    else       ir_mark(s.raw[i]);
  }
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
    bool was_cap = capture_on;
    capture_on = false; // Pausa escuta durante emissão própria
    ir_send_signal(cur_sig);
    delay(40);
    capture_on = was_cap;
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
  uint16_t    mute;
  uint16_t    volup;
  uint16_t    voldn;
};

static const IRSpamEntry IR_SPAM[] = {
  {"Samsung",    IR_PROTO_SAMSUNG, 0xE0E0, 0x40BF, 0xF00F, 0xE01F, 0xD02F},
  {"LG TV",      IR_PROTO_NEC,     0x20DF, 0x10EF, 0x906F, 0x40BF, 0xC03F},
  {"Sony",       IR_PROTO_SONY,    0x0000, 0x0A90, 0x0290, 0x0490, 0x0C90},
  {"TCL",        IR_PROTO_NEC,     0x00BF, 0x48B7, 0xE21D, 0xD22D, 0x52AD},
  {"AOC",        IR_PROTO_NEC,     0x01FE, 0x48B7, 0xB847, 0x58A7, 0x7887},
  {"Philips",    IR_PROTO_NEC,     0x01FE, 0x48B7, 0x906F, 0x58A7, 0x7887},
  {"Toshiba",    IR_PROTO_NEC,     0x02FD, 0x48B7, 0xB847, 0x58A7, 0x7887},
  {"Philco",     IR_PROTO_NEC,     0x00FF, 0x02FD, 0x08F7, 0x40BF, 0xC03F},
  {"Panasonic",  IR_PROTO_NEC,     0x02FD, 0x48B7, 0x28D7, 0x58A7, 0x7887},
  {"Multilaser", IR_PROTO_NEC,     0x00FF, 0x02FD, 0x08F7, 0x58A7, 0x7887},
};
#define IR_SPAM_COUNT ((int)(sizeof(IR_SPAM) / sizeof(IR_SPAM[0])))

static uint8_t  ir_spam_mode = SPAM_MODE_TV_B_GONE;

void ir_set_spam_mode(uint8_t m) {
  ir_spam_mode = (m < SPAM_MODE_COUNT) ? m : 0;
}

uint8_t ir_get_spam_mode() {
  return ir_spam_mode;
}

const char *ir_get_spam_mode_name(uint8_t m) {
  switch (m) {
    case SPAM_MODE_TV_B_GONE: return "TV-B-GONE";
    case SPAM_MODE_ALL_P_V:   return "ALL (PWR+VOL)";
    case SPAM_MODE_MUTE:      return "MUTE SPAM";
    case SPAM_MODE_VOL_UP:    return "VOL+ SPAM";
    case SPAM_MODE_VOL_DOWN:  return "VOL- SPAM";
    default:                  return "SPAM";
  }
}

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

static uint8_t rev8(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

// inverte os bits de cada byte, mantendo a ordem dos bytes no transmitido
static uint16_t rev_bytes16(uint16_t v) {
  return (uint16_t)(rev8(v >> 8) | ((uint16_t)rev8(v & 0xFF) << 8));
}

void ir_tick() {
  // Se saiu da tela de gravacao, encerra imediatamente e libera o PB_2
  if (capture_on && current_screen != SCR_IR_RECORD) {
    ir_stop_capture();
  }

  // Captura contínua e em tempo real: qualquer novo clique em botão do controle
  // atualiza imediatamente a tela e as ondas, sem travar nem exigir limpeza manual.
  if (capture_on) {
    capture_once();
  }

  // Heartbeat serial periódico
  if (capture_on &&
      (uint32_t)(millis() - ir_last_status_ms) > 1500) {
    ir_last_status_ms = millis();
    DEBUG_SER_PRINT("[IR] waiting... edges=");
    DEBUG_SER_PRINT((unsigned)cap_edges);
    DEBUG_SER_PRINT(" level=");
    DEBUG_SER_PRINT(ir_pin_level() ? "HIGH" : "LOW");
    DEBUG_SER_PRINT("\n");
  }

  // Spam scheduler dinâmico (com TV-B-Gone e modos de ação)
  uint32_t spam_gap = (ir_spam_mode == SPAM_MODE_TV_B_GONE) ? 420 : 550;
  if (spam_running && (millis() - spam_last_ms > spam_gap)) {
    spam_last_ms = millis();

    const IRSpamEntry &e = IR_SPAM[spam_idx];
    IRSavedSignal tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.protocol = e.protocol;

    uint16_t cmd = e.power;
    const char *action_label = "POWER";

    switch (ir_spam_mode) {
      case SPAM_MODE_TV_B_GONE:
        cmd = e.power;
        action_label = "POWER";
        break;
      case SPAM_MODE_MUTE:
        cmd = e.mute;
        action_label = "MUTE";
        break;
      case SPAM_MODE_VOL_UP:
        cmd = e.volup;
        action_label = "VOL+";
        break;
      case SPAM_MODE_VOL_DOWN:
        cmd = e.voldn;
        action_label = "VOL-";
        break;
      case SPAM_MODE_ALL_P_V:
      default:
        cmd = spam_vol ? e.volup : e.power;
        action_label = spam_vol ? "VOL+" : "POWER";
        break;
    }

    if (e.protocol == IR_PROTO_SONY) {
      tmp.address = 0;
      tmp.command = cmd;
    } else {
      tmp.address = rev_bytes16(e.address);
      tmp.command = rev_bytes16(cmd);
    }

    ir_send_signal(tmp);
    spam_sent++;

    DEBUG_SER_PRINT("[IR SPAM] [");
    DEBUG_SER_PRINT(ir_get_spam_mode_name(ir_spam_mode));
    DEBUG_SER_PRINT("] ");
    DEBUG_SER_PRINT(e.brand);
    DEBUG_SER_PRINT(" -> ");
    DEBUG_SER_PRINT(action_label);
    DEBUG_SER_PRINT(" (cmd 0x");
    DEBUG_SER_PRINT((unsigned)cmd);
    DEBUG_SER_PRINT(")\n");

    if (ir_spam_mode == SPAM_MODE_ALL_P_V) {
      if (spam_vol) {
        spam_vol = false;
      } else {
        spam_vol = true;
        spam_idx = (spam_idx + 1) % IR_SPAM_COUNT;
      }
    } else {
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

  pinMode(IR_RX_PIN, INPUT_PULLNONE); // Usa INPUT_PULLNONE para nao forcar pull-down no PB_2
  delay(5);
  int dn = digitalRead(IR_RX_PIN) ? 1 : 0;

  pinMode(IR_RX_PIN, INPUT_PULLUP);
  delay(5);

  DEBUG_SER_PRINT("[IR] pin test PB_2: pullup=");
  DEBUG_SER_PRINT(up);
  DEBUG_SER_PRINT(" floating=");
  DEBUG_SER_PRINT(dn);
  DEBUG_SER_PRINT(" -> ");

  if (!up && !dn) {
    DEBUG_SER_PRINT("LINE HELD LOW: pino em curto com GND ou OUT na perna GND\n");
  } else if (up && dn) {
    DEBUG_SER_PRINT("LINE HELD HIGH: TSOP OUT em repouso (HIGH) ou ligado a VCC\n");
  } else if (up && !dn) {
    DEBUG_SER_PRINT("Linha em alta impedancia / repouso normal\n");
  } else {
    DEBUG_SER_PRINT("estado indeterminado, confira a fiacao\n");
  }

  pinMode(IR_RX_PIN, INPUT_PULLUP);
}

void ir_init() {
  pinMode(IR_TX_PIN, OUTPUT);
  analogWriteResolution(8);
  analogWritePeriod(26);          // ~38.4 kHz
  analogWrite(IR_TX_PIN, 0);

  // IR_RX_PIN (PB_2 / RX1 / D5) fica totalmente livre em alta impedancia (INPUT_PULLNONE).
  // NUNCA usar pinMode(INPUT) aqui porque ativaria pull-down interno no AmebaD.
  pinMode(IR_RX_PIN, INPUT_PULLNONE);
  pinRemoveMode(IR_RX_PIN);

  ir_store_load();
  DEBUG_SER_PRINT("[IR] TX pin D");
  DEBUG_SER_PRINT(IR_TX_PIN);
  DEBUG_SER_PRINT(" (PA_12) 38kHz pronto, RX pin PB_2 (RX1/D5) late-armed em Record\n");
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

    // Right side vector graphics for each card (Solid, bold & high-contrast)
    if (i == 0) {
      // RECORD: Solid Sensor Eye + Inverted Iris + Incoming Carrier Waves + REC Badge
      int rx = cx + 101, ry = 33;
      // Solid Sensor Body
      display.drawRBox(rx - 5, ry - 4, 12, 15, 2);
      // Optical Top Dome
      display.drawDisc(rx + 1, ry - 5, 4);
      // Inverted sensor iris aperture (high contrast negative space)
      display.setColorIndex(0);
      display.drawDisc(rx + 1, ry + 2, 3);
      display.setColorIndex(1);
      display.drawDisc(rx + 1, ry + 2, 1);
      // Dual thick (2px) incoming carrier wave arcs on the left
      display.drawCircle(rx + 1, ry - 3, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      display.drawCircle(rx + 1, ry - 3, 8, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      display.drawCircle(rx + 1, ry - 3, 11, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      display.drawCircle(rx + 1, ry - 3, 12, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      // Solid REC dot indicator
      display.drawDisc(rx - 10, ry - 8, 3);
    } else if (i == 1) {
      // EMIT: Solid Universal Remote Control + Inverted Keypad + Radiant Carrier Waves
      int mx = cx + 96, my = 35;
      // Solid Remote Body & Emitter LED Tip
      display.drawRBox(mx - 6, my - 11, 13, 23, 2);
      display.drawRBox(mx - 2, my - 14, 5, 4, 1);
      // Inverted negative space buttons (Power, D-Pad, Keypad)
      display.setColorIndex(0);
      display.drawDisc(mx, my - 6, 2);         // Power button
      display.drawBox(mx - 3, my, 7, 2);         // Horizontal D-Pad
      display.drawBox(mx - 1, my - 2, 3, 6);     // Vertical D-Pad
      display.drawBox(mx - 3, my + 6, 2, 2);     // Key 1
      display.drawBox(mx + 2, my + 6, 2, 2);     // Key 2
      display.setColorIndex(1);
      // Triple thick (2px) radiant outgoing carrier waves
      display.drawCircle(mx + 3, my - 8, 6, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(mx + 3, my - 8, 7, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(mx + 3, my - 8, 11, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(mx + 3, my - 8, 12, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(mx + 3, my - 8, 16, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(mx + 3, my - 8, 17, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
    } else if (i == 2) {
      // SPAM: Solid High-Voltage Lightning Bolt + Dual Thick Shockwaves + Diamond Sparks
      int sx = cx + 101, sy = 33;
      // Solid High-Voltage Lightning Bolt
      display.drawTriangle(sx + 3, sy - 14, sx - 6, sy + 1, sx + 2, sy + 1);
      display.drawTriangle(sx - 2, sy - 1, sx + 5, sy - 1, sx - 4, sy + 14);
      display.drawBox(sx - 3, sy - 1, 6, 3);
      // Dual thick (2px) shockwave arcs
      display.drawCircle(sx - 2, sy, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      display.drawCircle(sx - 2, sy, 8, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      display.drawCircle(sx + 2, sy, 7, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      display.drawCircle(sx + 2, sy, 8, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
      // Solid energy sparks (diamonds)
      display.drawTriangle(sx - 10, sy - 8, sx - 8, sy - 11, sx - 6, sy - 8);
      display.drawTriangle(sx - 10, sy - 8, sx - 8, sy - 5, sx - 6, sy - 8);
      display.drawTriangle(sx + 6, sy + 8, sx + 8, sy + 5, sx + 10, sy + 8);
      display.drawTriangle(sx + 6, sy + 8, sx + 8, sy + 11, sx + 10, sy + 8);
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
// WAVEFORM DISPLAY (Visualizador Osciloscópio no OLED)
// =============================================================================
void draw_ir_waveform(int x, int y, int w, int h, const uint16_t *raw, int count) {
  display.drawFrame(x, y, w, h);
  if (count <= 2 || raw == nullptr) {
    // Linha de centro do osciloscópio (idle / escuta)
    int mid_y = y + h / 2;
    display.drawHLine(x + 2, mid_y, w - 4);
    // Blip de varredura ativo animado
    int blip_x = x + 3 + (int)((millis() / 8) % (w - 8));
    display.drawVLine(blip_x, mid_y - 2, 5);
    display.drawVLine(blip_x + 1, mid_y - 1, 3);
    return;
  }

  int max_edges = count > 36 ? 36 : count;
  uint32_t total_us = 0;
  for (int i = 0; i < max_edges; i++) {
    total_us += raw[i];
  }
  if (total_us == 0) total_us = 1;

  int y_high = y + 2;
  int y_low  = y + h - 3;
  int cur_x  = x + 2;
  int max_x  = x + w - 2;

  bool level_high = true;
  for (int i = 0; i < max_edges && cur_x < max_x; i++) {
    int pulse_w = ((uint32_t)raw[i] * (w - 4)) / total_us;
    if (pulse_w < 1) pulse_w = 1;
    if (cur_x + pulse_w > max_x) pulse_w = max_x - cur_x;

    int cur_y = level_high ? y_high : y_low;
    display.drawHLine(cur_x, cur_y, pulse_w);

    int next_x = cur_x + pulse_w;
    if (i + 1 < max_edges && next_x < max_x) {
      display.drawVLine(next_x, y_high, h - 5);
    }

    cur_x = next_x;
    level_high = !level_high;
  }
}

void render_ir_record() {
  draw_header("IR RECORD", have_sig ? "CAPTURED" : "READY");

  // Visualizador osciloscópico de ondas ocupando a área central
  draw_ir_waveform(4, 15, 120, 22, cur_sig.raw, have_sig ? cur_sig.count : 0);

  display.setFont(u8g2_font_5x7_tf);
  if (ir_toast[0]) {
    draw_str_centered(43, ir_toast, 1);
  } else if (have_sig) {
    char code[32];
    if (cur_sig.protocol == IR_PROTO_NEC || cur_sig.protocol == IR_PROTO_SAMSUNG) {
      snprintf(code, sizeof(code), "%s A:0x%04X C:0x%04X", proto_name(cur_sig.protocol), cur_sig.address, cur_sig.command);
    } else if (cur_sig.protocol == IR_PROTO_SONY) {
      snprintf(code, sizeof(code), "SONY CMD:0x%04X", cur_sig.command);
    } else {
      snprintf(code, sizeof(code), "RAW %u pulses", (unsigned)cur_sig.count);
    }
    draw_str_centered(43, code, 1);
  }

  // Botões de ação direta
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

  // Waveform do sinal salvo
  draw_ir_waveform(4, 14, 120, 15, s->raw, s->count);

  display.setFont(u8g2_font_5x7_tf);
  char code[32];
  if (s->protocol == IR_PROTO_NEC || s->protocol == IR_PROTO_SAMSUNG) {
    snprintf(code, sizeof(code), "%s A:0x%04X C:0x%04X", proto_name(s->protocol), s->address, s->command);
  } else if (s->protocol == IR_PROTO_SONY) {
    snprintf(code, sizeof(code), "SONY CMD:0x%04X", s->command);
  } else {
    snprintf(code, sizeof(code), "RAW %u edges", (unsigned)s->count);
  }
  draw_str_centered(37, code, 1);

  if (ir_delete_confirm) {
    draw_str_centered(47, "ENTER AGAIN = DELETE", 1);
  } else if (ir_toast[0]) {
    draw_str_centered(47, ir_toast, 1);
  } else {
    draw_str_centered(47, "Ready to Emit", 1);
  }

  draw_btn(4, 51, 38, 12, "EMIT",   ir_detail_sel == 0);
  draw_btn(45, 51, 38, 12, "DELETE", ir_detail_sel == 1);
  draw_btn(86, 51, 38, 12, "BACK",   ir_detail_sel == 2);
}

// =============================================================================
// RENDER: SPAM
// =============================================================================
void render_ir_spam() {
  char badge[16];
  snprintf(badge, sizeof(badge), "%s", ir_get_spam_mode_name(ir_spam_mode));
  draw_header("IR BLAST", badge);

  const IRSpamEntry &e = IR_SPAM[spam_idx];

  display.setFont(u8g2_font_7x14B_tf);
  draw_str_centered(24, e.brand, 1);

  display.setFont(u8g2_font_5x7_tf);

  // Barra de progresso para TV-B-Gone
  if (ir_spam_mode == SPAM_MODE_TV_B_GONE) {
    int pct = ((spam_idx + 1) * 100) / IR_SPAM_COUNT;
    display.drawFrame(14, 32, 100, 7);
    int bar_w = (pct * 96) / 100;
    if (bar_w > 0) display.drawBox(16, 34, bar_w, 3);

    char pbuf[24];
    snprintf(pbuf, sizeof(pbuf), "POWER #%d/%d (%d%%)", spam_idx + 1, IR_SPAM_COUNT, pct);
    draw_str_centered(47, pbuf, 1);
  } else {
    char line[32];
    const char *act = "CMD";
    uint16_t cval = e.power;
    if (ir_spam_mode == SPAM_MODE_MUTE) { act = "MUTE"; cval = e.mute; }
    else if (ir_spam_mode == SPAM_MODE_VOL_UP) { act = "VOL+"; cval = e.volup; }
    else if (ir_spam_mode == SPAM_MODE_VOL_DOWN) { act = "VOL-"; cval = e.voldn; }
    else { act = spam_vol ? "VOL+" : "POWER"; cval = spam_vol ? e.volup : e.power; }

    snprintf(line, sizeof(line), "%s  0x%04X  #%lu", act, (unsigned)cval, (unsigned long)spam_sent);
    draw_str_centered(36, line, 1);

    // Indicador de marcas em bolinhas
    for (int i = 0; i < IR_SPAM_COUNT; i++) {
      int dx = 20 + i * 9;
      if (i == spam_idx) display.drawRBox(dx - 1, 43, 7, 3, 1);
      else              display.drawDisc(dx + 2, 44, 1);
    }
  }

  draw_hint("UP/DN:Mode  ENTER:Stop");
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
          menu_sel = 2;
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
      if (up || dn) {
        if (have_sig) {
          ir_clear_current();
          toast_set("Listening...");
        }
      }
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
            flush_input(); // Trava repetição para disparar somente 1 vez por clique
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
          flush_input(); // Trava repetição para disparar somente 1 vez por clique
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
      if (up) {
        ir_spam_mode = (ir_spam_mode + SPAM_MODE_COUNT - 1) % SPAM_MODE_COUNT;
        toast_set(ir_get_spam_mode_name(ir_spam_mode));
      } else if (dn) {
        ir_spam_mode = (ir_spam_mode + 1) % SPAM_MODE_COUNT;
        toast_set(ir_get_spam_mode_name(ir_spam_mode));
      } else if (enter || left) {
        ir_stop_spam();
        current_screen = SCR_IR_MENU;
        menu_sel = 2;
      }
      break;

    default:
      break;
  }
}

// =============================================================================
// EXPORTAÇÃO FORMATO FLIPPER ZERO (.ir)
// =============================================================================
void ir_export_flipper() {
  DEBUG_SER_PRINT("\n=======================================================\n");
  DEBUG_SER_PRINT("Filetype: IR signals file\n");
  DEBUG_SER_PRINT("Version: 1\n");
  DEBUG_SER_PRINT("# Exported from RTL8720DN Havoc IR Suite\n");
  DEBUG_SER_PRINT("# -----------------------------------------------------\n");

  ir_store_load();
  if (saved_n == 0 && !have_sig) {
    DEBUG_SER_PRINT("# Nenhum sinal gravado para exportar.\n");
    DEBUG_SER_PRINT("=======================================================\n");
    return;
  }

  // Se houver um sinal capturado recente na memória
  if (have_sig) {
    DEBUG_SER_PRINT("# Sinal Atual em Memoria:\n");
    DEBUG_SER_PRINT("name: CURRENT_CAPTURE\n");
    if (cur_sig.protocol == IR_PROTO_NEC) {
      DEBUG_SER_PRINT("type: parsed\nprotocol: NEC\n");
      char buf[64];
      snprintf(buf, sizeof(buf), "address: %02X %02X 00 00\ncommand: %02X %02X 00 00\n",
               (unsigned)(cur_sig.address & 0xFF), (unsigned)((cur_sig.address >> 8) & 0xFF),
               (unsigned)(cur_sig.command & 0xFF), (unsigned)((cur_sig.command >> 8) & 0xFF));
      DEBUG_SER_PRINT(buf);
    } else if (cur_sig.protocol == IR_PROTO_SAMSUNG) {
      DEBUG_SER_PRINT("type: parsed\nprotocol: Samsung32\n");
      char buf[64];
      snprintf(buf, sizeof(buf), "address: %02X %02X 00 00\ncommand: %02X %02X 00 00\n",
               (unsigned)(cur_sig.address & 0xFF), (unsigned)((cur_sig.address >> 8) & 0xFF),
               (unsigned)(cur_sig.command & 0xFF), (unsigned)((cur_sig.command >> 8) & 0xFF));
      DEBUG_SER_PRINT(buf);
    } else {
      DEBUG_SER_PRINT("type: raw\nfrequency: 38000\nduty_cycle: 0.500000\ndata:");
      for (int i = 0; i < cur_sig.count; i++) {
        DEBUG_SER_PRINT(" ");
        DEBUG_SER_PRINT((unsigned)cur_sig.raw[i]);
      }
      DEBUG_SER_PRINT("\n");
    }
    DEBUG_SER_PRINT("# -----------------------------------------------------\n");
  }

  // Exporta sinais gravados na Flash
  for (int idx = 0; idx < saved_n; idx++) {
    const IRSavedSignal &s = saved_sigs[idx];
    char name_buf[32];
    snprintf(name_buf, sizeof(name_buf), "name: SAVED_SLOT_%d\n", idx + 1);
    DEBUG_SER_PRINT(name_buf);

    if (s.protocol == IR_PROTO_NEC) {
      DEBUG_SER_PRINT("type: parsed\nprotocol: NEC\n");
      char buf[64];
      snprintf(buf, sizeof(buf), "address: %02X %02X 00 00\ncommand: %02X %02X 00 00\n",
               (unsigned)(s.address & 0xFF), (unsigned)((s.address >> 8) & 0xFF),
               (unsigned)(s.command & 0xFF), (unsigned)((s.command >> 8) & 0xFF));
      DEBUG_SER_PRINT(buf);
    } else if (s.protocol == IR_PROTO_SAMSUNG) {
      DEBUG_SER_PRINT("type: parsed\nprotocol: Samsung32\n");
      char buf[64];
      snprintf(buf, sizeof(buf), "address: %02X %02X 00 00\ncommand: %02X %02X 00 00\n",
               (unsigned)(s.address & 0xFF), (unsigned)((s.address >> 8) & 0xFF),
               (unsigned)(s.command & 0xFF), (unsigned)((s.command >> 8) & 0xFF));
      DEBUG_SER_PRINT(buf);
    } else {
      DEBUG_SER_PRINT("type: raw\nfrequency: 38000\nduty_cycle: 0.500000\ndata:");
      for (int i = 0; i < s.count; i++) {
        DEBUG_SER_PRINT(" ");
        DEBUG_SER_PRINT((unsigned)s.raw[i]);
      }
      DEBUG_SER_PRINT("\n");
    }
    DEBUG_SER_PRINT("# -----------------------------------------------------\n");
  }
  DEBUG_SER_PRINT("=======================================================\n\n");
}
