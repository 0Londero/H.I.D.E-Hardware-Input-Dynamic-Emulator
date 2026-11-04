# H.I.D.E - **H**ardware **I**nput **D**ynamic **E**mulator

![Plataforma](https://img.shields.io/badge/plataforma-Raspberry%20Pi%20Pico%20RP2040-C51A4A)
![Firmware](https://img.shields.io/badge/firmware-C%2B%2B-f34b7d)
![Status](https://img.shields.io/badge/status-educacional-informational)

> Um pass-through de mouse USB com rapid-fire e recoil control que eu construí
> num Raspberry Pi Pico (RP2040), configurável pelo navegador.

> ⚠️ **Projeto educacional.** Use apenas localmente e em jogos offline. Não use
> em jogos online ou competitivos: viola os termos de serviço e é antiético.

## Sumário

- [O que é](#o-que-e)
- [Recursos](#recursos)
- [Arquitetura](#arquitetura)
- [Materiais](#materiais)
- [Montagem (solda)](#montagem-solda)
- [Firmware (compilar e gravar)](#firmware-compilar-e-gravar)
- [Site (configurar)](#site-configurar)
- [Controles pelo mouse](#controles-pelo-mouse-sem-o-site)
- [Polling rate do mouse](#polling-rate-do-mouse)
- [Estrutura do repositório](#estrutura-do-repositorio)
- [Solução de problemas](#solucao-de-problemas)
- [A utopia: Teensy 4.1](#a-utopia-teensy-41)
- [Versionamento](#versionamento)
- [Licença](#licenca)

## O que é

Eu queria aprender mais sobre Arduino, solda, microcontroladores e USB, então
construí o H.I.D.E: um dispositivo que fica "entre" o meu mouse e o PC. Ele lê
o mouse real, aplica transformações (rapid-fire e compensação de recoil com
movimento natural) e envia o resultado ao PC como um mouse USB comum, sem
instalar nada no computador.

## Recursos

- **Pass-through** de mouse sem software no PC.
- **Rapid-fire** com timing humanizado (Gaussiana, random walk, jitter).
- **Recoil control** com curvas suavizadas (Bézier, smoothstep, ruído 1/f).
- **Controle físico** pelo próprio mouse (botão do meio + scroll).
- **Configuração** pelo navegador via Web Serial, com perfis salvos em JSON.

## Arquitetura

```
Mouse físico (com fio ou dongle 2.4 GHz)
      |
      |  USB host (PIO-USB): D+ = GP0, D- = GP1
      v
Raspberry Pi Pico (RP2040)
      |
      |  Core 1: lê o mouse e enfileira (fila SPSC)
      v
      Core 0: rapid-fire + recoil + jitter
      |
      |  USB device (mouse HID)
      v
     PC

Site (Web Serial)  <--  serial CDC  -->  Pico
```

- **Core 1**: roda o USB host (PIO-USB), lê o mouse e enfileira os relatórios.
- **Core 0**: aplica os algoritmos e envia os relatórios HID finais ao PC.
- O site conversa com o Pico por uma porta serial (CDC), trocando JSON.

## Materiais

A peça **recomendada** e a que **eu usei** (as duas funcionam):

| Componente | Recomendado | O que eu usei |
|---|---|---|
| Microcontrolador | Raspberry Pi Pico (RP2040) | mesma peça |
| Conector do mouse | breakout USB-A fêmea (DIP, 4 pinos) | mesma peça |
| Resistores de dados | 22 Ω (1/4 W) | 33 Ω (1/4 W, 5%) |
| Cabo do Pico | micro-USB (dados) | Cabo micro-USB 2.0 |
| Mouse | qualquer mouse HID (com fio ou dongle 2.4 GHz) | Razer Orochi V2 (sem fio) |

Notas:

- O mouse pode ser **qualquer um** que conecte por USB: com fio ou sem fio via
  dongle 2.4 GHz. Eu uso o Razer Orochi V2 (sem fio) para mostrar que isso
  também funciona.
- No Orochi V2, configure o polling em **500 Hz** no Razer Synapse.
- 33 Ω funciona no lugar dos 22 Ω recomendados; com fios curtos é tranquilo.

## Montagem (solda)

Ligação do conector USB-A fêmea ao Pico:

| USB-A fêmea | Pico | Detalhe |
|---|---|---|
| VBUS (5 V) | VBUS (pino 40) | nunca no 3V3 |
| D- | GP1 (pino 2) | via resistor |
| D+ | GP0 (pino 1) | via resistor |
| GND | GND (pino 3) | |

Passo a passo resumido:

1. Meça o conector com multímetro (não confie só no silk screen).
2. Solde GND, depois VBUS, depois D+/D- com os resistores.
3. Torça e encurte os fios de dados (menos de 10 cm).
4. Teste 5 V e ausência de curto antes de plugar o dongle.

Avisos: nunca solde alimentado; nunca inverta 5V/GND; nunca ligue VBUS no 3V3.

## Firmware (compilar e gravar)

1. Arduino IDE com o core **Raspberry Pi Pico/RP2040** (Earle Philhower).
2. Bibliotecas: **Adafruit TinyUSB**, **Pico-PIO-USB** (sekigon-gps), **ArduinoJson v6**.
3. Placa "Raspberry Pi Pico"; *Tools → USB Stack* = "Adafruit TinyUSB" e
   *Tools → CPU Speed* = 240 MHz.
4. Abra `firmware/firmware.ino` e compile/envie.

## Site (configurar)

1. Abra `html/index.html` no Chrome ou Edge.
2. Conecte o Pico e clique em "Conectar dispositivo"; escolha a porta do Pico.
3. Ajuste os valores, salve perfis e "Envie ao Pico".

## Controles pelo mouse (sem o site)

- **Botão do meio por 2 s**: liga/desliga o fast-fire.
- **Botão do meio + scroll**: ajusta a velocidade (CPS).
- **LED**: pisca na velocidade durante o ajuste; aceso = ligado, apagado = desligado.

## Polling rate do mouse

O polling rate (125 / 500 / 1000 Hz) é quantas vezes por segundo o mouse
reporta ao PC. O "mouse virtual" do Pico envia ao PC a cada 2 ms, então ele
entrega no máximo cerca de 500 relatórios por segundo.

- **125 Hz** (a cada 8 ms): sobra folga, sem perda.
- **500 Hz** (a cada 2 ms): a entrada casa com a saída. É o ideal no Pico.
- **1000 Hz** (a cada 1 ms): entram 1000 relatórios/s, mas o Pico só entrega
  cerca de 500/s. A fila enche e parte do movimento é descartada. Funciona, mas
  sem ganho e com perda de suavidade.

Por isso o guia recomenda **500 Hz** no Razer Synapse. Em uma placa com USB
high-speed (como a Teensy 4.1), dá para configurar o device para 1 ms e usar
1000 Hz (ou até 8 kHz) sem descarte.

## Estrutura do repositório

```
firmware/      firmware do Pico (firmware.ino + firmware.cpp)
html/          site (index.html)
css/           estilos (style.css)
js/            lógica do site (app.js)
readmeassets/  imagens e GIFs usados neste README
scripts/       scripts auxiliares
```

## Solução de problemas

- **Cursor não mexe**: verifique o boot protocol do dongle (o firmware lê apenas
  boot protocol) e a polaridade de D+/D-.
- **Pico não aparece no PC**: o cabo pode ser "charge-only" (só carga). Segure
  BOOTSEL e veja se aparece a unidade `RPI-RP2`.
- **Scroll (roda) não ajusta o CPS**: em boot protocol o mouse pode enviar só 3
  bytes (botões, X, Y), sem o wheel. Se o ajuste por scroll não responder, o
  dongle não está enviando o 4º byte; nesse caso o ajuste funciona só pelo site.

## A utopia: Teensy 4.1

<img align="right" src="readmeassets/oL.gif" width="180" alt="custo">

Quando eu comecei, queria usar uma **Teensy 4.1** (NXP i.MX RT1062). Todo mundo
que eu consultei disse que ela era a placa ideal para esse tipo de projeto. Ela
é mesmo a mais próxima da "placa perfeita", mas o preço...

Ela custa uns R$ 550 a R$ 880 importada (não tem revenda no Brasil), enquanto o
Pico oficial sai por R$ 36 a R$ 40 (e clones por R$ 19 a R$ 25). Ou seja, a
Teensy sai umas 18 vezes mais cara, e a diferença é quase toda imposto e frete:
US$ 31,50 contra US$ 4 é só 8x de tabela.

### O que seria a placa ideal

O ideal para um passthrough USB seria: dois controladores OTG independentes,
VBUS controlável por software (para resetar o dongle sem desconectar), CPU
rápida com FPU, DMA no USB, EEPROM real, clock estável, bootloader
drag-and-drop e uma biblioteca de USB host madura. Nenhuma placa barata junta
tudo isso.

Um detalhe importante: "dual-core Cortex-M7 a 400+ MHz" não existe em placa
maker acessível. Chips com dois núcleos Cortex-M7 são raros (o i.MX RT1170, por
exemplo, usa M7 + M4) e caros. A placa "utópica" com esse preço é aspiracional,
não comprável.

### Comparativo

| Critério | Pico (RP2040) | Teensy 4.1 | "Utópica" |
|---|---|---|---|
| 2 portas USB (host + device) | parcial (device nativo + host via PIO) | sim (2 controladores) | sim (2 OTG) |
| CPU | 2x Cortex-M0+ (133 MHz) | 1x Cortex-M7 (600 MHz) | 2x Cortex-M7 (400+ MHz) |
| FPU hardware | não | sim (single + double) | sim (single + double) |
| Dual-core | sim | não | sim |
| DMA no USB | parcial (device) | sim | sim |
| RAM | 264 KB | 1 MB | 512 KB+ |
| EEPROM real | não (emulada) | sim (4 KB) | sim |
| Biblioteca USB host | Pico-PIO-USB + TinyUSB | USBHost_t36 | madura |
| VBUS controlável | não | não | sim |
| Preço no Brasil | ~R$ 19-40 | ~R$ 550-880 | não existe |
| Veredito | melhor custo/benefício | mais próximo do ideal | aspiracional |

### E além da Teensy?

Nenhuma placa barata atende a lista "utópica" inteira. Quem chega perto em algum
critério:

- **i.MX RT1170** (M7 1 GHz + M4, 2 OTG, 2 MB RAM): mais rápido e dual-core,
  mas custa US$ 200, não tem flash interna e é placa de evaluation.
- **STM32H747** (M7 480 + M4 240, 2 OTG): dual-core por ~US$ 30 numa Nucleo, mas
  o host HID é menos turnkey e não tem EEPROM.
- **RP2350 (Pico 2)**: barato, mas tem 1 USB só (host via PIO), então não serve.

Ou seja, a Teensy 4.1 continua sendo o teto prático para passthrough HID. O que
falta nela (VBUS controlável por software) se resolve com um load switch e um
sensor de corrente externos, não trocando de placa.

Conclusão: para aprender e prototipar, o Pico é a escolha racional (e até mais
didática, pois expõe USB, PIO e concorrência). A Teensy 4.1 seria o upgrade sem
atrito para caso de uso high-speed de verdade ou produto robusto, mas, pelo
preço, eu fico com o Pico.

## Versionamento

O projeto usa [Versionamento Semântico](https://semver.org/lang/pt-BR/). A versão
atual é **1.0.0**, e as mudanças de cada versão são registradas no
[`CHANGELOG.md`](CHANGELOG.md).

## Licença

Este projeto está licenciado sob a [Licença MIT](LICENSE).

© 2026 Otávio Londero.
