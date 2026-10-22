# 🐰 R4BB1T HAVOC — Dual-Band Deauther & BLE Spammer
### Firmware Tático Autônomo para RTL8720DN (AmebaD / BW16)
**Display OLED SH1106 128x64 • D-Pad Tático de 5 Botões • Wi-Fi Dual-Band (2.4GHz + 5GHz) • BLE 5.0**

[![Platform](https://img.shields.io/badge/Platform-Realtek%20RTL8720DN%20(BW16)-00599C?style=for-the-badge&logo=microchip)](https://www.amebaiot.com/)
[![Bands](https://img.shields.io/badge/Wi--Fi-2.4GHz%20%2B%205GHz%20Dual--Band-success?style=for-the-badge&logo=wi-fi)](https://github.com/Mr4bb1t/RTL870dn-r4bb1t-havoc)
[![Bluetooth](https://img.shields.io/badge/Bluetooth-BLE%205.0%20(EvilAppleJuice)-007AFF?style=for-the-badge&logo=bluetooth)](https://github.com/Mr4bb1t/RTL870dn-r4bb1t-havoc)
[![Display](https://img.shields.io/badge/Display-OLED%20SH1106%20128x64-blueviolet?style=for-the-badge)](https://github.com/Mr4bb1t/RTL870dn-r4bb1t-havoc)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)

---

```
   ██████╗  ██╗  ██╗██████╗ ██████╗  ██╗████████╗    ██╗  ██╗ █████╗ ██╗   ██╗ ██████╗  ██████╗
   ██╔══██╗██║  ██║██╔══██╗██╔══██╗███║╚══██╔══╝    ██║  ██║██╔══██╗██║   ██║██╔═══██╗██╔════╝
   ██████╔╝███████║██████╔╝██████╔╝╚██║   ██║       ███████║███████║██║   ██║██║   ██║██║     
   ██╔══██╗╚════██║██╔══██╗██╔══██╗ ██║   ██║       ██╔══██║██╔══██║╚██╗ ██╔╝██║   ██║██║     
   ██║  ██║     ██║██████╔╝██████╔╝ ██║   ██║       ██║  ██║██║  ██║ ╚████╔╝ ╚██████╔╝╚██████╗
   ╚═╝  ╚═╝     ╚═╝╚═════╝ ╚═════╝  ╚═╝   ╚═╝       ╚═╝  ╚═╝╚═╝  ╚═╝  ╚═══╝   ╚═════╝  ╚═════╝
                     [ D U A L - B A N D   R F   O P S   &   B L E   S P A M ]
```

---

## 📖 Sumário
- [⚡ Visão Geral](#-visão-geral)
- [✨ Principais Recursos](#-principais-recursos)
  - [1. Wi-Fi Scanner Dual-Band (2.4GHz + 5GHz)](#1-wi-fi-scanner-dual-band-24ghz--5ghz)
  - [2. Wi-Fi Deauthentication Attack](#2-wi-fi-deauthentication-attack)
  - [3. Wi-Fi Beacon Flooder Turbo (3 Modos)](#3-wi-fi-beacon-flooder-turbo-3-modos)
  - [4. Apple BLE Proximity Spam (EvilAppleJuice Port)](#4-apple-ble-proximity-spam-evilapplejuice-port)
  - [5. Simulador Web Integrado](#5-simulador-web-integrado)
- [🔌 Esquema Elétrico & Pinout](#-esquema-elétrico--pinout)
  - [Pinout do Módulo BW16](#pinout-do-módulo-bw16)
  - [Diagrama de Conexões Físicas](#diagrama-de-conexões-físicas)
- [🎮 Controles e Navegação](#-controles-e-navegação)
  - [Teclado Virtual On-Screen](#teclado-virtual-on-screen-phone-style)
- [🚀 Guia de Compilação e Gravação](#-guia-de-compilação-e-gravação)
  - [1. Configurar Arduino IDE](#1-configurar-arduino-ide)
  - [2. Modo de Gravação (Burn Mode)](#2-modo-de-gravação-burn-mode)
- [📁 Estrutura do Repositório](#-estrutura-do-repositório)
- [⚠️ Aviso Legal (Disclaimer)](#️-aviso-legal-disclaimer)

---

## ⚡ Visão Geral

O **R4BB1T HAVOC** é um dispositivo autônomo e portátil de auditoria e segurança de radiofrequência desenvolvido sobre o SoC **Realtek RTL8720DN (AmebaD / Módulo B&T BW16)**. Equipado com um processador Dual-Core ARM Cortex-M33 (200 MHz) e rádio com suporte nativo a **Wi-Fi Dual-Band (2.4 GHz e 5 GHz)** e **Bluetooth Low Energy 5.0**, o dispositivo não depende de computadores, smartphones ou servidores web para operar.

Toda a interação é feita localmente em um display **OLED SH1106 de 1.3" (128x64)** de alto contraste, guiado por um **D-Pad tático de 5 botões** com resposta instantânea e sistema de desligamento de emergência (*Kill-Switch*).

---

## ✨ Principais Recursos

### 1. Wi-Fi Scanner Dual-Band (2.4GHz + 5GHz)
- **Varredura Completa**: Canais 2.4GHz (1 a 14) e canais 5GHz UNII-1, UNII-2 e UNII-3 (36 até 165).
- **Radar Circular Gráfico**: Animação em tempo real exibindo pulso radial e saturação do espectro.
- **Telemetria ao Vivo**: Exibição clara de SSID, Canal (com suporte a 3 dígitos sem cortes na tela, ex: `c149`), RSSI e criptografia.
- **Texto Deslizante (*Marquee*)**: Rola nomes de rede longos dinamicamente sem truncar caracteres.

### 2. Wi-Fi Deauthentication Attack
- **Injeção de Quadros 802.11**: Disparo maciço de quadros de desassociação e desautenticação forçando desconexão de clientes do Ponto de Acesso.
- **Suporte Multi-Alvo**: Permite marcar múltiplos APs com seleção individual `[X]`.
- **Animação Tática**: Efeito visual temático simulando rompimento de feixe de alta voltagem entre transmissor e cliente (`LINK: SEVERED`).
- **Canal Dinâmico**: O rádio sintoniza e cicla automaticamente o canal exato do AP atacado a cada pacote.

### 3. Wi-Fi Beacon Flooder Turbo (3 Modos)
Inunda o ambiente com dezenas ou centenas de redes falsas transmitidas simultaneamente em 2.4GHz e 5GHz:
1. **Modo Preset**: Emite listas temáticas pré-definidas (Rickroll, satírico, infraestrutura corporativa, hackers).
2. **Modo Clone / Copiar**: Clona automaticamente o SSID do AP escaneado e gera dezenas de redes idênticas.
   - *Bypass de Agrupamento*: Utiliza espaçamento invisível não-quebrável (NBSP) no final de cada SSID, forçando sistemas Android, iOS e Windows a listar **múltiplas redes separadas** em vez de agrupá-las em uma só.
3. **Modo Custom com Teclado On-Screen**: Permite digitar qualquer nome de rede personalizado diretamente no display.
- **Contador Ajustável**: Defina a quantidade exata de beacons desejada (50, 100, 300, 600, etc.) com ciclo contínuo sem travamentos ou buffer overflows (proteção contra o limite de 32 bytes do padrão 802.11).

### 4. Apple BLE Proximity Spam (EvilAppleJuice Port)
Port nativo e otimizado da suíte **EvilAppleJuice** para o rádio BLE 5.0 do chip RTL8720DN:
- Injeta pacotes de anúncio Bluetooth com o identificador de fabricante oficial da Apple (`Company ID: 0x004C`).
- Aciona pop-ups nativos e contínuos de pareamento em iPhones, iPads e MacBooks próximos.
- **Dispositivos & Payloads Emulados**:
  - `AirPods Pro 2`, `AirPods Max`, `AirPods (1ª/2ª/3ª Geração)`
  - `Beats Studio Buds`, `Powerbeats Pro`, `Beats Solo Pro`, `Beats Flex`, `Beats Studio 3`
  - `Apple TV Setup` & `Apple TV Pair`
  - `AirTag`, `Apple Watch`, `Transfer Number`, `Color Picker`, `Setup New Phone`
- Modo de rotação aleatória contínua para máxima evasão e impacto.

### 5. Simulador Web Integrado
O arquivo `simulator.html` incluso permite testar 100% da experiência da interface gráfica diretamente no navegador antes de gravar o firmware:
- Renderiza o display OLED pixel a pixel.
- D-Pad interativo clicável e mapeado no teclado do PC (setas ou `WASD` + `Enter`/`Espaço`).
- Validação completa de telas, menus, animações e layout.

---

## 🔌 Esquema Elétrico & Pinout

### Pinout do Módulo BW16
O firmware utiliza resistores internos de pull-up do RTL8720DN (`INPUT_PULLUP`). **Nenhum resistor externo é necessário**: basta ligar um lado de cada botão ao GPIO e o outro lado diretamente ao **GND**.

| Função | Pino GPIO | Pino Físico BW16 | Conexão Física | Configuração |
|:---|:---:|:---:|:---|:---|
| **OLED SCL** | `PA_25` | SCL | Pino SCL do Display OLED | I2C Clock (Endereço `0x3C`) |
| **OLED SDA** | `PA_26` | SDA | Pino SDA do Display OLED | I2C Data |
| **OLED VCC** | `3V3` | 3.3V | VCC do Display (3.3V Estável) | Alimentação |
| **OLED GND** | `GND` | GND | GND do Display | Terra Comum |
| **D-Pad UP (▲)** | `PA_12` | D12 | `PA_12` ──[ Botão Tátil ]── `GND` | Active-LOW (`INPUT_PULLUP`) |
| **D-Pad DOWN (▼)** | `PA_13` | D11 | `PA_13` ──[ Botão Tátil ]── `GND` | Active-LOW (`INPUT_PULLUP`) |
| **D-Pad LEFT (◀)** | `PA_15` | D9 | `PA_15` ──[ Botão Tátil ]── `GND` | Active-LOW (`INPUT_PULLUP`) |
| **D-Pad RIGHT (▶)** | `PA_27` | D2 | `PA_27` ──[ Botão Tátil ]── `GND` | Active-LOW (`INPUT_PULLUP`) |
| **D-Pad ENTER (●)** | `PA_14` | D10 | `PA_14` ──[ Botão Tátil ]── `GND` | Active-LOW (`INPUT_PULLUP`) |

### Diagrama de Conexões Físicas

```
                          MÓDULO REALTEK BW16 (RTL8720DN)
                            ┌────────────────────────┐
                            │ [ Antena Dual-Band PCB]│
                            │                        │
       [ OLED SH1106 ]      │ PA_25 (SCL) ───────────┼──> SCL (I2C)
     ┌─────────────────┐    │ PA_26 (SDA) ───────────┼──> SDA (I2C)
     │ VCC  GND SCL SDA│    │ 3V3         ───────────┼──> VCC (3.3V)
     └──┬────┬───┬───┬─┘    │ GND         ───────────┼──> GND Comum
        │    │   │   │      │                        │
        └────┼───┼───┼──────┴────────────────────────┘
             │   │   │
             │   └───┴──────────┐
             │                  │
             │       [ D-PAD DE 5 BOTÕES TÁTEIS ]
             │         ▲ [UP] PA_12
             │   ◀ [LEFT]   ● [ENTER]   ▶ [RIGHT]
             │     PA_15      PA_14       PA_27
             │         ▼ [DOWN] PA_13
             │
             └─────── [ Barramento de GND Comum para os Botões ]
```

---

## 🎮 Controles e Navegação

| Tela / Estado | Ações dos Botões |
|:---|:---|
| **Menu Principal** | `UP` / `DOWN` ou `LEFT` / `RIGHT`: Navega itens • `ENTER`: Seleciona |
| **Carrossel Wi-Fi** | `LEFT` / `RIGHT`: Desliza cards (DEAUTH, BEACON, SCAN) • `ENTER`: Confirma • `DOWN`: Volta |
| **Lista de APs** | `UP` / `DOWN`: Rola redes • `ENTER`: Marca/Desmarca `[X]` • `RIGHT`: Inicia ataque imediato • `LEFT`: Retorna |
| **Telas de Confirmação** | `LEFT`: `[ CANCEL ]` • `RIGHT`: `[ ATTACK ]` / `[ START ]` • `ENTER`: Executa |
| **Ataque em Execução** | **Qualquer botão (`ENTER`, `LEFT`, `DOWN`)**: *Kill-Switch* instantâneo, encerra transmissão |

### Teclado Virtual On-Screen (Phone-Style)

Projetado para telas compactas de 128x64 sem perder caracteres:

```
┌─────────────────────────────────┐
│ SSID: [ MyNetwork_            ] │  ← Preview com cursor piscante
├─────────────────────────────────┤
│ [a] [b] [c] [d] [e] [f] [g] [h] │  ← Grid tático 8 colunas x 4 linhas
│ [i] [j] [k] [l] [m] [n] [o] [p] │
│ [q] [r] [s] [t] [u] [v] [w] [x] │
│ [y] [z] [.] [-] [_] [ ] [ ] [ ] │
├─────────────────────────────────┤
│ [ABC] │  [SPACE]  │[DEL]│[ OK ] │  ← Barra de ações rápidas
└─────────────────────────────────┘
```

- **3 Páginas Rápidas**:
  - `Página 0 (abc)`: Letras minúsculas + pontuação base (`.`, `-`, `_`).
  - `Página 1 (ABC)`: Letras MAIÚSCULAS + pontuação base.
  - `Página 2 (123)`: Números `0-9` e símbolos especiais (`! @ # $ % & * ( ) - . , _ : ; ? / + =`).
- Pressione `UP` na primeira linha para alcançar a barra de ação (`[ABC/123/abc]`, `[SPACE]`, `[DEL]`, `[OK]`).

---

## 🚀 Guia de Compilação e Gravação

### 1. Configurar Arduino IDE
1. Baixe e instale a **Arduino IDE 2.x**.
2. Vá em **Arquivo > Preferências** e cole a URL do gerenciador de placas AmebaD em **URLs Adicionais**:
   ```
   https://github.com/ambiot/ambd_arduino/raw/master/Arduino_package/package_realtek.com_amebad_index.json
   ```
3. Abra **Ferramentas > Placa > Gerenciador de Placas**, busque por **AmebaD** e instale o pacote:
   - **Realtek Ameba Boards (32-bits ARM Cortex-M33 @ 200MHz)**.
4. Abra **Ferramentas > Gerenciar Bibliotecas** e instale:
   - **U8g2** (por *oliver*).
5. Selecione a placa:
   - **Ferramentas > Placa > AmebaD ARM > BW16 (RTL8720DN)**.
   - Selecione a Porta COM correspondente ao seu conversor USB-Serial.

### 2. Modo de Gravação (Burn Mode)
Para colocar o módulo BW16 em modo de gravação via serial (UART):
1. Mantenha pressionado o botão **BURN / DOWNLOAD** no módulo BW16.
2. Pressione rapidamente o botão **RESET (RST)**.
3. Solte o botão **BURN**. A placa entrará em modo de download UART.
4. Clique em **Upload / Carregar** na Arduino IDE.
5. Após o término da gravação (`Upload Image Done`), pressione **RESET** uma vez para iniciar o **R4BB1T HAVOC**.

---

## 📁 Estrutura do Repositório

```
RTL870dn-r4bb1t-havoc/
├── RTL8720dn-Deauther/             # Código-fonte principal do firmware
│   ├── RTL8720dn-Deauther.ino      # Ponto de entrada, setup() e loop()
│   ├── config.h                    # Mapeamento de pinos, offsets e timers
│   ├── debug.h                     # Macros de depuração serial
│   ├── ble_spam.h / .cpp           # Motor de BLE Proximity Spam (EvilAppleJuice)
│   ├── wifi_attacks.h / .cpp       # Injeção de Deauth e Beacon Flooder
│   ├── wifi_scanner.h / .cpp       # Scanner 2.4GHz e 5GHz com parsing
│   ├── ui_display.h / .cpp         # Renderização gráfica SH1106 / U8g2
│   ├── ui_state.h / .cpp           # Máquina de estados finitos e lógica D-Pad
│   ├── animations/                 # Sprites e animações táticas (bitmaps XBM)
│   └── src/packet-injection/       # Wrappers de baixo nível de injeção Wi-Fi
├── simulator.html                  # Simulador de display e D-Pad no navegador
├── LICENSE                         # Licença de código aberto (MIT)
└── README.md                       # Documentação técnica oficial
```

---

## ⚠️ Aviso Legal (Disclaimer)

> **ATENÇÃO:**  
> Este projeto foi desenvolvido exclusivamente para fins educacionais, testes de penetração autorizados e validação de robustez de redes e dispositivos de radiofrequência em laboratórios controlados.  
> O uso indevido deste software para atacar, interromper ou perturbar redes sem fio e dispositivos de terceiros sem autorização prévia e explícita é **ilegal** e sujeito a sanções civis e criminais sob as leis vigentes de telecomunicações e crimes cibernéticos.  
> O autor e os contribuidores **não se responsabilizam** pelo uso indevido das ferramentas aqui disponibilizadas.

---

<p align="center">
  <b>Desenvolvido por <a href="https://github.com/Mr4bb1t">r4bb1t</a></b><br>
  <i>Dual-Band Offensive & Defensive RF Research</i>
</p>
