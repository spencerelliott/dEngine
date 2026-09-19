# Dynamic shadows with PowerVR modifier volumes

What it would take to let dynamic models cast shadows on the environment,
using the PVR2's modifier volumes through the KallistiOS API.

This assumes [PR #2](https://github.com/spencerelliott/dEngine/pull/2) (the
native PVR backend) has landed — under GLdc none of this is reachable. PR #2
already ships `src/shadows.c` and `src/shadows.h` as empty placeholders with
an empty `adjacency_info_t`, which is exactly where this work goes.

## What the hardware offers

The PVR2 renders through five lists, binned in this order:

    PVR_LIST_OP_POLY   opaque polygons
    PVR_LIST_OP_MOD    opaque modifier volumes
    PVR_LIST_TR_POLY   translucent polygons
    PVR_LIST_TR_MOD    translucent modifier volumes
    PVR_LIST_PT_POLY   punch-through polygons

A modifier volume is a set of triangles submitted to a `_MOD` list. The tile
accelerator works out, per pixel per tile, whether that pixel falls inside
the volume, and then applies a second set of parameters to polygons in the
matching poly list that were submitted with a modifier-enabled header. No
stencil buffer, and — unlike a stencil approach — the receiving geometry is
never resubmitted.

Two modes, selected by `cmd.mod_normal` in the polygon header:

**Cheap shadow** (`mod_normal = false`). Inside the volume, the polygon's
final colour is multiplied by one global scale set with
`pvr_set_shadow_scale(true, scale)`. The receiving vertices stay the
ordinary 32-byte `pvr_vertex_t`. This is the mode to use.

**Two-parameter** (`mod_normal = true`). The header carries a second full
mode2/mode3 — a different texture, blend mode and fog — used inside the
volume, and every receiving vertex must become `pvr_vertex_tpcm`: 64 bytes,
two sets of UVs, colours and offset colours. That doubles vertex bandwidth
on every shadow-receiving polygon in the scene, whether or not it is in
shadow. Not worth it for this engine.

## The API

    void pvr_mod_compile(pvr_mod_hdr_t *dst, pvr_list_t list,
                         uint32_t mode, uint32_t cull);

No context step, unlike polygons and sprites. `mode` is
`PVR_MODIFIER_OTHER_POLY` for every triangle in a volume except the last,
which takes `PVR_MODIFIER_INCLUDE_LAST_POLY` (or `..._EXCLUDE_LAST_POLY`
for an exclusion volume).

Volume geometry is `pvr_modifier_vol_t` — one 32-byte struct carrying all
three vertices (`ax..cz`), already in screen space. You transform and
project them yourself, exactly as `SubmitVertex` in `renderer_pvr.c` already
does for ordinary vertices.

Receivers are built with

    void pvr_poly_cxt_txr_mod(pvr_poly_cxt_t *dst, pvr_list_t list,
                              int fmt,  int tw,  int th,  pvr_ptr_t txr,  pvr_filter_mode_t filt,
                              int fmt2, int tw2, int th2, pvr_ptr_t txr2, pvr_filter_mode_t filt2);
    void pvr_poly_mod_compile(pvr_poly_mod_hdr_t *dst, const pvr_poly_cxt_t *src);

Note the two texture descriptions, outside and inside. In cheap-shadow mode
the inside set is ignored, so pass the same texture twice.

Darkness is global, set once:

    void pvr_set_shadow_scale(bool enable, float scale_value);

And the opaque modifier bin has to exist. `initPVRRenderer` in PR #2
currently passes:

    {PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0}

Index 1 is `OP_MOD` and it is **disabled**. It needs a real size — say
`PVR_BINSIZE_8` — which comes out of the same VRAM pool textures live in.

## Three tiers, cost ordered

**Tier 0 — projected blob.** An alpha-blended textured quad on the floor
under the model, in `TR_POLY`. Two triangles per caster, no modifier
volumes, no risk. Most Dreamcast games shipped with this. It should exist
regardless, as the fallback and as the LOD for distant casters.

**Tier 1 — cheap shadow volume from a low-poly proxy.** Per dynamic caster,
extrude a hand-authored low-poly shadow proxy away from the light, submit
the result to `OP_MOD` as one closed volume, and give shadow-receiving
level geometry modifier headers. **This is the target.**

**Tier 2 — per-frame silhouette extraction from the render mesh.** Needs
edge adjacency (the empty `adjacency_info_t`), a per-triangle
`dot(normal, lightDir)` sign test each frame, and extrusion of the boundary
edges. Because the skeleton moves, it has to be redone every frame for every
animated caster. Only after Tier 1 ships.

## What Tier 1 costs

One caster with a 40-triangle proxy has roughly a 20-edge silhouette: about
40 extruded quad triangles plus caps, call it 80 triangles, 80 x 32 bytes =
2.5KB per caster per frame into `OP_MOD`. Four casters is 10KB against the
1MB vertex buffer PR #2 allocates. Geometry is not the problem.

The per-pixel cost is. The TA evaluates volume coverage for every tile the
volume touches, and a shadow volume is tall — it runs from the model to the
floor and past it. Extruding a **finite** distance rather than to infinity is
what keeps the touched-tile count down, and it is the single most important
tuning knob here.

