//
// Created by Spencer Elliott on 2023-10-23.
//
// Native PowerVR2 backend built straight on the KallistiOS PVR API. It replaces
// the GLdc/OpenGL path on Dreamcast: vertices are transformed with the SH4
// matrix unit, clipped against the near plane and fed to the tile accelerator
// as short triangle strips.
//

#include "renderer_pvr.h"
#include "camera.h"
#include "stats.h"
#include "world.h"
#include "obj.h"
#include "font.h"
#include "filesystem.h"

#include <kos.h>
#include <dc/pvr.h>
#include <dc/matrix.h>
#include <dc/video.h>
#include <math.h>
#include <stdint.h>

// Older KallistiOS releases predate the no-divide transform macro. It is only
// an ftrv, so provide it when the toolchain's dc/matrix.h does not.
#ifndef mat_trans_nodiv
#define mat_trans_nodiv(x, y, z, w) { \
        register float __x __asm__("fr0") = (x); \
        register float __y __asm__("fr1") = (y); \
        register float __z __asm__("fr2") = (z); \
        register float __w __asm__("fr3") = (w); \
        __asm__ __volatile__( \
                              "ftrv   xmtrx,fv0\n" \
                              : "=f" (__x), "=f" (__y), "=f" (__z), "=f" (__w) \
                              : "0" (__x), "1" (__y), "2" (__z), "3" (__w) ); \
        x = __x; y = __y; z = __z; w = __w; \
    }
#endif

// The PVR is a deferred tile renderer, so back faces cost binning but almost no
// fill. Culling stays off by default because a mis-guessed winding hides all of
// the single sided level geometry; flip this to PVR_CULLING_CCW once the
// orientation has been confirmed on hardware.
#define PVR_RENDERER_CULL_MODE      PVR_CULLING_NONE

#define PVR_RENDERER_FILTER         PVR_FILTER_BILINEAR

#define PVR_RENDERER_MAX_TEXTURES   256

// Depth (1/w) handed to 2D geometry. The camera near plane is far enough out
// that nothing in the scene can reach this value.
#define PVR_RENDERER_2D_DEPTH       1.0f

// MD5 texture coordinates are packed as shorts scaled by DE_SHRT_MAX.
#define PVR_RENDERER_ST_SCALE       (1.0f / 32767.0f)

// DTEX stores the PVR texture control word in its type field. Bits 25..30 hold
// the stride, twiddle, pixel format and VQ flags the TA wants; bit 31 is the
// mipmap flag, which the PVR API takes separately.
#define DTEX_TYPE_FMT_MASK          0x7e000000u
#define DTEX_TYPE_MIPMAPPED         0x80000000u

#define PVR_LIST_CLOSED             (-1)

typedef struct pvr_texture_s {
    pvr_ptr_t ptr;
    uint32_t  fmt;
    uint16_t  width;
    uint16_t  height;
    uint8_t   mipmapped;
} pvr_texture_t;

// A vertex after the model-view-projection transform but before the perspective
// divide. x/y already carry the viewport scale, so dividing by w lands straight
// in screen space.
typedef struct pvr_xvertex_s {
    float    x;
    float    y;
    float    w;
    float    u;
    float    v;
    uint32_t argb;
} pvr_xvertex_t;

static pvr_texture_t pvrTextures[PVR_RENDERER_MAX_TEXTURES];
static uint          numPVRTextures = 0;

static pvr_xvertex_t *transformCache = NULL;
static uint           transformCacheSize = 0;

// eye space -> screen space, viewport transform folded in
static matrix_t screenProjMatrix __attribute__((aligned(32)));
// world space -> screen space
static matrix_t viewProjMatrix __attribute__((aligned(32)));
static matrix_t modelMatrix __attribute__((aligned(32)));

static pvr_vertex_t scratchVertex __attribute__((aligned(32)));

static int  currentList = PVR_LIST_CLOSED;
static int  sceneActive = 0;
static uint currentTextureId = 0;
static int  headerDirty = 1;

// w below this is behind the near plane. Kept in sync with the camera so the
// PVR path clips exactly where the OpenGL path used to.
static float nearW = 1.0f;

