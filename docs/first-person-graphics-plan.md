# First-person graphics: map-aligned scenery plan

## Goal

Make F11 show a coherent, walkable view of the same scene the isometric
renderer depicts: walls meet at corners, scenery sits on the ground, openings
stay open, and objects are correctly in front of or behind one another as the
camera moves.

The supplied comparisons show two related failures: large scenery art is
projected into fragments that do not follow the visible wall runs, and the
first-person wall representation itself reads as disconnected repeated blocks.
These need to be treated as geometry and scene-composition problems, not
polished away with a scale or texture tweak.

## Core decision

Stop guessing 3D geometry from the dimensions and opaque outline of an
isometric FRM. Use explicit, map-aligned geometry for the structures that need
it, and keep the original isometric renderer as the untouched reference view.

Do not attempt to convert every Fallout asset or build a general-purpose 3D
engine. Prove a small, deliberately authored path on the two demonstrated
scenes first. Preserve existing behavior for scenery without a reviewed
definition.

## What is causing the mismatch

The current large-scenery path in `src/first_person_scenery.h` fits a line to
the image's opaque lower edge, estimates a single span, and wraps source pixels
around a thin slab. That image is an isometric projection, not a facade
texture: it may already contain top faces, side faces, perspective-like
shading, ground slope, and separate scene pieces. Its bottom edge cannot tell
the renderer where the real footprint, corners, or wall joins are. The slab
can therefore be internally consistent as a ray target and still be in the
wrong place or have the wrong shape.

The first-person wall pass in `src/tile.cc` separately derives surfaces from
wall objects placed on the hex grid, traversing a denser grid than the floor
projection. It estimates solid-cell and oriented-wall bounds during ray
traversal. The screenshot's repeated block appearance should be traced against
that map topology before changing its dimensions: the issue may be a mapping,
orientation, adjacency, or corner interpretation rather than just wall height.
Fixing scenery slabs alone will not make these two representations connect.

## Target rendering model

Introduce a small first-person scene description made from vertical textured
surfaces, not inferred slabs:

- Each surface has explicit map/world-space endpoints, bottom and top heights,
  a source art frame and crop, UV orientation, and transparency behavior.
- Long straight wall runs share exact endpoints. Corners use adjoining faces;
  doorways and open entrances are represented as gaps in the geometry, not
  painted black areas.
- Composite landmarks use a few reviewed faces (front, returns, and any
  important side planes). Model only silhouette-changing depth. Avoid turning
  the entire isometric image into one texture stretched over a proxy.
- Geometry is keyed by stable asset/prototype identity plus any necessary map
  instance data. It is never selected solely because an image is wide.
- Texture choices are explicit per face. Reuse an art crop only when it
  represents that face; do not assume alternate isometric rotations are
  perspective-ready wall textures.

Use the same world coordinates as the map and its wall/collision grid as an
alignment reference, while keeping visual geometry independent where the map
uses invisible blockers or decorative backdrops. A visual mesh must not change
movement or collision.

All scenery surfaces must participate in one coherent depth and transparency
contract with procedural walls, doors, the floor, and actors. Software and
accelerated output must agree on geometry, coverage, texture sampling, and
occlusion. Keep the existing indexed-color and palette behavior.

## Delivery plan

### 1. Capture the scene truth

Before changing rendering, make repeatable evidence for each screenshot:

- Record the map/elevation, camera tile and heading, relevant object PID/FID,
  frame/rotation, object offsets, and nearby wall-object tiles and orientations.
- Save paired isometric and F11 captures from the same scene state and
  identifiable camera position.
- Add a temporary/debug overlay for hex and floor-grid coordinates, wall
  orientation, wall-cell bounds, scenery anchors, and ray-hit locations.
- Trace visible wall joins back to the data and the conversion code. Confirm
  whether apparent gaps are missing map segments, reversed axes, corner flags,
  cell-boundary arithmetic, or only a rendering seam.

**Gate:** explain every large fragment and every broken wall run in the
screenshots in terms of a specific object, map boundary, or renderer step.
Do not tune global dimensions until this is known.

### 2. Make wall topology continuous

Treat wall topology as its own deliverable, not as scenery art:

- Verify hex-to-render-grid conversion, including reversed X, elevation,
  boundary cells, and all wall orientations/corner flags.
- Build connected wall runs from adjacent compatible map cells, preserving
  doors, windows, half-height walls, pass-through flags, and actual openings.
