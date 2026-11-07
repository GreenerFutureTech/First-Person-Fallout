# Multiplayer F11 Controls Port Plan

## Goal

Make the F11 first-person preview fully controllable in the multiplayer client,
while preserving the dedicated server as the sole authority for game state and
preserving all existing single-player F11 behavior.

The multiplayer client already enters and leaves F11 with the F11 key. This
plan addresses the controls that are currently absent, misrouted, or unsafe
when the client is in first-person mode.

## Findings from the current implementation

### Input ownership differs between single-player and the viewer

- The single-player main loop sends input through `gameHandleKey` in
  `src/main.cc`. `gameHandleKey` in `src/game_ui.cc` first calls
  `gameHandleFirstPersonPreviewInput`, then handles first-person movement,
  clicks, topology controls, and the rest of the game key map.
- The multiplayer viewer loop in `mainClientViewer` deliberately does not call
  `gameHandleKey`. It calls `gameHandleFirstPersonPreviewInput`, handles F11
  explicitly, then routes its supported multiplayer inputs through viewer
  handlers in `src/main.cc`. This boundary is intentional: ordinary
  `gameHandleKey` branches can change the local simulation, save or load local
  games, or issue actions without using the server-authoritative wire path.
- `gameHandleFirstPersonPreviewInput` currently handles relative mouse look,
  mouse capture/release state, and the custom right-click event. It does not
  handle the first-person WASD movement switch or the first-person world-click
  action logic; those are in `gameHandleKey`.
- The viewer has a separate `keyCode == -2` mouse handler for isometric
  multiplayer actions. If F11 mouse events are not intercepted before that
  branch, they are interpreted as ordinary isometric clicks rather than
  first-person picks.

### F11 state and camera controls live in `tile.cc`

`src/tile.cc` owns the preview state, camera/yaw, hit targets, world picking,
movement selection, topology overlay, and rendering. The relevant public
surface is declared in `src/tile.h`.

- `tileFirstPersonPreviewLook` applies horizontal relative mouse movement to
  yaw. `gameHandleFirstPersonPreviewInput` gets deltas from
  `mouseDeviceGetRelativeMotion` and applies the configured mouse sensitivity.
  Vertical mouse movement is intentionally ignored.
- `tileFirstPersonPreviewMove` maps a relative direction (forward, right,
  backward, left) against the current yaw to the nearest valid hex direction.
  It currently registers a local run animation for keyboard movement. It also
  checks local combat AP and buffers one movement request while the actor is
  busy; the buffered request expires after 500 ms in
  `tileFirstPersonPreviewProcessPendingMove`. Click-to-move separately chooses
  walk or run from the running preference.
- First-person picking is already exposed through
  `tileFirstPersonPreviewPickCritterAt`,
  `tileFirstPersonPreviewPickObjectAt`,
  `tileFirstPersonPreviewPickDoorAt`, and
  `tileFirstPersonPreviewPickGroundTileAt`. These use rendered hit targets
  and wall-depth data, so they can be reused for viewer-side selection without
  deriving targets from the isometric hex cursor.
- `tileFirstPersonPreviewTurnToTile` animates the camera yaw toward a picked
  tile. `tileFirstPersonPreviewSetMoveMarkerTile` controls the visible
  destination marker.
- `tileFirstPersonPreviewToggleTopology` is bound to F9 in `gameHandleKey`.
  It cycles through the topology overlay positions and then turns the overlay
  off. `tileFirstPersonPreviewZoomTopology` changes its zoom, clamped to levels
  1 through 3.
- While the topology overlay is active, `gameHandleKey` consumes mouse-wheel
  input and `+`/`-` before the normal brightness handlers. With the overlay
  inactive, `+`/`-` retain their existing brightness behavior.
- `tileFirstPersonPreviewToggle` resets preview state on entry and restores
  normal isometric rendering on exit. Camera follow and refresh are maintained
  through the main loop and tile refresh hooks; they should not be duplicated
  in the multiplayer dispatcher.

### Mouse capture and event production are split across files

- `src/dinput.cc` owns relative/absolute mode. Relative mode accumulates mouse
  deltas; forced relative mode is used while the first-person cursor is
  captured.
- `src/mouse.cc` processes SDL/touch input, builds the engine's mouse-event
  state, updates the cursor position, and exposes `mouseGetEvent`,
  `mouseGetPosition`, and `mouseResetButtonState`. It does not decide which
  multiplayer action a click means.
- `src/input.cc` enqueues the special first-person left/right click event codes
  on mouse-down while F11 is active, except while specific modal game modes
  own the input. `inputGetInput` can separately return `-2` for a mouse-state
  event. Those are two representations in one input pipeline; the viewer
  must consume each edge once rather than send one click twice.
