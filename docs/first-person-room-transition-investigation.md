# F11 room transition hitch investigation

Reported symptom: a brief lag spike on entering or leaving an enclosed room.
The cause is not yet confirmed by a runtime capture.

## Source findings

- `objectSetLocation` in `object.cc` compares the current and previous roof
  regions when the player changes square. Crossing a boundary synchronously
  restores the old roof and hides the new one with `tile_fill_roof`, then
  expands the dirty rectangle to the screen size.
- `tile_fill_roof` uses a stack to flood-fill connected roof tiles. Its cost
  depends on the connected roof area, not just the doorway. It runs outside
  the existing F11 render timer.
- The F11 floor cache already compares only `FloorTileFrmId(...).fid()`.
  `HalfTileFrmId` masks this to the low 16 bits, so roof visibility changes
  alone do not invalidate the cache. A rebuild remains expensive if an actual
  floor change occurs: 100 x 100 cells, 16 x 16 samples per cell, up to nine
  neighboring tiles per sample.
- `tileRefreshGame` queues an F11 refresh regardless of dirty rectangle size;
  expanding that rectangle does not itself cause an isometric roof render.
- Existing 60-frame averages obscure isolated spikes. Floor-cache construction
  is included in total render time but excluded from the floor-stage timer.
  Renderer presentation and GPU texture uploads are outside this timer too.

## Added diagnostics

All new timing is gated by the existing `[debug] show_fps=1` setting:

- `F11 roof fill`: timestamp, player tile, elevation, starting square,
  visibility direction, processed stack tasks, and elapsed milliseconds.
  Task count includes repeated visits, not just distinct roof tiles.
- `F11 floor cache rebuild`: timestamp, location, and cache-build duration.
- `F11 slow frame`: individual renders taking at least 50 ms, with setup,
  cache check/build, floor, wall, actor submission, and overlay durations.
  Cache time is part of setup; these fields must not be added together.

These are investigation changes, not a confirmed performance fix. Millisecond
timers and debug logging are suitable for locating a large hitch, not precise
microbenchmarks. Logging may itself perturb frame times.

## Reproduction

1. Build the instrumented executable. In the game's `fallout2.cfg`, set
   `[debug] show_fps=1` and `mode=log`.
2. Use ordinary F11 mode, with experimental F8 mode off. Walk repeatedly into
   and out of the same room. Include a similar walk in open space as a control.
3. Preserve `debug.log` before restarting the game (the log is overwritten).
   Compare timestamps around `F11 roof fill` with nearby slow frames/rebuilds.
4. A costly roof fill implicates the synchronous region traversal. A costly
   rebuild requires investigating actual floor mutations; roof bits alone
   should not trigger it. Actor spikes, especially only on first exit, warrant
   investigating newly visible art. If these CPU timings remain short during
   the hitch, profile presentation/texture upload and the movement loop next.

Validation so far: source-path inspection and `git diff --check`. No runtime
capture or compile was possible in this workspace: no configured build tree
or available C++/CMake toolchain was found.
