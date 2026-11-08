# TODO / Roadmap

> Plano de evolução do projeto, organizado por fases e áreas. Cada item traz a
> viabilidade, o esforço e uma nota de "como fazer". Nada aqui é necessário para o
> funcionamento básico; são estudos e melhorias.

## Prioridades (recomendação)

### Onda 1: agora, sem Tauri (alto valor, baixo custo)
Design system (tokens), Rapid-fire Burst, Aplicar vs Salvar, barra de status,
live log e modo demo. Deixam o site com cara de app profissional sem adicionar
dependência nova.

### Onda 2: depois (médio/alto)
Telemetria do mouse + visualização ao vivo, Anti-recoil base/max/ramp, flashing de
firmware (UF2), importar/exportar config e perfis com cor própria.

### Onda 3: distribuição (se desejar)
Migração para Tauri 2 (app desktop). É o maior salto (Rust + Vite + TypeScript),
mas o firmware não muda e dá para adiar sem perder nada do design.

---

## 1. Interface / Design system

### 1.1 Tokens semânticos (do documento)
- Adicionar tokens: --surface, --surface-raised, --line-control, --text-secondary,
  --success, --warning, --danger e os fundos *-soft.
- Motivo: corrige contraste (o --text-faint e o --field atuais falham em
  acessibilidade) e separa "o que é" (ridge/bronze) de "para que serve"
  (success/warning/danger).
- Como fazer: criar css/tokens.css com as variáveis da seção 3.2 do documento e
  trocar as cores fixas por tokens semânticos. Nenhum estado pode depender só de
  cor (usar texto ou ícone junto).
- Esforço: baixo. Viabilidade: alta.

### 1.2 Layout de app (sidebar + cabeçalho + barra de status)
- Estrutura da seção 4 do documento: sidebar por grupos (uso diário, dispositivo,
  manutenção, app), cabeçalho com título + pill de estado e barra de status fixa.
- Estado em três níveis: barra de status -> pill do cabeçalho -> painel.
- Como fazer: adaptar o index.html atual para esse esqueleto. Fontes IBM Plex
  (Sans, Sans Condensed, Mono) via Fontsource (offline), não Google Fonts.
- Esforço: médio. Viabilidade: alta.

### 1.3 Componentes
- Botão principal preenchido com --accent (no máximo um por grupo), secundário com
  borda --line-control, perigo com --danger + confirmação.
- Controle segmentado (ex.: OFF | CONT | BURST), pills de estado com fundo *-soft.
- Campos com borda --line-control e foco --focus.
- Esforço: médio. Viabilidade: alta.

---

## 2. Features de firmware (mouse)

### 2.1 Rapid-fire Burst
- Modos Off | Cont | Burst. Parâmetros: shots/s, pulse width, burst rounds,
  burst delay e burst cooldown.
- Como fazer: no firmware, adicionar estado de burst (contador de rounds + timer de
  cooldown); na interface, controle segmentado. Reaproveita o timing orgânico atual.
- Esforço: médio. Viabilidade: alta.

### 2.2 Anti-recoil base/max/ramp
- Em vez de counts/s fixo: base offset, max offset e ramp time (o recuo cresce com
  o tempo de rajada até o máximo).
- Como fazer: no applyRecoilControl, multiplicar a base por um fator que cresce de
  1 até max/base ao longo do ramp time. Preview da curva na interface.
- Esforço: médio-alto. Viabilidade: viável (aproveita as camadas organic).

### 2.3 Aplicar vs Salvar (importante)
- Aplicar muda a RAM e vale na hora; EEPROM.commit() só ao salvar.
- Motivo: escrita em flash pausa o código e pode dar engasgo no mouse.
- Como fazer: já existe settingsDirty e save no release; separar os botões
  "Aplicar" e "Salvar no Pico" e a barra "Alterações não salvas".
- Esforço: baixo. Viabilidade: alta.

---

## 3. Site / UX

### 3.1 Barra de status + pills de estado
- Conexão, firmware, protocolo e estado em pills (estado em três níveis).
- Esforço: baixo-médio. Viabilidade: alta.

