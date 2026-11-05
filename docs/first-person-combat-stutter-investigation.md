# F11 Combat Movement Stutter Investigation

## Current symptom

In F11 first-person view, moving during combat becomes laggy or choppy. The
same movement is reported to become smooth immediately after toggling from
F11 to isometric view and back to F11. The toggle workaround has been reported
as reliable. Combat stutter was also reproduced in a cave without trees, so
trees are not required to trigger it.

The issue is still unresolved. No controlled frame-time measurements or
profiling captures have been collected yet.

## Attempts and observed results

| Attempt | Change | Result |
| --- | --- | --- |
| Shorter combat camera interpolation | Reduced camera follow duration from 180 ms to 80 ms during combat. | User reported a small improvement, but the stutter remained. This setting is still present. |
| Reset camera position on combat transition | Reinitialized camera start and target positions when `isInCombat()` changed; cleared the buffered movement direction. | Initially seemed fixed, but later testing did not reproduce the fix. The transition logic remains in `tile.cc`. |
| Reset camera and yaw at player combat-turn start | Invalidated the first-person camera, cleared the movement marker/buffer, and synced yaw to `gDude->rotation` after `attackInit`. | User tested and reported no improvement. Removed; do not retry this timing hook. |
| Synchronize camera yaw to player at combat entry | Made combat entry use the same player-facing yaw calculation as enabling F11. | No fix by itself; the full mode-refresh state reset is now applied after combat state becomes active. |
| Defer full F11 refreshes to the outer main loop | Skipped immediate F11 refreshes for dirty rectangles. | Broke character animation updates because combat animation loops render independently of the outer main loop. Reverted. Immediate F11 refreshes remain. |
| Reject movement while the player is animating | Added an `animationIsBusy(gDude)` guard. | Movement felt clunky because repeated WASD input was discarded. Replaced with a one-step input buffer. |
| Buffer one WASD step while busy | Stored the latest movement direction while the player animation was busy; the buffer expires after 500 ms. | Did not resolve combat stutter. This buffer and its processor remain in `tile.cc`; F11 toggle clears the buffer. |
| Process buffered movement in the combat input loop | Added a call to `tileFirstPersonPreviewProcessPendingMove()` inside `_combat_input`. | User reported no improvement. This call is currently present in `combat.cc`; the outer main loop also processes the buffer. |
| Refresh the first-person scene in the combat animation wait loop | Added pending-move processing and a full first-person refresh on each `_combat_turn_run()` iteration. | User reported no improvement. Removed. |
| Show the existing FPS counter during combat | Added `renderFpsCounter()` calls to `_combat_input` and `_combat_turn_run`, gated by `settings.debug.show_fps`. | Removed after user clarified that mouse look stays smooth and only camera movement lags; frame rate is unlikely to explain the camera-only symptom. |
| Follow the active combat movement animation directly | The F11 camera used the same `gDude->x/y` sub-tile position that the isometric renderer uses while combat movement is active. | Reverted; this did not reproduce the reliable F11 toggle fix. |
| Emulate the F11 transition on combat entry | After `COMBAT_STATE_IN_COMBAT` is set, perform the tile-layer equivalent of turning F11 off and back on. | Implemented; this uses the same camera/topology resets and both the isometric and first-person refresh paths, without changing mouse mode. The current viewing yaw is restored after the refresh. |
| Cache tree-size classification | Cached the per-art tree check used by the double-height scenery work. | No improvement; cache and its includes were removed. The user later reverted the double-height tree changes. |
| Reorder billboard raster loops and check transparency first | Switched sprite traversal to row-major order and skipped transparent pixels before wall-depth checks. | Combat lag still occurred in a cave without trees. This optimization did not explain or fix the combat-only symptom. |
| Coalesce queued WASD events | Replaced queued movement keys with the newest direction while F11 was active. | No effect; change was removed. |
| Discard relative mouse delta on combat transition | Drained accumulated relative mouse motion when combat state changed. | No effect; change was replaced. |
| Reset input and mouse mode on combat entry | Flushed SDL/input events, reset mouse buttons, and toggled relative mouse mode. | No effect and prevented F11 from toggling during combat. Reverted. |
| Cache per-column billboard opacity bounds | Added a cache of the first/last opaque pixel for each source sprite column and skipped transparent columns and empty vertical ranges during F11 billboard rendering. | User reported no noticeable FPS improvement near forests. The change remains in `tile.cc`, but the dominant cost is still unidentified. No controlled frame-time measurement was collected. |
| Cull distant scenery before art/projection work | Skip scenery billboards with a first-person depth greater than 24 world units, while retaining critters and items at the existing range. | Newly attempted; user validation pending. This is intentionally conservative and can be tuned or removed if distant scenery is expected to remain visible. |
| Adaptive coarse scenery rasterization | Render scenery billboard samples in 2x2, 3x3, or 4x4 blocks based on projected height; actors and items remain full resolution. | Newly attempted; user validation pending. This targets the large close-range trees and corn stalks where per-pixel depth testing scales explosively. |
| Request an accelerated SDL presentation renderer | Request `SDL_RENDERER_ACCELERATED`, log the selected backend, and explicitly fall back when unavailable. | Phase 1 GPU groundwork; this accelerates presentation only and does not yet move F11 billboard rasterization off the CPU. |
| Hybrid GPU scenery billboard pass | Cache palette-converted scenery FRMs as SDL textures, submit wall-clipped billboard quads, and sort them far-to-near; retain CPU rendering for critters/items and fallback when acceleration is unavailable. | Newly implemented; build/test and user FPS validation pending. |
| Extend GPU billboard pass to items | Submit item billboards through the cached GPU texture path and force nearest-neighbor texture scaling; retain CPU critter rendering. | Newly implemented; user reports the scenery pass greatly reduced FPS loss. Build/test and item/actor overlap validation pending. |
| Collapse fully visible GPU billboards | Detect scenery/item spans with no wall occlusion and submit one clipped quad instead of treating them as multiple visibility segments. | Newly implemented; preserves the existing wall-clipped path and reduces CPU submission overhead for open outdoor views. User validation pending. |
| Reject off-screen billboards before art work | Use a conservative projected radius to discard objects fully outside the first-person viewport before facing, prototype, and FRM processing. | Newly implemented; targets dense maps where many scenery objects are outside the view. User validation pending. |
| Preserve item labels in GPU path | Re-render item text labels into the CPU overlay before returning from GPU billboard submission. | Correctness follow-up; prevents item labels from disappearing when item billboards use GPU rendering. |
| Prevent GPU billboards from covering critters | Temporarily tested elevation-wide and visible-critter CPU fallbacks, then removed them after they caused dense forests to lose the GPU path. | Reverted; do not reintroduce a frame-wide critter fallback. |
| Render critters through GPU billboard pass | Submit critter FRMs through the same cached texture and wall-clipped GPU path as scenery/items, with far-to-near sorting across all GPU billboards. | Newly implemented; targets the remaining close-billboard lag and removes the CPU/GPU compositing conflict. User validation pending. |
| Merge adjacent GPU billboard clips | Coalesce adjacent wall-visible horizontal clips with the same texture, destination, and depth before sorting/drawing. | Newly implemented; reduces SDL draw calls when a billboard crosses small wall-depth transitions. User validation pending. |
| Reject fully wall-occluded GPU billboards | Test projected billboard columns against the existing wall/sill depth arrays before creating GPU submissions. | Newly implemented; avoids texture submission and draw work for scenery/items/critters hidden entirely behind walls. User validation pending. |
| Reject fully occluded actors before facing and FRM work | Scan an oversized projected footprint against both wall and sill depth before calculating billboard facing, prototype details, or locking the FRM. | Newly implemented; safely skips actors hidden behind walls while preserving potentially visible edge columns. User validation pending. |
| Avoid redundant GPU clip-state changes | Cache the active SDL clip rectangle during the sorted billboard draw loop and update it only when the next billboard uses a different clip. | Newly implemented; reduces renderer state calls when adjacent submissions share a clip rectangle. User validation pending. |
| Preallocate the GPU billboard queue | Reserve a conservative per-frame billboard capacity before object traversal so dense scenes do not repeatedly grow and copy the submission vector. | Newly implemented; reduces allocation/copy overhead in dense scenery scenes. User validation pending. |
| Preserve GPU billboards across extra presentations | Keep the submitted billboard queue until the next F11 render instead of clearing it after every `renderPresent()`. Combat and modal UI can present multiple times without rerendering the scene, preventing billboards from flickering on those extra presentations. | Newly implemented; F11 combat flicker fix. User validation pending. |
| Composite CPU overlays after GPU billboards | Track screen rectangles updated after the F11 scene refresh and replay them after the GPU billboard pass. Menus, widgets, and the software cursor now remain above GPU-rendered objects without restoring CPU billboard rendering. | Newly implemented; fixes UI/cursor ordering. User validation pending. |
| Render the floor in adaptive blocks | Sample distant floor rows near the horizon in 4x4 blocks and retain 2x2 blocks in the close foreground, reducing CPU floor work while preserving detail where perspective changes fastest. | Newly implemented; targets the measured dominant floor stage. User validation pending. |
| Render wall spans in vertical 2-pixel blocks | Sample wall textures once for each two-pixel vertical span and fill both pixels, while retaining one ray and wall-depth result per screen column. | Newly implemented; reduces wall texture sampling and framebuffer writes without changing horizontal wall boundaries or billboard depth columns. User validation pending. |
| Cache scenery prototype profiles | Cache prototype-stable wide/medium/tall/rock/tiny scenery flags after the first lookup; keep frame-specific art-size fallback calculations dynamic. | Newly implemented; removes repeated long prototype classification chains in dense scenery scenes. User validation pending. |
| Add GPU first-person backend capability probe | Expose accelerated renderer name and target-texture support, and log the capability needed by the upcoming GPU floor/wall composition backend without changing the active CPU path. | Newly implemented; groundwork only, no visual or FPS change expected. |
| Verify SDL OpenGL context ownership | Confirm that the SDL window has a current OpenGL context before any future shader pass is enabled; retain the CPU fallback while SDL owns presentation state. | Newly implemented; prevents unsafe raw-GL integration and records whether isolated shader integration can begin. |
| Collapse CPU overlay replay history | Replace the unbounded list of updated UI rectangles with one union rectangle per F11 scene. Replaying the current framebuffer once covers all updated areas while preventing combat/menu presentation cost from growing over time. | Newly implemented; reduces overlay draw calls during repeated combat and modal presentations. User validation pending. |
| Composite reduced floor through GPU texture | On accelerated target-texture renderers, keep reduced CPU floor sampling but upload the reduced floor image and scale it on the GPU before billboards; use the existing CPU floor path when unsupported. | Newly implemented; moves floor block filling and final floor composition to the GPU. User validation pending. |
| Revert reduced GPU floor composition | Removed the reduced floor texture conversion, wall-mask scan, upload, and SDL composition after runtime measurements showed lower floor time but higher total frame time. | User measured total F11 time rising to approximately 5.5-6.4 ms from the previous approximately 3.2 ms; CPU adaptive floor rendering is restored. |
| Use row fills for adaptive floor blocks | Replace the inner per-pixel floor write loop with `memset` for each horizontal block while preserving the same 4x4 distant and 2x2 foreground samples. | Newly implemented; targets CPU floor write overhead without changing sampling or visual output. |
| Extend distant floor sampling band | Keep 4x4 floor samples through the upper two-thirds of the floor and retain 2x2 samples only in the close foreground. | Reverted after user measurements remained similar or slightly worse, with floor timing around 1.4-1.75 ms and no reliable total-frame improvement. |
| Hoist wall-ray invariants | Move the doubled camera coordinates and wall intersection constants outside the per-column ray loop. | Newly implemented; preserves ray results while removing repeated invariant calculations from every wall column. User validation pending. |
| Filter F11 actor traversal by elevation | Add an elevation-filtered object visitor and use it for F11 actor rendering, avoiding callback dispatch for objects on other map elevations while preserving the existing callback filters. | Newly implemented; targets actor-stage overhead in scenes with objects across multiple elevations. User validation pending. |
| Select isolated OpenGL shader backend | Confirmed that further floor/wall GPU migration requires an isolated OpenGL composition backend rather than SDL texture uploads or raw GL calls mixed into the SDL renderer. | User selected the isolated OpenGL approach; no runtime backend change made yet because the shader pass must be added atomically with a CPU fallback. |
| Probe isolated OpenGL shader resources | Added runtime loading, compilation, linking, and cleanup of a modern GLSL 3.30 core floor-projection shader through SDL's current OpenGL context. | Capability/resource probe only; rendering remains on the existing SDL/CPU path, so FPS behavior should be unchanged. |
| Log OpenGL shader environment | Record the active OpenGL version, GLSL version, and vendor after the probe succeeds. | Newly implemented; provides the compatibility data needed before enabling shader floor textures. |
| Confirm shader hardware environment | User confirmed the probe on NVIDIA OpenGL 4.6.0 with GLSL 4.60 and NVIDIA driver 595.97. | The target supports the modern isolated shader path; no compatibility fallback is needed for the user's active hardware. |
| Add persistent GPU floor-cache resource | Added an OpenGL RGBA floor-cache texture with nearest filtering and palette tracking. Explicit atlas-update calls replace the texture contents even when dimensions are unchanged. | Resource groundwork only; the texture is not composed while SDL owns the active frame target. |
| Upload row-major floor atlas | Convert the cell-major CPU floor cache into a world-addressable row-major atlas and submit it only when the floor layout changes. | Reverted after the game crashed entering F11 during the first live OpenGL texture upload; shader/resource probe remains, but no GL texture upload is performed. |
| Separate shader readiness from scene readiness | Add an explicit scene-backend state and validate an offscreen OpenGL framebuffer during renderer initialization while keeping F11 drawing disabled. | Prevents F11 from issuing raw GL resource/draw calls until an isolated target owns the complete composition lifecycle. |
| Add isolated scene target lifecycle | Add guarded begin/end APIs that bind the validated framebuffer and viewport, then restore the caller's framebuffer and viewport. | Lifecycle groundwork only; no F11 rendering uses the target yet. |
| Stage generated floor cache | Copy the indexed floor cache into renderer-owned storage only when the floor layout changes. | Keeps future GL texture upload work out of the CPU floor callback; no visible rendering change yet. |
| Stage first-person camera packet | Copy camera position, direction, camera plane, and map dimensions through the renderer API each F11 frame. | Completes the CPU-only input packet needed by a future renderer-owned GPU floor pass; no visible rendering change. |
| Add guarded staged-floor draw | Add a renderer-owned API that palette-expands and uploads the staged cache only inside the bound isolated scene, then applies live camera uniforms and draws the shader floor pass. | First live-data GPU floor operation is available for controlled integration; no F11 call site invokes it yet. |
| Revert staged-floor diagnostic call | Removed the first live staged-floor invocation after it crashed entering F11 from the CPU render callback. | Confirms that GL texture/draw operations must run in a renderer-owned frame phase, not during tile rendering while SDL owns presentation. |
| Preserve F12 screenshot behavior | Kept F12 assigned to the existing screenshot handler instead of using it to activate the incomplete OpenGL scene. | Avoids changing an established shortcut before the full renderer-owned presentation path is ready. |
| Reserve F8 for experimental OpenGL mode | Added an F8 toggle for the renderer-side OpenGL backend transition. | Provides the requested activation key while keeping the existing F11 path as fallback; current OpenGL output is floor-only. |
| Add presentation readiness gate | Have the SDL presentation phase report shader and isolated-target readiness once while F8 is armed. | Establishes the safe renderer-phase activation boundary before the whole-renderer transition. |
| Verify staged inputs at presentation | Check the renderer-owned floor cache and camera packet from the presentation gate. | Confirms the complete CPU-to-renderer data handoff without entering unsafe GL work. |
| Gate on active F11 frame | Mark the staged packet active only while a valid F11 scene is being rendered. | Prevents future OpenGL presentation from consuming stale F11 data during menus or ordinary map rendering. |
| Add presentation eligibility predicate | Require F8 armed state, shader readiness, target readiness, staged inputs, and an active F11 frame together. | Creates the exact guarded condition for a future visible OpenGL presentation switch while keeping the backend disabled. |
| Revert renderer-owned live floor draw | Removed the first live floor draw from `renderPresent()` after F8 crashed in F11 view. | Confirms that SDL's renderer must be fully suspended or replaced before any raw GL draw, even from the presentation function. |
| Implement F8 backend transition | F8 now tears down SDL ownership, recreates the shader target, and presents the staged floor through direct OpenGL swapping; disarm/failure restores SDL. | First actual backend switch; intentionally floor-only and experimental, with SDL fallback. |
| Separate GL context readiness from SDL renderer readiness | Allow shader initialization after the SDL renderer is destroyed, provided the window still has a current OpenGL context. | Fixes the transition's initialization gate; SDL ownership must be released before direct GL presentation. |
| Create explicit experimental GL context | Create and make current a dedicated SDL OpenGL context after destroying the SDL renderer; delete it before SDL fallback recreation. | Fixes the remaining transition failure caused by SDL renderer destruction removing the renderer-owned context. |
| Correct live floor orientation and redundant CPU work | Flip the staged cache's vertical shader coordinate and skip CPU floor rasterization while F8 OpenGL floor presentation is active. | Corrects the CPU/OpenGL row-origin mismatch and removes duplicated floor work from the experimental path. |
| Correct OpenGL screen-Y origin | Convert bottom-left `gl_FragCoord.y` to top-down screen coordinates before the horizon discard. | Moves the shader floor from the upper half to the lower half of the direct OpenGL presentation. |
| Restrict floor geometry to lower viewport half | Replace the shortened fullscreen triangle with a lower-half rectangle. | Keeps the horizon level while cutting approximately half of the floor-only fragment workload at 1920x1080. |
| Compare floor-only OpenGL performance | User reports substantially lower FPS in F8 floor presentation than regular F11 in the same scene. | The first comparison was misleading: F8 still performed CPU wall, actor, and billboard generation even though the direct OpenGL path discarded those layers. |
| Skip undisplayed CPU scene work in F8 | Return from the F11 renderer after staging the floor cache and camera while the experimental OpenGL backend is active. | Removes CPU wall raycasting, actor rendering, billboard submission, and related interaction traversal from the floor-only path; full-scene composition remains future work. |
| Add GPU wall-column composition | Stage projected wall bounds and shade per screen column, upload them as a compact OpenGL texture, and fill wall spans after the floor pass. | Moves wall pixel fill to OpenGL while retaining the existing CPU topology/ray traversal; texture materials, billboard depth, and UI composition remain incomplete. |
| Add OpenGL billboard composition | Reuse the existing visible billboard submissions with cached palette-expanded GL textures, far-to-near ordering, alpha blending, and wall clip ranges. | Brings scenery/items/critters into F8 without SDL texture ownership; shared GPU depth and UI composition remain incomplete. |
| Test isolated shader draw path | Run a non-presented fullscreen-triangle draw into the validated target using a core-profile VAO, then restore the default framebuffer. | Initialization smoke test only; visible F11 composition remains unchanged. |
| Restore GL state after shader smoke test | Save and restore framebuffer, viewport, clear color, shader program, VAO, active texture unit, and 2D texture binding around the initialization draw test. | Prevents the non-presented validation draw from perturbing SDL presentation state. |
| Resolve floor shader uniforms at initialization | Resolve all floor projection uniform locations once after shader linking and reject the shader program if the interface is incomplete. | Initialization-only groundwork; no visible rendering change. |
| Exercise floor sampler in smoke test | Bind a startup-owned 1x1 RGBA texture and set all floor projection uniforms before the non-presented draw. | Validates sampler/uniform state without live F11 uploads or visible composition changes. |
| Add opt-in F11 stage timing | When the existing FPS debug setting is enabled, report rolling 60-frame averages for total F11 render, floor projection, wall rendering, and actor submission time. | Newly implemented; measurement only, no timing calls when FPS debug is disabled. |

