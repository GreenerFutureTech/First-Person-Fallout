# F11 cursor hold-menu compositing fix

The F11 action menu creates a fullscreen `WINDOW_TRANSPARENT` window, filled
with zero except for its menu artwork. The GPU presentation path previously
restored every visible window's complete rectangle from the CPU framebuffer
after drawing billboards. That framebuffer contains the floor/walls and UI,
but not GPU sprites. Restoring the fullscreen menu rectangle therefore erased
the sprites for as long as the menu was visible.

There was a second overwrite path: generic `_GNW95_ShowRect` dirty rectangles
were recorded and replayed over billboards. A window refresh rectangle can
contain the base scene, especially when refreshing a transparent window, so
it is not a reliable indication of opaque UI coverage.

F11 now uses live window coverage for UI restoration. Opaque windows retain
their full rectangle; buffered transparent windows contribute only nonzero
pixel runs, merged vertically where possible. Generic dirty rectangles are
not recorded/replayed as F11 overlays. Cursor restore/draw handling remains
separate, and non-F11 overlay behavior is preserved.

## Validation

Added standalone C++ tests for exact nonzero coverage, a fullscreen transparent
menu containing a small opaque block, rectangle coalescing, cleared menus,
offscreen origins, clipped source offsets, and randomized transparency masks.
`git diff --check` passes. Compilation, C++ test execution, and in-game testing
are pending because no C++/CMake toolchain is available here.

```powershell
cmake -S tools/tests -B out/render-tests
cmake --build out/render-tests --config Release
ctest --test-dir out/render-tests -C Release --output-on-failure
```

Manual checks: enter F11 cursor mode with critters/scenery visible; click,
hold to open the action menu, move through its entries, release/cancel, and
open the skills menu. Sprites should remain visible outside opaque UI pixels,
including the transparent area of the action-menu window. Also check opaque
inventory/dialog windows, cursor movement, and ordinary isometric menus.
