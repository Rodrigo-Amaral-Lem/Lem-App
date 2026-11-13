> Linux: para a correção do conflito entre executável e pasta QML, consultar `build-linux-0.1.10.md`.

# Compilar a app Qt

Projeto C++17 / Qt Quick, com CMake 3.25+ e Qt 6.12 ou superior. É independente do protótipo antigo; não importa a antiga base Excel nem o M937.

Módulos: Core, Gui, Qml, Quick, QuickControls2, QuickDialogs2 e Network. Sem Python no dispositivo. O servidor é o único componente Python.

## Qt Creator

Abrir `app/CMakeLists.txt`. Selecionar o kit da plataforma. Build e Run. Em Arguments, `--demo` abre um report fictício local sem servidor. A mesma opção também está disponível em Conta → Ver demonstração local.

## Linux (ambiente de desenvolvimento adicional)

```bash
sudo apt update
sudo apt install build-essential libgl-dev libglx-dev libopengl-dev
cmake -S app -B app/build/linux -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build app/build/linux --parallel
./app/build/linux/LEM --demo
ctest --test-dir app/build/linux --output-on-failure
```

Instalar CMake e Ninja pelo gestor de pacotes/Qt Tools se necessário. Adaptar o caminho Qt. `cmake --fresh` limpa uma configuração anterior se tiver sido usado o kit errado.

## Windows / MinGW 64-bit

Usar o kit Desktop Qt 6.12 MinGW 64-bit no Qt Creator, com o MinGW correspondente instalado pelo Qt Maintenance Tool. Não misturar bibliotecas MSVC com MinGW. Para distribuir, usar a instalação CMake que executa o deployment Qt/QML:

```powershell
cmake --install app/build/windows --prefix dist/windows
```

Adaptar `app/build/windows` ao diretório real de build do Qt Creator. Entregar toda a pasta `dist/windows`, incluindo DLLs, QML e plugins. Testar numa máquina sem Qt. Certificar que o plugin TLS e as respetivas dependências estão disponíveis para HTTPS. A assinatura do executável/instalador será preparada mais tarde.

## Android

**From 0.1.3:** configure `LEM_ANDROID_OPENSSL_ROOT` before building Android. Follow `docs/android-openssl.md`; the OpenSSL libraries must be deployed inside the APK.

Instalar o kit Qt 6.12 Android e o SDK/NDK/JDK compatíveis através da configuração Android do Qt Creator. Abrir o mesmo CMake, selecionar Android arm64-v8a, configurar dispositivo/emulador e executar. O deployment padrão Qt gera o manifesto com acesso à Internet.

Em Android, **Tirar fotografia** abre a aplicação de câmara do dispositivo. **Escolher fotografia existente** abre o seletor nativo de ficheiros/galeria. Nas restantes plataformas mantém-se o seletor de ficheiros. Ver `docs/android-camera-icons.md`. JPEG/PNG são conservados; outros formatos só são convertidos se o plugin de imagem do kit os conseguir ler. HEIC depende do suporte do dispositivo/Qt; usar JPEG quando necessário. Confirmar no dispositivo o seletor de imagens, URI `content://`, persistência após reinício e destino de exportação PDF/ZIP.

Para distribuição: configurar identidade da app, assinatura Android e build release no Qt Creator. Não incluir keystore/passwords no projeto.

## iOS

Compilar num Mac com Xcode e kit Qt para iOS. Configurar signing team, bundle ID `pt.lem.app` e dispositivo. O sistema permite selecionar documentos/imagens através do diálogo nativo; validar a integração com Fotos e Ficheiros no dispositivo. Exportação usa o diálogo Guardar, não há botão dedicado de partilha nesta versão. Para TestFlight/App Store serão necessárias conta Apple e assinatura.

Documentação oficial de plataformas e toolchains:
- https://doc.qt.io/qt-6/supported-platforms.html
- https://doc.qt.io/qt-6/android.html
- https://doc.qt.io/qt-6/ios.html

## Validação desta entrega

Compilação real em Linux com Qt 6.12.0/GCC e teste de arranque QML. API ensaiada com SQLite temporária, incluindo integração real com o cliente C++ e dois dispositivos simulados. Não foram produzidos/assinados APK, IPA ou executável Windows, nem executado PostgreSQL/compose nesta sessão. O código tem configuração para as três plataformas pedidas; o próximo passo é validar cada kit e dispositivo.
