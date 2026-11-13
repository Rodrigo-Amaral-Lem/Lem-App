# Desktop page scrollbars - 0.1.37 R1

The report and Management scrollbars were attached inside clipped page content.
Later card content could cover them. The catalogue did not attach a desktop
scrollbar; its separate full-height indicator was Android-only.

All three desktop pages now attach a shared `UiPageScrollBar` to their actual
Flickable/ListView, but paint it in the outer window gutter. The bar is above the
page content, beside the cards, and ends above the sync footer. The thumb and
track have explicit light-theme colours and remain visible when scrolling is
possible, including while idle. Mouse dragging retains Qt's attached scrollbar
behaviour, including the catalogue's header/origin handling.

Only the active page's bar appears. Empty/short lists have no redundant bar.
Android retains its independent position indicator and existing chrome animation;
page widths, content bounds and footer geometry have not changed.

Validation:

- New desktop scrollbar regression passed for reports, catalogue and Management:
  outside-content geometry, mouse dragging, return to top, inactive-page hiding,
  empty catalogue and Android indicator selection.
- Desktop keyboard paging, Home/End and wheel regression passed.
- Scroll-end bounds and hidden/visible footer regression passed.
- Nonzero Android safe-area and animation regression passed with no binding loops.
- Desktop screenshots were reviewed using an offscreen Qt 6.10.2 runtime. Full
  Windows/Linux release builds were not performed here.

Rebuild the app. No server or database update is needed for this correction.
Client/protocol version stays 0.1.37; Android build code increases to 142.
