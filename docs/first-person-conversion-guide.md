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

### GPU billboard migration plan

F11 mouse-look and map rectangle invalidations now request a deferred scene
refresh. Main/player-combat/NPC-animation loops also request their continuous
camera refresh; all presentation loops flush pending work through
`renderPresent()`. This coalesces repeated requests without relying on a return
to the outer main loop. Picking, screenshots, and the FPS overlay flush first
when they require current scene data. Explicit synchronous refreshes remain for
mode transitions and action-menu setup. Disabling F11 clears pending work.
Native build and runtime validation are pending; compare turning FPS and verify
player/NPC combat animation, turn-and-click targeting, modal menus, and F11
toggles. The earlier main-loop-only deferral broke combat and must not be
reintroduced.

Current checkout optimization (November 2026): door-top occlusion is stored as
one vertical span per screen column, matching the single cap retained by ray
traversal, rather than a full-resolution double depth buffer. Empty scenes
skip cap-depth queries. GPU billboard visibility rejection checks the endpoints
of each horizon band; bands without an overlapping occluding cap are clipped
once per column and adjacent visible columns are submitted as rectangles.
Bands overlapping caps retain the existing detailed clipping path. This removes
resolution-dependent work in the common case without changing door geometry.
Randomized algorithm-level comparisons against dense per-pixel depth passed;
native compilation and in-game 1080p/1440p comparisons remain pending because
the local Visual Studio/CMake toolchain was unavailable. Validate open/closed
doors, sprites crossing door tops, window sills, and CPU fallback before release.

The current F11 renderer is a CPU software renderer. `tileRenderFirstPersonPreview`
writes palette indices into the shared 8-bit window buffer; SDL uploads that
completed image to a texture afterward. The GPU therefore does not accelerate
wall raycasting, floor projection, billboard scaling, alpha testing, or depth
checks.

The migration should be incremental and retain the CPU renderer as a fallback:

1. **Verify the presentation renderer.** Request an accelerated SDL renderer,
   record the selected backend, and fall back explicitly when acceleration is
   unavailable. This is the first implementation step.
2. **Cache GPU-ready FRM textures.** Convert each scenery frame from indexed FRM
   pixels plus the active palette to an RGBA texture once, retaining transparent
   index 0 as alpha 0. Invalidate or update cached textures on palette changes.
3. **Submit scenery billboards to the GPU.** Keep CPU projection and object
   visibility initially, but replace CPU per-pixel scenery rasterization with
   textured billboard submissions. Keep critters and items on the CPU path until
   occlusion is proven.
4. **Preserve depth and wall occlusion.** The preferred implementation is an
   OpenGL shader pass with a wall-depth texture or depth buffer. A simple
   `SDL_RenderCopy` overlay is not sufficient because it cannot represent
   per-column wall depth.
5. **Move floor and walls only after billboards are validated.** A full shader
   raycaster is a later phase, not a prerequisite for the billboard win.

The initial GPU step is implemented in `src/svga.cc`: SDL now requests
`SDL_RENDERER_ACCELERATED`, logs the selected backend, and exposes
`screenIsHardwareAccelerated()` for the upcoming billboard backend. This does
not yet move F11 billboard work to the GPU; it makes the presentation path
explicit and provides a safe capability check for the next phase.

The first billboard pass is now implemented as a hybrid: scenery remains
projected and clipped against CPU wall-depth columns, but its indexed FRM
pixels are converted to cached RGBA SDL textures and scaled/blended by the
accelerated renderer. Critters and items continue using the CPU rasterizer.
The CPU renderer remains active automatically when SDL acceleration is absent.
This first pass clips against wall depth and sorts scenery far-to-near, but it
does not yet share the CPU actor-depth buffer; actor/scenery overlap is a
known follow-up before making the GPU path the default.

