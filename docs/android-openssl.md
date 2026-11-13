# Lem — Android OpenSSL deployment

The logged `/system/lib/libcrypto.so ... not accessible for the namespace` line means a library-load attempt reached Android's unsuffixed system library. It does not, by itself, prove that all TLS loading failed; check the new `LEM TLS:` diagnostic and an actual HTTPS operation.

## Rebuild in Qt Creator

1. In Preferences → SDKs → Android (Devices → Android in some versions), download/reinstall Android OpenSSL. Use the Android repository recommended by Qt: https://github.com/KDAB/android_openssl. This is separate from desktop OpenSSL.
2. Identify its root folder. It must contain `ssl_3/arm64-v8a/libcrypto_3.so` and `ssl_3/arm64-v8a/libssl_3.so` for the phone in the supplied log. Use the complete repository, not individually renamed files.
3. Update the application sources with this package. If preserving local modifications, the changed production files are `app/CMakeLists.txt`, `app/src/main.cpp`, `app/src/Lem.cpp`, plus the new `app/cmake/AndroidOpenSSL.cmake`.
4. Remove previous manually added OpenSSL includes/FetchContent blocks and old OpenSSL entries in Additional Libraries from the local build. This version manages deployment through its own CMake module; the Qt Creator checkbox is not needed. Keep unrelated application libraries.
5. Select the Android kit and run CMake. Leave `LEM_ANDROID_OPENSSL_ROOT` empty for automatic detection. The module checks SDK paths from CMake/environment, Windows `%LOCALAPPDATA%/Android/Sdk`, Linux `~/Android/Sdk` and macOS `~/Library/Android/sdk`. Both `android_openssl/ssl_3/<ABI>` and Qt Creator downloads in `android_openssl/openssl-android-<ABI>/ssl_3/<ABI>` are supported, including separate archives for multi-ABI builds. A manually configured path still takes priority; clear an obsolete value to enable automatic detection.
6. Run CMake. Look for `LEM Android OpenSSL (_3):` followed by the two suffixed libraries per selected ABI. For multi-ABI builds, every ABI needs its own pair.
7. Use a new Android build directory or clear the CMake configuration when changing paths/ABIs. Rebuild the APK and deploy it over the installed app using the same signing key. Do not uninstall to troubleshoot TLS: uninstalling can remove unsynchronised local drafts.
8. In Application Output, confirm the current app version and find `LEM TLS:`. The expected diagnostic includes `suffix= _3`, an OpenSSL backend, `supportsSsl= true`, and a non-empty OpenSSL runtime version. Then test HTTPS login.

## Optional persistent custom location

If OpenSSL is installed outside the detected SDK locations, set this once in
PowerShell (adapt the location if necessary):

```powershell
setx LEM_ANDROID_OPENSSL_ROOT "$env:LOCALAPPDATA\Android\Sdk\android_openssl"
```

Restart Qt Creator afterwards so it receives the new environment. Future extracted
project versions and new build folders inherit it. A CMake cache value has first
priority, then this environment variable, then automatic discovery. A wrong explicit
value fails with an error rather than silently switching to another installation.
The module does not download libraries or disable certificate validation.

## What changed

- Android builds append the suffixed OpenSSL libraries to `QT_ANDROID_EXTRA_LIBS`, so Qt's deployment tool includes them in the APK. No external library is downloaded automatically by this project.
- Missing libraries, conflicting old OpenSSL entries present when the module runs, non-ELF files, and mismatched ELF architecture headers stop configuration with an explanatory error. The header check does not establish binary provenance, SONAME correctness, OpenSSL version or cryptographic integrity; obtain the actual Android build from the repository above.
- `ANDROID_OPENSSL_SUFFIX=_3` is set before constructing the application or using Qt Network. Desktop builds retain their normal TLS backend behavior.
- Android startup logs backend availability, active backend, Qt's build OpenSSL version, runtime version and `supportsSsl()`. These logs contain no credentials or tokens.
- Login gives an explanatory error when HTTPS is requested without functional TLS support. Existing certificate validation stays enabled.

## Inspect an APK if necessary

Open the generated APK as a ZIP. For arm64 it should contain `lib/arm64-v8a/libcrypto_3.so`, `lib/arm64-v8a/libssl_3.so` and a Qt TLS plugin whose filename contains `opensslbackend`. Other ABI directories need their own libraries. If the two libraries are present but `supportsSsl=false`, enable `QT_DEBUG_PLUGINS=1` temporarily in the run environment and capture the backend-loading messages.

Do not copy `/system/lib/libcrypto.so`, use desktop OpenSSL DLLs, merely rename arbitrary `.so` files, disable certificate checking or switch the production server to HTTP. The official Android binaries include matching library names and internal dependencies.

## Validation limits

Thirteen CMake configuration tests passed using synthetic ELF headers, including automatic SDK discovery, nested Qt Creator downloads, split ABI archives, persistent environment override and explicit-override precedence. Existing checks cover: arm64 deployment paths, missing SSL rejection, wrong-ABI rejection, multi-ABI lists, old-library conflict rejection, complete no-asm pairs in Debug, and desktop configuration without Android OpenSSL. Run them with `python app/tests/test_android_openssl.py` and CMake on PATH.

These configuration checks do not use an Android SDK/NDK. An APK was not generated or tested on a device in this revision. Windows folder discovery was simulated with temporary folders, not executed on the user's laptop. The successful configuration tests do not demonstrate working OpenSSL binaries or a successful TLS handshake. The backend and device test in step 8 is still required.

Sources:
- https://doc.qt.io/qt-6/android-openssl-support.html
- https://doc.qt.io/qt-6/cmake-target-property-qt-android-extra-libs.html
- https://doc.qt.io/qt-6.12/qsslsocket.html
