#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\wifi_attacks.h"
#ifndef WIFI_ATTACKS_H
#define WIFI_ATTACKS_H

// <vector> is safe on Ameba (does not pull in <string>/<tuple>)
// <map> is NOT safe - it pulls <tuple>-><string> which breaks std::vsnprintf
#undef max
#include <vector>

#include "config.h"
#include "wifi_scanner.h"
#include "src/packet-injection/packet-injection.h"
#include "wifi_util.h"

#define NUM_BEACON_SSIDS    25
#define MAX_CHANNELS        16    // max unique channels we track
#define BEACON_MAX_COUNT   999    // configurable beacon quantity upper bound
#define BEACON_MIN_COUNT     1

extern const char* BEACON_SSIDS[];

// Replaces std::map<int, std::vector<int>> to avoid <map> include
struct ChannelGroup {
  int channel;
  int targets[MAX_SCAN_RESULTS];
  int count;
};

// ── Deauth state ──────────────────────────────────────────
extern ChannelGroup deauth_groups[MAX_CHANNELS];
extern int          deauth_group_count;
extern int          current_group_idx;
extern uint32_t     sent_frames;
extern bool         isDeauthing;

// ── Beacon state ──────────────────────────────────────────
extern uint8_t      beacon_mac[6];
extern uint8_t      broadcast_mac[6];
extern int          beacon_ssid_idx;       // current SSID index in cycle
extern uint32_t     beacon_frames;         // total frames sent
extern bool         isBeaconing;

extern BeaconMode   beacon_mode;           // RANDOM / CLONE / CUSTOM
extern int          beacon_count;          // how many distinct SSIDs to cycle (1-999)
extern char         beacon_custom_ssid[33];// CUSTOM mode base name (max 32 chars+NUL)

// Fill `out` with the SSID for position `idx`.
// Appends enough invisible padding (repeating 0x20 0xC2 0xA0 Non-Breaking
// Space sequences) so every index produces a UNIQUE string, preventing
// phones from merging them into one AP entry.
// `out` must be at least 36 bytes.
void get_beacon_ssid(int idx, char* out, size_t outLen);

// ── Telemetry & Crash Diagnostic Counters ──────────────────
extern uint32_t     deauth_fail_count;
extern uint32_t     beacon_fail_count;

void log_system_diagnostics(const char* context);

// ── API ───────────────────────────────────────────────────
void start_deauth();
void stop_deauth();
void run_deauth_frame();

void start_beacon();
void stop_beacon();
void run_beacon_frame();

#endif // WIFI_ATTACKS_H