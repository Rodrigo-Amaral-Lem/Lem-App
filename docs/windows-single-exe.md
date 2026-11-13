# Windows — distribuir apenas Lem.exe

Este modo compila as bibliotecas Qt, os plugins necessários, a interface QML,
os recursos e o runtime do compilador dentro de `Lem.exe`. A comunicação HTTPS
usa Schannel (TLS do Windows), dispensando as DLLs de OpenSSL. As DLLs do próprio
Windows continuam a ser usadas a partir do sistema. Não é um instalador ou um
arquivo que extrai DLLs para uma pasta temporária.

É necessário **compilar o Qt estático uma vez**; os kits normais do Qt Online
Installer são partilhados e continuam a produzir um EXE dependente de DLLs.
Este pacote contém o código e o processo de build, não um EXE Windows já compilado.
Android/Linux mantêm a configuração anterior. No Windows, CMake ativa agora
`LEM_WINDOWS_STANDALONE=ON` por omissão em configurações novas.
A versão/protocolo é agora 0.1.33 e a correção R1 do scroll é preservada.
Esta versão inclui correções de segurança na app e servidor: atualizar ambos,
sem reset da base de dados. Ver [revisão de segurança](../../Common/docs/security-review-0.1.33.md),
incluindo a pendência dos patches Qt 6.12.0.

## Preparação (uma vez, no Windows)

1. No Qt Maintenance Tool, instalar os **Sources do Qt 6.12.0**, CMake e Ninja.
   O caminho habitual dos Sources é `C:\Qt\6.12.0\Src`.
2. Instalar Python 3 e disponibilizar `python.exe` no PATH (desativar o alias da
   Microsoft Store se este abrir a loja). CMake >= 3.25 e Ninja também no PATH.
3. Usar o MinGW-w64 **15.1 de 64 bits** do Qt 6.12, ou MSVC 2022 x64 com Windows SDK.
   Qt e Lem têm de ser compilados com o mesmo compilador/arquitetura.
4. Usar caminhos Qt curtos e sem espaços. Reservar espaço para fontes, objetos
   e instalação estática; o primeiro build é substancialmente maior e mais demorado
   do que compilar só a app. `-Jobs 4` limita o paralelismo; reduzir se faltar RAM.