### 3.2 Live log / Recent Activity
- Log com nível (INFO/SUCCESS/WARNING/ERROR), horário e auto-scroll (pausa se o
  usuário rolar para cima).
- Esforço: baixo. Viabilidade: alta.

### 3.3 Importar/exportar config (JSON)
- Já existe load/save; adicionar exportar a config para JSON e importar de volta.
- Esforço: baixo. Viabilidade: alta.

### 3.4 Perfil com cor própria
- Cada perfil define seu --accent, deixando claro qual está em uso.
- Esforço: baixo. Viabilidade: alta.

### 3.5 Paleta de comandos (Ctrl+K)
- Encontrar qualquer ajuste ou ação sem navegar pelos menus.
- Esforço: médio. Viabilidade: viável depois.

### 3.6 i18n (português e inglês)
- Separar os textos desde já (ex.: i18next) para não ter retrabalho.
- Esforço: médio. Viabilidade: viável depois.

### 3.7 Conversor de fire-rate (delay em ms)
- Aceitar "delay entre tiros" em ms além de RPM (ex.: 150 ms = ~6.7 CPS).
- Esforço: baixo. Viabilidade: alta.

---

## 4. Telemetria e visualização ao vivo (mouse)
- Firmware enviar telemetria compacta (linha curta, nao JSON) a ~60 Hz pela serial
  CDC: botoes pressionados, dx/dy apos rapid-fire/recoil, scroll e taxa de
  relatorios. Nao usar ArduinoJson para isso; manter o repasse em primeiro lugar
  e nunca bloquear o mouse (guard com availableForWrite).
- Multiplexar telemetria e config na mesma CDC com prefixo (ex.: linha "T:..." para
  telemetria, JSON para config); so enviar quando a porta estiver aberta.
- Site: camada de dispositivo le a telemetria e desenha o mouse ao vivo em Canvas
  (botoes acendendo, rastro de movimento, roda de scroll), com metricas (taxa,
  dx, dy, botoes).
- Opcional: preview do padrao de recoil (curva base/max/ramp) ao lado dos campos,
  para ver o movimento antes de aplicar.
- Esforço: alto (exige o firmware transmitir dados). Viabilidade: média.

---

## 5. Firmware: ferramentas

### 5.1 Flashing de firmware (UF2)
- Validar UF2 (família RP2040 0xE48BFF56), entrar em BOOTSEL, detectar a unidade
  RPI-RP2 e copiar o arquivo.
- Como fazer: comando reset_usb_boot() no firmware; no app, ler o .uf2, validar e
  copiar para o volume.
- Esforço: médio. Viabilidade: viável.

### 5.2 Detecção dinâmica de hardware (futuro)
- Adicionar comando info ao firmware para responder chip ID, tamanho de flash,
  etc. via Web Serial, e exibir as specs reais no painel de informações da
  interface (hoje as specs do RP2040 são fixas no código).
- Esforço: baixo-médio. Viabilidade: alta.

---

## 6. Biometria comportamental (realismo)
- Correlação entre clique e movimento (hoje são sorteados de forma independente).
- Perfil idiossincrático por usuário (cada pessoa tem um padrão estável e único).
- Ruído dependente do sinal (jitter que escala com a velocidade do movimento).
- Variabilidade entre sessões.
- Esforço: médio-alto. Viabilidade: viável.

---

## 7. Fechar a malha (feedback / closed-loop)
- Hoje o recoil é open-loop: aplica um padrão fixo sem "ver" o alvo.
- Estudar como dar feedback ao firmware (ex.: rastrear o cursor ou ler a tela),
  para a correção reagir ao que acontece, como um humano.
- Esforço: alto. Viabilidade: difícil (limite estrutural).

---

## 8. Spoofing de dispositivo USB (identidade)
- Copiar VID/PID e strings do mouse real (ex.: Razer Orochi V2) no firmware, para
  o PC nao distinguir o Pico do mouse original.
- Copiar o report descriptor HID exato do mouse (via lsusb -v no Linux, ou USBView
  / usbhid-dump no Windows) e ajustar o MouseReport para casar.
