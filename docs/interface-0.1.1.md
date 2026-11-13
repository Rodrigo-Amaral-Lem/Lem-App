# LEM — interface 0.1.1

Esta edição melhora a apresentação da aplicação. O problema de cores comunicado no Windows foi identificado pelo utilizador como execução de uma versão antiga; não foi reproduzido como falha da versão atual.

## Critérios de design aplicados

- Contraste de referência WCAG 2.2: texto normal ≥ 4,5:1; informação visual essencial de controlos e foco ≥ 3:1. Estes valores são critérios de design, não uma certificação integral de acessibilidade.
- Atributos com contornos visíveis e foco de teclado distinto; seleções com texto branco sobre verde escuro.
- Paleta explícita e controlos Base personalizados, com Fusion para os restantes controlos. Menus da aplicação são desenhados dentro da janela. As janelas de ficheiros do sistema conservam o tema do sistema operativo.
- Texto principal de 16 px lógicos; texto das fotografias aumentado de 11 para 14 px. Botões, atributos e opções de listas com altura de 44 px lógicos.
- Rótulos permanentes nos atributos de criação de contratos e técnicos. Os exemplos dentro dos atributos complementam o rótulo.
- Ações principais destacadas; estados de reports continuam identificados por palavras, sem depender apenas da cor.
- Títulos e IDs longos podem quebrar linha. Navegação compacta em ecrãs estreitos.
- Versão visível em Conta, para distinguir instalações antigas.

### Contrastes calculados

| Uso | Cores | Contraste |
|---|---|---|
| Texto principal / branco | #20313c / #ffffff | 13,41:1 |
| Texto principal / fundo | #20313c / #f2f6f7 | 12,33:1 |
| Texto auxiliar / branco | #526470 / #ffffff | 6,15:1 |
| Texto auxiliar / fundo | #526470 / #f2f6f7 | 5,65:1 |
| Texto branco / verde | #ffffff / #135f58 | 7,49:1 |
| Contorno / branco | #70818b / #ffffff | 4,04:1 |
| Contorno / fundo | #70818b / #f2f6f7 | 3,71:1 |

Cálculo por luminância relativa sRGB. A WCAG é uma referência útil também para esta aplicação nativa; pixels lógicos Qt não são uma declaração de equivalência normativa a CSS px.

## Atualizar no Windows

1. Fecha a app. Extrai este pacote para uma pasta nova, sem misturar versões. Também podes substituir apenas os ficheiros de `app` do teu projeto, preservando a pasta `shared` existente.
2. Abre `app/CMakeLists.txt` no Qt Creator e seleciona o kit Desktop Qt 6.12.0 MinGW 64-bit correspondente à tua instalação.
3. Configura, compila e executa. Se estiveres a reutilizar uma pasta de build de outro kit, seleciona uma pasta de build nova.
4. Em **Conta**, confirma **Versão 0.1.1**. Ao usar um atalho externo, aponta-o para o executável que acabaste de compilar.

Os nomes internos da aplicação e a localização dos dados locais mantêm-se. Esta atualização não requer deploy do servidor nem reset da base de dados.

## Validação efetuada

O QML foi carregado com Qt 6.10.2 através de PySide6, com dados fictícios a substituir o backend, numa execução sem ecrã com renderização por software. O teste forçou uma paleta escura do sistema. Foram inspecionados contratos, gestão, report, conta, lista de contratos aberta e menu móvel, a 1100×820 e/ou 390×844. Os cenários concluíram sem avisos QML. Os dez pares de cores verificados passaram os limiares aplicáveis.

Não foi compilado o executável C++ Qt 6.12 nesta sessão e não foi feito um teste nativo em Windows, Android ou iOS. A validação visual isolada não testa autenticação, persistência, importação, sincronização, leitores de ecrã, teclado virtual ou escalas de DPI específicas dos dispositivos. Esses fluxos e as funções do servidor não foram alterados.

## Fontes consultadas

- W3C — contraste de texto: https://www.w3.org/WAI/WCAG21/Understanding/contrast-minimum
- W3C — contraste de componentes: https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html
- W3C — tamanho mínimo dos alvos: https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html
- Qt — personalização de controlos e limitações dos estilos nativos: https://doc.qt.io/qt-6/qtquickcontrols-customize.html
- Qt — seleção de estilos: https://doc.qt.io/qt-6/qtquickcontrols-styles.html

## Correção CMake para Android

A regra `install(TARGETS LEM ...)` inclui agora `LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}`. Em Android, o alvo criado por `qt_add_executable` é uma biblioteca de módulo; versões CMake anteriores a 4.5 exigem esse destino explícito. A correção resolve a mensagem “install TARGETS given no LIBRARY DESTINATION for module target”. Não constitui validação de um build Android completo. Para executar no Windows, selecionar um kit Desktop MinGW, em vez de Android. Depois de substituir `app/CMakeLists.txt`, voltar a executar CMake no Qt Creator.

A regra corrigida foi validada isoladamente com CMake 3.31.6: configuração, compilação e instalação de um alvo MODULE de teste passaram.
