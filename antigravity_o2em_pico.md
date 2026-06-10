# Projeto: Portabilidade do `libretro-o2em` para Raspberry Pi Pico (RP2040)

## Objetivo Principal
Este documento orienta o agente (Antigravity/AI) na tarefa de realizar um "port" do código fonte do emulador `libretro-o2em` (escrito originalmente em C para a API Libretro) para o **Raspberry Pi Pico C/C++ SDK** (bare-metal). O objetivo final é criar o projeto "Odyssey on Pico".

## Tarefas de Refatoração

### 1. Remoção da Camada Libretro
* **Ação:** Remover todas as referências ao arquivo `libretro.c` e ao cabeçalho `libretro.h`.
* **Ação:** Criar um novo arquivo `main.c` que conterá a função `main()` nativa do Pico SDK. Este arquivo deverá inicializar os periféricos de hardware, montar a ROM (via array flash ou SD Card) e iniciar o loop principal da emulação.

### 2. Mapeamento de Hardware: Saída de Vídeo
O Odyssey² usa o chip VDC (Intel 8245) para gerar vídeo.
* **Libretro Original:** O core preenche um *framebuffer* em memória (ex: `retro_video_refresh_t`) com cores ARGB/RGB565.
* **Nova Implementação (Pico):** O emulador deve enviar o sinal de vídeo em formato analógico (Vídeo Composto/RCA Amarelo).
* **Hardware Alvo:** DAC passivo de resistores (1KΩ, 470Ω, 220Ω).
* **Pinos:** `GPIO20`, `GPIO21`, `GPIO22`.
* **Diretriz de Código:** Injetar uma biblioteca de geração de vídeo composto baseada no PIO (Programmable I/O) do RP2040, convertendo a paleta do VDC para sinais Luma/Chroma que os módulos PIO compreendam. 

### 3. Mapeamento de Hardware: Saída de Áudio
O Odyssey² original é Mono, e a extensão "The Voice" adiciona fala.
* **Libretro Original:** Envia batches de samples 16-bit para o frontend via `retro_audio_sample_batch_t`.
* **Nova Implementação (Pico):** Converter a geração de samples para modulação de largura de pulso (PWM) suportada pelo RP2040, criando um pseudo-estéreo (Dual-Mono).
* **Hardware Alvo:** Filtro passa-baixa RC (Resistor-Capacitor).
* **Pinos:** `GPIO27` (Canal Direito RCA Vermelho), `GPIO28` (Canal Esquerdo RCA Branco).
* **Diretriz de Código:** Configurar o `hardware_pwm` do Pico SDK. Criar uma interrupção de timer (ou usar DMA) para alimentar os valores dos samples gerados pelo core O2EM diretamente nos *slices* PWM (`pwm_set_chan_level`).

### 4. Mapeamento de Hardware: Joysticks (Entrada P1 e P2)
* **Libretro Original:** Usa `retro_input_state_t` para ler mapeamentos abstratos de controle (RetroPad).
* **Nova Implementação (Pico):** Leitura de hardware serial via Shift Registers.
* **Hardware Alvo:** 2x CIs 74HC165 ligados em cascata (Daisy Chain).
* **Pinos:** * `GPIO10` (Clock)
    * `GPIO11` (Latch / Load)
    * `GPIO12` (Data In - Q7 do primeiro CI)
* **Diretriz de Código:** Substituir a polling de entrada por uma rotina GPIO padrão que executa o "Latch", envia 16 pulsos de clock e lê os 16 bits vindos de `GPIO12` (D0-D4 para P1 e D0-D4 para P2).

### 5. Mapeamento de Hardware: Teclado Alfanumérico
O console original conta com um teclado matricial (6x8).
* **Libretro Original:** Captura os callbacks de teclado padrão do host.
* **Nova Implementação (Pico):** Leitura via expansor I2C.
* **Hardware Alvo:** CI MCP23017.
* **Pinos I2C:**
    * `GPIO4` (SDA)
    * `GPIO5` (SCL)
* **Diretriz de Código:** Configurar o `hardware_i2c` do Pico SDK (`i2c0` ou `i2c1`). O core deve enviar bytes de configuração I2C para habilitar pull-ups internos e varrer os bancos GPA (6 pinos) e GPB (8 pinos) para identificar as teclas pressionadas, traduzindo as coordenadas (Linha x Coluna) para o mapa de teclado do Intel 8048.

### 6. Relógio e Sincronização (Timing)
O microcontrolador Intel 8048 no O2 roda a uma fração específica de clock.
* **Ação:** Garantir que o loop principal da CPU no `main.c` do Pico faça o *throttling* correto, utilizando alarmes de timer (`add_repeating_timer_us`) do Pico SDK para manter a execução travada nos quadros nativos (NTSC ~60Hz / PAL ~50Hz).
