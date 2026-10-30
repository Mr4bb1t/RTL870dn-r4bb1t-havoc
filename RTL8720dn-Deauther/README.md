# RTL8720DN R4BB1T HAVOC — Documentação Técnica

Firmware de auditoria de radiofrequência autônomo para o SoC Realtek RTL8720DN (AmebaD / Módulo BW16). Implementa injeção de quadros brutos 802.11 em bandas duplas (2.4 GHz e 5 GHz), transmissão de pacotes de anúncio BLE 5.0 (Bluetooth Low Energy) e um controle universal IR com gravação, emissão e spam de códigos.

---

## 1. Especificações Técnicas de Hardware

| Parâmetro | Detalhes Técnicos |
|:---|:---|
| **SoC** | Realtek RTL8720DN (Módulo B&T BW16) |
| **CPU Principal** | ARMv8-M Cortex-M33 (KM4) operando a até 200 MHz |
| **CPU Secundária** | ARMv8-M Cortex-M23 (KM0) operando a 20 MHz |
| **Memória** | 512 KB SRAM interna, 4 MB Flash externa via interface QSPI |
| **Rádio Wi-Fi** | IEEE 802.11 a/b/g/n 1T1R Dual-Band |
| **Faixas de Frequência Wi-Fi** | • **2.4 GHz:** 2.412 GHz – 2.484 GHz (Canais 1 a 14)<br>• **5 GHz:** 5.180 GHz – 5.825 GHz (UNII-1, UNII-2, UNII-2e, UNII-3; Canais 36 a 165) |
| **Rádio Bluetooth** | Bluetooth Low Energy (BLE) 5.0 (suporte a GAP Broadcaster e Observer) |
| **Interface do Display** | Barramento I2C por hardware operando a 400 kHz (Fast Mode) |
| **Controlador de Display** | SH1106 OLED monocromático (resolução 128x64 pixels, endereço I2C `0x3C`) |
| **Entradas de Usuário** | 5 GPIOs em modo `INPUT_PULLUP` ativo-baixo (D-Pad de 5 direções) |
| **Controle IR** | Receptor TSOP/VS1838 em PB_2 (RX1 / D5, late-armed) + emissor 38 kHz em D12 (PA_12) |
| **Alimentação** | 3.3V DC regulado (consumo de pico durante transmissão RF de até 450 mA) |

---

## 2. Pinagem e Interface Elétrica

### 2.1 Display OLED SH1106 (I2C)
O display se comunica diretamente com o controlador I2C0 do RTL8720DN:

| Sinal OLED | Pino GPIO (RTL8720DN) | Pino Físico BW16 | Nível Lógico | Descrição |
|:---|:---:|:---:|:---:|:---|
| **VCC** | 3V3 | 3.3V | 3.3V | Alimentação principal |
| **GND** | GND | GND | 0V | Referência comum de terra |
| **SCL** | `PA_25` | SCL | 3.3V | Clock I2C (requer resistor pull-up ou interno) |
| **SDA** | `PA_26` | SDA | 3.3V | Dados I2C bidirecionais |

### 2.2 Entradas de Botões (D-Pad de 5 Vias)
Todas as entradas utilizam resistores de elevação internos configurados via `pinMode(pin, INPUT_PULLUP)`. O acionamento ocorre ao fechar o contato elétrico diretamente com o terra (**Active-LOW**):

| Chave | Função | Pino GPIO | Pino Físico BW16 | Conexão Externa | Estado Normal | Estado Pressionado |
|:---|:---|:---:|:---:|:---|:---:|:---:|
| **SW1** | UP | `PB_3` | D6 | `PB_3` ──[ Chave ]── `GND` | HIGH (3.3V) | LOW (0V) |
| **SW2** | DOWN | `PA_13` | D11 | `PA_13` ──[ Chave ]── `GND` | HIGH (3.3V) | LOW (0V) |
| **SW3** | LEFT | `PA_30` | D3 | `PA_30` ──[ Chave ]── `GND` | HIGH (3.3V) | LOW (0V) |
| **SW4** | RIGHT | `PA_27` | D2 | `PA_27` ──[ Chave ]── `GND` | HIGH (3.3V) | LOW (0V) |
| **SW5** | ENTER | `PA_14` | D10 | `PA_14` ──[ Chave ]── `GND` | HIGH (3.3V) | LOW (0V) |

*Debounce:* Implementado por software na máquina de estados com janela de bloqueio de 150 ms para eliminar repiques mecânicos.

### 2.3 Controle IR (Receptor + Emissor) e Compartilhamento de PB_2 (RX1 / Download)