- `src/game_mouse.cc` returns early from `gameMouseRefresh` while the
  first-person preview is enabled. Normal isometric cursor updates and
  edge-scroll behavior are therefore not a substitute for first-person mouse
  look, picking, or interaction.
- Right-click in F11 releases/captures the mouse. In absolute mode, clicks are
  aimed at the visible pointer and can trigger a hold action menu. In relative
  mode, the pointer is hidden and clicks target the screen center; holding left
  opens the centered action menu. Menu transitions temporarily release mouse
  capture and restore it depending on whether an interaction opens loot.

### First-person mouse actions currently execute local game actions

The F11 path in `gameHandleKey` handles clicks differently in and out of combat:

- In combat, a released left click inside the isometric window picks a critter,
  object, door, or ground tile. It can start a local `_combat_attack_this`,
  loot, use a door/scenery, or register a local move animation. The Ctrl
  modifier affects the local AP calculation before movement.
- Out of combat, the captured-pointer path targets the center; the released
  pointer path targets the cursor. A short click tries talk, loot, use, or a
  move. A held click opens the F11 action menu when a suitable target is under
  the pointer.
- `gameFirstPersonPreviewShowActionMenu` renders the menu locally, but its
  selection switch directly calls local actions such as `actionTalk`,
  `actionPush`, `actionUseSkill`, `actionPickUp`, loot/inventory functions,
  and object use. That selection UI can be reused, but the action-dispatch
  switch must not be copied wholesale into multiplayer.
- The viewer already has multiplayer wire mappings in `src/main.cc`, including
  `viewerSendPrimaryVerb`, `viewerSendActionMenuVerb`, skill targeting, loot
  requests, combat attack forwarding, `mv`/`cmove`, and busy/turn gates. F11
  should resolve first-person targets and route choices through those paths or
  a shared viewer-safe dispatcher.

## Control inventory and multiplayer treatment

| F11 control | Existing single-player path | Multiplayer treatment |
|---|---|---|
| F11 enter/exit | `gameToggleFirstPersonPreview` | Already routed in the viewer loop. Preserve capture reset and mode-refresh behavior. |
| Mouse look | Relative delta -> `tileFirstPersonPreviewLook` | Keep in the existing preview-input helper; verify input pumping in main and modal/combat loops and preserve sensitivity. |
| Right-click capture/release | Special first-person right-click event in `gameHandleFirstPersonPreviewInput` | Keep as client-local UI state; consume its corresponding mouse event so it does not also cycle multiplayer cursor mode. |
| W/A/S/D movement | `gameHandleKey` -> `tileFirstPersonPreviewMove` | Translate the relative direction to a first-person destination, then send `mv <tile> <run>` out of combat or `cmove <tile> <run>` in combat. Do not register a local movement animation. Let server validation and streamed movement decide the result. |
| Combat attack click | First-person target pick -> `_combat_attack_this` | Reuse the viewer attack-commit hook where its assumptions hold; enforce the viewer's current-turn and presentation-busy gates. Do not execute a second local attack path. |
| Out-of-combat talk/use/pickup/loot/door | Local `action*` / `_action*` calls | Resolve first-person target locally, require a valid synchronized `netId` when the action needs an entity, and send the existing wire verb. Reuse pending-loot/modal handling. |
| First-person hold action menu | `gameFirstPersonPreviewShowActionMenu` plus local action switch | Reuse its presentation/selection behavior or factor a viewer-safe selection callback. Route the chosen menu item through the existing multiplayer action-menu sender; never run its local action switch in a viewer. |
| Ground click movement | Ground-tile picker, marker, yaw turn, local animation registration | Preserve picking, marker, and camera turn locally; send the selected tile via `mv`/`cmove`. Keep combat gates and avoid optimistic simulation changes. |
| F9 topology overlay | `tileFirstPersonPreviewToggleTopology` | Route as a client-local render toggle only when F11 is active. |
| `+`/`-` and wheel topology zoom | `tileFirstPersonPreviewZoomTopology` before brightness handling | Preserve overlay-dependent precedence and consume wheel events exactly once. Outside topology zoom, preserve viewer-local brightness behavior only if verified as safe and supported. |
| Tab automap | Temporarily exits F11, opens automap, then restores F11 and mouse mode | Decide and implement as client-local UI lifecycle; restore preview and capture state on cancel, modal exit, combat entry, and rebaseline. Never use Tab to mutate or load local world state. |
| Other shared game keys | `gameHandleKey` includes inventory, skilldex, character screen, combat, hand, pipboy, rest, options, saves, and other actions | Use the viewer's existing key dispatch for already supported wire actions and local-only screens. Explicitly block or replace local-save/load and simulation-mutating branches. Do not solve this by calling `gameHandleKey` wholesale. |

