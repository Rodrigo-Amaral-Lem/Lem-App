# Review navigation — 0.1.46 R2

The review action previously aligned a question with the scroll viewport's top,
where the overlay containing the report title and question-group tabs obscured
its beginning. It now reveals the groups and positions the card below the full
overlay height, with the normal page gap.

The action stops kinetic scrolling and dismisses the keyboard, then waits for
lazy group creation, saved-position restoration and the previous editor's commit
before measuring. Queued work is discarded if the report or target changes.

The offset remains bounded by the actual content height. A final question may
remain lower on screen when there is not enough content below it; no extra blank
tail is added. Scroll viewport size and content extent do not change to achieve
the alignment.

Regression: `app/tests/test_review_position.py` failed before this correction
(the first card was 143 px behind the group overlay in the desktop fixture).
It checks first, middle and last questions, same-group and cross-group jumps,
shown/hidden chrome, desktop/mobile/landscape sizes, and stable scroll bounds.
Full report UI and measurement-input tests also run offscreen with a mock
backend. Physical Android verification remains necessary.

App-only change: version 0.1.46, Android build code 156. No server update or
database reset is required.