| Sinal IR | Pino GPIO (RTL8720DN) | Pino Físico BW16 | Descrição |
|:---|:---:|:---:|:---|
| **IR_RX** | `PB_2` | RX1 / D5 (PB2) | Saída demodulada do receptor TSOP/VS1838 (ativo-baixo, 38 kHz) / LP_UART_RX |
| **IR_TX** | `PA_12` | D12 | LED IR via transistor NPN/PNP, modulado por PWM de 38 kHz |

- **Receptor:** qualquer módulo TSOP38xx / VS1838B / HX1838. Alimentação em 3.3 V, saída ligada ao pino **PB_2** (RX1 / D5).
- **Emissor:** LED IR 940 nm em série com resistor (100–220 Ω para 3.3 V) e transistor de chaveamento. O D12 fornece a portadora de 38 kHz; o transistor evita drenar corrente diretamente do GPIO.
- **Persistência:** os sinais gravados são armazenados no setor reservado ao usuário da flash (`0x00100000`, biblioteca `FlashMemory` do core AmebaD), sobrevivendo a reboots e regravações normais do sketch.
- **Visualizador de Forma de Onda (Waveform):** O OLED desenha os pulsos reais (marks e spaces) como um osciloscópio digital logo após a captura ou na inspeção de sinais salvos.
- **Suíte de Ataques IR / TV-B-Gone:** Modos alternáveis em tempo real via direcional UP/DOWN:
  1. **TV-B-GONE:** Varredura rápida de desligamento (POWER) de todas as marcas com barra de progresso gráfica de 0 a 100%.
  2. **ALL (PWR+VOL):** Alterna Power e Volume+ sequencialmente.
  3. **MUTE SPAM:** Disparo contínuo de Mute.
  4. **VOL+ / VOL- SPAM:** Variação contínua de volume.
- **Marcas incluídas na base:** Samsung, LG TV, Sony, TCL, AOC, Philips, Toshiba/Semp, Philco, Panasonic e Multilaser.
- **Exportação Flipper Zero (`.ir`):** Envie `ir export` ou `ir dump` no Serial Monitor para exportar todos os sinais no formato padrão de arquivos `.ir` do Flipper Zero.

#### Gerenciamento Meticuloso de PB_2 (IR_RX vs. RX1 Download Mode)

O pino **PB_2** é simultaneamente o **strap de boot/download**, a linha **RX1** (`LP_UART_RX`) usada pelo bootloader para gravação UART e a entrada do receptor infravermelho.

Para garantir que um **não derrube o outro**, o firmware adota uma política estrita:

1. **Repouso e Boot (Alta Impedância sem Pull-down):** No boot e durante toda a navegação comum do sistema, o pino PB_2 permanece em `INPUT_PULLNONE` com `pinRemoveMode(IR_RX_PIN)`. 
   > ⚠️ **Nota Crítica sobre o Core AmebaD:** A chamada `pinMode(pin, INPUT)` neste core ativa o **Pull-Down interno**. Portanto, NUNCA utilize `pinMode(INPUT)` no PB_2, pois isso puxaria a linha para LOW falsamente, interferindo no boot e no RX1. Usa-se sempre `INPUT_PULLNONE` + `pinRemoveMode()`.
2. **Ativação Tardia Exclusiva (Late-Arm em Record):** O pino só é armado como `INPUT_PULLUP` quando o usuário entra ativamente na tela de gravação (`SCR_IR_RECORD`).
3. **Desativação Imediata na Saída:** Qualquer transição para fora da tela de gravação (seja voltando ao menu, timeout ou comando serial) chama imediatamente `ir_stop_capture()`, restaurando PB_2 para `INPUT_PULLNONE`.
4. **Modo Download Seguro:** Ao acionar o Download Mode (via menu About ou comando serial `flash`/`download`), o firmware desliga explicitamente o receptor, libera o pino PB_2 para o hardware de UART e programa `BIT_UARTBURN_BOOT` antes do reset.
5. **Dica de Hardware (Isolamento Elétrico):** Se o seu módulo TSOP possuir saída push-pull ativa (e não coletor aberto), recomenda-se inserir um resistor de **1 kΩ em série** entre o OUT do TSOP e o PB_2. Assim, quando o gravador USB-UART enviar os pacotes de firmware em RX1, o sinal do programador sobrepõe o TSOP com total segurança sem disputa de corrente.

O `ir_init()` (chamado no `setup()`) **não toca no pino**. O `pinMode(INPUT_PULLUP)` só é aplicado dentro de `ir_start_capture()` ao entrar na tela de gravação, e revertido para `INPUT` em `ir_stop_capture()` ao sair.

#### Pinos que NÃO podem ser usados como IR_RX nesta placa