Os caminhos de exemplos são ilustrativos: confirmar os nomes dos diretórios
instalados no computador. O script não instala ferramentas nem altera o kit Qt
normal. Se o configure pedir a escolha/aceitação da licença Qt, responder segundo
a licença que estás a utilizar. A distribuição estática deve respeitar essa licença;
consultar [licenciamento Qt](https://www.qt.io/licensing/).

## MinGW: primeiro build

Abrir PowerShell na raiz `Lem` extraída. Adicionar as pastas de CMake/Ninja ao PATH
se ainda não estiverem disponíveis. Por exemplo, se estes forem os caminhos reais:

```powershell
$env:PATH = 'C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;' + $env:PATH
.\app\windows\build-standalone.ps1 -BuildQt `
  -QtSource 'C:\Qt\6.12.0\Src' `
  -QtBuild 'C:\Qt\build-6.12.0-static-mingw' `
  -QtPrefix 'C:\Qt\6.12.0-static-mingw' `
  -Compiler MinGW -ToolchainBin 'C:\Qt\Tools\mingw1510_64\bin'
```

Se a política local impedir a execução do script, executar na mesma janela:
`Set-ExecutionPolicy -Scope Process Bypass`. O efeito termina ao fechar a janela.

## Builds seguintes (reutilizam o Qt estático)

```powershell
.\app\windows\build-standalone.ps1 `
  -QtPrefix 'C:\Qt\6.12.0-static-mingw' `
  -Compiler MinGW -ToolchainBin 'C:\Qt\Tools\mingw1510_64\bin'
```

O resultado é **`app\build\windows-static\portable\Lem.exe`**. Copiar esse ficheiro
para o PC de destino. A pasta `portable` é usada para testar o arranque só com o
EXE. O relatório `dll-imports.json` fica na pasta de build, fora de `portable`.
Os rascunhos/fotografias/dados criados durante utilização continuam no armazenamento
normal do utilizador; não ficam embutidos no EXE e não são transportados ao copiá-lo.

## Alternativa MSVC

Abrir **Developer PowerShell / x64 Native Tools for Visual Studio 2022**, garantir
CMake, Ninja e Python no PATH e usar:

```powershell
.\app\windows\build-standalone.ps1 -BuildQt -Compiler MSVC `
  -QtSource 'C:\Qt\6.12.0\Src' `
  -QtBuild 'C:\Qt\build-6.12.0-static-msvc' `
  -QtPrefix 'C:\Qt\6.12.0-static-msvc' `
  -BuildDirectory 'C:\build\lem-static-msvc'
```

Nos builds seguintes omitir `-BuildQt`. Não reutilizar a mesma pasta de build ou
prefixo Qt entre MinGW e MSVC. `/MT` liga o runtime MSVC estaticamente; no MinGW,
`-static -static-libgcc -static-libstdc++` inclui também o runtime/pthreads.

## Qt Creator

Depois de o Qt estático estar instalado, pode ser registado como kit separado
no Qt Creator (o seu `bin\qmake.exe`, compilador correspondente e Ninja).
Usar uma pasta de build nova e Release: CMake já ativa
`LEM_WINDOWS_STANDALONE=ON` por omissão no Windows. Não é necessário passar a opção
nem executar o script para compilar no Qt Creator com esse kit. O script é a via recomendada porque também verifica
as dependências e testa a cópia isolada. Só ligar essa opção no kit Qt partilhado
resulta num erro explícito a pedir Qt estático; não transforma DLLs existentes.
Para um build partilhado deliberado, definir `LEM_WINDOWS_STANDALONE=OFF`. Uma
pasta antiga pode ter guardado OFF em cache: usar uma pasta nova ou alterar essa
variável para ON no Qt Creator.

## CMake diretamente (com Qt estático já instalado)

```powershell
cmake -S app -B build-win-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0-static-mingw
cmake --build build-win-static --parallel 4
```

Executar num ambiente com o compilador desse Qt, Ninja, CMake e Python no PATH.
Não é necessário um flag adicional para ativar o modo estático no Windows.
O resultado deste comando fica em `build-win-static/Lem.exe`.

## Verificações e limites

- A configuração exige Qt estático e plugins Windows/offscreen, JPEG e Schannel.
- O scanner QML inclui explicitamente os estilos Fusion/Basic. No modo standalone,
  os imports do motor QML procuram apenas recursos embutidos, evitando um falso
  sucesso por encontrar a instalação de Qt do programador.
- `verify-pe.py` lê imports PE normais e diferidos e rejeita Qt, OpenSSL, runtime
  MinGW/MSVC e outras DLLs externas. Não aprova qualquer DLL só por existir em
  System32; usa uma lista explícita de bibliotecas do Windows.
- O script copia só o EXE, retira caminhos Qt/QML/plugin do ambiente e executa
  `--smoke-test`: arranque QML, recursos de checklist, TLS e suporte JPEG.
  O teste usa dados fictícios temporários e não faz login nem contacta o servidor.
- A análise PE não descobre chamadas arbitrárias a LoadLibrary; o smoke test é
  complementar. Fazer a verificação final num Windows sem Qt instalado: abrir,
  fazer login HTTPS, ver/anexar fotografia JPEG e exportar um report fechado.
- A funcionalidade foi preparada num ambiente Linux. O EXE/MSVC/MinGW e o script
  PowerShell não foram executados em Windows nesta entrega. Os testes do leitor PE
  usam fixtures binárias e os da configuração CMake usam targets Qt simulados;
  não substituem uma compilação real Qt 6.12/Windows.

Se o script acusar uma DLL externa, não a copiar para contornar a verificação:
reconstruir a dependência estaticamente ou ajustar o Qt. Se uma biblioteca legítima
do Windows não estiver na lista, verificar a sua origem antes de acrescentá-la.
Uma configuração Qt interrompida/incompatível exige uma **nova** `-QtBuild`;
o script nunca apaga automaticamente uma árvore de build ou um kit instalado.

## Referências técnicas

- [Qt Windows: ligação estática](https://doc.qt.io/qt-6/windows-deployment.html#static-linking)
- [Construir Qt no Windows](https://doc.qt.io/qt-6/windows-building.html)
- [Configuração Qt e Schannel](https://doc.qt.io/qt-6/configure-options.html#ssl)
- [Plugins estáticos](https://doc.qt.io/qt-6/qt-import-plugins.html)
- [Estilos em builds estáticos](https://doc.qt.io/qt-6/qtquickcontrols-deployment.html#static-builds)

## Diagnóstico de build.ninja em falta

`ninja: error: loading 'build.ninja': GetLastError() = 2` indica que o ficheiro
não existe na pasta usada para compilar. Não demonstra que Ninja esteja em falta;
o CMake deve gerar esse ficheiro antes do build. O erro de configuração original
pode estar várias linhas acima.

O script agora verifica `build.ninja` antes de cada build (Qt e Lem), não confunde
um CMakeCache/marcador com geração concluída, e só grava o marcador após confirmar
o ficheiro gerado. Uma configuração parcial com assinatura igual volta a executar
configure se o ficheiro faltar. Configurações antigas/sem marcador ou de outro
compilador continuam a exigir uma pasta de build nova, sem apagar a anterior.

Cada fase e comando aparece na consola. O log é criado automaticamente em
**`app/windows-static-transcript.log`**; não é necessário acrescentar `*>` ao comando.
Em Windows PowerShell 5.1, mensagens nativas de stderr são apresentadas, mas só o
código de saída decide se o comando falhou. Isto evita que a conversão de stderr
em NativeCommandError interrompa o tratamento antes de verificar o resultado.

Estas alterações melhoram a validação e o diagnóstico; o erro Qt/CMake original
no computador do utilizador continua a precisar do log completo para ser identificado.
A validação local do script foi sintática; a execução real Windows não foi testada
neste ambiente.

## Authz.dll / Mscms.dll no verificador

A lista de DLLs do sistema inclui agora `authz.dll` (Windows Authorization API)
e `mscms.dll` (Windows Color Management). Foram reportadas pelo executável real
ligado com Qt estático/MinGW 15.1; eram falsos positivos do verificador e não
bibliotecas externas que precisem de ser copiadas ou embutidas.

Referências Microsoft:
- https://learn.microsoft.com/en-us/windows/win32/api/authz/nf-authz-authzinitializeresourcemanager
- https://learn.microsoft.com/en-us/windows/win32/api/icm/nf-icm-opencolorprofilew

Depois de atualizar `app/windows/verify-pe.py`, repetir o script **sem `-BuildQt`**,
com o prefixo Qt já instalado e a pasta de build Lem existente. O passo de ligação
volta a executar o verificador atualizado, seguido do teste de arranque isolado.
Não é necessário recompilar Qt. Os testes cobrem imports normais e diferidos para
estas duas DLLs; Qt/OpenSSL/runtimes externos continuam a ser rejeitados.