static pvr_texture_t *GetPVRTexture(uint textureId) {
    if (textureId == 0 || textureId > numPVRTextures)
        return NULL;

    return &pvrTextures[textureId - 1];
}

static uint32_t PackColor(const vec4uchar_t col) {
    return ((uint32_t)col[3] << 24) | ((uint32_t)col[0] << 16) |
           ((uint32_t)col[1] << 8) | (uint32_t)col[2];
}

static int EnsureTransformCache(uint numVertices) {
    pvr_xvertex_t *grown;

    if (numVertices <= transformCacheSize)
        return 1;

    grown = (pvr_xvertex_t *)realloc(transformCache, numVertices * sizeof(pvr_xvertex_t));

    if (grown == NULL) {
        printf("[renderer_pvr] Could not grow the transform cache to %u vertices\n", numVertices);
        return 0;
    }

    transformCache = grown;
    transformCacheSize = numVertices;

    return 1;
}

// Emits the polygon header describing the current texture/list state. The PVR
// keeps this state per list, so it has to be re-sent whenever either changes.
static void FlushPVRState(void) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr __attribute__((aligned(32)));
    pvr_texture_t *texture;

    if (!headerDirty || currentList == PVR_LIST_CLOSED)
        return;

    texture = GetPVRTexture(currentTextureId);

    if (texture != NULL) {
        pvr_poly_cxt_txr(&cxt, (pvr_list_t)currentList, texture->fmt, texture->width,
                         texture->height, texture->ptr, PVR_RENDERER_FILTER);
        cxt.txr.mipmap = texture->mipmapped ? 1 : 0;
    } else {
        pvr_poly_cxt_col(&cxt, (pvr_list_t)currentList);
    }

    // Everything else is left at the KOS defaults: ARGB packed vertex colours,
    // gouraud shading, PVR_DEPTHCMP_GREATER (correct for 1/w depth) and a
    // texture environment that modulates by the vertex colour.
    cxt.gen.culling = PVR_RENDERER_CULL_MODE;

    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    headerDirty = 0;
}

// The PVR wants its lists submitted in order and each opened at most once per
// scene. Every caller here moves forward through OP -> TR, never back.
static void BeginPVRList(pvr_list_t list) {
    if (currentList == (int)list)
        return;

    if (currentList != PVR_LIST_CLOSED)
        pvr_list_finish();

    pvr_list_begin(list);

    currentList = (int)list;
    headerDirty = 1;
}

static void EndPVRList(void) {
    if (currentList == PVR_LIST_CLOSED)
        return;

    pvr_list_finish();
    currentList = PVR_LIST_CLOSED;
}

static void BuildScreenProjMatrix(float fovDegrees, float aspect, int width, int height) {
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    float cotFovY2 = 1.0f / tanf((float)(fovDegrees * DEG_TO_RAD) * 0.5f);

    memset(screenProjMatrix, 0, sizeof(screenProjMatrix));

    // KOS matrices are row-vector: v' = v * M. Building the viewport transform
    // into the projection lets a single ftrv produce coordinates that only need
    // a divide by w to be screen space, with y already flipped for the PVR's
    // top-left origin.
    screenProjMatrix[0][0] = halfWidth * cotFovY2 / aspect;
    screenProjMatrix[1][1] = -halfHeight * cotFovY2;
    screenProjMatrix[2][0] = -halfWidth;
    screenProjMatrix[2][1] = -halfHeight;
    screenProjMatrix[2][2] = 1.0f;
    screenProjMatrix[2][3] = -1.0f;
}

static void BuildViewProjMatrix(void) {
    point_t eye, center;
    vector_t up;

    BuildScreenProjMatrix(camera.fov, camera.aspect, renderWidth, renderHeight);

    nearW = camera.zNear;

    eye.x = camera.position[0];
    eye.y = camera.position[1];
    eye.z = camera.position[2];
    eye.w = 1.0f;

    center.x = camera.position[0] + camera.forward[0];
    center.y = camera.position[1] + camera.forward[1];
    center.z = camera.position[2] + camera.forward[2];
    center.w = 1.0f;

    up.x = camera.up[0];
    up.y = camera.up[1];
    up.z = camera.up[2];
    up.w = 1.0f;

    mat_identity();
    mat_apply(&screenProjMatrix);
    mat_lookat(&eye, &center, &up);
    mat_store(&viewProjMatrix);
}