| Pino | GPIO | Motivo do conflito |
|:---:|:---:|:---|
| D9 | PA_15 | **UART0-RX do `Serial`** — ao chamar `Serial.begin()`, o periférico UART toma conta do MUX do pino e o mantém em LOW permanentemente |
| D1/D4\* | PA_29 | **RX da ponte USB-UART** — conectado ao conversor USB do BW16; um receptor IR neste pino bloqueia o fluxo do bootloader e impede a gravação |

---

## 3. Arquitetura de Software e Módulos

O código está estruturado em módulos C++ para desacoplar a camada de controle de radiofrequência da interface de usuário:

```
RTL8720dn-Deauther/
├── RTL8720dn-Deauther.ino      # Loop principal, timers de polling de botões e dispatcher
├── config.h                    # Mapeamento de pinos GPIO, temporizações e constantes
├── debug.h                     # Macros condicionais para telemetria serial via UART
├── wifi_scanner.h / .cpp       # Camada de varredura de canais 2.4GHz e 5GHz via driver Ameba
├── wifi_attacks.h / .cpp       # Montagem e injeção de quadros 802.11 (Deauth e Beacons)
├── ble_spam.h / .cpp           # Formatação e disparo de pacotes BLE GAP Advertising
├── ir_remote.h / .cpp          # Gravação, decodificação, emissão e spam de sinais IR
├── ui_state.h / .cpp           # Máquina de estados finitos (FSM) e controle de fluxos
├── ui_display.h / .cpp         # Renderização de texto e buffers gráficos via U8g2
├── src/packet-injection/       # Envoltórios da API de envio de pacotes raw do RTL8720DN
└── simulator.html              # Emulador estático de hardware e display para testes locais
```

---

## 4. Mecanismos de Operação de Radiofrequência

### 4.1 Varredura de Redes Wi-Fi (Dual-Band Scanner)
- **Modo Promíscuo / Varredura de Canais:** O módulo executa varredura com channel hopping configurado sequencialmente nos canais de 2.4 GHz (1 a 14) e nos canais UNII-1/2/3 de 5 GHz (36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165).
- **Extração de Informações:** A cada beacon ou probe response recebido, a rotina extrai e armazena:
  - BSSID (endereço MAC de 6 octetos da interface do AP);
  - Canal operacional primário (com suporte a exibição de 3 dígitos sem estouro de buffer);
  - RSSI em dBm;
  - Tipo de encriptação (Open, WEP, WPA-PSK, WPA2-PSK, WPA3);
  - SSID (nome legível da rede, armazenado em até 32 bytes).
- **SSIDs repetidos na lista:** a deduplicação é feita por **BSSID**, não por SSID. Duas ou três linhas com o mesmo nome em 2,4 GHz são rádios diferentes (mesh/repetidor, rede guest, WPA2/WPA3 transition etc.). A lista mostra `xN` quando o nome se repete e o cabeçalho exibe os últimos octetos do BSSID da linha selecionada. O log serial `[SCAN] Nomes repetidos` lista todos os BSSIDs de cada nome — se forem iguais, é bug; se forem diferentes, são APs distintos.
- **Redes `*HIDDEN*`:** são SSIDs realmente ocultos (`SSID.len == 0` no beacon/probe response), não erro de parsing. A lista mostra `*H:<sufixo do BSSID>` para diferenciá-las e o log serial marca cada uma com `[HIDDEN]`. Elas reaparecem em todo scan porque continuam no ar; compare o BSSID entre scans para confirmar que é o mesmo AP.

---

### 4.2 Ataque de Desautenticação 802.11 (Wi-Fi Deauthentication)

#### O que faz:
Transmite sequências contínuas de quadros de gerenciamento desautenticando clientes conectados a um determinado Ponto de Acesso (Access Point), forçando a interrupção do enlace de dados.

#### Como funciona internamente:
1. **Sintonia de Canal:** O firmware sintoniza o rádio do RTL8720DN no canal exato do AP alvo através da função `wext_set_channel(WLAN0_NAME, target_channel)`.
2. **Estrutura do Pacote Injetado:** Monta um quadro de gerenciamento IEEE 802.11 (Tipo `0x00`, Subtipo `0x000C` — *Deauthentication Frame*):
   - **Frame Control:** `0x00C0` (Quadro de Gerenciamento de Desautenticação);
   - **Duration / ID:** `0x013A` (duração típica de reserva de meio);
   - **Destination Address (Addr1):** `FF:FF:FF:FF:FF:FF` (Broadcast para atingir todas as estações) ou o MAC de uma estação específica;
   - **Source Address (Addr2):** BSSID do Ponto de Acesso falsificado;
   - **BSSID (Addr3):** BSSID do Ponto de Acesso;
   - **Sequence Control:** Incrementado ciclicamente a cada quadro;
   - **Reason Code:** `0x0007` (*Class 3 frame received from nonassociated STA*) ou `0x0001` (*Unspecified reason*).
