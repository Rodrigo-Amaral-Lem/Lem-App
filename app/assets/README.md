# LEM branding assets

`lem-original.png` is the supplied reference, preserved unchanged. The app name is Lem. The Android application ID is pt.lem.app from 0.1.23. It creates a separate installation from older package IDs; synchronize pending work before switching.

`../../shared/branding/lem-logo.png` is a high resolution transparent interpretation of the reference without the small tagline. It is the shared master for PDF exports, the header and the startup screen and as the source for the Android adaptive foreground. `lem-icon-1024.png` is the square app icon master with a light background. `lem-icon-256.png` is the desktop window/icon resource. `lem.ico` includes 16, 32, 48, 64, 128 and 256 px Windows icon representations. Platform sizes were prepared mechanically from these masters; regenerate them if replacing the masters.

Android uses the five legacy mipmap densities plus an adaptive icon. The foreground has 18% inset so the entire wordmark remains inside the central safe area under round/squircle masks.

Generation used the built-in image generation tool, with the supplied image as the edit target. Prompt: preserve the cyan electric plug and dark grey lowercase "lem" wordmark; remove the small "LOADING ELECTRIC MOBILITY" tagline; clean square icon composition on a light background, with room for launcher masks; no extra text, 3D, shadow or framing. A second background-extraction pass requested true transparent alpha with the same geometry, colours and positioning, including transparent counters and plug interior. The result is an interpretation of the low-resolution reference, not an official vector master.