- Give adjoining faces shared endpoints and consistent height/texture
  coordinates. Avoid arbitrary per-cell thickness that creates visible gaps
  or doubled corners.
- Keep the ray traversal or surface generation tied to the same resolved wall
  geometry. Do not render one interpretation and use a different one for
  occlusion.

Add topology tests for straight runs, all corner orientations, T-junctions,
door/window breaks, adjacent elevations, and map edges. Include regression
fixtures derived from the two demonstrated scenes.

**Gate:** a debug wireframe follows the intended map wall boundaries with no
missing or doubled segments before final materials are considered.

### 3. Prove one authored scenery surface

Implement the narrowest end-to-end vertical-surface path and add an explicit
definition for one simple, straight wall segment in the problem scene:

- Place it from known map coordinates, not from its FRM dimensions.
- Map a reviewed facade crop to the segment; initially prefer a clean,
  repeatable crop over trying to preserve every isometric detail.
- Render its exact endpoints, height, transparency, palette, and per-pixel
  depth in both software and accelerated modes.
- Keep the old rendering path available for unconfigured assets and retain an
  easy way to compare old and new output during development.

**Gate:** the test segment meets its map boundaries at multiple camera angles,
has no holes at the ends, and correctly occludes an actor placed both in front
of and behind it.

### 4. Author the two demonstrated structures

For each target structure, create explicit data for only the visible geometry
needed to read it correctly:

- Divide a long facade into continuous map-aligned runs.
- Add side returns at real corners and leave entrances open.
- Place large landmark faces from their actual footprint and height; split
  separate pieces instead of assigning the whole composite FRM one footprint.
- Select source crops per face and validate them at front, oblique, and
  end-on views. If the installed art has no suitable face texture, document
  that content limitation rather than stretching an unrelated isometric view.

Keep the definitions close to the renderer's existing conventions and easy to
audit. Avoid a new external asset format or a large editor until the first
couple of definitions demonstrate that hand-authored data is too cumbersome.

**Gate:** in paired captures, the first-person structure's base, corners, and
openings correspond to the same scene boundaries seen in isometric view.

### 5. Integrate and harden

- Replace heuristic slab rendering for explicitly authored assets. Do not
  silently apply the slab model to another wide FRM.
- Keep ordinary billboards for unconfigured objects, with their existing
  opaque-bounds visibility fix.
- Ensure rendering, picking, wall/door occlusion, floor clipping, and actors
  all use the same surface depth and transparency rules.
- Compare software and accelerated screenshots at the same camera states.
- Measure frame time in a dense scene and optimize only after correctness;
  bound geometry lookup and reuse static mesh data.
- Remove temporary diagnostics or put them behind the existing debug
  conventions. Keep F11-off pixels and gameplay collision unchanged.

## Acceptance suite

The graphics change is ready only when all of the following hold:

1. **Alignment:** wall bases and endpoints land on intended map boundaries;
   connected pieces meet at corners without visible gaps or doubled faces.
2. **Scene integrity:** doors/openings remain traversable and visually open;
   decorative scenery does not become an accidental blocker.
3. **Depth:** walls, scenery, floor, doors, and actors occlude in the correct
   order while walking and turning around the structures.
4. **View coverage:** front, both oblique directions, sides, and close-range
   views show no floating fragments, disappearing faces, or accidental
   back-face reuse.
5. **Renderer parity:** software and accelerated rendering have matching
   silhouettes, placement, and occlusion.
6. **Regression:** isometric mode, collision/movement, doors, small props,
   ordinary billboards, and the existing sprite-visible-bounds fix remain
   unchanged.
7. **Repeatability:** retain paired screenshots and automated geometry,
   topology, and ray/depth tests for both problem scenes.

## Explicit non-goals

- No full 3D reconstruction of every Fallout map or asset.
- No collision or save-format changes.
- No global enlargement of sprites or walls as a substitute for alignment.
- No assumption that the isometric FRM itself contains suitable textures for
  every perspective-facing surface.
- No claim of visual success based only on unit tests; the game must be run
  with the actual assets and the target scenes visually inspected.

## Risk and fallback

Hand-authoring surfaces costs some per-map work, but confines the cost to the
landmarks that benefit from it and makes placement explainable. The larger
risk is inaccurate source art: geometry can align perfectly while a stretched
or semantically wrong texture still looks poor. Keep geometry and texture
mapping separate so art crops can be improved without shifting boundaries.
If a target cannot be represented cleanly with a few surfaces, keep its
current fallback and add a documented asset-specific exception rather than
weakening the global renderer.