3. **Disparo:** O buffer montado é transmitido diretamente pela interface física em modo promiscuous/raw frame injection através de `wifi_send_raw_frame()`.
4. **Multi-Target:** Caso múltiplos APs tenham sido selecionados pelo operador, o algoritmo executa round-robin cíclico entre os canais e alvos marcados.

---

### 4.3 Inundação de Redes Wi-Fi Falsas (Beacon Flooder)

#### O que faz:
Injeta continuamente quadros de anúncio de rede (Beacons) no ar, criando dezenas ou centenas de redes Wi-Fi virtuais no espectro de 2.4 GHz e 5 GHz.

#### Como funciona internamente:
1. **Construção do Quadro Beacon IEEE 802.11 (Subtipo `0x0008`):**
   - **Frame Control:** `0x0080`;
   - **Endereço de Destino:** `FF:FF:FF:FF:FF:FF`;
   - **Endereço de Origem (BSSID):** Gerado dinamicamente com o bit *Locally Administered* habilitado no primeiro octeto (`MAC[0] | 0x02`), garantindo unicidade para cada rede falsa;
   - **Campos Fixos de Gerenciamento:** Timestamp de 8 bytes, Beacon Interval (típico `0x0064` = 100 TU ≈ 102.4 ms) e Capability Information (`0x0411` para rede ESS aberta ou com WPA2);
   - **Information Elements (IEs):**
     - *IE 0 (SSID):* Comprimento e octetos do nome da rede;
     - *IE 1 (Supported Rates):* 1, 2, 5.5, 11, 6, 9, 12, 24 Mbps;
     - *IE 3 (DS Parameter Set):* Canal de transmissão atual.

2. **Modos Operacionais:**
   - **Modo Preset:** Emite listas estáticas pré-programadas em flash.
   - **Modo Clone / Copiar:** Captura os SSIDs das redes reais identificadas no scanner e reproduz réplicas contínuas.
   - **Modo Custom (Teclado On-Screen):** O operador define uma string arbitrária diretamente no display através do teclado virtual.

3. **Mecanismo Anti-Agrupamento (Bypass de Deduplicação de SSIDs):**
   - Dispositivos modernos (Android, iOS, Windows) utilizam a string exata do SSID como chave de hash para consolidar múltiplos BSSIDs na mesma entrada da lista de conexões.
   - Para forçar a exibição de múltiplos pontos de acesso idênticos no dispositivo alvo, o firmware concatena sequências de espaços não-quebráveis (*Non-Breaking Spaces* - NBSP, codificados como `0xC2 0xA0` em UTF-8) ao final da string.
   - Como cada rede anunciada possui um número distinto de bytes NBSP, o sistema operacional as classifica como redes distintas, enquanto o renderizador de fontes do smartphone omite o caractere visualmente.

4. **Controle de Buffer e Limite Estrito do Padrão 802.11:**
   - O campo de tamanho do *IE 0 (SSID)* do padrão IEEE 802.11 possui 1 byte, permitindo no máximo 32 octetos.
   - O firmware impõe truncamento estrito em 32 bytes em todas as rotinas (`snprintf` e checagem de limites), eliminando corrupção de memória e congelamento de transmissão durante ciclos longos de pacotes.

5. **Drenagem do Anel de TX (Channel Hopping) + Agenda Round-Robin:**
   - O anel de descritores de gerenciamento do RTL8720DN satura em ~33 quadros quando o rádio permanece preso em um único canal, fazendo `alloc_mgtxmitframe()` retornar `NULL` e travando o firmware após poucos pacotes.
   - O flood alterna a sintonia a cada `BEACON_HOP_EVERY_FRAMES` (16) beacons entre o canal primário (`1`) e um canal de escape (`11`, mesma lógica do *ghost group* do deauth), o que esvazia o anel periodicamente e mantém a transmissão estável por tempo indefinido.
   - **Agenda orientada a manter todas as redes no ar:** o firmware envia **1 beacon por vez**, em round-robin estrito (`0,1,...,N-1,0,...`). O espaçamento é `gap = clamp(BEACON_REFRESH_MS / N, BEACON_MIN_PACING_MS, BEACON_MAX_PACING_MS)`, para que uma rodada completa com os `N` SSIDs feche em ~`BEACON_REFRESH_MS` (200 ms por padrão). Na prática, 20 SSIDs → 1 beacon a cada 10 ms → cada uma das 20 redes é reanunciada a cada ~200-250 ms, dentro da janela de validade dos scanners.
   - Durante o flood o redraw do OLED cai para 100 ms (era 30 ms): cada frame I2C a 400 kHz bloqueia o loop por ~23 ms e atrasaria a rodada.
   - O envio não trava a interface: a espera entre beacons usa `delay(1)` (cede CPU ao driver a cada 1 ms), então botões, render e serial continuam responsivos. **Isso é obrigatório:** o `loop()` do Arduino roda em prioridade alta no FreeRTOS; sem bloquear, a task de TX do WiFi não roda, os buffers de gerenciamento não voltam ao pool e todo beacon seguinte falha com `TX_ERR_ALLOC_FRAME (-4)`. Falha de alocação (`TX_ERR_ALLOC_FRAME`) aplica respiro de 20 ms e força re-sintonia no beacon seguinte; após `BEACON_MAX_CONSEC_FAIL` falhas consecutivas o ataque é interrompido para proteger o firmware.
   - O display mostra `APs:N XXXms`, onde `XXXms` é a duração real da última rodada completa. Se ficar muito acima de `BEACON_REFRESH_MS`, o driver está engasgando: confira `Alloc` no log `[BEACON-STAT]` e reduza `N`.
   - O endereço MAC é derivado deterministicamente do índice do SSID (bit *Locally Administered* ativo), reutilizando o mesmo BSSID para o mesmo SSID falso — como o deauth faz com o BSSID real do alvo.

