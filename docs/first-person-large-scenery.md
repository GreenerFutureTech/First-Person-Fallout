# Large scenery disappearing and splitting in F11

The reported reproduction is the map where a new character starts (Temple of
Trials). The findings below come from the renderer and the checked-in
`tests/golden/artemple_idle.golden.txt` snapshot, not an in-game visual capture.

## Why it happens

`tileFirstPersonRenderActor` previously treated every non-door scenery object
as a camera-facing billboard, regardless of its footprint:

- It rejected objects using their anchor depth (`transformY <= 0.1`). A long
  structure can still extend in front of the camera after its anchor passes
  behind it.
- Horizontal culling used a one-unit radius, followed by a narrow placeholder
  silhouette. Both tests ran before the real FRM width was known. A visible
  end could therefore disappear when only the center/placeholder was offscreen
  or behind a wall.
- The whole image used one depth: its anchor's depth. Comparing that against
  the wall depth at each screen column can cut holes through a long image.
  GPU sorting likewise gave the entire image one depth relative to actors and
  other scenery. The CPU path also used that constant depth.
- Camera-facing art rotates the visible span while collision and adjacent
  scenery stay fixed. Pieces that meet from one direction can separate or
  overlap from another. Changing the sampled FRM rotation with the camera can
  also change the silhouette. Merely increasing the billboard width does not
  address this representation mismatch.

The graybox pass deliberately excludes ordinary scenery to avoid hiding trees
behind solid cells. Making all scenery into wall cells would reintroduce that
problem. It would also miss scenery marked `OBJECT_NO_BLOCK`: the starting
map's snapshot contains such pieces alongside separate blocking-hex objects
(PID `0x02000158`). Rendering classification must not require a collision flag.

## Implemented approach

Large generic scenery uses a fixed, shallow textured volume instead of a
billboard. `first_person_scenery.h` contains its independently testable math.

The automatic rule selects non-flat generic scenery whose opaque span is at
least 160 source pixels wide and has a vertical column at least 64 pixels
tall. Normally it must also be at least 1.5 times wider than its tallest
column. A taller facade may bypass that ratio only when its fitted lower edge
matches one of Fallout's two map-axis slopes within 0.08 and has at most 2
pixels RMS error. This admits clean facades such as `TEMPLE04.FRM` (241 pixels
wide, 198-pixel maximum column, slope -0.259, 0.3-pixel RMS error), which the
original ratio excluded, while rejecting irregular composites. Using column
height avoids mistaking a long isometric wall's ground slope for tree height.
Doors, small props, tall narrow foliage and flat ground decorations retain
their existing paths. The rule does not depend on movement or lighting flags.

The renderer fits the artwork's lower edge and reverses the isometric ground
projection to estimate its direction, length and placement. It honors the
object and FRM offsets, uses the object's stored rotation, and grounds texture
columns individually to remove their baked-in isometric slope. The volume is
0.2 world units thick; its end faces reuse tall texture strips. Transparent
openings in the original artwork remain transparent.

Each screen ray intersects the full footprint and obtains its own surface
depth and texture coordinate. Conservative bounds include all four corners
and retain volumes crossing the near plane. GPU rendering submits clipped
texture columns through the existing depth-sorted queue; CPU rendering uses
the same intersections with its per-pixel depth buffer. Wall, sill and door-cap
clipping use each column's depth. Wall texture spans use a separate per-pixel
depth buffer to order textured wall hits against each other; it does not
depth-test those textures against scenery artwork. Scenery picking uses the
corresponding source column and visible vertical intervals.

Ordinary billboards now cull against the actual opaque artwork bounds. The
placeholder bounds are used only when artwork cannot be loaded.

No map, save, object flags, movement or collision rules are changed.

## Verification and remaining visual check

The standalone renderer suite includes `first_person_scenery_test`. It checks
classification, both isometric ground directions, art/object offsets,
transparent openings, reverse and end-on views, parallel rays, a camera inside
the volume, and near-plane crossings. It compares 160,000 rays against an
independent four-segment intersection reference, including verification that
screen culling never discards an intersecting ray.

Initial validation on `co-op_merge`: all four CTest renderer tests passed. `tile.cc`
compiled with GCC in the game configuration and passed a separate mapper
configuration syntax check. `git diff --check` passed. A clean full-game GCC
build stopped on unrelated errors in unchanged files: missing
`gProgramIsActive` in `audio_engine.cc`, an `int`/`MapElevation` conversion in
`interpreter_extra.cc`, and obsolete `PID_TYPE`/`_critter_flag_check` names in
`invariants.cc`. No runnable updated game binary or in-game visual result is
claimed from that build.

The fix was subsequently ported to `FPS-Co-op-Build`, preserving that branch's
combat outlines, camera controls and actor sizing. All four renderer tests
passed again and the modified `tile.cc` compiled with GCC on that branch.
The full-game build failures above describe the original branch; a full game
build and visual verification have not been repeated for the destination branch.

Run the suite with:

```text
cmake -S tools/tests -B out/build/render-tests
cmake --build out/build/render-tests
ctest --test-dir out/build/render-tests --output-on-failure
```

The footprint is an estimate from 2D artwork, not an authored 3D mesh. Irregular
or curved backdrops can need explicit per-art geometry in a later refinement.
Visual verification remains necessary with the installed game assets: start a
new game, enable F11, turn beside the long scenery at the starting location,
then walk alongside both ends and look back. Check that the pieces remain
fixed, their visible ends survive turns, and wall/doorway occlusion stays
correct. Compare the software fallback with the accelerated renderer. Toggle
F11 off to confirm the original isometric view remains unchanged.

