# Progressive Android menus — 0.1.47 R1

Page menus now track the actual vertical page displacement, rather than toggling
an open/closed state after a small direction threshold. Their reveal fraction
changes proportionally across a full toolbar stack's travel (minimum 96 logical
pixels). Reversing direction reverses the reveal immediately. Kinetic scrolling
continues that movement; stopping partway leaves the menus at that fraction.

No time animation chases the finger. The short easing animation remains only
for explicit navigation resets outside an active scroll. At the page top the
menus are always shown, and the main Lem/back/options/account header stays fixed.
Desktop menus remain visible.

The controls slide as opaque surfaces. A half-transparent control would let
question text show through its label, so opacity is not used to animate their
travel. Off-screen controls cannot intercept taps.

The reading viewport now reaches the fixed header and bottom screen edges,
removing the residual fixed pale strips when the overlaid menus are hidden.
Horizontal margins are retained. Equivalent **constant** top/bottom content
insets preserve the reading layout and scroll range; they never depend on the
reveal fraction. Review navigation uses the updated full overlay inset and is
still clamped at the actual final question.

Validation includes real Qt touch sequences with intermediate fraction checks,
direction reversal and partial stop; unchanged viewport/content/indicator sizes;
swipe-to-end regressions on reports and charger IDs; same/cross-group review;
desktop scrollbars; and the report UI suite. Intermediate offscreen captures
were inspected for clipping and transparency overlap. Tests use Qt 6.10.2 and a
mock backend; physical Android/Qt 6.12 validation remains to be performed.

App-only revision: version 0.1.47, Android build code 159. A server already on
0.1.47 does not need another deployment. No database changes.
