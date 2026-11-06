# F11 billboard palette cache

The previous SDL billboard cache hashed all 256 palette entries. Any change
recreated, converted, and uploaded every subsequently submitted texture.
Color cycling is enabled by default; `cycle.cc` updates colors 229 through
255, with its fastest timer at 1000/30 milliseconds. Even sprites using none
of those colors lost their cached textures on those updates.

The cache now records the palette entries used by each texture's opaque
pixels and the palette captured at upload. On a new palette revision it
compares only those entries. Matching textures are reused without conversion,
texture creation/destruction, or upload. A successful comparison is remembered
for the revision, so multiple wall-clipped submissions do not repeat the scan.
Changes are compared exactly, without hash collisions.

The snapshot is retained while a texture is invisible, so multiple intervening
palette changes are handled correctly. Index zero is transparent and does not
depend on its palette RGB. Used animated colors and fade changes still rebuild
textures. Dimension mismatches and renderer teardown retain their existing
cache replacement/clear behavior.

Cost: approximately 800 extra bytes of palette/dependency storage per cached
texture, plus a revision field. Each visible texture scans at most 255 palette
entries once per changed palette revision. This replaces per-pixel conversions
and GPU allocations/uploads for textures unaffected by cycling. Benefit depends
on the visible artwork; no measured FPS improvement is claimed yet.

## Validation

The standalone regression tests compare cache validity with independently
expanded RGBA pixels. They cover every RGB component of every palette entry,
transparent pixels, ordinary and cycling colors, unseen intermediate changes,
new upload snapshots, and fading to black. They require no game data or SDL:

```powershell
cmake -S tools/tests -B out/palette-tests
cmake --build out/palette-tests --config Release
ctest --test-dir out/palette-tests -C Release --output-on-failure
```

In-game comparison: use the same F11 camera in a dense scenery area with
color cycling enabled. Compare FPS and frame times before/after, after the
initial texture warmup. Also check animated fire/monitor sprites, fades,
leaving and returning to an area, and F11 toggling for visual regressions.

`git diff --check` passes. Compilation, execution of the regression tests,
and in-game measurement remain pending because this workspace has no available
C++/CMake toolchain or WSL installation.
