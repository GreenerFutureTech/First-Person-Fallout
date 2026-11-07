# F11 door-cap billboard clipping

The accelerated billboard path already scans only horizontally when walls
and window sills are the only occluders. Overlapping a door's overhead span
previously switched it to a width-by-height visibility scan, creating temporary
vectors on every row. Large nearby sprites made this fallback expensive.

The updated fallback collects the top and exclusive bottom edges of relevant
door caps. Within each interval between these edges, visibility is constant
vertically: ordinary wall/sill depth depends only on the column, and the caller
already splits at the horizon. It therefore samples one row per interval and
extends the resulting clip rectangles over the interval's full height. Existing
vertical merging still combines identical adjacent runs. Temporary vectors
retain capacity across sprites and frames.

For width W, height H, and B distinct vertical intervals, depth tests fall from
O(W * H) to O(W * B), plus collecting and sorting up to two edges per column.
B cannot exceed H because bounds are distinct integer rows within the sprite.
For example, one flat cap inside a 600-row band produces three width scans
instead of 600. Irregular cap edges can produce many intervals and reduce the
benefit. The no-cap path retains its existing horizontal clipping algorithm.

This changes the CPU work used to build clips, not sprite resolution, view
distance, palette handling, draw order, or the software rendering path. It is
not a confirmed fix for the separate room-transition hitch.

## Validation

- An independently executed PowerShell model compared band samples with the
  former per-pixel visibility rule across 150 randomized scenes, including
  both sides of the horizon: all 307,200 comparisons matched.
- Added C++ regression coverage for empty/reversed/out-of-range caps, exclusive
  endpoints, equal depths, overlapping caps, varied column depths, reused
  storage, and the three-band operation-count example. The tests also compare
  sampled visibility against the per-pixel rule.
- `git diff --check` passes. C++ compilation and execution remain unverified:
  no C++ compiler or CMake is available in this environment.

Run the standalone tests on a development machine:

```powershell
cmake -S tools/tests -B out/render-tests
cmake --build out/render-tests --config Release
ctest --test-dir out/render-tests -C Release --output-on-failure
```

For in-game validation, compare the same F11 view with large sprites behind
an open doorway, then check door edges while moving/turning, overlapping doors,
windows, and a view with no doors. Check both clipping accuracy and actor-stage
timings. No measured FPS gain is claimed yet.
