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

The ARTEMPLE map overrides the prototype FIDs on several placed scenery
objects. Its facade instances and actual artwork are:

| Proto ID | Map FID frame | Map-instance FRM |
|---:|---:|---|
| 1359 | 1373 | `TEMPLE01.FRM` |
| 1360 | 1374 | `TEMPLE02.FRM` |
| 1361 | 1375 | `TEMPLE03.FRM` |
| 1362 | 1376 | `TEMPLE04.FRM` |
| 1363 | 1377 | `TEMPLE05.FRM` |
| 1364 | 1378 | `TEMPLE06.FRM` |
| 1368 | 1382 | `temple07.frm` |
| 1369 | 1383 | `temple08.frm` (hidden; layout anchor only) |

The fire pits are map objects 1365 and 1366 (frames 1379 and 1380). Proto
1368's prototype default is `HOWITZR2.FRM`, but its placed ARTEMPLE instance
uses `temple07.frm`. Proto 1370 has no placed instance in ARTEMPLE.

### Prototype membership correction

The backwall group is based on the placed ARTEMPLE object IDs, not the
prototype-default FIDs: 1359-1364 and 1368-1369. This includes the door-typed
1359 object because its map-instance art is `TEMPLE01.FRM`; it excludes the
fire pits and the unplaced proto 1370. The regression test covers these
inclusions and exclusions. Proto 1369 remains in the group as a layout anchor,
but its pixels are hidden; its replacement `temple07.frm` profile is used only
to preserve the tier alignment. Objects 1359 and 1368 retain their
map-projected horizontal positions and map-derived vertical levels rather than
being packed into an artificial row. Proto 1363 (`TEMPLE05.FRM`) and proto 1364 (`TEMPLE06.FRM`), the
rightmost upper panels, are lowered by half a world unit to keep their seam
aligned. All panels share the same backwall depth plane.

The shared Temple backdrop plane is moved two map rows back (1.0 world unit)
and four map columns left (96 projected-X pixels) as a group, to clear and
align the actual door opening.

### Base flattening and overlapping panels

The original backwall renderer aligned every source column's lowest opaque
pixel to the common panel base. That incorrectly treated cutouts and isolated
low pixels as ground, shifting the whole column and visibly tearing the
texture below the head. Temple panels now flatten against a fitted isometric
ground line only when the profile has a reliable map-aligned lower edge;
irregular silhouettes retain their actual per-column base to avoid introducing
gaps. When panels overlap at the shared depth, the software renderer permits
later opaque panel pixels to replace an exact depth tie, matching the
accelerated renderer's stable submission order. Both rules are limited to the
Temple backwall; generic scenery is unchanged.

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

The renderer's existing per-column ray intersection, wall/sill-depth clipping,
projected-floor clipping, CPU/GPU drawing, transparency, and hit-target paths
are reused. The override also includes narrow or flat-flagged Temple facade
panels that the general large-scenery classifier would not otherwise select.
If a grouped panel cannot be loaded or has no opaque pixels, the code falls
through to the existing billboard behavior, with its anchor placed at the
common backwall depth.
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
  - Adds a fitted source baseline and explicit equal-depth compositing rule for
    the shared panorama.
  - Retains the earlier classifier addition: tall, clean facade art can use
    the large-scenery path when its base follows a Fallout map-axis slope.
- `tools/tests/first_person_scenery_test.cc`
  - Added assertions that backwall panels share depth and axis while
    preserving projected horizontal placement, relative vertical placement,
    and scale, including the compensation for shared-plane depth in projected X.
  - Verifies all eight Temple prototype IDs and rejects adjacent gate,
    fire-pit, and howitzer IDs.
  - Verifies clean profiles use the fitted baseline, irregular profiles keep
    their actual lower edge, and equal-depth panels can composite.
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

For the fitted-baseline and equal-depth compositing correction:

- `cmake --build out/build/windows-x64-vs18 --config Release --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Release/Fallout2-FPS.exe`.
- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `git diff --check` — passed.

After adding the reliability guard for irregular Temple profiles, only
`git diff --check` was rerun. Per the user's preference, the game build and
renderer tests for that final adjustment are left for the user.

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
