# FSOS Terminal Scroll Fix V27

- Terminal output now has a persistent scroll offset instead of always rendering only the newest rows.
- PageUp/PageDown scroll a page at a time.
- VMware VMMouse wheel Z deltas are exposed by the mouse driver and routed by WM as wheel pseudo-keys.
- Wheel scrolling moves three terminal rows per notch and only affects the focused window under the pointer.
- A modern scrollbar thumb is displayed when output exceeds the viewport.
- New command execution snaps the terminal back to the newest output.

Validation performed in this environment:
- `tools/test_terminal_scroll_v27.py`: all checks passed.
- `gcc -fsyntax-only` for `user/terminal.c`, `user/wm.c`, `kernel/drivers/mouse.c`: passed with project include paths.

VMware runtime scrolling should be verified on the target VM; this environment does not include the user's VMware instance.