## Proposed implementation design

### 1. Establish one F11 input owner in the viewer

Add a small viewer-side dispatch boundary in `mainClientViewer`, preferably a
helper that receives the `ClientConnection`, key code, mouse state, and current
viewer busy/combat state. It should report whether the event was consumed.

The dispatch order should be explicit:

1. Continue to call `gameHandleFirstPersonPreviewInput` for shared preview
   mechanics that are already safe (relative mouse look and capture state).
2. Handle F11 toggle itself.
3. If F11 is active, route preview-owned key and mouse events to the
   first-person viewer dispatcher.
4. Only unconsumed events may proceed to the existing general multiplayer
   key/mouse handlers.

Do not call `gameHandleKey` from the viewer. The current no-`gameHandleKey`
boundary prevents local simulation writes and local save/load behavior from
being mistaken for authoritative network actions.

The ownership predicate must cover both the special click event codes and
`-2` mouse-state events, including press, hold/repeat, and release. A click
must not fall through from the F11 path into the isometric mouse handler or
be submitted twice.

### 2. Separate selection from simulation mutation

Review the F11 functions that combine UI, target selection, and action
execution. Keep or extract the client-safe pieces:

- first-person object/critter/door/ground picking;
- cursor capture/release and menu presentation;
- marker and camera yaw presentation;
- local selection menus and client-only windows.

Replace local action calls at viewer dispatch points with the matching wire
command. First-person click and menu behavior should reuse the existing
viewer mappings instead of creating parallel verb semantics. All picked
entities must be checked for a usable positive `netId`; invalid or stale
targets should fail closed rather than act on an unrelated object.

For actions not currently supported over the viewer wire, explicitly identify
the missing server verb and add it as a separate protocol/server task instead
of falling back to a local mutation.

### 3. Add a pure first-person movement destination path

Refactor the tile movement selection so viewer code can ask for the tile
selected by a relative F11 direction without queuing a local animation. Keep
the existing single-player `tileFirstPersonPreviewMove` behavior by composing
that selection helper with its current animation path.

The viewer path should:

- compute the destination relative to current yaw using the existing hex
  direction selection;
- preserve the existing run semantics: keyboard movement currently requests a
  run, while click-to-move follows the running preference. Translate those
  choices to the `mv`/`cmove` run flag;
- send the appropriate server command;
- let server-side pathing/AP checks and authoritative movement results decide
  success;
- clear or supersede pending movement consistently when leaving F11, changing
  elevation, receiving a rebaseline, or opening a modal.

Do not locally animate the authoritative actor to the destination before the
server streams movement. The viewer already has presentation code for streamed
movement; it should remain the only source of authoritative movement visuals.

### 4. Preserve F11 modal and pointer lifecycle

Audit every F11 transition around action menus, loot/inventory, dialogue,
automap, skill selection, pause/options, combat entry, map change, and
rebaseline. Establish one cleanup/restore path that:

- releases relative mode before pointer-driven UI;
- clears stale mouse button edges and pending long-press/menu state;
- keeps `gFirstPersonPreviewMouseReleased` consistent with actual device mode;
- restores captured/visible cursor state after the UI closes if F11 remains
  enabled;
- does not re-enable F11 if a modal or server transition intentionally closed
  it;
- continues pumping the network during blocking viewer modals where the
  existing service ticker supports that.

The F11 action-menu loop currently pumps `inputGetInput` and renders; confirm
that the installed viewer service ticker continues `conn.pump()` during this
loop before reusing it for multiplayer selections.

### 5. Keep shared key behavior deliberate

Build an explicit allowlist of keys while F11 is active:

- first-person movement and camera/render controls;
- viewer-supported gameplay keys whose existing dispatch already sends wire
  verbs or opens read-only/local UI;
- the F11 toggle and safe exit/modal controls.

For every remaining `gameHandleKey` case, classify it as one of:

1. client-only presentation/preference operation that may be reused safely;
2. supported authoritative action that must use an existing viewer wire
   mapping;
3. unsupported server action that must remain disabled until a wire path
   exists;
4. local save/load/simulation operation that must never run in the viewer.

This audit is required for F11-specific branches such as Tab automap and
topology zoom as well as shared keys such as combat, inventory, skills,
character sheet, pipboy, and rest. Preserve the existing viewer's handling for
supported keys and do not duplicate or shadow it in the new dispatcher.

