#ifndef PACKET_INJECTION_H
#define PACKET_INJECTION_H

#include <Arduino.h>
#include "../../debug.h"

// Codigos de status detalhados para diagnostico de injecao
enum TxStatus {
  TX_SUCCESS            =  0, // Quadro transmitido/enfileirado com sucesso
  TX_ERR_INVALID_PARAM  = -1, // Ponteiro de quadro nulo ou tamanho invalido
  TX_ERR_WLAN_INFO_NULL = -2, // rltk_wlan_info e nulo (driver WiFi nao inicializado)
  TX_ERR_ADAPTER_NULL   = -3, // Ponteiro padapter e nulo (estrutura do driver corrompida)
  TX_ERR_ALLOC_FRAME    = -4, // alloc_mgtxmitframe retornou nulo (descritores TX esgotados / fila cheia)
  TX_ERR_BUFFER_NULL    = -5, // Buffer de payload (+0x80) e nulo (HardFault evitado!)
  TX_ERR_DUMP_FAILED    = -6, // dump_mgntframe retornou codigo de erro
  TX_ERR_RATE_LIMIT     = -7  // Descartado pelo limitador local (<8ms entre quadros)
};

// Telemetria de injecao para diagnostico de crash interno
struct TxTelemetry {
  uint32_t tx_attempted;
  uint32_t tx_succeeded;
  uint32_t tx_alloc_failures;   // Qtd de falhas em alloc_mgtxmitframe (esgotamento de descritores)
  uint32_t tx_dump_failures;    // Qtd de falhas em dump_mgntframe
  uint32_t tx_param_failures;   // Parametros ou ponteiros invalidos
  int      last_error_code;     // Ultimo codigo de erro TxStatus
  int      last_dump_ret;       // Ultimo retorno retornado por dump_mgntframe
  uint32_t last_fail_ms;        // Timestamp millis da ultima falha
  uint32_t tx_rate_limited;     // Quadros descartados pelo limitador de 8ms
};

extern TxTelemetry tx_telemetry;

void reset_tx_telemetry();
const char* tx_status_str(int status);

typedef struct __attribute__((packed)) {
  uint16_t frame_control = 0xC0;
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number = 0;
  uint16_t reason = 0x06;
} DeauthFrame;

typedef struct __attribute__((packed)) {
  uint16_t frame_control = 0x80;
  uint16_t duration = 0;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number = 0;
  uint64_t timestamp = 0;
  uint16_t beacon_interval = 0x64;
  uint16_t ap_capabilities = 0x21;
  uint8_t ssid_tag = 0;
  uint8_t ssid_length = 0;
  uint8_t ssid[255];
} BeaconFrame;

/*
 * Import the needed c functions from the closed-source libraries
 * The function definitions might not be 100% accurate with the arguments as the types get lost during compilation and cannot be retrieved back during decompilation
 * However, these argument types seem to work perfect
*/
extern uint8_t* rltk_wlan_info;
extern "C" void* alloc_mgtxmitframe(void* ptr);
extern "C" void update_mgntframe_attrib(void* ptr, void* frame_control);
extern "C" int dump_mgntframe(void* ptr, void* frame_control);

int wifi_tx_raw_frame(const void* frame, size_t length);
int wifi_tx_deauth_frame(const void* src_mac, const void* dst_mac, uint16_t reason = 0x06);
int wifi_tx_beacon_frame(const void* src_mac, const void* dst_mac, const char *ssid);

#endif

