# Fallout 2 CE: First-Person Conversion Guide

## Purpose

This guide is the shared technical starting point for adding an optional
first-person play style to this Fallout 2 CE fork. It describes the engine as
it exists today, the constraints that shape the conversion, and a staged
implementation that keeps the original game mode usable.

This is a rendering and control conversion, not a rewrite of Fallout's rules.
The first implementation should keep map loading, objects, scripts, combat,
dialogue, inventory, saves, and mod compatibility in the existing engine.

## Recommendation

Build a separate, opt-in first-person renderer and camera over the existing map
and object model. Do not replace the isometric renderer or rewrite the
simulation in the first milestone.

The first data-connected vertical slice should load one small interior map,
derive simple walkable and wall geometry, and render it with flat colors from a
fixed first-person camera. Add movement and collision only after the geometry
matches the map. Keep interaction, combat, and the existing interface in their
current code paths until the renderer and player/world synchronization are
proven.

**Project direction: grid-based stylized 2.5D.** Build simple, shallow
first-person geometry from Fallout's map topology, rendered with a
Wolfenstein-style raycaster or equivalent constrained perspective renderer.
Use existing Fallout art as surface textures where it works, with small,
explicit mappings or authored texture treatments for assets that do not
translate directly. True 3D models and a general-purpose 3D asset pipeline are
outside the initial scope.

The map's square floor/roof grid and hex object grid serve different purposes:
use square tiles to locate floor and roof surfaces, and world objects to
identify and orient walls, doors, scenery, and actors. The renderer should
build a temporary scene from these sources rather than changing map or save
formats. Do not infer wall geometry from floor art alone. Fallout's FRMs are
often isometric sprites with baked-in lighting, transparency, and orientation,
not seamless rectangular textures; validate each chosen asset on a prototype
surface before treating it as a general-purpose material.

Alternatives considered:

1. **Stylized 2.5D** — construct walkable space from the current tile/object
   data, use a raycaster or similarly constrained projection, and adapt
   existing FRM art as billboards or wall-facing art where it makes sense.
   This is the selected direction because it can reuse the existing map model,
   but it will not make every isometric asset look like a natural 3D texture.
2. **True 3D** — define or acquire 3D geometry, materials, and animation assets
   and create a real 3D scene representation for maps and actors. The current
   repository does not contain that asset pipeline or a 3D renderer. This is a
   substantially larger project and needs an explicit asset/licensing and
   renderer decision.


## How the current game is organized

### Main loop and input

`mainLoop` in `src/main.cc` polls `inputGetInput`, passes the event to
`gameHandleKey`, processes script requests and map transitions, then presents
the frame and throttles it. `gameHandleKey` in `src/game.cc` handles the
existing keyboard, wheel, and mouse-driven isometric controls and opens the
existing game modes (inventory, dialogue, combat, menus, and so on).

`src/input.cc` and `src/dinput.cc` are the SDL input boundary. Relative mouse
mode already exists, but it is selected according to fullscreen/mouse-lock
settings; it is not currently a first-person look controller. Treat first
person movement/look as a new gameplay input mode. Avoid changing the meaning
of the existing isometric controls when the feature is off.

### Map and rendering

`isoInit` in `src/map.cc` creates the isometric map window, initializes the art,
tile, and object systems, and uses the visible game area above the interface
bar. The software renderer draws into the window's palette-indexed pixel
buffer; `src/svga.cc` transfers pixels through SDL and presents the frame.

`src/tile.cc` owns the current map projection and tile drawing. A map refresh
draws floor tiles, objects that render before roofs, roof tiles, and remaining
objects, in that order. `src/object.cc` draws FRM frames into the same
framebuffer and implements important isometric wall/roof occlusion behavior.
Refreshing map rectangles can be partial; a moving camera, by contrast, will
normally need the first-person view redrawn as a whole.

The two map grids are not interchangeable:

- The map has a 100 x 100 square grid at each of three elevations. Each
  `TileData` entry stores packed floor and roof tile FIDs. `tile.cc` maps those
  square tiles to isometric screen coordinates and draws their FRM art.
- The simulation has a 200 x 200 hex grid. Objects, critters, pathfinding,
  combat distances, and many script operations use hex tile numbers and
  elevations.

Walls, scenery, doors, and critters are world `Object`s, not a complete 3D
level mesh. Objects store a tile, elevation, offsets, rotation, frame, and
flags. `OBJ_TYPE_WALL` and `OBJ_TYPE_SCENERY` rendering has special visibility
rules; collision, shooting, lighting, transparency, and open-door behavior
also depend on object flags and existing object queries.

### Simulation and persistence

`src/animation.cc`, `src/object.cc`, and `src/combat.cc` operate on the hex
grid and existing object identities. Movement and pathfinding use discrete
tiles and blocking-object queries. The current save model stores map/object
state, not a free-moving 3D camera or a new continuous physics state.