An earlier renderer optimization cached the floor FRM lookup and floor sample
map. No measured FPS result was recorded for that change.

## Billboard FPS investigation

The forest symptom is distinct from the combat movement stutter: the user
reported F11 performance falling to approximately 7 FPS near forests,
especially with tall tree billboards. The first targeted attempt focused on
transparent-pixel traversal inside `tileFirstPersonRenderActor`. It did not
produce a noticeable improvement, which indicates that the main cost may be
object traversal, frame locking/art lookup, depth-buffer writes, or the number
of projected billboard columns rather than transparent pixel checks.

The next attempt should measure frame time with no scenery, a small number of
scenery objects, and a forest view, then profile or instrument the separate
stages of `tileRenderFirstPersonPreview`. Do not assume another sprite-pixel
micro-optimization addresses the bottleneck without that measurement.

## What toggling F11 resets

The F11-off/on path changes more state than the combat-transition code:

- `tileFirstPersonPreviewToggle()` invalidates camera initialization, resets
  the move marker and pending move, resets topology state, and recalculates yaw
  from `gDude->rotation` when F11 is enabled.
- `gameHandleKey()` changes forced relative-mouse mode. Enabling F11 also resets
  the input event queue and mouse button state; disabling F11 restores the
  mouse mode and clears first-person click/menu state.
- The isometric renderer draws between the two F11 transitions.

The user's mouse look remains smooth during the problem, while camera
translation lagged. Following the player's animated sub-tile position in combat
fixed the lag. A 45 ms eased transition has now been added to smooth the
animation samples without restoring the previous long chase.

## Current working-tree status

- `tile.cc` contains the 80 ms combat camera interpolation, combat-state camera
  reinitialization, and one-step movement buffer with a 500 ms timeout.
- `combat.cc` currently calls `tileFirstPersonPreviewProcessPendingMove()` in
  the player combat input loop. This was the latest attempted fix and was
  reported ineffective.
- The latest attempt to reset input and mouse mode at combat entry was removed.
- Combat entry now performs the same first-person camera/yaw refresh as an F11 off/on transition.
- The player-turn-start camera reset was also removed after it failed to help.
- The double-height tree changes and the tree-classification cache are absent.

## Next investigation

If combat movement still stutters, collect frame-time measurements while
checking whether the animation's `gDude->x/y` offsets are advancing at the
same rate as the isometric renderer.