The next pass also submits item billboards to the same GPU path. GPU texture
sampling is forced to nearest-neighbor so enlarged FRM pixels retain the
original art style. Critter FRMs now use the same GPU billboard path. GPU billboards are sorted
far-to-near together and wall-clipped before submission, so scenery no longer
has to be disabled when critters are visible. Critter text labels and other
CPU-only overlays remain a follow-up because the SDL GPU pass is composited
after the CPU framebuffer.

Adjacent clips from the same billboard are coalesced before the depth sort to
reduce SDL draw calls when wall visibility changes across neighboring columns.
Billboards whose projected columns are entirely behind wall/sill depth are
rejected before GPU texture submission.
The actor callback also performs a conservative wall/sill occlusion scan using
an oversized projected footprint before facing and FRM work, avoiding CPU setup
for actors that cannot contribute any visible pixels.

Fully wall-visible scenery and item billboards now use a single GPU submission
instead of walking the wall-depth span as multiple visibility segments. The
per-column clipping path remains for billboards intersecting walls.

The actor projection path also rejects objects that are conservatively outside
the viewport before doing facing, prototype, or FRM work. This reduces CPU
setup for actors that cannot contribute any visible pixels. The GPU draw loop
also avoids repeating SDL clip-state changes when consecutive sorted
billboards use the same clip rectangle. The per-frame GPU billboard queue also
reserves a conservative capacity before object traversal to avoid repeated
allocation and copying in dense scenes.

GPU billboard submissions now persist across repeated presentations until the
next F11 render begins. This is required because combat and modal UI paths can
call `renderPresent()` more than once without rerendering the tile scene.
The queue is explicitly cleared when F11 is disabled.

CPU overlay rectangles updated after the F11 scene refresh are replayed after
the GPU billboard pass. This preserves the expected ordering for menus,
widgets, and the software cursor while retaining GPU rendering for the scene.

The current CPU floor pass retains fixed 2x2 samples; the earlier adaptive
4x4 treatment was removed to preserve detail. Walls now rasterize first and
record their floor-covering bottom edge per column. Entirely covered 2x2 blocks
skip floor sampling, while partial blocks keep the original sample and write
only uncovered pixels. Sample coordinates still advance through skipped blocks
to preserve texture alignment. Openings and above-horizon door caps do not
suppress visible floor. Exit markers and billboards render after both passes.

Floor-cache invalidation now compares only the floor half of packed tile IDs.
Room entry/exit changes roof visibility in the other half; it previously caused
an unnecessary full floor-cache rebuild. Floor art/flag changes and elevation
changes still invalidate the cache. This is a candidate fix for room-transition
hitches, pending runtime confirmation. Algorithm-level reference checks matched
3,000 randomized framebuffers and verified roof-only versus floor-bit changes;
native build and in-game 1080p/1440p tests remain pending because the local C++
toolchain is unavailable. Compare wall edges, windows, open doors, and room
transitions, preserving the existing floor quality.

The CPU wall pass likewise samples each wall texture column in vertical
2-pixel blocks. Wall ray traversal and one depth result per screen column are
preserved, so billboard clipping remains column-accurate while wall shading
does less per-pixel work.

Scenery prototype classification is also cached by prototype ID. Stable visual
flags are reused across frames, while frame-specific art dimensions continue to
be queried dynamically so animation and alternate frames retain their existing
scaling behavior.
overhead from dense scenery outside the camera while retaining a generous
projected radius to avoid clipping visible billboards.

### Direct GPU floor and wall migration

The next large performance boundary cannot use the SDL 2D renderer alone.
F11 still spends CPU time in two full-screen stages:

- floor projection samples the cached floor map for every lower-half screen
  pixel;
- wall raycasting walks the hex grid for every screen column and then shades
  wall pixels into the CPU framebuffer.

