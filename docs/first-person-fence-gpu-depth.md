# First-person fence depth pass

One-height wooden walls (cell type 1, wood material, rendered as transparent
wood type 7) opt affected sprites into a shared OpenGL depth-texture pass.
Wooden building walls and other wall types retain their existing behavior.
Wall raycasting, wall colors, and wall-to-wall visibility still use the CPU.

The CPU already knows the nearest opaque wall depth at each screen pixel.
The shader samples that depth and discards a sprite fragment if a wall is
closer or at equal depth. A transparent fence texel does not write wall depth,
so the sprite can appear through its opening. Critters use pixel depths;
scenery/items additionally retain the existing conservative wall/sill policy.
Sprite-to-sprite ordering remains stable back-to-front, including outlines.

Depth storage is uploaded column-major as a transposed float texture, with no
transpose pass. A small second texture carries wall/sill depths. Preparation
is lazy: scenes without visible sprites behind a fence need no depth upload.
Affected sprites share the upload. Identical depths reuse the previous GPU
textures even when actors animate and the scene refreshes. Sprite textures
remain in the existing palette-aware cache; there are no screen-sized sprite
mask uploads. At 1920x1080 a changed pixel-depth image transfers about 8 MiB,
independent of fence proximity and the number of affected sprites.

The pass uses SDL's existing OpenGL compatibility context. Queued SDL work is
flushed before direct GL commands; the shader restores GL state and synchronizes
SDL's texture-binding cache before menus/cursors resume drawing. Initialization
requires float/NPOT textures and at least three fragment texture units. Shader
or depth-upload failure retains CPU clipping; other SDL backends retain their
existing paths. Resources are destroyed before SDL's context is destroyed.

## Live comparison

The pass is enabled by default when supported. The startup debug log reports
`F11 fence depth shader: enabled` or the fallback. To compare the old clipping
path, set `$env:F2_FENCE_GPU_DEPTH = '0'` in the PowerShell session that launches
the game, then restart it. Set it to `'1'` and restart to restore the shader.
This switch does not restore the superseded per-sprite CPU mask uploads.

With `show_fps` enabled, `F11 GPU avg` includes:

- `upload`: existing full-screen color-framebuffer upload time.
- `depthPack`: CPU conversion/comparison of wall depths.
- `depthUpload`: depth texture transfer API time, including any driver wait.
- `depthKB`: bytes actually transferred; unchanged depths should reach zero.
- `queued`, `draws`: submitted pieces and rendering batches.

Depth preparation occurs during scene building, before presentation. It is
included in actor/scene timing and is reported separately from presentation's
`total`; do not add it twice to scene timing. These are CPU elapsed timers, not
GPU timestamp queries. A high upload duration can include a driver stall.

Use the same camera position for comparisons: distant fence, close fence,
moving alongside it, then stationary while critters animate. Check critters,
scenery, overlapping fences, walls behind gaps, combat outlines, and open UI
windows. Compare frame time as well as upload time. Live gameplay performance
has not been established by the synthetic benchmark.

## Regression test

Configure the game build with `-DFALLOUT_BUILD_GPU_DEPTH_TEST=ON`, then build
and run `first-person-gpu-depth-test`. It uses hidden OpenGL windows and compares
pixels in memory; it creates no screenshots. Tests cover depth/alpha behavior,
wall/sill/cap policy, equality, SDL overlays, scaling, resize/context recreation,
unchanged-depth reuse, column-only updates, and fallback selection.

Pass `--benchmark` for a 1080p synthetic comparison. The old SDL case uses
precomputed clipping geometry, so its reported time intentionally excludes
CPU clipping/geometry construction. The shader cases include depth preparation
and distinguish unchanged from changed depth images. Both wait for GPU
completion in the test only; the game adds no `glFinish` calls.