## Implementation sequence

1. **Input/event audit:** document actual event order for key down, mouse down,
   hold/repeat, mouse up, right-click capture toggle, and relative-mode motion.
   Record which loop owns each event in normal play, F11, combat animation, and
   blocking modals.
2. **Safe pure helpers:** add or extract movement destination selection and
   any first-person selection helper needed to avoid local mutation. Add small
   tests for yaw-to-hex direction, invalid edge destinations, and unchanged
   single-player movement behavior.
3. **Viewer dispatcher:** introduce a clear consumed/unconsumed result for F11
   controls; prevent F11 mouse events from reaching isometric mouse handlers.
   Route F9, topology zoom, movement keys, and pointer capture/look first.
4. **Wire action mapping:** connect first-person target selection and menu
   choices to current viewer commands and its combat/OOC busy gates. Validate
   target `netId`, turn ownership, and presentation state before sending.
5. **Modal lifecycle:** support F11 pointer clicks and holds, menu selection,
   loot/dialogue/inventory, and any approved automap path; verify capture and
   input edge cleanup for success, cancel, and modal interruption.
6. **Shared-key audit:** reconcile F11-active key handling with the existing
   multiplayer key dispatcher and record unsupported/local-only actions
   explicitly. Do not expose local save/load or unported state mutations.
7. **Regression and live verification:** run focused tests, build both targets,
   then test the control matrix below with a real server/client session.

## Validation matrix

### F11 mode and camera

- Enter/exit F11 repeatedly while connected; the camera refreshes and yaw
  starts from the actor's current facing without stale pending movement.
- Relative mouse motion rotates smoothly in both single-player and viewer
  modes; sensitivity matches preferences; vertical motion does not change yaw.
- Right-click releases and recaptures the pointer. Neither transition creates
  an unintended cursor-mode change or gameplay action.
- Windowed and fullscreen modes behave correctly, including loss/regain of
  focus and relative-mode acquisition failure.

### Movement and server authority

- W/A/S/D in each of four camera-relative directions selects the corresponding
  adjacent hex and moves through server-streamed `mv` or `cmove`.
- Confirm keyboard movement's run behavior and click-to-move's walk/run
  preference behavior; test any supported modifier behavior separately.
- Out-of-combat and combat movement obey their respective server rules;
  insufficient AP, busy animation, invalid destination, and stale move input
  do not cause optimistic local movement.
- Test repeated movement while the streamed actor is busy and the existing
  pending-move timeout behavior.
- Click-to-move in captured-center and released-pointer modes selects the
  intended visible ground tile, respects wall occlusion, updates the marker,
  and does not send an isometric `tileFromScreenXY` destination.

### Mouse targeting and actions

- In both cursor modes, verify critter, scenery/object, door, and ground
  selection against the rendered first-person view and wall-depth occlusion.
- Short click, hold-menu, release, cancel, and cursor movement select the same
  menu item as single-player.
- Talk, use, pickup, corpse/container loot, door use, skill selection, push,
  and look each route through a supported multiplayer wire action or are
  explicitly disabled if no safe wire action exists.
- Combat attacks use the viewer attack hook and current-turn/busy gates; no
  direct local combat mutation or duplicate click is produced.
- Unsynchronized objects (`netId <= 0`), deleted targets, and stale hit targets
  are ignored safely.

### Overlay, shared keys, and lifecycle

- F9 cycles each topology-overlay state and off; wheel and `+`/`-` zoom only
  when the overlay is enabled, preserve zoom bounds, and do not leak a wheel
  event into the viewer's other controls.
- Tab automap and other explicitly supported modals restore correct F11,
  pointer, and network-pump state on close and cancel.
- Supported multiplayer hotkeys retain their current meaning while F11 is
  active; unsupported local save/load and simulation-only branches never run.
- Open each relevant modal while in F11, then test normal close, Escape,
  combat start, map/elevation transition, disconnect, and rebaseline.
- Regression-test ordinary single-player F11 controls and ordinary
  multiplayer isometric controls to detect shared input-path regressions.

## Acceptance criteria

- Every F11 preview control in scope has an explicit viewer route and a test.
- No viewer-only F11 input calls an unhooked local simulation action or local
  save/load path.
- Each gameplay action is sent once through an existing or deliberately added
  authoritative wire command; the viewer waits for streamed state rather than
  locally committing server-owned state.
- Mouse capture, first-person picking, combat/OOC behavior, modal cleanup, and
  F11 exit are reliable under both pointer modes.
- Existing single-player behavior and multiplayer non-F11 controls remain
  unchanged.