// dEngine keeps entity matrices in OpenGL's column-major float[16], which has
// the same memory layout as a row-vector matrix_t, so this is a straight copy.
static void SetEntityMatrix(const de_matrix_t entityMatrix) {
    memcpy(modelMatrix, entityMatrix, sizeof(matrix_t));
}

// Loads model * view * projection into the SH4's internal matrix. Called again
// for every batch of vertices so no submission in between can be relied on to
// have left the matrix registers alone.
static void ApplyEntityTransform(void) {
    mat_load(&viewProjMatrix);
    mat_apply(&modelMatrix);
}

static inline void SubmitVertex(uint32_t flags, const pvr_xvertex_t *vertex) {
    float invW = 1.0f / vertex->w;

    scratchVertex.flags = flags;
    scratchVertex.x = vertex->x * invW;
    scratchVertex.y = vertex->y * invW;
    scratchVertex.z = invW;
    scratchVertex.u = vertex->u;
    scratchVertex.v = vertex->v;
    scratchVertex.argb = vertex->argb;
    scratchVertex.oargb = 0;

    pvr_prim(&scratchVertex, sizeof(scratchVertex));
}

static inline void SubmitTriangle(const pvr_xvertex_t *a, const pvr_xvertex_t *b,
                                  const pvr_xvertex_t *c) {
    SubmitVertex(PVR_CMD_VERTEX, a);
    SubmitVertex(PVR_CMD_VERTEX, b);
    SubmitVertex(PVR_CMD_VERTEX_EOL, c);
}

static void LerpVertex(pvr_xvertex_t *dst, const pvr_xvertex_t *from,
                       const pvr_xvertex_t *to, float t) {
    uint32_t fromColor = from->argb;
    uint32_t toColor = to->argb;
    uint32_t result = 0;
    int shift;

    dst->x = from->x + (to->x - from->x) * t;
    dst->y = from->y + (to->y - from->y) * t;
    dst->w = from->w + (to->w - from->w) * t;
    dst->u = from->u + (to->u - from->u) * t;
    dst->v = from->v + (to->v - from->v) * t;

    for (shift = 0; shift < 32; shift += 8) {
        float a = (float)((fromColor >> shift) & 0xff);
        float b = (float)((toColor >> shift) & 0xff);
        uint32_t channel = (uint32_t)(a + (b - a) * t);

        result |= (channel & 0xff) << shift;
    }

    dst->argb = result;
}

// Sutherland-Hodgman against the single near plane (w >= nearW). A triangle
// crossing it produces at most a quad, which is emitted as two triangles.
// Returns how many triangles reached the tile accelerator.
static int ClipAndSubmitTriangle(const pvr_xvertex_t *a, const pvr_xvertex_t *b,
                                 const pvr_xvertex_t *c) {
    const pvr_xvertex_t *in[3];
    pvr_xvertex_t out[4];
    int numOut = 0;
    int i;

    in[0] = a;
    in[1] = b;
    in[2] = c;

    for (i = 0; i < 3; i++) {
        const pvr_xvertex_t *current = in[i];
        const pvr_xvertex_t *next = in[(i + 1) % 3];
        int currentInside = current->w >= nearW;
        int nextInside = next->w >= nearW;

        if (currentInside)
            out[numOut++] = *current;

        if (currentInside != nextInside) {
            float t = (nearW - current->w) / (next->w - current->w);
            LerpVertex(&out[numOut++], current, next, t);
        }
    }

    if (numOut < 3)
        return 0;

    SubmitTriangle(&out[0], &out[1], &out[2]);

    if (numOut == 4) {
        SubmitTriangle(&out[0], &out[2], &out[3]);
        return 2;
    }

    return 1;
}

