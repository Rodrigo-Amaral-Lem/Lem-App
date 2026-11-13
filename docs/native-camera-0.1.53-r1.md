# Normal Android camera and crop controls — 0.1.53 R1

The photo and video buttons open the installed camera in its normal still-image or video mode. This lets the camera provide its usual interface, including its own gallery shortcut where the device/vendor supports it. Lem cannot force the vendor's gallery button to be shown. After capture, return to Lem using Back and select the image/video in the system file picker. This extra selection is needed because normal camera mode does not return an evidence file to the caller. Cancelling the picker attaches nothing. Original gallery media is not deleted or overwritten.

If no app supports normal camera mode, Lem falls back to the previous capture intent with automatic return. That mode may have a reduced camera UI. No broad media-library or camera permission is added; only the chosen media URI is copied. Videos still must pass the existing MP4/3GP, 60-second and 20-MB checks. Normal camera settings are controlled by the camera app, so choose suitable quality/duration before recording.

The Android evidence row has five controls: camera, photo gallery, video camera, video gallery (gallery icon with video badge), and Obs. Each control retains a minimum 44-pixel target and an accessible description. Video recording no longer requires opening a menu. Desktop retains file attachment controls.

The crop preview reserves a 28-pixel gutter around the fitted image. Its corner squares are 24 pixels wide and the hit area extends into the gutter, so a corner on the image border is still fully visible and touchable. Coordinates remain normalized to the displayed image and original resolution. Letterboxing is not included in the saved crop.

Validation: Android Java sources compiled against Android/AndroidX APIs; a JVM test covers camera/picker transitions, recreation and fallback. Real QML pointer tests cover five controls on one row, expanded crop hit regions and apply/cancel behavior at narrow mobile, landscape and desktop sizes. Related photo delegate, measurement and import-preview UI checks also pass. No physical Android camera/gallery launch has been exercised here; test recording, Back, selection and cancellation on the phone before field use.

This is a client-only revision of 0.1.53. Rebuild/install the app; no server deployment or database reset is needed. Android versionCode is incremented to 166.

Android API reference: https://developer.android.com/reference/android/provider/MediaStore
