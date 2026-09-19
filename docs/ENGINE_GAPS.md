# What dEngine still needs to be a game engine

An audit of `master` (2591fde) plus the four open pull requests, and what is
left over once they all land.

## The open pull requests

| PR | Adds | Base |
| --- | --- | --- |
| [#2](https://github.com/spencerelliott/dEngine/pull/2) | Native PowerVR renderer replacing GLdc, CMake asset pipeline | 2591fde |
| [#3](https://github.com/spencerelliott/dEngine/pull/3) | Game logic API: objects, behaviors, input, scene lifecycle | 2591fde |
| [#4](https://github.com/spencerelliott/dEngine/pull/4) | Rigid body physics and collision queries | 2591fde |
| [#5](https://github.com/spencerelliott/dEngine/pull/5) | Audio: preloaded SFX and streamed music | 2591fde |

All four branch from the same commit; none is stacked on another. Between
them they cover the renderer, the gameplay seam, simulation and sound —
which is to say they cover most of what an engine needs and almost none of
what a *game* needs.

### They overlap, and one pair conflicts outright

| File | #2 | #3 | #4 | #5 |
| --- | --- | --- | --- | --- |
| `CMakeLists.txt` | x | x | x | x |
| `src/dEngine.c` | | x | x | x |
| `src/world.c` | | x | | x |
| `src/main.c` | x | x | | |
| `src/renderer.c` | x | x | | |
| `src/renderer_fixed.c` | x | x | | |
| `build_assets.sh` | deletes | | | edits |

**#2 deletes `build_assets.sh`; #5 edits it** (adding `wav` to the copied
extensions). That is a textual conflict, but the silent version is worse:
#2 replaces the script with per-file CMake rules whose
`ASSET_COPY_EXTENSIONS` list is `cp dat map mtl obj md5mesh md5anim scene
cfg lbr` — no `wav`. Merge both PRs and the build succeeds, the audio code
runs, and every clip fails to load because no `.wav` was ever copied to the
disc. The fix is one word in `CMakeLists.txt`.

Suggested order: **#2 → #3 → #4 → #5**. #2 is the foundation and touches
the most plumbing; #3 carries a batch of independent engine bug fixes (map
bounds check, `FreeModel` leaks, an unguarded texture dereference in
`RenderEntitiesF`) that are worth having early; #4 is only useful once #3
provides a dynamic-object seam; #5 is the smallest and just needs the `wav`
line when it rebases onto #2's asset rules.

## What is still missing

### Rendering and performance

1. **No culling of any kind.** `RenderEntitiesF` walks all of `map[]` and
   transforms every vertex of every entity, every frame. There is no
   frustum test, no distance cull, no PVS. `camera.h` declares
   `visibility_set_t` as an empty struct and `dEngine.h`'s own TODO list
   opens with "Add precalculator Visibility Set". Per-entity AABB frustum
   rejection is about a hundred lines and is the single largest win
   available on this hardware.

2. **Only the first mesh of an MD5 model is ever drawn.**
   `renderer_fixed.c:249` reads `for (i = 0; i < 1 /*md5Object.md5Model.num_meshes*/; i++)`,
   and `World_ReadMD5s` loops the same way. A character built from several
   meshes with different materials — body, head, weapon — cannot be
   rendered today.

3. **No animation system above playback.** `MD5_Update` advances one clip.
   There is no blending or cross-fade between clips, no per-bone masking,
   no animation events, no bone attachment sockets (for a weapon or a
   muzzle flash), no root motion.

4. **Lighting is one global `light_t`.** No per-entity lighting, no
   multiple or dynamic lights, no light culling, no baked vertex lighting.
   The PVR backend in #2 does not light at all — colour comes from the
   model's vertex colours.

5. **No fog, particles, billboards, decals, sky or screen transitions.**
   Fog in particular is nearly free on the PVR and is missing.

6. **No LOD, instancing or static batching.** Every OBJ in the map is its
   own entity with its own draw and its own matrix upload.

7. **`renderer_progr.c` is dead code.** 847 lines implementing the GL ES
   2.0 uber-shader path, shadow mapping, specular and bump mapping — and
   it is not in the CMake `SOURCES` list. Neither is anything under
   `data/shaders/`. Either restore it as the host-side development
   renderer or delete it; leaving it is a trap for the next reader.

### World and content

8. **No spatial partitioning.** #4 adds sweep-and-prune along X for
   physics, but the renderer has nothing and gameplay has no way to ask
   "what is near me".

9. **The map format cannot place anything.** `demo.map` is one shared
   matrix followed by a flat list of model paths. There are no per-entity
   transforms, no spawn points, no trigger volumes, no named entities and
   no key/value properties. A game needs to author placement and
   properties; right now every object in a level sits at the same matrix.

10. **Nothing generates collision from the level.** #4 gives sphere and
    AABB colliders but no tooling or import path builds them out of the
    level OBJs, so someone places every collider by hand.

11. **No asset residency story.** Everything loads at scene start into a
    16MB heap. Textures are not refcounted — #3 documents that
    `TEX_LoadTexture` returns cache hits by struct copy, so several
    materials alias one `data` pointer and freeing per scene would double
    free. VRAM is never reclaimed. This is the constraint that will
    actually stop a real game, well before frame rate does.

### Game systems

12. **No save games.** On this hardware that means the VMU: `vmu_pkg_t`,
    `vmufs_write`, an icon, and handling no-card / card-full / card-pulled.
    Nothing in the tree touches it, and a shipped game cannot skip it.

13. **No UI.** `font.c` draws a bitmap string through `RenderString`. There
    is no layout, no widgets, no menu navigation or focus model, no
    localisation, no dialogue or subtitle system.

14. **No game state machine.** `main.c` is `while(1) dEngine_HostFrame();`.
    Boot, title, menu, play, pause, game over have nowhere to live.

15. **Input stops at the pad.** #3 adds a maple driver and an action/
    binding layer, which is the right shape, but there is no rumble
    (`purupuru`), no VMU LCD icon, no second controller, no keyboard or
    mouse — all of which KOS supports.

16. **No frame pacing.** The main loop spins as fast as it can and
    `timediff` is raw wall time. #4 introduces a fixed accumulator, but for
    physics only; rendering and gameplay have no vsync-locked step.

17. **No console or cvars.** `config.c` is ten lines and does nothing.
    `commands.c` is a five-entry enum driving the free-fly camera.

### Build and engineering

18. **No CI, and almost no tests.** #4's host-side physics test is the only
    one. The non-Dreamcast branch of `CMakeLists.txt` is a bare `else()`
    with nothing in it, so there is no host build to run tests against.

19. **Failure handling is `exit(0)`.** `Comm_AddCommand` on overflow,
    `World_Init` on a missing config. Neither is acceptable once a game
    is loading content at runtime.

## Suggested order of work

1. Land #2, #3, #4, #5 in that order; fix the `wav` extension when #5
   rebases.
2. Frustum culling with per-entity AABBs — the cheapest large win.
3. Multi-mesh MD5 rendering with a material per mesh.
4. Entity placement and properties in the map format.
5. VMU save/load.
6. Game state machine and a menu/UI layer.
7. Animation blending and bone sockets.
8. Texture refcounting and VRAM reclaim.
9. Dynamic shadows — see `docs/SHADOWS.md`.
