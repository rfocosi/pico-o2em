# Contexto de Desenvolvimento: Odyssey on Pico (Port Libretro-O2EM para Pico SDK)

Este documento estabelece as especificações de hardware e as diretrizes de engenharia de software para o port do emulador `libretro-o2em` rodando nativamente no microcontrolador Raspberry Pi Pico através do Pico SDK (C/C++). Use estas especificações para reestruturar os subsistemas de I/O, áudio, vídeo e leitura de mídias.

---

## 1. Arquitetura de Mapeamento de Hardware (Pinout Geral)

| Pino do Raspberry Pi Pico | Componente / Periférico | Função Técnica |
| :--- | :--- | :--- |
| **Pino 36 (3V3 OUT)** | VCC de todos os CIs (74HC165, MCP23017, 74HC595) | Alimentação lógica geral (3.3V) [Adicionado fora das fontes] |
| **Pino 40 (VBUS)** | Slot de Cartucho (Pino D) | Alimentação de +5V para barramento do cartucho [Adicionado fora das fontes] |
| **Pinos GND (Vários)** | Malhas RCA, terras de chips, joysticks e cartucho | Referência de terra comum do sistema [Adicionado fora das fontes] |
| **GPIO 28 (Pino 34)** | Filtro RC -> RCA Branco | Saída de Áudio: Canal Esquerdo (Simulado Mono/Estéreo) [Adicionado fora das fontes] |
| **GPIO 27 (Pino 32)** | Filtro RC -> RCA Vermelho | Saída de Áudio: Canal Direito (Simulado Mono/Estéreo) [Adicionado fora das fontes] |
| **GPIO 20 (Pino 26)** | Resistor 1KΩ -> RCA Amarelo | DAC de Vídeo Composto (Luma/Sync Baixa) [Adicionado fora das fontes] |
| **GPIO 21 (Pino 27)** | Resistor 470Ω -> RCA Amarelo | DAC de Vídeo Composto (Luma/Sync Média) [Adicionado fora das fontes] |
| **GPIO 22 (Pino 29)** | Resistor 220Ω -> RCA Amarelo | DAC de Vídeo Composto (Luma/Sync Alta) [Adicionado fora das fontes] |
| **GPIO 10 (Pino 14)** | Pino 2 de ambos os CIs 74HC165 | Barramento dos Joysticks: Sinal de Clock [Adicionado fora das fontes] |
| **GPIO 11 (Pino 15)** | Pino 1 de ambos os CIs 74HC165 | Barramento dos Joysticks: Sinal de Latch/Load [Adicionado fora das fontes] |
| **GPIO 12 (Pino 16)** | Pino 9 (Q7) do primeiro 74HC165 | Barramento dos Joysticks: Entrada Serial de Dados [Adicionado fora das fontes] |
| **GPIO 4 (Pino 6)** | Pino 13 (SDA) do MCP23017 | Teclado Matriz: I2C Data (Pull-up externo de 4.7KΩ) [Adicionado fora das fontes] |
| **GPIO 5 (Pino 7)** | Pino 12 (SCL) do MCP23017 | Teclado Matriz: I2C Clock (Pull-up externo de 4.7KΩ) [Adicionado fora das fontes] |
| **GPIO 14 (Pino 19)** | Pino 14 (SER) do primeiro 74HC595 | Dumper de Cartucho: Entrada Serial de Endereço [Adicionado fora das fontes] |
| **GPIO 15 (Pino 20)** | Pino 11 (SRCLK) de ambos os 74HC595 | Dumper de Cartucho: Shift Register Clock [Adicionado fora das fontes] |
| **GPIO 16 (Pino 21)** | Pino 12 (RCLK) de ambos os 74HC595 | Dumper de Cartucho: Storage Register/Latch Clock [Adicionado fora das fontes] |
| **GPIO 17 (Pino 22)** | Pino F do Slot do Cartucho | Dumper de Cartucho: Sinal ~PSEN (Chip Enable ativo em Low) [Adicionado fora das fontes] |
| **GPIO 18 (Pino 24)** | Pino 12 do Slot do Cartucho | Dumper de Cartucho: Sinal P11 (Seletor de Banco Alto) [Adicionado fora das fontes] |
| **GPIO 19 (Pino 25)** | Pino 13 do Slot do Cartucho | Dumper de Cartucho: Sinal P10 (Seletor de Banco Baixo) [Adicionado fora das fontes] |
| **GPIO 6 a 13** | Pinos 2 a 9 do Slot do Cartucho | Dumper de Cartucho: Leitura direta do Barramento de Dados (DB0-DB7) [Adicionado fora das fontes] |

---

## 2. Subsistemas e Diretrizes de Implementação de Software

### 2.1. Subsistema de Vídeo Composto (Módulos PIO)
* **Objetivo:** Substituir a engine de renderização de vídeo Libretro (que gera framebuffers pesados) para usar as máquinas de estado programáveis (PIO) do RP2040.
* **Lógica de Hardware:** Três pinos digitais agregados por resistores funcionam como um DAC passivo de 3 bits, gerando os níveis de tensão elétrica exatos para o sinal de TV (Sincronismo, Black Level e níveis de Luminância) [Adicionado fora das fontes].
* **Ação no Código:** Abstraia as rotinas do chip de vídeo original (VDC) do Odyssey² (registros `$A0` a `$A3` e memória de objetos) diretamente para temporizações PIO [Adicionado fora das fontes]. Não envie a saída para `retro_video_refresh_cb`; direcione os dados diretamente para os buffers de DMA associados às State Machines da PIO de vídeo.

