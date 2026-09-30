# FSOS GUI concept implementation

This revision moves the native GOP GUI toward the supplied concept image.

## Core rendering changes
- Native GOP logical canvas now expands to the whole physical screen using the existing integer scale factor; the previous centered 320x200 letterbox is removed.
- Theme metrics remain in logical pixels so they are not scaled twice.
- Shell/UI Chinese glyphs use the embedded 24x24 glyph source reduced to an 8x8 logical cell, matching ASCII spacing and then rendered through the native AA path.
- Common unsupported UTF-8 UI symbols are mapped to safe ASCII glyphs instead of being emitted byte-by-byte as `?`.

## Shell changes
- Desktop icons use a single left-hand column.
- Dock is compact and centered.
- Right system panel is compact enough for the upper-right corner.
- A restrained top status bar is added.
- Start menu tiles are reduced so the menu remains coherent on the expanded logical canvas.
- Window defaults are smaller and centered for modern desktop composition.

## Login
- Replaced the old blue/gray list-style login with a dark, rounded, centered FSOS login card.

## Validation
- Existing automated test suite: 41/41 passing.
- Freestanding C syntax checks pass for the modified GUI/rendering sources.

A full UEFI/VMware build still requires the project's Windows toolchain (MinGW/NASM) and should be run on the user's Windows development environment.
