dEngine physics
===============

A small rigid body simulation built for the Dreamcast: sphere and axis aligned
box colliders, gravity, impulse based contact resolution, and the queries a game
actually needs (rays, overlaps, sweeps, a character move).

It lives in two modules:

| file           | what it does                                                       |
| -------------- | ------------------------------------------------------------------ |
| `collision.[ch]` | shapes, overlap tests, ray casts and shape casts. No state.      |
| `physics.[ch]`   | bodies, the fixed step simulation loop, the solver and queries.  |

Both are independent of the renderer, the filesystem and the asset pipeline, so
they build and run on a host machine as well as on hardware.


Design
------

**Linear dynamics only.** Bodies have a position and a velocity, no orientation
and no angular velocity. Combined with shapes that are always axis aligned this
removes inertia tensors, matrix inversions and multi point manifolds from the
hot path. A box that gets shoved never topples, which is exactly what most
games want from crates, pickups, projectiles and elevators anyway.

**No allocation.** Bodies, broad phase pairs, contacts and the warm starting
cache all live in fixed size arrays sized by `PHYS_MAX_BODIES` (256),
`PHYS_MAX_PAIRS` (1024) and `PHYS_MAX_CONTACTS` (512). That is roughly 80KB of
BSS, known at link time, and nothing can fragment the 16MB heap at runtime.
Override the defines at build time if a game needs a different shape of world.

**Fixed timestep.** `PHYS_Update()` takes the frame time in milliseconds and
runs as many 1/60s steps as it owes, up to five per frame. A long hitch (a disc
load, say) is clamped instead of turning into a spiral of catch up steps. The
simulation therefore behaves the same whether the renderer manages 60, 30 or 12
fps. `PHYS_GetRenderPosition()` blends the two most recent steps so bodies still
look smooth when the display rate and the step rate disagree.

**The step itself**, in order:

1. *forces* — gravity, per body force accumulators and damping go into the
   velocities.
2. *integrate* — velocities move the bodies. Bodies flagged `PHYS_FLAG_CCD` are
   swept against static geometry first so fast movers cannot tunnel.
3. *broad phase* — sweep and prune along X. The body list is kept sorted by the
   minimum X of each bound and re-sorted with an insertion sort, which is linear
   when the scene is coherent, i.e. every frame after the first.
4. *narrow phase* — sphere/sphere, sphere/box and box/box tests turn the
   surviving pairs into one contact each.
5. *solve* — sequential impulses with accumulated clamping, warm started from
   the impulses of the previous step, then a positional pass that removes the
   leftover overlap.
6. *sleep* — bodies that have been slow for half a second are parked and skipped
   entirely until something touches them.

Warm starting is what makes stacks work with only four velocity iterations:
without it every step starts from zero and a pile slowly sinks into itself. The
impulses are kept in a small open addressed table keyed by the pair of body ids.


Units
-----

The module is unit agnostic. Gravity defaults to `-9.81` per second squared,
which reads as "one world unit is one meter". dEngine's Doom 3 maps are authored
in inches, so a game built on them wants something like:

```c
PHYS_SetGravity(0.0f, -1066.0f, 0.0f);          /* Doom 3 gravity, in inches */
PHYS_SetSolverParams(4, 3, 0.5f, 0.8f);         /* slop scales with the world */
PHYS_SetSleepParams(8.0f, 0.5f);                /* so does the sleep threshold */
```

The slop (the penetration the solver leaves alone) and the sleep threshold are
the two values that must follow the scale of the world. Everything else is
scale free.


Getting started
---------------

`dEngine_Init()` already calls `PHYS_Init()` and `dEngine_HostFrame()` already
calls `PHYS_Update(timediff)`. A game that never creates a body pays nothing.

```c
#include "physics.h"

collider_t collider;
rigidbody_t* crate;
vec3_t spawn = { 0.0f, 200.0f, 0.0f };

/* Static level geometry: a floor. */
COL_MakeBox(&collider, 1000.0f, 8.0f, 1000.0f);
PHYS_CreateBody(PHYS_STATIC, floorPosition, &collider);

/* A crate that falls onto it. */
COL_MakeBox(&collider, 16.0f, 16.0f, 16.0f);
crate = PHYS_CreateBody(PHYS_DYNAMIC, spawn, &collider);
PHYS_SetMass(crate, 20.0f);
crate->friction = 0.6f;
crate->restitution = 0.1f;
crate->userData = entity;
```

Then, once per frame, push the simulated position into whatever the renderer
draws:

```c
PHYS_BodyToMatrix(crate, entity->matrix);   /* writes the translation only */
```

`PHYS_BodyToMatrix()` uses the interpolated position, so it stays smooth even
though physics runs on its own clock.


Body types
----------

| type             | moved by            | mass     | typical use                  |
| ---------------- | ------------------- | -------- | ---------------------------- |
| `PHYS_STATIC`    | nothing             | infinite | level geometry, walls, floors |
| `PHYS_DYNAMIC`   | forces and contacts | finite   | crates, barrels, projectiles |
| `PHYS_KINEMATIC` | the game            | infinite | platforms, doors, the player |

