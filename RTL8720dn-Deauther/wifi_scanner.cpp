// STL MUST come first
#undef max
#include <vector>

#include "wifi_scanner.h"
#include "debug.h"

std::vector<WiFiScanResult> scan_results;
bool net_marked[MAX_SCAN_RESULTS] = {false};
int  net_sel          = 0;
int  net_scroll       = 0;
volatile bool scan_done        = false;
volatile bool scan_in_progress = false;

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
    // A ordenação é feita pela thread principal (setup/do_scan) depois que
    // scan_done fica visível, evitando ordenar o vector concorrentemente
    // com a thread de eventos do WiFi.
    scan_done = true;
    scan_in_progress = false;
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

int countSameSSID(const String &ssid) {
  int n = 0;
  for (size_t i = 0; i < scan_results.size(); i++) {
    if (scan_results[i].ssid == ssid) n++;
  }
  return n;
}

// Diagnóstico: imprime SSID + BSSID de cada rede. É a única forma de provar
// que duas linhas com o mesmo nome são rádios diferentes (BSSIDs diferentes)
// e que uma linha "*HIDDEN*" é um SSID realmente oculto (SSID.len == 0 no
// beacon), não um erro de parsing.
void dumpScanResults() {
  char line[110];
  DEBUG_SER_PRINT("[SCAN] Lista completa (SSID | BSSID | canal | RSSI):\n");
  for (size_t i = 0; i < scan_results.size(); i++) {
    WiFiScanResult &r = scan_results[i];
    snprintf(line, sizeof(line), "  %2u  %-22.22s  %s  ch%02u  %4d dBm%s\n",
             (unsigned)i,
             r.ssid.c_str(),
             r.bssid_str.c_str(),
             (unsigned)r.channel,
             (int)r.rssi,
             (r.ssid.length() == 0 || r.ssid == "*HIDDEN*") ? "  [HIDDEN]" : "");
    DEBUG_SER_PRINT(line);
  }

  DEBUG_SER_PRINT("[SCAN] Nomes repetidos (mesmo SSID, BSSIDs distintos):\n");
  bool found_dup = false;
  for (size_t i = 0; i < scan_results.size(); i++) {
    // Só processa o primeiro índice de cada nome
    bool already = false;
    for (size_t k = 0; k < i; k++) {
      if (scan_results[k].ssid == scan_results[i].ssid) { already = true; break; }
    }
    if (already) continue;

    int n = countSameSSID(scan_results[i].ssid);
    if (n < 2) continue;
    found_dup = true;
    snprintf(line, sizeof(line), "  '%s' x%d:", scan_results[i].ssid.c_str(), n);
    DEBUG_SER_PRINT(line);
    for (size_t j = i; j < scan_results.size(); j++) {
      if (scan_results[j].ssid != scan_results[i].ssid) continue;
      DEBUG_SER_PRINT(" ");
      DEBUG_SER_PRINT(scan_results[j].bssid_str);
      DEBUG_SER_PRINT("(ch");
      DEBUG_SER_PRINT((int)scan_results[j].channel);
      DEBUG_SER_PRINT(")");
    }
    DEBUG_SER_PRINT("\n");
  }
  if (!found_dup) DEBUG_SER_PRINT("  nenhum\n");
}

void startScan() {
  scan_done        = false;
  scan_in_progress = true;

  int ret = wifi_scan_networks(scanResultHandler, NULL);
  if (ret != RTW_SUCCESS) {
    // Não deixa a UI esperar o timeout inteiro por um scan que nem começou.
    DEBUG_SER_PRINT("[SCAN] Falha ao iniciar varredura. codigo=");
    DEBUG_SER_PRINT(ret);
    DEBUG_SER_PRINT("\n");
    scan_in_progress = false;
    scan_done        = true;
    return;
  }

  // Limpa só depois que o driver aceitou o scan: se o pedido falhar
  // (RTW_TIMEOUT por já existir um scan rodando), a lista anterior é mantida.
  scan_results.clear();
  memset(net_marked, 0, sizeof(net_marked));
  net_sel          = 0;
  net_scroll       = 0;
}