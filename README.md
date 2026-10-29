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
| **Controle IR** | Receptor TSOP/VS1838 em D9 (PA_15) + emissor 38 kHz em D12 (PA_12) |
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

### 2.3 Controle IR (Receptor + Emissor)

| Sinal IR | Pino GPIO (RTL8720DN) | Pino Físico BW16 | Descrição |
|:---|:---:|:---:|:---|
| **IR_RX** | `PA_15` | D9 | Saída demodulada do receptor TSOP/VS1838 (ativo-baixo, 38 kHz) |
| **IR_TX** | `PA_12` | D12 | LED IR via transistor, modulado por PWM de 38 kHz |

- **Receptor:** módulo TSOP38xx / VS1838B / HX1838 em 3.3 V, saída ligada diretamente ao D9.
- **Emissor:** LED IR 940 nm + resistor (100–220 Ω) + transistor NPN/MOSFET de canal N. Nível alto do GPIO = portadora ativa.
- **Persistência:** os sinais gravados ficam no setor de usuário da flash (`0x00100000`, biblioteca `FlashMemory`) e sobrevivem a reboots.
- **Modo download:** o `D5 (PB_2)` é o strap de boot/download desta placa — mantenha-o **livre** (jumper no chip para gravar). Com fio no D5 o ROM não entra em modo de gravação; por isso o receptor usa o D9.
- A documentação detalhada de gravação, emissão, spam e layout da flash está em [`RTL8720dn-Deauther/README.md`](RTL8720dn-Deauther/README.md).

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

3. **Rotação de Endereço MAC (MAC Spoofing):**
   - A cada ciclo ou troca de payload, o firmware gera um novo endereço de rádio aleatório (Random Static/Private Address), evitando que o subsistema de Bluetooth do sistema operacional filtre os pacotes repetidos por supressão de duplicatas.

---

### 4.5 Controle Universal IR (Gravar / Emitir / Spam)

O menu principal ganhou o card **IR Remote**, com ícone animado de TV (14 frames 48x48). Dentro dele:

- **Record:** captura bordas em microssegundos por polling do pino D9, separa quadros por intervalos >15 ms e tenta decodificar **NEC**, **Samsung** e **Sony SIRC**. Exibe protocolo/código e oferece **SAVE**, **REPLAY** e **BACK**. Até **8 sinais** são gravados na flash.
- **Emit:** lista os sinais salvos; ENTER abre o detalhe com **EMIT**, **DELETE** (com confirmação dupla) e **BACK**. Sinais decodificados são reenviados pelo protocolo; sinais desconhecidos são reproduzidos borda a borda.
- **Spam:** alterna **VOLUME +** e **POWER** continuamente (~850 ms), percorrendo **Samsung, LG, Sony, Toshiba e NEC TV**. ENTER/LEFT interrompe.

| Protocolo | Cabeçalho | Bit 0 | Bit 1 | Bits | Ordem |
|:---|:---:|:---:|:---:|:---:|:---|
| NEC | 9000 / 4500 µs | 560 / 560 µs | 560 / 1690 µs | 32 | LSB primeiro |
| Samsung | 4500 / 4500 µs | 560 / 560 µs | 560 / 1690 µs | 32 | LSB primeiro |
| Sony SIRC | 2400 / 600 µs | 600 µs mark | 1200 µs mark | 12 | MSB primeiro |

**Persistência:** setor de 4096 bytes em `0x00100000`, com cabeçalho `IR01` + versão + contagem + até 8 `IRSavedSignal` (~406 bytes cada). A gravação usa `FlashMemory.update(true)` e sobrevive a reboots e regravações normais do sketch.

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
  - `FlashMemory` (incluída no core AmebaD; usada para persistir os sinais IR).

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
