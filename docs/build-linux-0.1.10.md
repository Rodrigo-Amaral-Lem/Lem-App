# Correção do build Linux 0.1.10

O erro `cannot open output file LEM: Is a directory` acontece quando o módulo QML `LEM` cria uma pasta com o mesmo caminho reservado ao executável Linux.

A configuração passa a definir explicitamente:

```cmake
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")
qt_add_qml_module(LEM
 URI LEM VERSION 1.0
 OUTPUT_DIRECTORY "${QT_QML_OUTPUT_DIRECTORY}/LEM"
 QML_FILES qml/Main.qml
)
```

O executável mantém-se em `<build>/LEM`; os metadados QML passam para `<build>/qml/LEM`. O URI e os caminhos de recursos incorporados continuam os mesmos.

## No Qt Creator

1. Usar o `app/CMakeLists.txt` corrigido ou o pacote 0.1.10.
2. Em Projects → Build Settings, escolher uma **pasta de build nova**, por exemplo `Desktop_Qt_6_12_0_Debug_fixed`.
3. Executar Run CMake e Build.

Uma pasta de build nova evita reutilizar o diretório `LEM` já gerado na localização antiga. Clean/Rebuild ou `cmake --fresh` não garantem, por si só, a remoção dessa pasta antiga.

Em alternativa, depois de atualizar o CMake, pode renomear apenas a pasta gerada `<build>/LEM` para `<build>/LEM_qml_old`, antes de executar Run CMake e Build. Não renomear a pasta do projeto que contém `app`, `server` e `deploy`.

Esta alteração é do build do cliente. Não requer atualizar o servidor nem apagar dados. O cliente mostra 0.1.10; a API incluída mantém a versão 0.1.9.

Referência: [Qt — QT_QML_OUTPUT_DIRECTORY](https://doc.qt.io/qt-6/cmake-variable-qt-qml-output-directory.html).