static void SubmitIndexedTriangles(const ushort *indices, uint numIndices) {
    uint submitted = 0;
    uint i;

    FlushPVRState();

    for (i = 0; i + 2 < numIndices; i += 3) {
        const pvr_xvertex_t *a = &transformCache[indices[i]];
        const pvr_xvertex_t *b = &transformCache[indices[i + 1]];
        const pvr_xvertex_t *c = &transformCache[indices[i + 2]];

        if (a->w >= nearW && b->w >= nearW && c->w >= nearW) {
            SubmitTriangle(a, b, c);
            submitted++;
        } else if (a->w >= nearW || b->w >= nearW || c->w >= nearW) {
            submitted += ClipAndSubmitTriangle(a, b, c);
        }
    }

    STATS_AddTriangles((int)submitted);
}

void UploadMatrix(matrix_t *mat) {
    mat_load(mat);
}

void OrthoMatrix(matrix_t *mat, float left, float right, float bottom, float top,
                 float znear, float zfar) {
    memset(mat, 0, sizeof(matrix_t));

    (*mat)[0][0] = 2.0f / (right - left);
    (*mat)[1][1] = 2.0f / (top - bottom);
    (*mat)[2][2] = -2.0f / (zfar - znear);
    (*mat)[3][0] = -(right + left) / (right - left);
    (*mat)[3][1] = -(top + bottom) / (top - bottom);
    (*mat)[3][2] = -(zfar + znear) / (zfar - znear);
    (*mat)[3][3] = 1.0f;
}

void PerspectiveMatrix(matrix_t *mat, float angle, float aspect, float znear, float zfar) {
    float cotFovY2 = 1.0f / tanf((float)(angle * DEG_TO_RAD) * 0.5f);

    memset(mat, 0, sizeof(matrix_t));

    (*mat)[0][0] = cotFovY2 / aspect;
    (*mat)[1][1] = cotFovY2;
    (*mat)[2][2] = (zfar + znear) / (znear - zfar);
    (*mat)[2][3] = -1.0f;
    (*mat)[3][2] = 2.0f * zfar * znear / (znear - zfar);
}

void Set3DPVR(void) {
    pvr_wait_ready();
    pvr_scene_begin();
    sceneActive = 1;

    currentTextureId = 0;
    headerDirty = 1;
    renderer.isBlending = 0;

    BeginPVRList(PVR_LIST_OP_POLY);
    BuildViewProjMatrix();
}

void Set2DPVR(void) {
    // Text and sprites are alpha blended, so they belong in the translucent
    // list. RenderEntitiesPVR has usually opened it already.
    BeginPVRList(PVR_LIST_TR_POLY);

    if (!renderer.isBlending) {
        renderer.isBlending = 1;
        STATS_AddBlendingSwitch();
    }
}

void StopRenditionPVR(void) {
    EndPVRList();

    if (sceneActive) {
        pvr_scene_finish();
        sceneActive = 0;
    }
}

void SetTexturePVR(unsigned int textureId) {
    if (textureId == currentTextureId)
        return;

    currentTextureId = textureId;
    headerDirty = 1;

    STATS_AddTexSwitch();
}

void RenderEntitiesMD5PVR(void *md5Void) {
    md5_object_t *md5Object;
    md5_mesh_t *currentMesh;
    int i, j;

    md5Object = (md5_object_t *)md5Void;

    for (i = 0; i < 1/*md5Object->md5Model.num_meshes*/; i++) {
        currentMesh = &md5Object->md5Model.meshes[i];

        if (!EnsureTransformCache((uint)currentMesh->num_verts))
            return;

        ApplyEntityTransform();

        for (j = 0; j < currentMesh->num_verts; j++) {
            const vertex_t *source = &currentMesh->vertexArray[j];
            pvr_xvertex_t *dest = &transformCache[j];
            float x = source->pos[0];
            float y = source->pos[1];
            float z = source->pos[2];
            float w = 1.0f;

            mat_trans_nodiv(x, y, z, w);

            dest->x = x;
            dest->y = y;
            dest->w = w;
            dest->u = source->text[0] * PVR_RENDERER_ST_SCALE;
            dest->v = source->text[1] * PVR_RENDERER_ST_SCALE;
            dest->argb = PackColor(source->col);
        }

        SubmitIndexedTriangles(currentMesh->vertexIndices, (uint)currentMesh->num_tris * 3);
    }
}