## Temple of Trials backwall

The eight Temple facade scenery prototypes (1360-1365, 1369, and 1370) are a
single panorama in F11. The adjacent gate (1359), fire pits (1366-1367), and
howitzer (1368) are separate objects, not backdrop panels. Each panel is
placed using both components of its original isometric tile projection:
projected screen X determines its horizontal position on the
shared world-X plane, and projected screen Y determines its vertical offset.
This preserves the source map's left-to-right ordering and places map pieces
that appear higher in the isometric scene higher in the F11 panorama. The
horizontal conversion subtracts the shared plane's isometric Y contribution
before placing panels along world X, keeping the backdrop centered when its
depth changes. Backwall columns use a fitted base line only when the profile
has a reliable map-aligned lower edge; irregular silhouettes retain their
actual per-column base to avoid introducing gaps. Equal-depth overlapping
Temple panels composite in map order in the software renderer, matching the
accelerated path. Vertical spacing uses the same
48-pixel-per-world-unit scale as horizontal placement, preventing isometric
Y values from exaggerating the gaps between panels. Panel height uses this
same scale rather than the shorter generic-scenery scale.

The Temple panels are ordered by facade prototype (1360-1365, 1369, then
1370) and packed edge-to-edge using each FRM's visible horizontal span,
centered around their original group bounds. This removes gaps and overlaps
caused by placing each panel at its independent isometric tile position while
preserving the intended left-to-right artwork sequence. All panels share a
common baseline. The resulting panorama is
anchored to the solid wall tile nearest the facade group's center, rather than
a fixed distance from the entry tile. Temple panels render over the procedural
wall rather than being clipped to its per-column ray hits, so gaps in the wall
graybox do not break up the packed art; transparent areas remain transparent.
If no solid wall tile is found, the existing shared-plane placement 7.5 world
units in front of the entry tile is used. This map-specific
override includes the narrow and flat-flagged panels that the general
large-scenery heuristic intentionally leaves alone. Fire-pit props and
unrelated collision/blocker objects are not included. The isometric renderer,
movement, and collision continue to use the original map objects.

## Temple blocked-floor boundaries

F11 derives floor appearance from floor FRMs, while movement uses blocking
objects. A blocked tile therefore does not automatically look like a wall.
On Arroyo Temple, F11 searches within eight hexes of the facade panels and the
two fire pits (prototypes 1366 and 1367), then draws half-height wall cells on
the perimeter of blocked tiles. The search is limited to those known landmark
areas and its result is cached per map and elevation; it does not scan other
maps. Doors, actors, facade panels, and fire-pit props are excluded from the
blocking test. Movement and collision remain authoritative and unchanged.

This is intentionally Temple-specific. A general rule based on all blocking
scenery affected unrelated maps and was reverted after F11 began crashing on
Klamath; the exact crash cause was not confirmed. Apply the visual treatment
to another map only after identifying its specific blocked-floor landmarks
and validating it in-game.

On Arroyo Village, the central Stone Monument (`head1.frm`, scenery prototype
938) is rendered as an enlarged, depth-aware slab. Its nine-tile front wall
segment (wall prototype 621) is suppressed in F11 so the monument fills that
space rather than appearing behind a row of graybox columns. Other wall pieces
and collision remain unchanged. The northern Mountain and Cavern scenery
(prototypes 1370 and 1371) are rendered as depth-tested backdrops at the north
map edge rather than foreground-facing billboards. The F9 topology overlay
shows the center ray's first solid wall hit as a yellow tile outline and prints
its tile number, row, and column for diagnosing occlusion mismatches.

### Diagnosing scenery that draws through walls

Start with the exact center-ray hit rather than guessing which nearby prop
supplies the artwork. Enable the F9 topology overlay while aiming at the
offending image; read the displayed tile/row/column, then inspect the map
objects at that tile to identify the wall prototype and verify the wall appears
in the F11 topology. This establishes that the raycaster found the wall, but
does not by itself prove every scenery pixel is depth-tested against it.

Trace the scenery render path for exceptions that bypass normal wall-depth
clipping. In particular, keep special prototype groups map-scoped: prototype
IDs are not globally unique descriptions of artwork. Arroyo Temple's
`firstPersonSceneryIsTempleBackwallProto` group includes prototype 1370, while
Arroyo Village uses that same ID for its Mountain backdrop. Before this
correction, the Village Mountain was classified as a Temple backwall and its
pixels skipped the ordinary wall-depth test, so it could show through any
building wall even though the center ray correctly hit the building. Gate the
Temple-backwall classification on `MAP_ARROYO_TEMPLE`; do not suppress or move
nearby props to compensate for a render-classification/depth-test error.

Temple facade prototypes are routed into the shared backwall before the
ordinary scenery-door exclusion. This matters because one of the facade
prototypes is tagged as a door in its prototype data even though its art is a
backwall panel. That exception is limited to the recognized Temple backwall
group and the Arroyo Temple map; prototype IDs can refer to unrelated scenery
on other maps.

The F11 click diagnostic identified the unwanted panel as scenery prototype
931 at tile 12727, elevation 2. Earlier suppression attempts also required
`gMapHeader.index == MAP_ARROYO_TEMPLE`; that extra condition did not match the
object in the running game, despite the player being in the Temple area, so
the renderer kept drawing it. Matching the diagnostic's object type,
prototype, tile, and elevation instead excluded the exact instance from both
the F11 actor renderer and graybox builder. When a map-scoped prototype check
fails, capture the object's click diagnostic and prefer its specific instance
identity over assuming the runtime map index. The map object, collision, and
other objects using prototype 931 remain unchanged.