---

### 4.4 Disparo de Pacotes Bluetooth Low Energy (Apple BLE Proximity Spam)

#### O que faz:
Transmite pacotes de anúncio BLE 5.0 não conectáveis (*Non-Connectable Undirected Advertising*) formatados com as especificações do protocolo proprietário de proximidade da Apple Inc., provocando a abertura automática de janelas modais de emparelhamento em dispositivos iOS, iPadOS e macOS no raio de alcance.

#### Como funciona internamente:
1. **Configuração do Controlador BLE GAP:**
   - Canal de rádio: Canais primários de anúncio BLE (37, 38 e 39 — 2.402 GHz, 2.426 GHz e 2.480 GHz);
   - Intervalo de anúncio: Configurado na menor janela permitida (20 ms a 40 ms) para alta taxa de entrega;
   - PDU Type: `ADV_NONCONN_IND` (0x02) ou `ADV_IND` (0x00).

2. **Formatação da Carga Útil (Advertising Data - AD):**
   - **Flags Element (Tamanho 0x02, Tipo 0x01):** `0x06` (`LE General Discoverable Mode` | `BR/EDR Not Supported`);
   - **Manufacturer Specific Data Element (Tipo 0xFF):**
     - **Company Identifier:** `0x004C` (Apple, Inc., formatado em Little-Endian: `0x4C, 0x00`);
     - **Protocol Type:** `0x07` ou `0x10` (indica ação de emparelhamento por proximidade / AirDrop / Continuity);
     - **Device Model Code:** Identificador específico do acessório:
       - `0x02, 0x07`: AirPods Pro (2ª Geração);
       - `0x0A`: Apple TV Setup Prompt;
       - `0x05`: Beats Studio Buds;
       - `0x1E`: AirTag;
       - `0x0B`: Transfer Phone Number;
       - `0x10`: Apple Watch Pairing;
     - **Authentication / Status Mask:** Bytes de estado de bateria simulados (ex: case 100%, fone esquerdo 100%, fone direito 100%).