The GPU billboard pass does not accelerate either stage. Moving them requires
an OpenGL renderer path using the existing `SDL_WINDOW_OPENGL` window, with
SDL renderer presentation replaced or isolated behind a backend interface.
The renderer now exposes a capability probe for accelerated target textures
and records that capability at startup. This is only a backend seam; the CPU
floor/wall path remains authoritative until the GPU pass can be initialized
and composed without breaking overlays or fallback behavior.
The backend also verifies that an OpenGL context is current before shader
integration is attempted. Raw OpenGL calls remain disabled while SDL owns the
active renderer state; this avoids invalidating SDL texture and clip state on
platforms where the accelerated renderer is not OpenGL.
Overlay replay is bounded to one union rectangle per F11 scene rather than
retaining every historical UI update. Since the source texture contains the
current final CPU framebuffer, the union safely preserves menu and cursor
ordering while avoiding unbounded presentation work.

When `debug.show_fps` is enabled, F11 reports rolling 60-frame timing averages
for total render, floor projection, wall rendering, and actor submission. The
timers are disabled when the existing FPS display setting is off, allowing the
next GPU migration step to target the measured dominant stage.

An initial reduced GPU floor composition prototype was tested on accelerated
target-texture renderers. It reduced the measured floor stage, but per-frame
palette conversion, wall masking, texture upload, and SDL composition increased
total F11 frame time, so the prototype was reverted. The adaptive CPU floor
path remains authoritative until a shader pass can move the projection work
itself rather than uploading a completed reduced image.
The planned shader pipeline is:

1. Upload the floor cache as a GPU texture/atlas and pass camera position,
   direction, viewport size, and horizon to a floor shader.
2. Upload wall-cell type/orientation data as a compact integer texture or
   buffer; perform DDA ray traversal in a fragment shader.
3. Sample GPU wall textures and apply distance/face lighting in the shader.
4. Render GPU billboards into the same depth-aware target.
5. Composite CPU-only UI, labels, and interaction overlays afterward.

