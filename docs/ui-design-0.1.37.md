# Lem interface and PDF design - 0.1.37

This release reviews the shared Android, Windows and Linux presentation layer.
It keeps the existing report workflow, local persistence, ownership rules and
synchronization scheduling. Android build code: **141**.

## Review and applied changes

| Finding | Change |
| --- | --- |
| The dark green interface did not match the cyan/grey logo. | Light neutral surfaces, dark grey text, cyan actions and a compact white header with the original logo. Native palette and QML controls use matching colours. |
| Branding and report status used the same green. | Cyan identifies actions/navigation. Green OK, red NOK and grey N/A identify selected answers. The answer labels and pressed state remain available; meaning does not depend on colour alone. |
| Long catalogue search hints were truncated on a phone. | Short hint, descriptive accessibility label, adjacent clear button, Escape to clear and Enter to dismiss the keyboard. The existing search scope and debounce remain intact. |
| Report completion was only a number inside a status sentence. | A three-pixel completion meter supplements the exact answer count. It is not synchronization progress or an assertion that all required evidence is present. |
| Desktop navigation did not identify the current page. | Contratos/Gestão uses the selected button treatment. |
| Empty metadata produced stray separators in desktop cards. | Only supplied location/model/CPE values appear; address and serial remain labelled. Android retains its deliberately concise ID/serial/address view. |
| Management displayed many empty Excel mappings before opening a file. | Mappings, preview and confirmation appear after a workbook has sheets. Controls keep their state and the existing import validation is unchanged. |
| PDFs lacked the app's logo. | Both server and offline exporters use the same embedded logo, dark cyan heading and thin cyan footer rule. Server metadata labels use a pale cyan background. |

The app's two-second startup screen retains the logo. The logo master now lives
at `shared/branding/lem-logo.png`: Qt bundles it under the existing resource URL
`:/branding/lem-logo.png`, and Docker includes it through `COPY shared`.
PDF generation never downloads the logo and offline export needs no login.

## Palette and interaction decisions

| Role | Colour | Contrast against white |
| --- | --- | --- |
| Logo accent | `#00A6BB` | Decorative accent; not used behind small white labels |
| Primary action / heading | `#007C91` | 4.89:1 |
| Main text | `#263238` | Above 12:1 on the page surface |
| Secondary text / N/A | `#52616A` | 6.41:1 |
| Selected OK | `#256B48` | 6.41:1 |
| Selected NOK / destructive text | `#A12626` | 7.45:1 |
| Page surface | `#F4F6F7` | Light neutral background |

White labels on filled buttons use the listed ratios. Hover/pressed primary
surfaces darken, focus rings remain visible, and compact header buttons retain
48 logical-pixel hit areas even though their visible surfaces are smaller.
The OK/NOK/N/A/erase row stays on one line at tested widths from 320 to 600.
The erase icon has an explicit accessible name and tooltip.

Reference principles used in the review:

- [Android accessible touch targets](https://developer.android.com/guide/topics/ui/accessibility/views/apps-views): at least 48 dp for touch interaction.
- [W3C contrast minimum](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html): 4.5:1 for normal text.
- [W3C use of colour](https://www.w3.org/TR/WCAG20/#visual-audio-contrast-without-color): preserve text/other cues alongside colour.

These are design checks, not a declaration of full accessibility conformance.
A physical-device TalkBack and enlarged-font review remains useful.

## Validation

- 142 backend/deployment tests passed, including PDF/ZIP permissions, evidence,
  large metadata, long text, damaged images, version policy and deployment scripts.
- 21 Android OpenSSL/backup and Windows standalone checks passed, including 16
  additional parameterized subtests. No platform build configuration was relaxed.
- Shared QML report workflow passed in narrow and desktop windows. Additional
  checks covered search/clear, loaded logo, selected answer colours, 320-600 px
  answer rows and empty/loaded workbook mapping visibility.
- Scroll bounds, chrome animation, nonzero safe areas, feedback layout, draft
  export guards, horizontal group navigation and desktop keyboard/wheel checks
  passed without QML warnings.
- Native C++ offline PDF export passed with no session, a closed local report,
  photographs and long notes. Missing/corrupt photos still preserve the chosen
  destination. The new PDF renderer/resource was compiled against the available
  Qt 6.10.2 validation runtime; full Windows/Android Qt 6.12 release builds were
  not performed in this environment.
- Server and native PDFs were rendered and visually inspected for logo placement,
  heading/metadata spacing, long-note continuation, footer and final-page layout.
  The refreshed example is `examples/report-demonstracao.pdf`.

## Installation

Rebuild/install the app for the interface and local PDF changes. Deploy the
server for the server-generated PDF and ZIP branding and the latest-version
notice (0.1.37). Minimum compatible client remains 0.1.28. No database reset or
re-import is required. The live server has not been modified as part of this work.
