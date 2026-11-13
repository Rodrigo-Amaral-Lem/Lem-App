# Measurement editing — 0.1.46 R1

Voltage and earth resistance inputs now edit only the numeric portion. The unit
is drawn separately at the right edge, while storage, sync and PDF output retain
the canonical `238,3 V` / `4564,4 Ω` representation.

The button reads **NA** (accessible name: Não Aplicável). Selecting it displays
NA as a placeholder in an empty numeric editor. Typing a new reading replaces
that state immediately; losing focus without typing preserves it. Free text
such as a partially erased NA label cannot be entered in numeric fields.

The underlying limits, automatic NOK and server protocol are unchanged. This is
an app-only revision of 0.1.46; the server does not require an update.

Validation: offscreen QML tests exercise real key input, precision limits,
editing a previously formatted voltage, replacing NA with a reading, shared
measurement cases and NOK boundaries. Physical Android testing is still needed.