The selected implementation direction is an isolated OpenGL backend. The
current SDL renderer remains the active backend until the shader backend can
own the GL composition target for a complete frame. The first implementation
unit should include shader compilation, GPU resource creation, and a complete
floor-only pass behind a runtime fallback; enabling raw GL calls from
`renderPresent()` while SDL owns the renderer is intentionally avoided.
The renderer now probes and compiles a modern GLSL 3.30 core floor-projection
shader through SDL's current context. The shader defines the intended
fullscreen-triangle, camera, horizon, viewport, and floor-cache sampling
interface. This is only a capability/resource probe; it does not draw or
change the active frame path. Startup also logs the OpenGL version, GLSL
version, and vendor so the first shader texture path can select compatible
formats and diagnose driver-specific fallback.
The active test system reports NVIDIA OpenGL 4.6.0, GLSL 4.60, and NVIDIA
driver 595.97, so the first real pass can use the modern shader interface
without a legacy compatibility branch.
The backend now has a persistent OpenGL floor-cache texture resource. It is
updated only when the floor atlas is explicitly rebuilt; it is not sampled or
composed yet while SDL owns the active frame target. The update replaces the
texture contents so same-sized floor layouts cannot leave stale GPU data.
The floor-cache atlas upload was attempted but reverted after a crash entering
F11 during the first live OpenGL texture upload. The shader/resource probe
remains, but GL texture uploads stay disabled until the isolated scene target
owns the GL state explicitly.
The renderer now creates and validates an offscreen OpenGL scene target during
renderer initialization, then restores the default framebuffer. Shader and
target availability still do not imply that F11 may issue GL draw or texture
calls; the scene backend remains disabled until F8 performs the explicit
whole-renderer ownership transition.
The backend now exposes a guarded begin/end lifecycle for that target. It
binds the framebuffer and viewport only inside the lifecycle and restores the
caller's framebuffer and viewport afterward; no F11 call site uses it until
the complete composition path is ready.
The F11 floor-cache builder now stages its generated indexed cache through a
renderer-owned copy when the map layout changes. Staging performs no OpenGL
calls and does not change visible rendering; a later scene-owned phase will
convert and upload the staged data while SDL presentation is suspended.
The F11 camera position, direction, camera plane, and map dimensions are
staged through the same CPU-only boundary each frame. This keeps the complete
GPU floor input packet together without allowing tile rendering to touch GL.
The renderer also has a guarded staged-floor draw operation. It uploads a
palette-expanded RGBA texture only while the isolated scene target is bound,
sets the live camera uniforms, and draws the fullscreen floor pass. It is
called only after F8 has removed SDL renderer ownership; while SDL owns the
active presentation path, the visible renderer remains CPU/SDL.
F12 remains the existing screenshot command. OpenGL scene activation will not
be tied to a gameplay key until the complete renderer-owned presentation path
is ready.
F8 is reserved for the experimental OpenGL mode. It arms or disarms a
renderer-side backend transition. The current experimental output is floor-only
and does not yet include walls, billboards, or UI.
The presentation phase now consumes that flag only as a readiness gate and
logs shader/target availability once per armed period. It still presents the
existing SDL frame and performs no raw GL calls. The gate also verifies that
the renderer-owned floor cache and camera packet are populated before a future
scene backend can consume them. The gate also tracks whether a valid F11 frame
is currently active, preventing a future presentation switch from consuming
stale staged data during menus or ordinary map rendering.
The renderer exposes a separate presentation-eligibility predicate requiring
all of those conditions simultaneously. F8 reports that predicate but leaves
the backend disabled. The live floor draw is not invoked while SDL's renderer
owns presentation; the existing SDL frame remains the visible output.
The approved experimental transition now tears down SDL renderer ownership
creates an explicit SDL OpenGL context, and then recreates the shader
resources. While F8 is active, the experimental
backend presents the staged floor directly through the OpenGL window swap.
This first transition is intentionally floor-only; walls, billboards, and UI
remain follow-up work. If initialization or presentation fails, SDL is
recreated as the fallback. The explicit context is deleted before SDL is
recreated.
The first live floor frame exposed two expected integration issues: CPU cache
rows are top-down while OpenGL texture rows are bottom-up, and the CPU floor
loop was still running redundantly during OpenGL presentation. The shader now
flips the cache's vertical coordinate, and CPU floor rasterization is skipped
only while the experimental OpenGL scene is active.
The first direct-window image also exposed OpenGL's bottom-left fragment
coordinate origin: the floor discard test was applied to the upper half of the
window. The shader now converts `gl_FragCoord.y` to the game's top-down screen
coordinate before applying the horizon test.
The floor-only geometry now uses a lower-half rectangle instead of shortening a
single fullscreen triangle. The latter created a diagonal top edge and slanted
the horizon. The rectangle avoids rasterizing the sky while keeping the
horizon level.
Runtime validation instead found that the direct OpenGL floor presentation was
substantially slower than regular F11. The cause was that the F11 callback
still generated CPU walls, actors, billboards, and interaction data even
though the OpenGL presentation discarded those layers. F8 now returns after
staging the floor and camera, so the floor-only experiment no longer pays for
undisplayed CPU scene work. This is still not a full-scene FPS comparison;
walls, billboards, and UI remain follow-up composition work.
The next incremental step adds a compact wall-column upload: the existing CPU
ray traversal supplies each screen column's projected bounds and shade, while
OpenGL fills the wall spans after the floor pass. This keeps collision and
topology behavior unchanged while moving wall pixel fill off the CPU. The
current wall pass uses a flat shader color; wall texture sampling and shared
depth with billboards remain follow-up work.
The experimental path now also accepts the existing scenery, item, and critter
billboard submissions without creating SDL textures. FRM pixels are palette
expanded into cached OpenGL textures, sorted far-to-near, alpha blended, and
drawn as clipped quads after the wall pass. Wall depth is still represented by
the CPU-generated clip ranges; a shared hardware depth buffer remains a later
optimization.
Initialization also runs a non-presented shader draw smoke test using a core
profile VAO and fullscreen triangle, then restores the default framebuffer.
The test now also restores the previous framebuffer, viewport, clear color,
shader program, VAO, active texture unit, and 2D texture binding so SDL
presentation state is not perturbed.
The shader uniform locations for the floor cache, camera, direction, camera
plane, viewport, horizon, and map size are now resolved during initialization.
The initialization smoke test now binds a real 1x1 RGBA cache texture and sets
all floor uniforms before drawing. This validates sampler/uniform updates while
the backend owns the isolated target; it does not upload live F11 floor data.

