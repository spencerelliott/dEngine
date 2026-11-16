# game/

Game logic lives here. Every `.c` file in this directory is compiled into the
engine binary automatically — there is no list to add yourself to and no CMake
file to edit.

## Writing a script

```c
#include <dgame.h>

void Game_Init(void)
{
    Game_Log("hello\n");
}

void Game_Update(float dt)
{
    /* ... */
}
```

That is the whole setup. `dgame.h` is the only header you need; it is also the
complete API reference, so read it rather than this file when you want to know
what is available.

Adding or removing a file here triggers a CMake reconfigure on your next build
(via `CONFIGURE_DEPENDS`), so `make` or `ninja` is enough — no need to re-run
`cmake` by hand.

## Hooks

Every hook is optional. Define the ones you want; the engine supplies a
do-nothing default for the rest, which is why an empty `game/` directory leaves
the engine behaving exactly as it did before this layer existed.

| Hook | When |
|---|---|
| `Game_Configure(dg_config_t*)` | Before any subsystem starts. Pick the resolution and boot scene here. |
| `Game_ConfigureInput()` | After the engine installs its default bindings, so you can add to or replace them. |
| `Game_Init()` | Exactly once, after the boot scene has loaded and the camera exists. One-time setup only. |
| `Game_Update(dt)` | Every frame, before the world updates. |
| `Game_LateUpdate(dt)` | Every frame, after the world updates and before anything draws. |
| `Game_RenderHUD()` | Inside the 2D pass. Use `HUD_DrawText`. |
| `Game_SceneLoaded(scene)` / `Game_SceneUnloading(scene)` | Around a scene swap. `Game_SceneLoaded` also fires for the boot scene, right after `Game_Init` — so per-scene setup belongs here, not in `Game_Init`. |
| `Game_Shutdown()` | On the way out. |

Only one file may define any given hook — two definitions of `Game_Update` is a
duplicate symbol error at link time. Split larger games across files by putting
the shared logic in behaviors and keeping one file as the entry point.

The hooks work by weak linkage: the engine defines each one weakly in
`src/game_hooks.c`, and your plain definition replaces it. This is also why
`game/` sources are handed straight to `add_executable` rather than bundled into
a static library — an archive member is only pulled in to resolve an undefined
symbol, and the weak defaults mean nothing is ever undefined, so your override
would be silently dropped.

## Behaviors

A behavior is per-object logic plus per-object state, so several scripts can
attach to the same object without knowing about each other.

```c
typedef struct { float t; } bob_state_t;

static void Bob_Update(dg_object_t self, void* state, float dt) { ... }

DG_BEHAVIOR(bob, {
    "bob",                 /* name  */
    sizeof(bob_state_t),   /* state */
    0,                     /* on_attach  */
    Bob_Update,            /* on_update  */
    0                      /* on_detach  */
});
```

`DG_BEHAVIOR` registers the definition before `main` runs, so
`GameObject_AddBehavior(obj, "bob")` finds it from anywhere.

If your toolchain turns out not to run C constructors, that name lookup is the
only thing that breaks. Two escape hatches, both safe to use unconditionally
because registration is idempotent:

- `GameObject_AddBehaviorPtr(obj, &bob)` — attaches by pointer, registering on
  first use. Never depends on the constructor.
- `DG_RegisterBehavior(&bob)` from `Game_Init`.

The engine prints `[Game] N behavior(s) registered.` at boot. If that says `0`
while you have `DG_BEHAVIOR` declarations, constructors are not running.

## Objects

Objects are addressed by `dg_object_t` handles, never by pointer. The world's
entity table is rebuilt from scratch on every scene load, so a stored pointer
would dangle. A handle packs a slot index plus a generation counter: once an
object is destroyed, every handle to it stops resolving and every accessor
becomes a safe no-op instead of writing into a recycled entity.

Two consequences worth knowing:

- **Handles do not survive a scene switch.** Re-acquire them in
  `Game_SceneLoaded`.
- **Entities loaded from a `.map` file carry a baked matrix.** Moving one with
  `GameObject_SetPosition` preserves the rotation the map gave it, but
  `GameObject_SetRotation` or `GameObject_SetScale` resets that orientation —
  a general matrix has no unique head/pitch/roll to recover.

There is a hard ceiling of `MAX_MAP_ENTITIES` (256) live entities, shared with
the scene loader. `GameObject_Spawn` returns `DG_INVALID_OBJECT` when the table
is full; every accessor tolerates that handle, so ignoring the return value
degrades to no-ops rather than corrupting anything.

## Input

Bind named actions once, then poll them. Game code never mentions a physical
button:

```c
dg_action_t fire = Input_DefineAction("fire");
Input_BindButton(fire, 0, DG_BTN_A);
...
if (Input_Pressed(fire)) { /* rising edge, this frame only */ }
```

An action can carry up to four bindings; the largest-magnitude one wins, so
binding a movement axis to both the stick and the D-pad lets either drive it.

The engine reserves the `__cam_*` action names for its built-in free-fly camera,
which is on by default and is what makes the pad work with an empty `game/`.
Turn it off with `Engine_SetDebugCameraEnabled(0)`, or call
`Game_DefaultFlyCamera(dt)` yourself from your own `Game_Update`.

## Scenes

`Game_RequestScene("data/scenes/foo.scene")` is deferred: the swap runs at the
end of the frame, after rendering, never mid-frame.

**Known limitation.** Unloading frees per-entity geometry but deliberately keeps
the material library and texture cache alive for the lifetime of the process.
`TEX_LoadTexture` returns cache hits by struct copy, so several materials share
one data pointer, one path allocation and one GL texture name — freeing per
scene would double-free. The material library is loaded once from
`data/materials.lbr` and is scene-independent, so retaining it costs a few KB,
while the geometry that *is* freed is what actually dominates RAM. The cost is
that GPU texture memory is never reclaimed, so cycling through scenes with
disjoint texture sets can eventually exhaust VRAM. Fixing it properly means
refcounting textures by path and de-aliasing the cache.

## Physics

Not implemented. There is no collision, no rigid bodies and no raycasting in the
engine, and this layer does not stub any. Objects go exactly where you put them.