### 2.2. Subsistema de Áudio (PWM Slices)
* **Objetivo:** Modular o som analógico sem usar codecs externos.
* **Lógica de Hardware:** O som gerado por modulação por largura de pulso (PWM) nos pinos GPIO 27 e 28 passa por um filtro passá-baixa RC externo conectado a um capacitor de bloqueio DC (eletrolítico de 47µF) para remover o offset contínuo de 3.3V antes de entrar na TV [Adicionado fora das fontes].
* **Ação no Código:** O áudio original do Odyssey² gerado pelo registrador de deslocamento de 24 bits (`$A7`-`A9`) controlado pelo registro `$AA` [Adicionado fora das fontes] deve ser convertido em frequências e ciclo de trabalho (duty cycle) configurados nos recursos de hardware de PWM do Pico SDK via interrupções (`irq`), descartando o backend do Libretro correspondente.

### 2.3. Subsistema de Joysticks (74HC165 Shift In)
* **Objetivo:** Ler os dois controles originais de 8 direções + ação utilizando apenas 3 pinos.
* **Lógica de Hardware:** Dois CIs 74HC165 são conectados em cascata (*daisy-chain*). O pino serial out `Q7` (pino 9) do segundo chip conecta-se à entrada serial `DS` (pino 10) do primeiro chip [Adicionado fora das fontes]. Puxados por resistores pull-up externos de 10KΩ, os botões fecham curto com o GND ao serem pressionados [Adicionado fora das fontes].
* **Ação no Código:** Implemente uma rotina cíclica que envia um pulso de Latch (para congelar o estado dos botões) e dispara 16 pulsos de clock para capturar em uma variável única (`uint16_t`) o estado de ambos os jogadores. Substitua as leituras internas da CPU que buscam o joystick via registradores do processador Intel 8048 por essa variável filtrada.

### 2.4. Subsistema do Teclado Alfa-Numérico (MCP23017 via I2C)
* **Objetivo:** Escanear a matriz de membrana física de 6 linhas por 8 colunas típica do Odyssey² [Adicionado fora das fontes].
* **Lógica de Hardware:** O chip expansor MCP23017 monitora as intersecções através das portas GPA0-GPA5 (linhas) e GPB0-GPB7 (colunas) [Adicionado fora das fontes].
* **Ação no Código:** Inicialize o barramento I2C nativo do Pico SDK nos pinos selecionados. Configure o MCP23017 via comandos I2C para habilitar os resistores pull-up internos e implemente a rotina de varredura sequencial. Mapeie os resultados na matriz de retorno que responde ao emulador na emulação do circuito original (pino `P24` para indicação de clique e `P25`-`P27` para identificação da coluna correspondente) [Adicionado fora das fontes].

### 2.5. Subsistema de Mídia (Dump & Play de Cartucho Físico)
* **Objetivo:** Ler cartuchos físicos originais (memórias ROM de 2K, 4K ou 8K) no momento da inicialização [Adicionado fora das fontes].
* **Lógica de Hardware:** O barramento de endereçamento expandido do cartucho (`A0`-`A11`) é controlado sequencialmente através de dois registradores de deslocamento 74HC595 em cascata [Adicionado fora das fontes]. O barramento de dados (`DB0`-`DB7`) envia as informações de volta diretamente para as portas lógicas configuradas de GPIO 6 a 13 do microcontrolador [Adicionado fora das fontes].
* **Ação no Código (Fluxo de Execução):**
  1. No escopo inicializador do firmware (`retro_init` / `main`), execute uma rotina prioritária de dumping.
  2. A rotina deve chavear os pinos seletores de banco (`P10`/`P11`) [Adicionado fora das fontes] e iterar de forma síncrona por todos os endereços físicos disponíveis no cartucho.
  3. O sinal `~PSEN` deve ser jogado em nível baixo temporariamente a cada leitura para habilitar a saída de dados da ROM do cartucho [Adicionado fora das fontes].
  4. Salve a totalidade dos bytes lidos no array estático global `rom_buffer[8102]`.
  5. Interrompa o subsistema de leitura física e inicialize o core da CPU apontando o ponteiro de execução do cartucho para a memória RAM interna populada, evitando lags ou acessos de barramento concorrentes com os loops de renderização de vídeo do PIO.

---

## 3. Diretiva Geral para a Geração de Código
Ao gerar os blocos de código para este projeto, utilize funções exclusivas do **Pico SDK padrão** (`hardware/pio.h`, `hardware/pwm.h`, `hardware/i2c.h`, `hardware/gpio.h`). Ignore ou elimine dependências de drivers de sistema operacional, bibliotecas POSIX ou alocações dinâmicas complexas incompatíveis com sistemas embarcados bare-metal de memória restrita.