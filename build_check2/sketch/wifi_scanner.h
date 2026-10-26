#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\wifi_scanner.h"
#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

// STL MUST come first to avoid _rtl_vsnprintf conflict
#undef max
#include <vector>

#include "config.h"
#include "wifi_conf.h"
#include "wifi_util.h"
#include "wifi_structures.h"

#define MAX_SCAN_RESULTS 64

extern std::vector<WiFiScanResult> scan_results;
extern bool net_marked[MAX_SCAN_RESULTS];
extern int  net_sel;
extern int  net_scroll;
extern bool scan_done;
extern bool scan_in_progress;

void startScan();
void sortScanResults();
rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result);

#endif // WIFI_SCANNER_H