This is valuable reuse: scripts, combat, saves, map transitions, and mod logic
can remain authoritative. It is also the central constraint: a first-person
camera must stay synchronized with the existing `gDude` object and must not
write directly to its fields in ways that bypass object registration,
animation, collision, save, or script behavior.

### Build and test surface

The project is C++17 and CMake-based. `FALLOUT_ENGINE_SOURCES` in
`CMakeLists.txt` is shared by the game and mapper targets. If a new renderer
requires a source file, register it with the shared engine only if both targets
can safely use it; otherwise keep the implementation/game integration
game-specific.

`CONTRIBUTING.md` documents the native build commands and the `ce-dat-tool` and
`ce-frm2png` asset-inspection tools. The `sfall_testing` directory contains
in-game script tests; it is not a general C++ unit-test suite. No standard
CTest/unit-test target was found during this inspection, so renderer math and
map conversion logic should get focused tests if they are introduced.

## Design rules for the conversion

1. **Keep a hard fallback.** First person must be disabled by default initially
   and switchable back to the original renderer without changing save data.
   Wire it through the existing settings/config conventions rather than a
   one-off hard-coded flag.
2. **Separate camera pose from game-object state.** Keep continuous camera
   position and yaw in renderer/controller state. Synchronize the player via
   established object APIs such as `objectSetLocation` and the existing
   rotation/movement paths. Preserve the hex tile, elevation, and six-way
   `Rotation` expected by scripts and combat; do not pretend a continuous yaw
   is stored in those fields.
3. **Keep the simulation authoritative.** Query existing object/tile data for
   occupancy and blocking. Do not create a second persistent map or duplicate
   combat/pathfinding rules. Decide and test how fine-grained first-person
   movement maps back to tile-based collision and script-visible location.
4. **Own the first-person render path.** Do not force perspective into
   `tileToScreenXY` or the existing isometric drawing loops. Add a mode-aware
   rendering seam/coordinator; have it use loaded map data and render into the
   map viewport (or a clearly owned buffer composed at that layer). Existing
   UI windows and the SDL presentation path should remain in place.
5. **Handle invalidation explicitly.** Map/object APIs currently request
   isometric rectangle refreshes. In first-person mode, ensure those changes
   invalidate or redraw the perspective view rather than accidentally invoking
   the old tile renderer. Camera movement itself requires a full-view redraw.
6. **Preserve CE compatibility.** Keep script-visible objects, map transitions,
   combat, autosaves, mapper behavior, and the original renderer working.
   Avoid changing on-disk map/save formats for the first slice.
7. **Treat art as a constraint, not a renderer detail.** Floor/roof FRMs are
   drawn as isometric square tiles; wall/scenery/actor FRMs are not mesh
   textures or model animations. Validate texture orientation, occlusion,
   transparency, lighting, elevation, and door state against real maps before
   choosing an asset-adaptation strategy.

## Staged implementation

### 0. Lock the prototype contract

The visual target is grid-based stylized 2.5D. Before implementing gameplay,
write down the prototype contract: how square-grid cells map to world units;
how wall objects and their rotations map to wall segments; how doors, openings,
and elevation are represented; how camera position maps back to the hex tile
and six-way facing exposed to the game; and whether turn-based combat is
retained. Do not change map or save formats.

### 1. Inventory a small representative map

Use an owned Fallout 2 installation and existing data tools; do not check game
data or copyrighted assets into this repository. Choose one compact interior
with a door, blocking walls, at least one object, and a critter. Inspect its
floor/roof FIDs, object types, rotations, elevations, flags, and transitions.
Compare the loaded map/object representation to what the new renderer needs.

Before rendering polished art, identify how the map indicates:

- walkable floor and elevation changes;
- wall/scenery segments, corners, and openings;
- open versus closed doors;
- objects that block movement, sight, or shots;
- roof visibility and indoor/outdoor lighting.

If the source data does not uniquely describe a needed 3D surface, record the
needed authored metadata rather than guessing from its isometric pixels.

### 2. Inspect a map's topology

While the F11 preview is active, press F9 to switch to a top-down diagnostic
of the loaded elevation. The full square tile grid shows floor tiles; object
markers show blocking/non-blocking walls, doors, scenery, critters, and items.
The player is a white point. Press F9 again to return to the graybox preview
and F11 to exit to the original view.

Use this view to inspect whether object positions and floor regions give us
enough information to build useful wall segments and openings. It is a
diagnostic of source data, not a claim that wall orientation or geometry has
already been correctly inferred. The current map format does not necessarily
encode a complete first-person room mesh. Record ambiguities against a
representative map before adding geometry-generation rules. Keep derived
geometry temporary; do not serialize it into saves or map files.