3. **Rotação de Endereço MAC (AdvA / MAC Spoofing):**
   - O iOS identifica um acessório pelo **endereço de origem do advertising (AdvA)**. Enquanto o AdvA não muda, ele exibe **um** popup e descarta os pacotes seguintes como "o mesmo dispositivo". É exatamente esse o sintoma de "só aparece um popup".
   - A cada troca de identidade o firmware executa, nesta ordem:
     1. `le_adv_stop()` e espera `GAP_ADV_STATE_IDLE` (o stack **ignora silenciosamente** troca de endereço/parâmetros se o advertising ainda não parou);
     2. `le_set_rand_addr(mac)` → HCI `LE_Set_Random_Address`;
     3. `le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, GAP_LOCAL_ADDR_LE_RANDOM)`;
     4. `le_adv_start()` → `LE_Set_Advertising_Parameters` com `own_addr_type = 1 (random)`;
     5. confirma que o GAP entrou em `GAP_ADV_STATE_ADVERTISING`.
   - **Formato do endereço:** o array do SDK é *little-endian* (`mac[0]` = LSB, `mac[5]` = MSB). Endereço *static random* exige os dois bits mais significativos em `11`, logo `mac[5] |= 0xC0`. Essa ordem foi confirmada no binário (`hci_cmd_addr_param` copia os 6 bytes sem inverter) e em `BLEAddr::str()`, que imprime `_btAddr[5]` primeiro.
   - **Hold por identidade:** `BLE_SPAM_REPEAT` (padrão `2`) mantém o mesmo MAC + payload no ar por N ciclos **sem parar o advertising** (20 ms de intervalo ≈ 5 pacotes por ciclo). Isso dá tempo do iOS registrar aquele MAC antes do próximo aparecer — trocar rápido demais faz o iPhone tratar os pacotes como ruído. Cadência final: `BLE_SPAM_INTERVAL_MS` (110 ms) × `BLE_SPAM_REPEAT` (2) ≈ **220 ms por MAC (~4-5 identidades novas por segundo)**.
   - **Potência de TX:** `BLE_TX_GAIN` (padrão `0x23` = **+4,5 dBm**, máximo documentado). O firmware chama o comando vendor `le_adv_set_tx_power(0, BLE_TX_GAIN)` uma vez por sessão, logo após o stack ficar pronto (`vendor_cmd_init()` já rodou). A tabela do SDK é `0x06` = −10 dBm, `0x1A` = 0 dBm, `0x23` = +4,5 dBm; `0` mantém o default do driver. O retorno da função só confirma o **enfileiramento** do HCI (o resultado final viria em `GAP_MSG_VENDOR_CMD_RSP`, que não registramos para não sobrescrever o callback de coexistence do core).
   - `BLE_MAC_ROTATE 0` desliga a rotação e usa o MAC público fixo do chip como fallback.
   - Se o controller recusar um endereço, o firmware **não desiste**: registra a falha, cai para o MAC público naquele ciclo e tenta novamente na próxima rotação.

#### Como verificar se a rotação está funcionando (no ar):
1. Abra um app de scanner BLE (nRF Connect, LightBlue) e toque em *Scan*.
2. Ligue o BLE Spam no BW16.
3. Observe vários dispositivos "AirPods" aparecendo e desaparecendo, **cada um com um endereço diferente** (tipo `Random`). Se o endereço ficar sempre igual, a rotação não chegou ao controller.
4. No serial (115200) cada rotação imprime:
   ```
   [BLE] Airpods id=0x02 len=31 mac=C2:9A:41:0B:77:E4 type=1(RAND) dt=452ms
   ```
   - `type=1(RAND)` → o GAP está com o modo aleatório ativo;
   - `type=0(PUB)` ou `(PUB)` após o MAC → caiu no fallback público;
   - `AVISO: adv nao entrou em ADVERTISING` → o controller recusou o endereço/MAC.

#### Diagnóstico rápido:
| Sintoma no serial | Significado | Ação |
|---|---|---|
| `mac=...` diferente a cada ~2 ciclos e `type=1(RAND)` | rotação aplicada no host | se o iPhone ainda mostra 1 popup, aumente `BLE_SPAM_REPEAT` |
| `TX power gain=0x23 -> enviado (cause=0)` | comando vendor de potência aceito na fila HCI | se `-> FALHOU`, o stack ainda não estava pronto: reinicie o spam |
| `le_set_rand_addr falhou` repetido | comando HCI recusado/enfileirado | reduza a cadência (`BLE_SPAM_INTERVAL_MS`) |
| `adv nao ficou IDLE` | stop ainda pendente | aumente o timeout em `adv_wait_idle()` ou a cadência |
| `adv nao entrou em ADVERTISING` | endereço inválido recusado pelo controller | reporte o log completo |

---

### 4.5 Suíte de Controle Infravermelho (IR Suite & TV-B-Gone)

O menu principal possui o card **IR Remote** posicionado estrategicamente antes de Games, com ícone animado de televisão CRT (14 frames de 48x48 pixels com efeito de desligamento em estrela/feixe de elétrons central e religamento). O menu interno em carrossel apresenta cards vetoriais com gráficos sólidos e de alto contraste:
- **Card 0 (RECORD):** Sensor óptico TSOP sólido com íris negativa, ondas de portadora incidentes e indicador `● REC`.
- **Card 1 (EMIT):** Controle remoto universal sólido com teclas em baixo-relevo negativo, emissor LED e ondas de portadora expansivas.
- **Card 2 (SPAM):** Raio de alta tensão sólido com ondas de choque duplas e faíscas em diamante.

