# Compilação no Qt Creator — 2026-11-13

- Compilação normal: LEM_WINDOWS_STANDALONE=OFF por defeito. Num build já configurado, definir a opção para OFF na configuração CMake do Qt Creator; o valor antigo permanece na cache.
- Executável único: usar o script app/windows/build-standalone.ps1 com um Qt estático. O script continua a passar LEM_WINDOWS_STANDALONE=ON explicitamente.
- O Qt instalado em C:/Qt/6.12.0/mingw_64 indica GCC 15.1.0 em mkspecs/qconfig.pri. Usar o kit com C:/Qt/Tools/mingw1510_64/bin/g++.exe e gcc.exe. Ao mudar de compilador, selecionar um diretório de build novo no Qt Creator.
- Corrigida a compilação do ícone com MinGW em caminhos com espaços (como LEM App): o recurso é compilado sem os diretórios de inclusão Qt que o windres interpretava incorretamente.
- Corrigido o teste de arranque, que ainda esperava oito grupos, embora a checklist tenha sete.

Validação: configuração da pasta de build indicada pelo utilizador concluída; compilação completa de Lem.exe com Qt 6.12.0/MinGW 15.1 concluída. O executável desta compilação é dinâmico e necessita das bibliotecas Qt para distribuição. O pacote continua a conter código-fonte.