Do not attempt to fake this by uploading the completed CPU framebuffer: that
only moves presentation and leaves the expensive loops unchanged. The
implementation should first add a backend capability probe and an isolated
shader test scene, then switch F11 floor/wall rendering behind a runtime
fallback. This is a larger architectural change than the SDL billboard pass.

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
data or copyrighted assets into this repository. Start with **Arroyo Temple:
Inner Sanctum** as the first wall-material proof. It is a compact cave interior
and a useful controlled scene for checking whether a small set of wall
materials reads well against Fallout's floor art. Verify in-game that the
loaded map has representative wall orientations, corners, openings, and any
transparent or door surfaces before treating it as the only geometry test.
The supplied F9 view shows a winding cave layout with several chambers joined
by narrow passages, so keep the first material proof local to the player's
marked chamber and validate passage connections separately.

Inspect its floor/roof FIDs, wall and scenery prototypes, rotations,
elevations, corner flags, transparency/light-through flags, and transitions.
Record the exact prototype IDs and source art candidates in working notes;
keep copyrighted game data and extracted art outside this repository. Compare
the loaded map/object representation with the F9 topology view and the original
isometric rendering before deciding how a wall material should be mapped.

The Inner Sanctum is the first **material and visual calibration** map, not a
claim that its cave geometry covers every Fallout map archetype. Once the
material scale, palette, and lighting are acceptable there, validate the
chosen treatment on a second scene with different wall construction (for
example, an outdoor wall or a metal interior) before generalizing the mapping.

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

While F9 is active, press `+` or `=` to zoom the topology view in and `-` to
zoom out. Zooming in keeps the player centered so the nearby chamber and connecting
passages can be inspected at larger cell sizes; zooming all the way out returns
to the full-map overview. The mouse wheel also zooms when its event is
available; outside the F9 diagnostic, the keys and wheel retain their normal
behavior.

Wall and scenery markers include a colored tick showing the object's six-way
rotation: magenta is NE, cyan is E, green is SE, blue is SW, pink is W, and
white is NW. A 2x2 pixel block at the cell's upper-right corner identifies
prototype corner flags: cyan is east, yellow is north, magenta is south, blue
is west, and white means multiple corner flags are set. The center marker
retains its existing color for object type/blocking state.

Use this view to inspect whether object positions and floor regions give us
enough information to build useful wall segments and openings. It is a
diagnostic of source data, not a claim that wall orientation or geometry has
already been correctly inferred. The current map format does not necessarily
encode a complete first-person room mesh. Record ambiguities against a
representative map before adding geometry-generation rules. Keep derived
geometry temporary; do not serialize it into saves or map files.

For the Inner Sanctum material proof, use the player marker to identify the
local chamber, then compare the wall segments around that chamber against the
F11 view at the same position and facing. Trace each broad face and side face
to its nearby topology marker before assigning material classes. Check the
connecting narrow passages for genuine openings and wall returns; do not
interpret the whole-map outline as one continuous room boundary.

### 3. Render the gray-box scene

