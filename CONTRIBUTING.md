# Contribuindo

Obrigado pelo interesse em contribuir com o **H.I.D.E** (Hardware Input Dynamic
Emulator). Este é um projeto educacional; contribuições que melhorem o código e
o aprendizado são bem-vindas.

## Como contribuir

1. Abra uma issue descrevendo o bug ou a melhoria (se ainda não existir).
2. Faça um fork e crie uma branch a partir da `main`.
3. Mantenha as mudanças pequenas e focadas.
4. Abra um Pull Request explicando o que mudou e por quê.

## Commits: Conventional Commits

Formato: `tipo(escopo opcional): descrição curta no imperativo`

| Tipo | Uso |
|---|---|
| `feat` | nova funcionalidade |
| `fix` | correção de bug |
| `docs` | documentação (README, CHANGELOG, comentários) |
| `style` | formatação, sem mudança de comportamento |
| `refactor` | refatoração, sem mudança de comportamento |
| `perf` | melhoria de performance |
| `test` | testes |
| `build` | build e dependências |
| `ci` | integração contínua |
| `chore` | manutenção (configs, gitignore, limpeza) |

Exemplos:

- `feat: add RPM to CPS converter`
- `fix: correct recoil vector scale`
- `docs: update soldering guide`
- `refactor: split monolithic HTML into css and js`
- `chore: remove temp folder`

Regras:

- Descrição curta (até 72 caracteres), minúscula, no imperativo, sem ponto final.
- Um commit por mudança lógica.
- Escopo opcional quando ajudar: `feat(ui): ...`, `fix(firmware): ...`.
- Mudança incompatível: `feat!: ...` ou rodapé `BREAKING CHANGE: ...`.

## Estilo de código

- **Firmware:** C++ (Arduino-Pico), em `firmware/`.
- **Site:** HTML, CSS e JavaScript puro (sem frameworks), em `html/`, `css/` e `js/`.
- Siga o estilo do código existente e não refatore o que não foi pedido.

## Documentação

- Atualize o `CHANGELOG.md` (padrão Keep a Changelog + SemVer) quando houver versão.
- Mantenha o `README.md` coerente com o comportamento real do firmware e do site.

## Escopo do projeto

Este projeto é **educacional** e deve ser usado apenas localmente, em jogos
offline.
