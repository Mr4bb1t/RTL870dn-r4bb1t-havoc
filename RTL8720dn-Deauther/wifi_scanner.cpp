// STL MUST come first
#undef max
#include <vector>

#include "wifi_scanner.h"

std::vector<WiFiScanResult> scan_results;
bool net_marked[MAX_SCAN_RESULTS] = {false};
int  net_sel    = 0;
int  net_scroll = 0;

rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  if (scan_result->scan_complete == 0) {
    rtw_scan_result_t *record = &scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0;
    WiFiScanResult result;
    result.ssid    = String((const char *)record->SSID.val);
    result.channel = record->channel;
    result.rssi    = record->signal_strength;
    result.is5GHz  = (record->channel >= 36);
    memcpy(&result.bssid, &record->BSSID, 6);
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             result.bssid[0], result.bssid[1], result.bssid[2],
             result.bssid[3], result.bssid[4], result.bssid[5]);
    result.bssid_str = String(bssid_str);
    if ((int)scan_results.size() < MAX_SCAN_RESULTS)
      scan_results.push_back(result);
  }
  return RTW_SUCCESS;
}

void startScan() {
  scan_results.clear();
  memset(net_marked, 0, sizeof(net_marked));
  net_sel    = 0;
  net_scroll = 0;
  wifi_scan_networks(scanResultHandler, NULL);
}