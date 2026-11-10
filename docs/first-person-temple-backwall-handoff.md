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

The grouped prototypes are scenery proto IDs `1359` through `1364`, plus
`1368` and `1369`:

| Proto ID | FRM |
|---:|---|
| 1359 | `TEMPLE01.FRM` |
| 1360 | `TEMPLE02.FRM` |
| 1361 | `TEMPLE03.FRM` |
| 1362 | `TEMPLE04.FRM` |
| 1363 | `TEMPLE05.FRM` |
| 1364 | `TEMPLE06.FRM` |
| 1368 | `temple07.frm` |
| 1369 | `temple08.frm` |

Those identities were resolved from `ARTEMPLE.MAP` and the scenery art/proto
lists in the user's Fallout data installation. The fire-pit scenery and
invisible collision/blocker objects are not part of this group.

At render time, F11 scans visible instances in the current elevation to find
the group's minimum and maximum world Y. It chooses the far Y edge based on
which side of the group the camera occupies. Every panel's backwall slab uses
that Y and a fixed world-X axis. The first version retained only each panel's
world-X anchor, which flattened away the map's isometric ordering and placed
every panel at the same vertical base. The latest adjustment uses the tile's
isometric screen projection: projected screen X positions each panel across
the shared plane, and projected screen Y raises or lowers it relative to the
lowest panel anchor. It also scales panorama height at the same 48-pixel per
world-unit ratio as its horizontal span instead of using the generic scenery's
shorter 80-pixel scale.

The renderer's existing per-column ray intersection, wall-depth clipping,
CPU/GPU drawing, transparency, and hit-target paths are reused. The override
also includes narrow or flat-flagged Temple facade panels that the general
large-scenery classifier would not otherwise select. If a grouped panel cannot
be loaded or has no opaque pixels, the code falls through to the existing
billboard behavior, with its anchor placed at the common backwall depth.

## Files changed for this request

- `src/tile.cc`
  - Added Temple backwall prototype recognition.
  - Added a per-frame bounds scan over visible facade instances at the current
    elevation and selected the far edge relative to the camera.
  - Added backwall group data to `TileFirstPersonActorRenderContext`.
  - Routed matching scenery through a shared-axis/shared-depth slab while
    preserving the existing renderer and fallback path.
- `src/first_person_scenery.h`
  - Added `firstPersonSceneryBackwallSlab`, which constructs a horizontal
    world-X slab at the shared depth while preserving each panel's projected
    map placement, horizontal offset, and relative vertical offset.
  - Retains the earlier classifier addition: tall, clean facade art can use
    the large-scenery path when its base follows a Fallout map-axis slope.
- `tools/tests/first_person_scenery_test.cc`
  - Added assertions that backwall panels share depth and axis while
    preserving projected horizontal placement, relative vertical placement,
    and scale.
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

For the latest adjustment requested with the new screenshots:

- `cmake --build out/build/render-tests` — passed.
- `ctest --test-dir out/build/render-tests -C Debug --output-on-failure` —
  all 4 renderer tests passed.
- `cmake --build out/build/windows-x64-vs18 --config Debug --target fallout2-ce`
  — passed; produced `out/build/windows-x64-vs18/Debug/Fallout2-FPS.exe`.
- `git diff --check` — passed.

A post-change in-game visual check remains necessary. Run the rebuilt
executable, load the Temple scene, and inspect the backwall from both sides
and while turning. Verify that panel order and vertical placement match the
isometric image, the increased height does not clip important artwork, the far
edge stays behind the player, and walls/actors still occlude the panorama
correctly.

## Important implementation detail / next consideration

“Deepest position” is currently approximated as the farther of the group's
minimum or maximum world-Y edge, selected according to the camera's side of
the group's Y midpoint. This makes the backwall remain behind the camera when
approached from either side. It is not a permanent map-authored depth and it
does not infer an exact visual footprint; it is a group-wide F11 projection
rule. If the in-game check shows a jump when crossing the midpoint, or a
specific Temple entry should expose the wall from behind, replace the
camera-relative selection with an explicit ARTEMPLE-specific backwall depth
and document the intended viewpoint.

All modifications remain uncommitted on branch `FPS-Co-op-Build`. No commit or
push was made.
