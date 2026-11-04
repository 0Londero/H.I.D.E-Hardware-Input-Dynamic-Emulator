# TODO / Estudos futuros

> Lista de ideias para evoluir o projeto. Nada aqui e necessario para o
> funcionamento basico; sao estudos para aprender mais. Sem ordem de prioridade.

## Spoofing de dispositivo USB (identidade)
- Copiar VID/PID e strings do mouse real (ex.: Razer Orochi V2) no firmware,
  para o PC nao distinguir o Pico do mouse original.
- Copiar o report descriptor HID exato do mouse (via `lsusb -v` no Linux, ou
  USBView / usbhid-dump no Windows) e ajustar o `MouseReport` para casar.
- Lembrete: isso fica no firmware (os descriptors sao enviados na enumeracao),
  nao no driver do PC.

## Fechar a malha (feedback / closed-loop)
- Hoje o recoil e open-loop: aplica um padrao fixo sem "ver" o alvo.
- Estudar como dar feedback ao firmware (ex.: rastrear o cursor ou ler a tela),
  para a correcao reagir ao que acontece, como um humano.

## Biometria comportamental (realismo)
- Correlacao entre clique e movimento (hoje sao sorteados de forma independente).
- Perfil idiossincratico por usuario (cada pessoa tem um padrao estavel e unico).
- Ruido dependente do sinal (jitter que escala com a velocidade do movimento).
- Variabilidade entre sessoes.

## Suporte a "report protocol" no USB host
- Hoje o firmware so le relatorios em "boot protocol". Se um dongle nao aceitar
  boot protocol, implementar a leitura do report protocol (parse do descriptor HID).

## Ajustes de qualidade pendentes
- Polling de 1000 Hz: intervalo do HID device para 1 ms (hoje e 2 ms).
- Fitts: implementar a lei de verdade (T = a + b*log2(2D/W)) ou manter o nome
  "desaceleracao progressiva".
- Limites superiores na validacao do JSON (alguns campos ainda aceitam extremos).
- Sentinela `== 0` no rollover de millis (janela de 1 ms a cada ~49 dias).
- Stack: se houver instabilidade, definir `bool core1_separate_stack = true;`
  no core do Arduino-Pico (aumenta a stack de 4 KB para 8 KB por core).

## Recoil control (melhorias)
- Resgatar o campo "Atraso de atualizacao" (updateDelay), hoje morto no firmware:
  a ideia era sincronizar a compensacao com o fire-rate da arma, para ela "pulsar"
  junto com os tiros em vez de descer continuamente (util para armas
  semiautomaticas lentas).
- Alternativa: compensacao em "pulsos" no ritmo do fire-rate, em vez de fluxo
  continuo.

## Site (melhorias)
- Conversor de fire-rate: aceitar tambem "delay entre tiros" em ms, pois alguns
  jogos registram o fire-rate como intervalo em ms em vez de RPM (ex.: "150 ms
  entre tiros" = ~6.7 CPS). Converter automaticamente para CPS.

## Detecção dinâmica de hardware (futuro)
- Adicionar comando `info` ao firmware para responder chip ID, tamanho de flash,
  etc. via Web Serial, e exibir as specs reais no painel de informações da
  interface, hoje as specs do RP2040 são fixas no código.

## Teste fisico (quando o Pico chegar)
- Gravar o sketch de teste (`firmware/teste_dongle/teste_dongle.ino`) para validar
  o boot protocol do dongle do mouse.
- Testar o fast-fire com o mouse parado (deve funcionar apos a refatoracao).
- Testar o ajuste de CPS pelo scroll (depende do mouse enviar o 4o byte).
