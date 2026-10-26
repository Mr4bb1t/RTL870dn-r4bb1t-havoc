#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\wifi_scanner.cpp"
// STL MUST come first
#undef max
#include <vector>

#include "wifi_scanner.h"

std::vector<WiFiScanResult> scan_results;
bool net_marked[MAX_SCAN_RESULTS] = {false};
int  net_sel          = 0;
int  net_scroll       = 0;
bool scan_done        = false;
bool scan_in_progress = false;

rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  if (scan_result == NULL) return RTW_ERROR;
  if (scan_result->scan_complete == 0) {
    rtw_scan_result_t *record = &scan_result->ap_details;
    char ssid_buf[33];
    uint8_t len = record->SSID.len;
    if (len > 32) len = 32;
    memcpy(ssid_buf, record->SSID.val, len);
    ssid_buf[len] = '\0';

    // Deduplication by BSSID (6-byte hardware MAC address)
    // Prevents duplicate entries when multiple beacons/probe responses arrive from the same radio
    for (size_t i = 0; i < scan_results.size(); i++) {
      if (memcmp(scan_results[i].bssid, &record->BSSID, 6) == 0) {
        // Keep the best signal level observed
        if (record->signal_strength > scan_results[i].rssi) {
          scan_results[i].rssi = record->signal_strength;
        }
        // Update SSID if it was hidden/blank and is now visible
        if ((scan_results[i].ssid.length() == 0 || scan_results[i].ssid == "*HIDDEN*") && len > 0) {
          scan_results[i].ssid = String(ssid_buf);
        }
        return RTW_SUCCESS;
      }
    }

    if ((int)scan_results.size() < MAX_SCAN_RESULTS) {
      WiFiScanResult result;
      if (len > 0) {
        result.ssid = String(ssid_buf);
      } else {
        result.ssid = String("*HIDDEN*");
      }
      result.channel = record->channel;
      result.rssi    = record->signal_strength;
      result.is5GHz  = (record->channel >= 36);
      memcpy(&result.bssid, &record->BSSID, 6);
      char bssid_str[18];
      snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               result.bssid[0], result.bssid[1], result.bssid[2],
               result.bssid[3], result.bssid[4], result.bssid[5]);
      result.bssid_str = String(bssid_str);
      scan_results.push_back(result);
    }
  } else {
    // Ameba sinaliza fim da varredura completa (todas as frequências 2.4G e 5G)
    scan_done = true;
    scan_in_progress = false;
    sortScanResults();
  }
  return RTW_SUCCESS;
}

void sortScanResults() {
  if (scan_results.size() <= 1) return;
  for (size_t i = 0; i < scan_results.size() - 1; i++) {
    for (size_t j = i + 1; j < scan_results.size(); j++) {
      if (scan_results[j].rssi > scan_results[i].rssi) {
        WiFiScanResult tmp = scan_results[i];
        scan_results[i] = scan_results[j];
        scan_results[j] = tmp;
      }
    }
  }
}

void startScan() {
  scan_results.clear();
  memset(net_marked, 0, sizeof(net_marked));
  net_sel          = 0;
  net_scroll       = 0;
  scan_done        = false;
  scan_in_progress = true;
  wifi_scan_networks(scanResultHandler, NULL);
}