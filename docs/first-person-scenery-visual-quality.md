# Improving first-person scenery alignment

## What the screenshots show

The isometric views show scenery arranged as grounded, connected map art. In
the first first-person scene, the large central landmark and nearby wall pieces
read as separate strips rather than one connected, grounded structure. In the
second, broad textured fragments sit above and apart from the row of visible
wall blocks instead of forming a continuous facade. In both cases, the
first-person artwork does not line up with the scene's apparent wall
boundaries.

This is more than a seam or scale problem. The first-person renderer does not
have the structures' actual 3D shapes or wall boundaries, so it cannot reliably
make these composite isometric images line up with the map.

## Why the current conversion breaks down

`src/first_person_scenery.h` classifies scenery from its image dimensions and
opaque pixels. For qualifying artwork, it fits a line to the opaque bottom
edge, converts that line into a world-space span, and wraps the whole image
around a shallow slab. `src/tile.cc` then raycasts that slab and samples one
source-art column for each screen column.

That is a useful approximation for a long, roughly straight object whose image
really represents one surface. It is not enough to recover the geometry of a
large Fallout backdrop or assembled structure:

- An isometric FRM is a rendered view, not a facade texture. It can combine
  front and side faces, depth, ground slope, shadows, and separate pieces in
  one image. Its opaque lower edge is not necessarily the object's footprint.
- A single fitted line and fixed-thickness slab cannot describe corners,
  alcoves, doorways, varying depths, or multiple disconnected parts. Reusing
  image columns for end faces makes that limitation especially visible at
  oblique angles.
- Image size is only a heuristic for geometry. A wide image can be a landmark
  or a composite backdrop, not one long wall. Applying one inferred span to it
  will place its pixels along a false boundary.
- The inferred span is not snapped to the map's wall or collision boundaries.
  Even a visually plausible span can therefore float beside, cross, or stop
  short of the wall segments used by the rest of the scene.

Changing the size thresholds or slab thickness might hide a particular
artifact, but cannot make the inferred shape match structures the image does
not describe.

## Recommended approach: simple authored geometry for selected scenery

Keep the existing isometric renderer unchanged. In first-person mode, use a
small per-asset geometry description for large scenery instead of inferring
geometry from image dimensions:

1. **Identify assets explicitly.** Select the scenery by stable prototype/art
   identity, not only by width and height. Leave unconfigured scenery on its
   current rendering path.
2. **Build wall runs from map-aligned segments.** Describe straight walls as
   vertical textured quads with endpoints, height, and facing. Place endpoints
   on the same map coordinates as their visible wall/collision boundaries.
   Give adjoining segments a tiny shared seam tolerance so pixel rounding does
   not open hairline gaps.
3. **Use a few planes for complex landmarks.** For a structure with corners or
   an entrance, describe its main facade, side returns, and opening as separate
   planes (or a small low-poly proxy). Map each plane to a deliberate crop of
   existing art where possible. Do not bend or stretch the entire isometric
   image around one inferred slab.
4. **Keep geometry and texture mapping stable.** Store world-space placement,
   vertical extent, and texture coordinates together. Use per-surface depth
   for wall and actor occlusion, and preserve transparent openings where the
   source art supports them.
5. **Use a safe fallback.** If an asset has no authored description, retain
   the ordinary billboard path until it has been reviewed. Do not silently
   treat every wide generic scenery frame as a wall.

This does not require converting every Fallout asset or building a general
3D-modeling pipeline. Start with the one or two structures in the screenshots.
For long, regular walls, a row of map-aligned quads is likely sufficient. For a
compound landmark, a handful of planes should be enough to establish the
correct footprint and major faces while keeping the game's original art style.

## Implementation and validation sequence

1. Add an explicit first-person scenery definition for one problematic
   structure, including its world-space footprint and surface-to-art mapping.
2. Render that structure through the existing depth and transparency paths;
   keep unrelated scenery on its existing path.
3. Compare the same camera position against the isometric view, then inspect
   the structure while facing its front, sides, and corners and while walking
   along its boundary.
4. Repeat for the next structurally different landmark only after the first
   mapping works. Keep CPU and accelerated rendering consistent.

The change is successful when wall surfaces meet at corners and openings,
their bases follow the map boundary, no large fragments float or disappear as
the camera turns, and objects in front of or behind the structure are occluded
in the expected order. F11-off rendering should remain unchanged.

## Scope note

This is a rendering-data problem, not a collision fix. Geometry used for
appearance should initially be authored independently of gameplay collision:
some visible backdrops intentionally have separate invisible blocking hexes.
Where a wall run does correspond to collision boundaries, those coordinates
are a useful alignment reference, but should not be assumed to fully describe
the artwork.