### 3. Render the gray-box scene

F11 now renders a first map-connected graybox in the map viewport. It starts
at the player's square-grid cell and facing, raycasts against wall objects
(including non-blocking walls), blocking scenery, and closed doors, and draws
flat-color perspective columns. Other critters appear as camera-facing
billboards using their current Fallout FRM frame, scaled by perspective and
occluded by the raycast wall depth. A small color marker shows party members
and same-team critters in green, non-party critters in amber outside combat,
and opposing teams in red during combat. Mouse look turns the view; F9 still
switches to the top-down source diagnostic. F11 restores the isometric view
and normal mouse mode. Actor and camera positions retain half-square offsets
from the underlying hex tiles so nearby critters do not collapse onto the
player's exact diagnostic position.

Graybox wall side faces use distinct saturated debug colors instead of gray
and brown shades that can disappear against the sky and ground.

This is deliberately a coarse first pass, not final topology: each blocking
object is treated as a full square-grid cell, and wall orientation, corners,
floor boundaries, and elevations have not yet been reconstructed as surfaces.
Open/non-blocking doors remain openings. Compare the view against F9 and the
isometric map to identify where this cell-based approximation fails before
adding texture or movement. Map/object refreshes redraw the preview; the
interface bar and modal UI remain existing windows.

### 4. Add a small texture proof

The F11 graybox now finds the nearest valid floor FRM on the loaded elevation
and projects that single palette-indexed image across the floor plane. Zero
pixels retain the graybox ground color. This is an intentionally crude proof:
it ignores per-cell floor changes and repeats the isometric tile image in a
square world pattern. Compare its transparency, seams, orientation, and baked
lighting against the graybox; use `ce-frm2png` to inspect a candidate frame.
If the result is unsuitable, record whether a cropped or authored tile
texture is needed before generalizing this renderer.

Once a floor image has been validated, repeat the experiment with one wall FRM
on a correctly oriented wall segment. Do not project the whole isometric wall
sprite as if it were a seamless rectangular material without inspecting it
first.

### 5. Add movement, collision, and synchronization

Add keyboard movement and relative mouse look in the gameplay input path.
Move the camera continuously only if collision against the derived geometry
can be validated; otherwise begin with tile-step movement. At each required
simulation boundary, synchronize `gDude` through existing APIs so map scripts,
combat, transitions, and saves observe a valid tile/elevation/rotation.

Test walls, corners, narrow doorways, open doors, map boundaries, elevation
changes, party members, knockback, combat entry/exit, and map transitions.
Avoid direct field writes and avoid changing the tile/pathfinder rules just to
make a camera appear to move.

### 6. Add existing content incrementally

Render static walls and floor first, then doors and interactive scenery, then
critters/items as correctly occluded billboards or the chosen 3D equivalents.
Next address lighting, roof/visibility behavior, combat targeting, and the
player weapon/hand presentation. Reuse current object IDs for selection and
interaction so that inventory, scripts, and combat continue to act on the
same objects.

Treat combat as its own integration milestone. A first-person crosshair can
select an object, but attack range, hit chance, line of sight, AP, and turn
order must still be resolved by the existing combat system unless a separate
gameplay redesign is approved.

### 7. Harden, configure, and expand

Make the mode a supported setting only after the vertical slice survives save
and reload, map changes, options/UI overlays, and representative mods. Add
configuration documentation and regression coverage. Expand to more map
archetypes only after walls, doors, elevation, roofs, and lighting work on the
representative map.

## Acceptance gates

Each milestone should pass the following checks before moving on:

- Original mode behaves as before with the feature disabled.
- Feature selection is deterministic and does not alter map/save formats.
- The chosen viewport renders at supported logical resolutions and respects
  the interface-bar area.
- Movement cannot cross a closed/blocking wall or door; open passages work.
- Existing object IDs, locations, scripts, combat, map transitions, and save /
  load still refer to the same game state.
- The map/renderer does not show hidden areas through roofs or walls where the
  original map rules say they should be occluded.
- The game and mapper targets still build if the renderer source is shared.
- Changes are checked with the relevant CMake build and in-game testing against
  real game data; data remains outside the repository.

## Working sequence and handoff

For implementation, work in small vertical slices: map inspection,
topology-to-geometry validation, gray-box rendering, one-texture proof,
input/camera, collision/synchronization, art adaptation, then combat and
compatibility. Keep each slice independently switchable and testable.
Before changing a shared surface, search its callers: in this engine a change
to input, object location, refresh callbacks, or screen buffers can affect the
mapper, scripts, mobile controls, and the original mode.

At the start of implementation, record the movement model, camera coordinate
system, collision model, combat decision, first test map, and which existing
assets can be reused versus which need authored metadata or replacement.
These are product choices that code inspection alone cannot settle.