#### 4.5.1 Gravação e Escuta Contínua (Record)
1. **Escuta Contínua e Responsiva:** Ao entrar na tela de gravação, o firmware arma `IR_RX` (PB_2 / RX1) e realiza amostragem temporal contínua. Pressionar qualquer botão do controle remoto atualiza a tela em tempo real com os novos pulsos e protocolo, sem necessidade de limpar manualmente o sinal anterior.
2. **Visualizador de Forma de Onda (OLED Oscilloscope):** Renderiza o trem de pulsos real capturado (marcas e espaços) diretamente no display OLED monocromático de 128x64 pixels. Em repouso, exibe uma linha de centro com rastreador animado (blip) de prontidão.
3. **Decodificação de Protocolos:**
   - **NEC:** Preâmbulo 9000/4500 µs, 32 bits de endereço/comando.
   - **Samsung:** Preâmbulo 4500/4500 µs, 32 bits.
   - **Sony SIRC:** Preâmbulo 2400/600 µs, 12 bits.
   - **RAW:** Sinais desconhecidos, ventiladores ou condicionadores de ar são preservados borda a borda para replicação exata.
4. **Disparo Único e Sem Duplicações:** A rotina de reprodução (`REPLAY`) e o tratamento de botões contam com `flush_input()`, disparando rigorosamente 1 único frame limpo por toque (eliminando repetições acidentais em televisores).
5. **Persistência em Flash:** Armazena até 8 slots completos no setor reservado ao usuário (`0x00100000`).

#### 4.5.2 Emissão e Gerenciamento de Memória (Emit)
- Lista os sinais salvos com protocolo e código hexadecimal.
- Opção **EMIT** para disparo único imediato pelo pino PA_12 (38 kHz PWM).
- Opção **DELETE** com confirmação em duas etapas para evitar exclusão acidental. A alteração é gravada imediatamente na flash.

#### 4.5.3 Modos de Ataque IR e TV-B-Gone (Spam)
O modo Spam possui seleção interativa de comportamento via botões **UP** e **DOWN** em tempo real:
1. **TV-B-GONE:** Varredura rápida de códigos de desligamento (POWER) para desligar aparelhos em massa, acompanhada por uma barra de progresso gráfica de 0 a 100%.
2. **ALL (PWR+VOL):** Alternância sequencial entre comandos de Power e Volume+.
3. **MUTE SPAM:** Bombardeamento contínuo de Mute para silenciamento de televisores.
4. **VOL+ / VOL- SPAM:** Variação contínua e rápida de volume.

**Marcas compatíveis na biblioteca:** Samsung, LG TV, Sony, TCL, AOC, Philips, Toshiba/Semp, Philco, Panasonic e Multilaser.

#### 4.5.4 Exportação para Flipper Zero (`.ir`)
O firmware permite exportar a biblioteca e o último sinal capturado diretamente para o ecossistema do Flipper Zero:
- Envie o comando `ir export` ou `ir dump` no Monitor Serial (115200 bps).
- O console gera a saída formatada com cabeçalho oficial do Flipper Zero (`Filetype: IR signals file`, `type: parsed` ou `type: raw`), pronta para salvar em um arquivo `.ir` no cartão SD do Flipper Zero.

#### 4.5.5 Codificação e Persistência
| Protocolo | Cabeçalho | Bit 0 | Bit 1 | Bits | Ordem |
|:---|:---:|:---:|:---:|:---:|:---|
| NEC | 9000 / 4500 µs | 560 / 560 µs | 560 / 1690 µs | 32 | LSB primeiro |
| Samsung | 4500 / 4500 µs | 560 / 560 µs | 560 / 1690 µs | 32 | LSB primeiro |
| Sony SIRC | 2400 / 600 µs | 600 µs mark | 1200 µs mark | 12 | MSB primeiro |

Layout da flash (setor de 4096 bytes em `0x00100000`):

```
+0x000  uint32 magic   = 0x49523031 ("IR01")
+0x004  uint16 version = 1
+0x006  uint16 count   (0..8)
+0x008  IRSavedSignal[8]   // 6 bytes de metadados + 200 x uint16 de bordas
```

Cada `IRSavedSignal` ocupa ~406 bytes; 8 sinais + cabeçalho ficam bem abaixo dos 4096 bytes do setor. A gravação usa `FlashMemory.update(true)` (apaga e reescreve o setor). O setor é reservado pelo próprio core AmebaD para dados do usuário, portanto não colide com a imagem do firmware.

> **Dica:** se o receptor estiver muito próximo do LED emissor, a própria emissão pode ser capturada. A captura por polling é suspensa durante o envio e retomada 60 ms depois.

---

## 5. Máquina de Estados e Operação da Interface (FSM)

A interface opera em uma máquina de estados finitos centralizada em `ui_state.cpp`:

