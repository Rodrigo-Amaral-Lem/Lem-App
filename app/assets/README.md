# Client branding assets

The app is named **Lem** and uses Android application ID `pt.lem.app`. Synchronize outstanding work
before switching to a different package identity or uninstalling.

| Asset | Role |
| --- | --- |
| [lem-original.png](lem-original.png) | Supplied raster reference, preserved unchanged |
| [lem-logo-tight.svg](../../shared/branding/lem-logo-tight.svg) | Tightly cropped shared logo for the header and static startup screen. The app provides a white background and clearance around the mark. |
| [lem-logo.png](../../shared/branding/lem-logo.png) | Shared transparent raster master |
| [lem-icon-1024.png](lem-icon-1024.png) | Square icon master with light background |
| [lem-icon-256.png](lem-icon-256.png) | Desktop window/icon image |
| [lem.ico](lem.ico) | Windows icon representations at 16, 32, 48, 64, 128 and 256 px |
| `startup/` | Three six-frame startup-animation variants bundled by CMake |

Android derivatives are under [android/res](../android/res): five legacy launcher densities and an
adaptive foreground with an 18% inset for mask safety. Regenerate/review platform derivatives when
deliberately replacing the master; changing only one PNG does not update every launcher resource.

The shared logo is an AI-assisted raster interpretation of the supplied low-resolution reference,
with the small tagline removed and transparent background and counters. The SVG wraps the raster
artwork with a tight viewport around the cyan plug and dark-grey lowercase wordmark. Platform
icons are prepared from the raster masters.

Customer artwork must be deliberately added to the appropriate tracked assets; files in a
developer's local resource folders are not automatically incorporated. See
[workspace ownership](../../../Common/docs/workspace.md) and
[build/deployment](../../docs/build-and-release.md) before changing assets or package identity.