A kinematic body pushes dynamic bodies and is never pushed back, so a moving
platform carries its cargo and an elevator cannot be stopped by standing on it.


Flags
-----

- `PHYS_FLAG_TRIGGER` — overlaps are detected and reported but never resolved.
  Checkpoints, damage volumes, pickups.
- `PHYS_FLAG_CCD` — the body is swept against static and kinematic geometry
  before it is moved, so it stops on thin walls instead of teleporting through
  them. Worth it for bullets, not for crates.
- `PHYS_FLAG_NO_SLEEP` — keeps a body awake forever.
- `PHYS_FLAG_DISABLED` — takes a body out of the simulation without destroying
  it, for object pooling.
- `PHYS_FLAG_GROUNDED` — read only, set by the solver when the body is resting
  on a surface facing up. `PHYS_IsGrounded()` reads it, `body->groundNormal`
  gives the slope.


Layers
------

Every body has one layer bit and a mask of the layers it collides with. Two
bodies interact only if each one's layer is in the other's mask:

```c
#define LAYER_WORLD   0x0001
#define LAYER_PLAYER  0x0002
#define LAYER_ENEMY   0x0004
#define LAYER_PICKUP  0x0008

PHYS_SetLayer(pickup, LAYER_PICKUP, LAYER_PLAYER);   /* only the player picks it up */
```

The same masks filter the queries.


Queries
-------

```c
raycast_hit_t hit;
vec3_t from, direction;

/* Hitscan weapon, ignoring the shooter, against everything but pickups. */
if (PHYS_Raycast(from, direction, 4000.0f, LAYER_WORLD | LAYER_ENEMY, shooter, &hit))
    Damage(hit.body->userData, hit.point, hit.normal);

/* Splash damage. */
rigidbody_t* touched[16];
int count = PHYS_OverlapSphere(center, 128.0f, PHYS_LAYER_ALL, touched, 16);

/* Where would this body end up if it moved by delta? */
if (PHYS_SweepBody(body, delta, PHYS_LAYER_ALL, &hit))
    printf("blocked at %.2f of the way\n", (double)hit.fraction);
```


Characters
----------

A player or an NPC usually wants to be driven directly rather than pushed
around by the solver. Make it kinematic and move it with `PHYS_MoveAndSlide()`,
which walks the motion forward, stops on the first surface, projects what is
left of the motion onto that surface and repeats up to four times:

```c
vec3_t motion;

vectorScale(wishDirection, speed * frameTime, motion);
motion[Y] += verticalVelocity * frameTime;

PHYS_MoveAndSlide(player, motion, LAYER_WORLD);

if (PHYS_IsGrounded(player))
    verticalVelocity = jumpPressed ? jumpSpeed : 0.0f;
else
    verticalVelocity -= gravity * frameTime;
```

It also pushes the body out of anything it is already stuck in before moving,
so a character that spawned inside geometry frees itself instead of jamming.


Events
------

```c
static void OnContact(rigidbody_t* a, rigidbody_t* b, const contact_t* contact)
{
    /* contact->normal points from a to b, contact->point is in world space. */
}

PHYS_SetContactCallback(OnContact);
PHYS_SetTriggerCallback(OnTrigger);
```

Both fire once per step for every touching pair: the contact callback after the
solver has run, the trigger callback as soon as the overlap is detected. They
report *touching*, not *started touching*: a game that wants enter and exit
events tracks them itself, which is a few lines and avoids paying for pair
bookkeeping that most callers do not need. Do not create or destroy bodies from
inside a callback.


Known limits
------------

These are deliberate. They are what buys the frame budget.

- **No rotation.** Bodies never spin, and a box collider is always axis aligned
  in world space. Rotating an entity does not rotate its collider.
- **One contact point per pair.** Fine without rotation, since only the normal
  and the depth matter.
- **Continuous detection is one sided.** A `PHYS_FLAG_CCD` body sweeps against
  static and kinematic bodies, not against other dynamic bodies. Two bullets
  fired at each other will pass through.
- **Mixed shape casts are conservative.** Box against box and sphere against
  sphere sweeps are exact. A sphere swept against a box uses the box grown by
  the sphere radius, so near a corner it can stop a fraction early, never late.
- **Sleeping bodies need a nudge.** If the ground under a sleeping body is
  deleted or teleported away, nothing wakes it. Call `PHYS_Wake()`.
- **The broad phase sorts on X only.** A level whose geometry is stacked along
  one axis produces more candidate pairs than one that is spread out. With a few
  hundred bodies this has not been a problem.


Tests
-----

The suite in `tests/physics_test.c` covers the collision primitives and the
simulation behaviours: falling, resting without sinking or jitter, bouncing,
friction, stacking, triggers, layers, tunneling, sleeping and waking, kinematic
pushing, the queries, pool exhaustion and determinism.

```sh
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_PHYSICS_TESTS=ON ..
make dEngine_physics_test && ./dEngine_physics_test
```

It links only `collision.c` and `physics.c`, so it needs neither a Dreamcast nor
the KOS toolchain.