- Lembrete: isso fica no firmware (os descriptors sao enviados na enumeracao), nao
  no driver do PC.
- Esforço: médio. Viabilidade: viável (área cinzenta; usar só para estudo).

---

## 9. Suporte a "report protocol" no USB host
- Hoje o firmware so le relatorios em "boot protocol". Se um dongle nao aceitar
  boot protocol, implementar a leitura do report protocol (parse do descriptor HID).
- Esforço: alto. Viabilidade: viável se o dongle exigir.

## 9.5 Mouses Razer (8 bytes) não funcionam no PIO-USB
- Mouses Razer (Orochi V2, Basilisk) enviam relatórios de 8 bytes em boot
  protocol e, após alguns segundos (centenas de relatórios), param de responder
  ao host PIO-USB (timeout na recepção, depois desconexão). Mouse genérico de
  3 bytes funciona normalmente.
- Diagnóstico feito (instrumentação do resultado da transferência): a falha é
  FAILED por timeout (nenhum byte recebido), não STALL, não CRC/enquadramento.
  O dispositivo simplesmente para de responder.
- Testado sem sucesso: PR #206, PR #211 (realinhamento de 1 bit), report
  protocol, debounce SE0. Não há correção conhecida.
- Conclusão: não funciona. Usar mouse de 3 bytes.
- Esforço: alto. Viabilidade: sem correção conhecida no momento.

---

## 10. Ajustes de qualidade pendentes
- Polling de 1000 Hz: intervalo do HID device para 1 ms (hoje e 2 ms).
- Fitts: implementar a lei de verdade (T = a + b*log2(2D/W)) ou manter o nome
  "desaceleracao progressiva".
- Limites superiores na validacao do JSON (alguns campos ainda aceitam extremos).
- Sentinela == 0 no rollover de millis (janela de 1 ms a cada ~49 dias).
- Stack: se houver instabilidade, definir bool core1_separate_stack = true; no core
  do Arduino-Pico (aumenta a stack de 4 KB para 8 KB por core).
- Esforço: baixo-médio. Viabilidade: alta.

---

## 11. Recoil control (melhorias)
- Resgatar o campo "Atraso de atualizacao" (updateDelay), hoje morto no firmware:
  a ideia era sincronizar a compensacao com o fire-rate da arma, para ela "pulsar"
  junto com os tiros em vez de descer continuamente (util para armas
  semiautomaticas lentas).
- Alternativa: compensacao em "pulsos" no ritmo do fire-rate, em vez de fluxo
  continuo.
- Esforço: médio. Viabilidade: viável.

---

## 12. Tauri desktop app (migração)
- Decisão: Tauri 2. O firmware continua igual; a interface fala com a porta serial
  por um plugin (Web Serial não funciona na WebView do Tauri).
- Camada de dispositivo com a mesma interface: TauriSerialDevice, WebSerialDevice e
  SimulatedDevice. O restante do app não sabe qual usa.
- Fases do documento: Fase 0 (contrato com o firmware) -> Fase 1 (empacotar e provar
  a comunicação) -> Fase 2 (tema) -> Fase 3 (organizar código) -> Fase 4 (camada de
  dispositivo) -> Fase 5 (telas e animações) -> Fase 6 (publicar).
- Pontos de atenção: EEPROM.commit() só ao salvar; telemetria sem JSON; repasse em
  primeiro lugar; detectar o Pico pela porta (Tauri) vs port picker (navegador);
  fixar a versão do plugin serialplugin e testar no Windows 11; navegador embutido
  varia por SO (WebKitGTK no Linux).
- Esforço: alto. Viabilidade: viável, mas é o maior salto (Rust + Vite + TypeScript).

---

## 13. Teste físico (quando o Pico chegar)
- Gravar o sketch de teste (firmware/teste_mouse/teste_mouse.ino) para validar o
  boot protocol do mouse.
- Testar o fast-fire com o mouse parado (deve funcionar apos a refatoracao).
- Testar o ajuste de CPS pelo scroll (depende do mouse enviar o 4o byte).
