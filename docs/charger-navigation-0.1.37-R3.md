# Contract ID addresses and navigation - 0.1.37 R3

Android, Windows and Linux charger cards now append `postal_code` to the address.
If an imported address already contains that postal-code text, it is not repeated.
The existing location fallback and S/N label are retained.

Each card has a navigation-arrow icon beside its ID. The button opens Google Maps
directions through the operating system using a fixed HTTPS Maps URL:

1. A complete, valid latitude/longitude pair takes priority over the address.
2. Missing, partial, malformed or out-of-range coordinates fall back to the address
   (or location, when the address is missing), with its postal code.
3. Without a coordinate pair or address/location, the action is disabled and its
   tooltip/accessibility label explains why. A postal code alone is not a precise
   enough destination for this action.

Both decimal points and decimal commas are supported. Latitude must be within
-90 to 90; longitude within -180 to 180. Empty values are not converted to zero,
and partially numeric text is rejected. Explicit numeric zero remains valid.
The complete destination is URL-encoded to preserve accents and punctuation.

The link uses Google's documented cross-platform directions URL with `api=1`
and `travelmode=driving`. It leaves the origin to Maps. On supported mobile
configurations the link opens Maps; otherwise it opens its web interface.
No Maps API key, location permission or server synchronization is added to Lem.
Route availability and current location are handled by Maps and the device.

Reference: https://developers.google.com/maps/documentation/urls/get-started

Validation:

- New QML navigation regression passed on desktop and narrow Android layouts:
  postal labels, coordinate priority, decimal commas, range/malformed guards,
  zero/boundary values, encoded address fallback, duplicate postal-code avoidance
  and missing-destination disabling.
- Actual button activation was tested with Qt's HTTPS URL handler intercepted
  locally, without opening an external route or making a network request.
- Existing page-end bounds, draft/list-position and horizontal-navigation tests passed.
- Existing desktop scrollbar visibility and mouse-dragging test passed.
- Mobile and desktop card screenshots were visually checked using the offscreen
  Qt validation runtime. Opening Maps on the physical phone remains to be tested.

Rebuild/install the app only. No server/database update is needed. Client/protocol
version remains 0.1.37; Android build code is 144.