void RenderEntitiesOBJPVR(void *objVoid) {
    obj_t *obj;
    uint i;

    obj = (obj_t *)objVoid;

    if (!EnsureTransformCache(obj->num_vertices))
        return;

    ApplyEntityTransform();

    for (i = 0; i < obj->num_vertices; i++) {
        const obj_vertex_t *source = &obj->vertices[i];
        pvr_xvertex_t *dest = &transformCache[i];
        float x = source->position[0];
        float y = source->position[1];
        float z = source->position[2];
        float w = 1.0f;

        mat_trans_nodiv(x, y, z, w);

        dest->x = x;
        dest->y = y;
        dest->w = w;
        dest->u = source->textCoo[0];
        dest->v = source->textCoo[1];
        dest->argb = PackColor(source->col);
    }

    SubmitIndexedTriangles(obj->indices, obj->num_indices);
}

static void RenderEntityPVR(entity_t *entity) {
    SetEntityMatrix(entity->matrix);

    SetTexturePVR(entity->material->textures[TEXTURE_DIFFUSE]->textureId);

    if (entity->type == ENTITY_OBJ)
        RenderEntitiesOBJPVR(entity->model);
    else
        RenderEntitiesMD5PVR(entity->model);
}

void RenderEntitiesPVR(void) {
    int i;
    entity_t *entity;

    // Opaque geometry first, into the list Set3DPVR opened. The PVR resolves
    // these per tile, so submission order does not matter here.
    for (i = 0; i < num_map_entities; i++) {
        entity = &map[i];

        if (entity->material->hasAlpha)
            continue;

        RenderEntityPVR(entity);
    }

    // Then everything blended, in the translucent list where the hardware
    // autosorts it against the opaque depth.
    BeginPVRList(PVR_LIST_TR_POLY);

    if (!renderer.isBlending) {
        renderer.isBlending = 1;
        STATS_AddBlendingSwitch();
    }

    for (i = 0; i < num_map_entities; i++) {
        entity = &map[i];

        if (!entity->material->hasAlpha)
            continue;

        RenderEntityPVR(entity);
    }
}

// 2D geometry arrives in the orthographic space the OpenGL path set up with
// glOrtho(-renderWidth, renderWidth, -renderHeight, renderHeight, ...), so it
// needs halving and a y flip to reach the PVR's top-left origin screen space.
static inline void Submit2DVertex(uint32_t flags, const svertex_t *source) {
    scratchVertex.flags = flags;
    scratchVertex.x = (source->pos[0] + renderWidth) * 0.5f;
    scratchVertex.y = (renderHeight - source->pos[1]) * 0.5f;
    scratchVertex.z = PVR_RENDERER_2D_DEPTH;
    scratchVertex.u = source->text[0];
    scratchVertex.v = source->text[1];
    scratchVertex.argb = PackColor(source->col);
    scratchVertex.oargb = 0;

    pvr_prim(&scratchVertex, sizeof(scratchVertex));
}

void RenderStringPVR(svertex_t *vertices, ushort *indices, uint numIndices) {
    uint i;

    FlushPVRState();

    for (i = 0; i + 2 < numIndices; i += 3) {
        Submit2DVertex(PVR_CMD_VERTEX, &vertices[indices[i]]);
        Submit2DVertex(PVR_CMD_VERTEX, &vertices[indices[i + 1]]);
        Submit2DVertex(PVR_CMD_VERTEX_EOL, &vertices[indices[i + 2]]);
    }

    STATS_AddTriangles(numIndices / 3);
}