F11 now renders a first map-connected graybox in the map viewport. It starts
at the player's square-grid cell and facing, raycasts against blocking wall
objects, blocking scenery, and closed doors, and draws
flat-color perspective columns. Other critters, items, and non-door scenery
appear as camera-facing billboards using their current Fallout FRM frame,
scaled by perspective and occluded by the raycast wall depth. Their size is
capped by object type, with the frame's aspect ratio preserved and the object
grounded at the projected floor position. Mouse look turns the view.
While in F11 mode, F9 cycles a small topology overview through the top-left,
top-right, bottom-left, and bottom-right corners, then hides it. F11 restores the isometric view
and normal mouse mode. Aim the crosshair at a visible critter and left-click
to talk to a living, talkable critter or loot a non-talkable critter using
Fallout's existing looting action. WASD moves one hex step relative to the
view direction, using the game's existing movement animation and collision
path. During combat, movement spends the existing movement AP (including
bonus move AP) and is unavailable when no movement AP remains. Actor and
camera positions retain half-square offsets from the underlying hex tiles so
nearby critters do not collapse onto the player's exact diagnostic position.
The camera interpolates between the player's hex tile positions over a short
transition, rather than interpreting isometric animation pixels as first-
person world coordinates.

During real-time operation and turn-based combat, mouse-look remains active
by default. Right-click to show the normal cursor and freeze the view. In
real-time, use the cursor on the interface bar or click a visible critter to
talk or loot; clicks in the 3D viewport do not fall through to isometric
picking.
During combat, select the weapon/action from the interface bar, then click a
visible enemy to send that target to Fallout's existing combat attack logic.
Right-click again to return to mouse-look. Attack range, line of sight, hit
chance, action points, and turn order remain controlled by the original
combat system.

F11 wall faces now use a palette-mapped stone-block proof material, with a
separate plank treatment for closed doors. The saturated orientation colors
remain in the F9 diagnostic only. The diagnostic takes a value-only snapshot
of visible wall, scenery, critter, and item markers during the normal F11
render. Every zoom level draws from that snapshot instead of traversing the
live object lists again while movement and animation may be updating them.
This keeps the overview stable at higher zoom levels and provides a safe
foundation for replacing the current point markers with player, critter, door,
and scenery icons.
Doors now use small black-outlined map icons, with red fill when closed and
green fill when open.
The map uses alternating floor tones, subtle cell borders, softer wall lines,
and a black-outlined red diamond for the player so the overview reads as a
map rather than a raw diagnostic grid. The player marker uses a thicker
outline and fill for visibility. The F11 crosshair includes a green facing
arrow that follows the current view direction. Object-specific icons remain a
follow-up improvement.

Wall raycasting uses the hex object grid at twice the square-floor resolution,
preserving each wall object's position instead of collapsing it into a square
floor cell. North/South and East/West walls are thin, finite-thickness slabs
spanning one half-square hex cell, using prototype orientation rather than the
object's six-way art rotation. This avoids the angle-dependent gaps of
zero-thickness planes and keeps nearby objects from automatically filling the
same square cell. The F9 topology markers are placed at the corresponding
hex-grid positions, though the full-map diagnostic remains pixel-limited.
Non-blocking and light-through wall/scenery objects are treated as openings,
matching the game's separate movement and sight flags. Walls marked as
corners, blocking scenery, and closed doors still use the conservative
hex-cell blocker when they block sight. This is an incremental geometry
improvement, not a complete reconstruction of wall segments: validate it
against representative maps before extending corner handling. Map/object
refreshes redraw the preview; the interface bar and modal UI remain existing
windows.

Wall objects carrying Fallout's transparent-glass, transparent-wall, or
light-through flag render as windows in F11: the upper section is transparent,
while the lower half remains a solid sill. Walls behind the window remain
visible through its transparent section. Explicitly non-blocking wall objects
remain openings. Solid walls are floor-anchored and extend upward to twice
their original projected height; window sills retain their floor-side
placement.

### 4. Add a small texture proof

The F11 graybox samples the floor FRM assigned to each square-grid cell rather
than repeating one nearby floor across the entire map. It maps each square
world cell onto the diamond-shaped source image, avoiding the transparent
corners that previously left irregular holes in the ground. Palette index
zero retains the graybox ground color; cells with hidden or unavailable floor
art also retain that fallback. This is still a stylized projection of
isometric art. Check texture orientation, seams, transparency, and baked
lighting on Arroyo village and an interior map.