```
 [SCR_SPLASH] ────► [SCR_MAIN_MENU]
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
   [SCR_WIFI_MENU]  [SCR_BLE_MENU]  [SCR_ABOUT]
          │               │
    ┌─────┴─────┐         ▼
    ▼           ▼   [SCR_BLE_ATTACK] ──(Qualquer Tecla)──► [SCR_BLE_MENU]
[SCR_SCAN]  [SCR_BEACON_SETUP]
    │           │
    ▼           ├── Modo Custom ──► [SCR_BEACON_KEYBOARD]
[SCR_NET_LIST]  │                           │
    │           ▼                           ▼
    │     [SCR_BEACON_CONFIRM] ◄────────────┘
    │           │
    │           ▼
    │     [SCR_BEACON_ATTACK] ──(Qualquer Tecla)──► [SCR_WIFI_MENU]
    │
    ▼
[SCR_DEAUTH_CONFIRM]
    │
    ▼
[SCR_DEAUTH_ATTACK] ──(Qualquer Tecla)──► [SCR_NET_LIST]
```

Além dos ramos acima, o menu principal também abre `SCR_GAMES_MENU` e `SCR_IR_MENU`. O ramo IR segue:

```
[SCR_IR_MENU] ──► [SCR_IR_RECORD] ──(BACK)──► [SCR_IR_MENU]
      │
      ├──► [SCR_IR_LIST] ──► [SCR_IR_DETAIL] ──(EMIT/DELETE/BACK)
      │
      └──► [SCR_IR_SPAM] ──(ENTER/LEFT)──► [SCR_IR_MENU]
```

### 5.1 Especificação do Teclado Virtual (SCR_BEACON_KEYBOARD)
- **Estrutura:** Matriz de 8 colunas por 4 linhas para seleção de caracteres, mais 1 linha superior de ações rápidas (`row -1`).
- **Páginas de Caracteres:**
  - `Página 0`: Alfabeto minúsculo (`a` a `z`), ponto (`.`), hífen (`-`), sublinhado (`_`).
  - `Página 1`: Alfabeto MAIÚSCULO (`A` a `Z`), ponto (`.`), hífen (`-`), sublinhado (`_`).
  - `Página 2`: Numerais decimais (`0` a `9`) e símbolos (`! @ # $ % & * ( ) - . , _ : ; ? / + =`).
- **Barra de Ações Rápidas (Linha Superior):**
  - `[ABC / 123 / abc]`: Alterna ciclicamente entre as 3 páginas;
  - `[SPACE]`: Insere espaço (`0x20`);
  - `[DEL]`: Remove o último caractere do buffer;
  - `[OK]`: Salva o SSID no buffer global e avança para a tela de confirmação.

---

## 6. Procedimento de Compilação e Gravação

### 6.1 Requisitos de Ferramentas
- **Arduino IDE:** Versão 2.2.x ou superior (ou `arduino-cli`).
- **Core de Placas:** Realtek AmebaD Arduino Core (`v3.1.5` ou superior).
  - URL de Placas Adicionais:
    ```text
    https://github.com/ambiot/ambd_arduino/raw/master/Arduino_package/package_realtek.com_amebad_index.json
    ```
- **Bibliotecas Requeridas:**
  - `U8g2` (instalável via Library Manager).
  - `FlashMemory` (já incluída no core AmebaD; usada para persistir os sinais IR).

### 6.2 Parâmetros da Placa
- **Placa:** `BW16 (RTL8720DN)`
- **Upload Speed:** `1500000` (ou `115200` para conversores seriais genéricos)
- **Port:** Selecionar a porta serial COM enumerada pelo conversor USB-UART

### 6.3 Procedimento para Modo de Download (UART Burn Mode)
O microcontrolador RTL8720DN requer nível lógico baixo no pino de inicialização de bootloader (`PA_07` / `LOG_RX` ou botão dedicado `BURN`) durante a transição de subida do sinal de reset:
1. Conecte o cabo USB ao conversor do BW16.
2. Pressione e mantenha pressionado o botão **BURN**.
3. Pressione e solte o botão **RESET (RST)**.
4. Solte o botão **BURN**. O chip entrará no modo de recepção UART ROM.
5. Inicie a compilação e upload pela Arduino IDE.
6. Ao término da mensagem de confirmação de gravação da imagem Flash, pressione **RESET** uma vez para iniciar a execução do firmware.

---

## 7. Aviso Legal (Disclaimer)

Este software foi projetado e distribuído estritamente para propósitos de pesquisa acadêmica em radiofrequência, testes de penetração autorizados e validação de robustez de protocolos de telecomunicação sem fio em ambientes laboratoriais controlados. 

A transmissão não autorizada de quadros de desautenticação 802.11, a interferência em serviços de telecomunicações e a emissão de pacotes BLE intrusivos em redes e dispositivos de terceiros sem autorização prévia constituem violações das normas de telecomunicações (incluindo diretrizes de agências reguladoras como ANATEL, FCC e equivalentes internacionais) e leis sobre crimes cibernéticos. O desenvolvedor não assume qualquer responsabilidade pelo uso indevido deste firmware.