void RenderSpritePVR(uint textureId, svertex_t *vertices, uint *indices, uint numIndices) {
    uint i;

    SetTexturePVR(textureId);
    FlushPVRState();

    // Sprite vertices are authored in raw screen pixels rather than the
    // orthographic space RenderString uses.
    for (i = 0; i + 2 < numIndices; i += 3) {
        uint j;

        for (j = 0; j < 3; j++) {
            const svertex_t *source = &vertices[indices[i + j]];

            scratchVertex.flags = (j == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
            scratchVertex.x = source->pos[0];
            scratchVertex.y = source->pos[1];
            scratchVertex.z = PVR_RENDERER_2D_DEPTH;
            scratchVertex.u = source->text[0];
            scratchVertex.v = source->text[1];
            scratchVertex.argb = PackColor(source->col);
            scratchVertex.oargb = 0;

            pvr_prim(&scratchVertex, sizeof(scratchVertex));
        }
    }

    STATS_AddTriangles(numIndices / 3);
}

void UploadTextureToGPUPVR(texture_t *texture) {
    pvr_texture_t *entry;
    pvr_ptr_t vram;

    if (!texture || !texture->data || texture->textureId != 0)
        return;

    if (numPVRTextures >= PVR_RENDERER_MAX_TEXTURES) {
        printf("[UploadTextureToGPUPVR] Out of texture slots, dropping '%s'\n", texture->path);
        return;
    }

    vram = pvr_mem_malloc(texture->dataLength);

    if (vram == NULL) {
        printf("[UploadTextureToGPUPVR] Out of VRAM for '%s' (%d bytes, %u free)\n",
               texture->path, texture->dataLength, (uint)pvr_mem_available());
        return;
    }

    // The asset pipeline already emits twiddled/VQ data in the PVR's native
    // layout, so this is a straight copy into VRAM.
    pvr_txr_load(texture->data, vram, texture->dataLength);

    entry = &pvrTextures[numPVRTextures];
    entry->ptr = vram;
    entry->fmt = texture->type & DTEX_TYPE_FMT_MASK;
    entry->width = (uint16_t)texture->width;
    entry->height = (uint16_t)texture->height;
    entry->mipmapped = (texture->type & DTEX_TYPE_MIPMAPPED) ? 1 : 0;

    texture->textureId = ++numPVRTextures;

    if (texture->file != NULL) {
        FS_CloseFile(texture->file);
        texture->file = NULL;
    }

    free(texture->data);
    texture->data = NULL;
}

void GetColorBufferPVR(uchar *buffer) {
    // The displayed framebuffer sits in the 32-bit VRAM area; PVR_FB_ADDR holds
    // its offset from the base of VRAM. Rows are read bottom up so the result
    // matches what glReadPixels used to hand back.
    const volatile uint16_t *framebuffer =
        (const volatile uint16_t *)(PVR_RAM_BASE + (PVR_GET(PVR_FB_ADDR) & 0x00fffffc));
    int x, y;

    for (y = 0; y < renderHeight; y++) {
        const volatile uint16_t *row = framebuffer + (renderHeight - 1 - y) * renderWidth;

        for (x = 0; x < renderWidth; x++) {
            uint16_t pixel = row[x];
            uint32_t r = (pixel >> 11) & 0x1f;
            uint32_t g = (pixel >> 5) & 0x3f;
            uint32_t b = pixel & 0x1f;

            *buffer++ = (uchar)((r << 3) | (r >> 2));
            *buffer++ = (uchar)((g << 2) | (g >> 4));
            *buffer++ = (uchar)((b << 3) | (b >> 2));
            *buffer++ = 255;
        }
    }
}

void initPVRRenderer(renderer_t *r) {
    // opaque polys, opaque modifiers, translucent polys, translucent modifiers,
    // punch-thrus. Only the two lists this backend submits to are given bins.
    pvr_init_params_t pvr_params = {
            {PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0},
            1024 * 1024,
            0, /* dma_enabled */
            0, /* fsaa_enabled */
            0, /* autosort_disabled */
    };

    r->type = PVR_RENDERER;
    r->props = 0;

    r->Set3D = Set3DPVR;
    r->StopRendition = StopRenditionPVR;
    r->SetTexture = SetTexturePVR;
    r->RenderEntities = RenderEntitiesPVR;
    r->UpLoadTextureToGpu = UploadTextureToGPUPVR;
    r->Set2D = Set2DPVR;
    r->RenderString = RenderStringPVR;
    r->GetColorBuffer = GetColorBufferPVR;
    r->RenderSprite = RenderSpritePVR;

    vid_set_mode(DM_640x480, PM_RGB565);

    if (pvr_init(&pvr_params) < 0) {
        printf("[initPVRRenderer] pvr_init failed\n");
        return;
    }

    pvr_set_bg_color(0.2f, 0.2f, 0.2f);

    printf("[initPVRRenderer] PVR ready, %u bytes of VRAM available for textures\n",
           (uint)pvr_mem_available());
}