The first F11 wall-material proof is generated in the renderer as a seamless
64x64 stone-block tile, mapped continuously from world coordinates across wall
faces. Side orientation and distance apply restrained palette shading.
Closed doors use a separate repeating wood-plank tile. This keeps the F9
diagnostic and existing raycast geometry intact while replacing the saturated
debug colors in F11.

Non-door scenery, including trees, remains an FRM billboard that faces the
camera and is depth-tested against the raycast walls. It must not be added to
the solid wall grid, or its texture is hidden behind the procedural wall
material. Closed doors remain structural blockers; open doors and billboard
scenery remain openings in that grid.

Use Arroyo Temple: Inner Sanctum to tune this first proof: compare stable F11
views with the same area in the original isometric renderer. Check whether the
block scale reads like the cave masonry, seams stay continuous across adjacent
slabs, the material fits the floor palette, and the two faces remain distinct
as the camera turns. The current mapping uses the same stone material for all
blocking walls and scenery; it does not yet classify temple prototypes into
separate materials.

Fallout wall FRMs are complete isometric scene sprites, not seamless
wall-surface textures; projecting them whole onto raycast faces stretches
rooflines, ground details, and transparent regions into distracting bands.
Do not use a raw wall FRM as a repeating texture. First determine whether a
specific source image contains a clean, repeatable surface region that can be
cropped and authored into a seamless material. If no source region survives
that proof, use a deliberately authored, palette-compatible seamless texture
or a restrained procedural surface treatment. Keep prototype-to-material
assignments explicit and small; avoid inferring a surface from the object's
six-way art rotation when the wall geometry uses a separate orientation.

Initial visual inspection of frame 0, direction 0 from the `temple01` through
`temple99` wall FRMs shows mostly discrete isometric wall runs, narrow side
pieces, corners, pillars, and arches. Their faces include perspective and
baked shading; the arch and corner images also contain geometry that cannot
tile cleanly. Treat these as object art, not ready-made raycast materials. For
the Inner Sanctum proof, start with a purpose-built, seamless stone material
and compare one carefully extracted FRM face crop only as an experiment. Keep
the crop only if it tiles without visible seams and its lighting does not
fight the floor art. This contact-sheet inspection is a candidate-art survey,
not a complete review of all frames or directions.

The inspected temple wall FRMs remain candidate sources for a crop comparison;
the renderer does not project or include those full sprites as textures.
Validate the generated stone and door treatments in-game, including
transparent half-height barriers and closed/open doors, before adding further
prototype-specific materials. Do not expand to a general material catalog
until the Inner Sanctum comparison is clean and at least one contrasting map
confirms that the treatment generalizes.

### 5. Expand movement, collision, and synchronization

Tile-step WASD movement uses Fallout's existing animation and collision path.
Outside combat it remains real-time; in combat each step is limited by the
player's available AP and uses the engine's existing movement AP accounting.
Move the camera continuously only if collision against the derived geometry
can be validated. At each required simulation boundary, synchronize `gDude`
through existing APIs so map scripts, combat, transitions, and saves observe
a valid tile/elevation/rotation.

Test walls, corners, narrow doorways, open doors, map boundaries, elevation
changes, party members, knockback, combat entry/exit, and map transitions.
Avoid direct field writes and avoid changing the tile/pathfinder rules just to
make a camera appear to move.

### 6. Add existing content incrementally

Render static walls and floor first, then doors and interactive scenery, then
critters/items as correctly occluded billboards or the chosen 3D equivalents.
Next address lighting, roof/visibility behavior, and the player weapon/hand
presentation. Reuse current object IDs for selection and interaction so that
inventory, scripts, and combat continue to act on the same objects.

Treat combat as its own integration milestone. F11 now supports mouse-look
and cursor-toggle modes for selecting weapons through the existing interface
bar and targeting visible critters by their first-person screen position.
Attack range, hit chance, line of sight, AP, and turn order are resolved by
the existing combat system. Validate weapon selection, target clicks, missed
clicks, combat entry/exit, and different attack modes in game before expanding
this path.

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
