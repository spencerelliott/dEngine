//
// Created by Spencer Elliott on 2023-10-23.
//

#include "renderer_pvr.h"
#include "camera.h"
#include "stats.h"
#include "world.h"
#include "obj.h"
#include <kos.h>
#include <math.h>

pvr_poly_hdr_t currPVRPolyHdr;
pvr_mod_hdr_t currPVRModHdr;

matrix_t currMatrix;

void UploadMatrix(matrix_t* mat) {

}

void OrthoMatrix(matrix_t *mat, float left, float right, float bottom, float top, float znear, float zfar) {

}

void PerspectiveMatrix(matrix_t* mat, float angle, float aspect, float znear, float zfar) {

}

void Set3DPVR() {

}

void Set2DPVR() {

}

void StopRenditionPVR() {

}

void SetTexturePVR(unsigned int textureId) {

}

void RenderEntitiesMD5PVR(void* md5Void) {
    md5_object_t* md5Object;
    md5_mesh_t* currentMesh;
    int i;

    for (i = 0; i < 1/*md5Object.md5Model.num_meshes*/; i++) {
        currentMesh = &md5Object->md5Model.meshes[i];
    }
}

void RenderEntitiesOBJPVR(void* objVoid) {
    obj_t* obj;
    obj = (obj_t*)objVoid;
}

void RenderEntitiesPVR() {
    int i;
    entity_t* entity;

    for(i=0; i < num_map_entities; i++) {
        entity = &map[i];
        SetTexturePVR(entity->material->textures[TEXTURE_DIFFUSE]->textureId);

        if (entity->material->hasAlpha ) {
            if (!renderer.isBlending) {
                renderer.isBlending = 1;
                STATS_AddBlendingSwitch();
            }
        }
        else {
            if (renderer.isBlending) {
                renderer.isBlending = 0;
                STATS_AddBlendingSwitch();
            }
        }

        if (entity->type == ENTITY_OBJ)
            RenderEntitiesOBJPVR(entity->model);
        else
            RenderEntitiesMD5PVR(entity->model);
    }
}

void RenderSpritePVR(uint textureId, svertex_t* vertices, uint* indices, uint numIndices) {

}

void RenderStringPVR(svertex_t* vertices,ushort* indices, uint numIndices) {

}

void UploadTextureToGPUPVR(texture_t *texture) {

}

void GetColorBufferPVR(uchar *buffer) {

}

void initPVRRenderer(renderer_t *renderer) {
    renderer->type = PVR_RENDERER;
    renderer->props = 0;

    renderer->Set3D = Set3DPVR;
    renderer->StopRendition = StopRenditionPVR;
    renderer->SetTexture = SetTexturePVR;
    renderer->RenderEntities = RenderEntitiesPVR;
    renderer->UpLoadTextureToGpu = UploadTextureToGPUPVR;
    renderer->Set2D = Set2DPVR;
    renderer->RenderString = RenderStringPVR;
    renderer->GetColorBuffer = GetColorBufferPVR;
    renderer->RenderSprite = RenderSpritePVR;

    pvr_init_params_t pvr_params = {
            {PVR_BINSIZE_16, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16 },
            1024 * 1024,
    };
    pvr_init(&pvr_params);
}