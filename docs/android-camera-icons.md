# LEM 0.1.4 — Android photo capture and font-independent icons

## Changes

- Dropdown chevrons, the compact navigation menu icon and section-navigation arrows are drawn in QML Canvas. They no longer rely on Unicode glyphs being present in the device font.
- On Android, the primary photo action is **Tirar fotografia**. It opens the installed camera application using `ACTION_IMAGE_CAPTURE` and a writable content URI for a full-size JPEG.
- **Escolher fotografia existente** opens the existing file/gallery picker. On Windows, Linux and iOS, the existing **Adicionar fotografia** picker behaviour is retained.
- The capture request retains the original report ID and checkpoint. The result is attached to that report even if another report is displayed when the app resumes. Normal ownership, draft status and image-size checks still apply.
- A private capture activity and scoped FileProvider share only the camera staging directory. No Qt Multimedia module or CAMERA permission is required because the external camera app performs capture. No broad storage permission is added.
- The Java activity retains pending output during recreation and writes its result atomically. The Qt client checks for a result while active, and can import a pending result after the original user's cached report is available again.
- Cancelling capture or having no camera app returns a message without attaching an image. On success, the temporary source is removed only after the image and report metadata have been saved. A rejected source is retained in the cache with `camera-result.failed.json`; it is not automatically attached or retried. Android may clear cache files, so staging is not a substitute for completing capture/import.

## Update

Close the application. Replace the project's `app` folder with this version and open `app/CMakeLists.txt` in Qt Creator. Keep the existing `LEM_ANDROID_OPENSSL_ROOT` setting. Run CMake, rebuild and deploy the APK over the installed application using the same signing key. No server deployment or database reset is required.

The Android package source directory is now `app/android`. It includes the app manifest, Java capture classes and scoped provider paths. If you maintain local Android manifest modifications, merge them into this directory rather than keeping a different package-source override. The Qt main activity and its existing document-sharing provider remain present.

## Verification completed

- QML tests with Qt 6.10.2/PySide6, software rendering and a simulated backend passed at desktop and 390×844 dimensions. They cover tab navigation, retained answers and photo metadata, Android camera-button routing to the original checkpoint, Android-only existing-photo action, desktop button behaviour and absence of QML warnings.
- Desktop and mobile rendered views were inspected, including the drawn icons and both Android photo actions.
- Both Java classes compiled with Eclipse ECJ using Java 8 language/bytecode compatibility, Android API 35 class references and AndroidX Core 1.13.1.
- Manifest/provider checks passed for the private activity, non-exported URI provider, camera-only cache path and absence of a CAMERA permission.
- All seven Android OpenSSL CMake configuration tests still pass.

A complete Qt C++/Android APK build and on-device camera round trip were not performed here. The native JNI bridge, camera-app-specific behaviour, lifecycle recovery, physical-device image import and upload still require device verification. Existing image limits remain 20 MB and 40 MP per evidence image; choose a normal camera resolution if the phone defaults to higher values.

## Device checks

1. Confirm **0.1.4** in Conta and check the dropdown/menu/section arrows.
2. Open an editable report, press **Tirar fotografia**, take a photo and accept it. Confirm a thumbnail appears under the same checkpoint.
3. Repeat and cancel: no extra evidence should appear.
4. Choose **Escolher fotografia existente** and attach an existing image.
5. Switch sections, restart the app, sync, and confirm both photos remain associated with the correct report item.

## Reference

The Android camera delegation and full-size output mechanism follows the platform documentation: https://developer.android.com/media/camera/camera-deprecated/photobasics

Android source packaging follows Qt's documented package-source mechanism: https://doc.qt.io/qt-6/cmake-target-property-qt-android-package-source-dir.html

The manifest adapts the Qt 6.10 Android application template, keeping its deployment placeholders for the installed Qt toolchain to fill.