## The hard parts, in the order they will bite

**1. The volume must be closed and consistently wound.** An open volume
gives garbage inside/outside results. This is why a hand-authored watertight
proxy beats deriving one from the render mesh — it makes closure a content
requirement rather than a runtime one.

**2. Near-plane clipping.** This is where the time will go.
`ClipAndSubmitTriangle` in `renderer_pvr.c` does Sutherland-Hodgman against
`w >= nearW` and drops triangles wholly behind it. For ordinary geometry
that is right; for a shadow volume it is not. Clipping a closed volume
against the near plane *opens* it, and pixels between the camera and the
volume's front cap stop being counted — so when the camera walks into a
shadow, the shadow inverts or disappears. The stencil-world answer is z-fail
(Carmack's reverse), which cannot be expressed here because the per-pixel
counting rule is not under software control. The practical options:

  - *(a)* Cap the volume at the near plane by adding the clipped
    cross-section as new geometry. Correct, and fiddly.
  - *(b)* Keep volumes small enough that the camera never enters one. Fine
    for a third-person game with a shadow under the player; useless in
    first person.
  - *(c)* Test the volume's bounding sphere against the camera position and
    fall back to the Tier 0 blob for that caster when it contains the
    camera.

Ship *(c)* first — it is a few lines and it is never wrong, only cheaper
looking. Do *(a)* later.

**3. Choosing receivers.** Only polygons submitted with a modifier header
are affected. Compiling every level entity that way costs the header
everywhere and produces self-shadowing artifacts on the caster. What is
wanted is a per-entity receiver flag — and `material.h` already defines an
unused `PROP_SHADOW` (0x80) which `material.c:124` already sets from the MTL
parser. The flag is parsed, present, and unused on the Dreamcast path.

**4. One global darkness.** `pvr_set_shadow_scale` sets one scale for the
whole frame. Every shadow in the scene is equally dark; a shadow cannot fade
with distance or light intensity without moving to the two-parameter mode.

**5. Opaque receivers only, to start.** `OP_MOD` modifies `OP_POLY`.
Alpha-blended level geometry — the grates and other `hasAlpha` materials
that PR #2 routes to `TR_POLY` — will not receive shadows unless `TR_MOD` is
also enabled, which is a second bin allocation and a second volume
submission.

**6. Lists are one-shot per frame.** `pvr_list_begin` cannot reopen a closed
list, and all of a list's contents must be submitted together. So the frame
becomes:

    OP_POLY   all opaque geometry, receivers carrying modifier headers
    OP_MOD    every shadow volume for every caster, one contiguous block
    TR_POLY   alpha materials, text, sprites, blob-shadow fallbacks

PR #2's `BeginPVRList` already moves strictly forward through the lists and
refuses to reopen one, so inserting `OP_MOD` between the two existing calls
fits its design with no restructuring. This is the part that is easy.

**7. Light direction.** The engine has exactly one `light_t`, and
`World_Update` can attach it to the camera. Extrusion needs a stable
direction — a camera-attached light makes the shadow swing with the view.
The scene format wants a separate fixed shadow-light direction.

## Where the work lands

| File | Change |
| --- | --- |
| `src/shadows.c/h` | Proxy loading, silhouette and extrusion, volume submission. Already stubbed by PR #2. |
| `src/renderer.h` | A `RenderShadowVolumes` entry point on `renderer_t`; a no-op in the GL path. |
| `src/renderer_pvr.c` | `BeginPVRList(PVR_LIST_OP_MOD)` between opaque and translucent; a `SubmitModifierTriangle`; receiver headers via `pvr_poly_cxt_txr_mod` when `material->prop & PROP_SHADOW`. |
| `initPVRRenderer` | Give `OP_MOD` a bin size; call `pvr_set_shadow_scale`. |
| Scene / map format | A `shadowProxy` path per caster, and a shadow-light direction per scene. |

Depends on PR #2, and on [PR #3](https://github.com/spencerelliott/dEngine/pull/3)
to know which entities are dynamic — a static entity's shadow belongs baked
into its texture, not extruded every frame.

Rough effort: Tier 0 is a day. Tier 1 is one to two weeks including the
near-plane fallback. Tier 2 should not be attempted until Tier 1 is running
on hardware.

## Sources

- [KallistiOS `dc/pvr.h`](https://raw.githubusercontent.com/KallistiOS/KallistiOS/master/kernel/arch/dreamcast/include/dc/pvr.h)
  and `dc/pvr/pvr_header.h`, `dc/pvr/pvr_misc.h`
- [KallistiOS: Headers](https://kos-docs.dreamcast.wiki/group__pvr__primitives__headers.html)
- [KallistiOS: Compilation](https://kos-docs.dreamcast.wiki/group__pvr__primitives__compilation.html)
- [PowerVR Introduction, DCEmulation](https://dcemulation.org/index.php?title=PowerVR_Introduction)
- [PowerVR Registers, Hitmen](https://hitmen.c02.at/files/docs/dc/powervr-reg.txt)
