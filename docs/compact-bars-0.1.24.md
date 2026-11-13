# Compact application bars — 0.1.24

Client update only. Recompile/install Lem 0.1.24 (Android version code 124).
Android package remains `pt.lem.app`; no server update or database reset is needed.

## Footer

- A single persistent row contains the operation-status icon, **Sinc. (N)** and,
  when a report is open, **Rever** or **Resumo**. The pending count remains visible.
- The geometric sync-arrows icon rotates while network operations or local saving
  are active. Its button has a filled teal background while active and returns to
  a stationary icon when idle. The icon does not require platform font glyphs.
- The status icon is always available, including while a request is busy. It opens
  the existing **Estado da operação** dialog with detailed stages, progress bars
  and byte counts. It does not itself start synchronization; **Sinc.** retains that action.
- Long operation names/photo-download summaries stay in the dialog and accessible
  button label. They no longer create another footer row. Desktop also shows a
  one-line summary beside the icon.
- Short-lived messages float above the content in compact mode instead of changing
  footer height. Tap the message to open full details or use its close icon. Modal
  dialogs remain above this feedback surface.

## Header and report heading

- The compact header removes extra vertical padding and the redundant loading
  circle. Progress remains conspicuous through the animated footer action.
- Back/menu/account controls have smaller 36 px visual surfaces inside unchanged
  48 px Android touch targets. Account login/demo/guest badges remain visible.
- Compact page margins and report-title spacing are reduced; the question-group
  strip uses its actual 56 px tab height rather than an extra empty vertical gap.
- The palette, report actions, ownership/conflict guards and synchronization timing
  are unchanged. These changes affect presentation and navigation to the existing
  status dialog, not the upload/download algorithms.

## Verification

Mock-backed QML interaction tests cover phone and desktop sizes and an additional
320 px phone width. They check stable footer height across idle/busy/long-message
states, header height, 48 px touch targets, narrow-screen button bounds, status-dialog
access while busy, animation during activity and stopped animation when idle.
Existing report/menu/camera/notes/review/conflict interactions remain covered.

The Qt 6.10.2 offscreen phone preview measures approximately 49 logical px for each
bar, including the style divider; the footer no longer grows during downloads.
A physical Android/Qt 6.12 build remains a separate validation step. System status
and navigation bars are controlled by Android and are not resized by this change.
