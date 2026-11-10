# F11 Temple of Trials backwall handoff

## User request

The Temple facade panels were now visibly affected by the large-scenery
renderer, but still looked like separate pieces. The user proposed treating
them as one picture: align them on the same X axis and put them at the deepest
position of the scenery, as they are meant to read as one backwall.

The attached screenshot showed the Temple art as separated floating fragments
above the graybox wall row. The intended change was limited to F11 rendering;
the isometric map view and gameplay collision were not to change.

## What changed

The Temple facade scenery is now recognized as a specific F11 backwall group.
Its panels are rendered on a shared world-space horizontal plane rather than
each panel deriving its own ground direction and depth from its isometric
artwork.

The grouped prototypes are scenery proto IDs `1360` through `1365`, plus
`1369` and `1370`:

| Proto ID | FRM |
|---:|---|
| 1360 | `TEMPLE01.FRM` |
| 1361 | `TEMPLE02.FRM` |
| 1362 | `TEMPLE03.FRM` |
| 1363 | `TEMPLE04.FRM` |
| 1364 | `TEMPLE05.FRM` |
| 1365 | `TEMPLE06.FRM` |
| 1369 | `temple07.frm` |
| 1370 | `temple08.frm` |

Those identities were checked against the scenery proto FIDs and `SCENERY.LST`
in the user's Fallout data installation. The neighboring IDs are not all part
of the facade: proto 1359 is `gate4.frm`, 1366-1367 are the two fire pits, and
1368 is `HOWITZR2.FRM`. None of those should be forced into the backwall.

### Prototype membership correction

The original hardcoded set was off by one: it omitted `TEMPLE06` and
`temple08`, and included the preceding gate and the howitzer. This is the
specific reason facade graphics could be absent or unrelated art could be
treated as part of the panorama. Membership is now based on the verified
prototype IDs above and covered by a regression test for both inclusion and
neighboring exclusions. The current ARTEMPLE state dump contains placed
instances of the Temple01-07 set; `temple08` is available as a facade asset
but is not placed in that map snapshot.

At render time, F11 scans visible instances in the current elevation to find
the panorama's vertical anchor and uses the map's entering tile to place the
shared plane 7.5 world units in front of it. For ARTEMPLE, the entering tile
is row 92 and the plane is approximately row 77, one map row farther toward
the facade than the previous row-78 placement. The previous camera-relative
far-edge choice used row 65 when approached from the south, and the later
nearest-edge choice left the wall at row 82. The first version retained only each panel's
world-X anchor, which flattened away the map's isometric ordering and placed
every panel at the same vertical base. The latest adjustment uses the tile's
isometric screen projection: projected screen X positions each panel across
the shared plane, with the shared plane's Y contribution subtracted when
converting screen X back into world X. This prevents a depth change from
shifting the complete panorama horizontally. Projected screen Y raises or
lowers panels relative to the lowest panel anchor at 48 projected pixels per
world unit. The earlier 24-pixel conversion doubled vertical separation and
left large gaps between the panels. The artwork also scales at 48 pixels per
world unit instead of using the generic scenery's shorter 80-pixel scale.

The renderer's existing per-column ray intersection, wall-depth clipping,
CPU/GPU drawing, transparency, and hit-target paths are reused. The override
also includes narrow or flat-flagged Temple facade panels that the general
large-scenery classifier would not otherwise select. If a grouped panel cannot
be loaded or has no opaque pixels, the code falls through to the existing
billboard behavior, with its anchor placed at the common backwall depth.
One Temple facade prototype is typed as a door in its proto data; the F11 door
exclusion now runs after backwall recognition so this panel is not dropped.
The large-scenery path likewise allows the recognized Temple group through
even when its proto subtype is not generic. This exception does not affect
ordinary doors.

## Files changed for this request

- `src/tile.cc`
  - Added Temple backwall prototype recognition.
  - Added a per-frame scan of visible facade instances at the current
    elevation and anchors the shared depth 7.5 world units in front of the
    map's entering tile.
  - Routes recognized Temple facade pieces through the backwall before the
    ordinary door exclusion, including the facade proto typed as a door.
  - Added backwall group data to `TileFirstPersonActorRenderContext`.
  - Routed matching scenery through a shared-axis/shared-depth slab while
    preserving the existing renderer and fallback path.
- `src/first_person_scenery.h`
  - Added `firstPersonSceneryBackwallSlab`, which constructs a horizontal
    world-X slab at the shared depth while preserving each panel's projected
    map placement, horizontal offset, and relative vertical offset.
  - Defines the verified Temple facade prototype set.
  - Retains the earlier classifier addition: tall, clean facade art can use
    the large-scenery path when its base follows a Fallout map-axis slope.
- `tools/tests/first_person_scenery_test.cc`
  - Added assertions that backwall panels share depth and axis while
    preserving projected horizontal placement, relative vertical placement,
    and scale, including the compensation for shared-plane depth in projected X.
  - Verifies all eight Temple prototype IDs and rejects adjacent gate,
    fire-pit, and howitzer IDs.
  - Retains the earlier tests for clean map-aligned facade classification and
    rejection of noisy bases.
- `docs/first-person-large-scenery.md`
  - Updated the classifier description and documented the Temple-specific
    backwall behavior and scope.

The existing untracked files `docs/first-person-graphics-plan.md` and
`docs/first-person-scenery-visual-quality.md` were not modified as part of this
change.

## Validation performed

- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed (`first_person_scenery`,
  `indexed_texture_palette`, `billboard_clip_bands`, and
  `transparent_window_rects`).
- `cmake --build out/build/windows-x64-vs18 --config Debug --target fallout2-ce`
  — passed; produced
  `out/build/windows-x64-vs18/Debug/Fallout2-FPS.exe`.
- `git diff --check` — passed.
- The game build emitted a warning in the unrelated existing file
  `src/sfall_arrays.cc` (C4312 pointer-size conversion); it did not prevent the
  build.

For the height/order adjustment requested with the previous screenshots:

- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `cmake --build out/build/windows-x64-vs18 --config Debug --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Debug/Fallout2-FPS.exe`.
- `git diff --check` — passed.

For the entrance-depth adjustment:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

For the projected-X centering adjustment:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

For the latest depth adjustment:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

For the one-row depth and panel-spacing adjustment:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

For the missing door-typed facade panel fix:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

For the Temple prototype membership correction:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed, including the Temple prototype inclusion and
  exclusion checks.
- `git diff --check` — passed.

A post-change in-game visual check remains necessary. Run the rebuilt
executable, load the Temple scene, and verify that the backdrop is centered
behind the doorway, panel order and vertical placement match the isometric
image, the increased height does not clip important artwork, and walls/actors
still occlude the panorama correctly. In-game visual verification is not
available from this session.

## Important implementation detail / next consideration

The shared plane is anchored 7.5 world units in front of the map's entering
tile, not at a camera-relative or facade-group edge. This is stable as the
camera moves and places the panorama toward the Temple facade. The wider
distance is intended to reduce visible gaps caused by perspective; in-game
verification is needed to confirm whether the panels connect correctly.

All modifications remain uncommitted on branch `FPS-Co-op-Build`. No commit or
push was made.
