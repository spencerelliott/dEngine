/*
 *  renderer.h
 *  dEngine
 *
 *  Created by fabien sanglard on 09/08/09.

 *
 */



#ifndef DE_RENDERER
#define DE_RENDERER

#define GL_11_RENDERER 0
#define GL_20_RENDERER 1



#define SHADOW_TYPE_NORMAL 0
#define SHADOW_TYPE_DISABLED 1
#define SHADOW_TYPE_VSM 2

#include "md5model.h"
#include "texture.h"
#include "font.h"

extern int renderWidth;
extern int renderHeight;

typedef struct renderer_t 
{
	uint type;
	
	uchar props;
	//uint supportBumpMapping;
	//uint shadowType;
	uchar isRenderingShadow;
	uchar isBlending;
	
	uint mainFramebufferId;
	
	uint statsEnabled;
	uint materialQuality;
	
	
	
	void (*Set3D)(void); 
	void (*StopRendition  )(void); 
//	void (*PushMatrix)(void);
//	void (*PopMatrix)(void);
	void (*SetTexture)(unsigned int);
	void (*RenderEntities)(void);
	void (*UpLoadTextureToGpu)(texture_t* texture);
	void (*Set2D)(void);
	void (*RenderString)(svertex_t* vertices,ushort* indices, uint numIndices);
	void (*GetColorBuffer)(uchar* data);
	void (*RenderSprite)(uint textureId, svertex_t* vertices, uint* indices, uint numIndices);
} renderer_t;

extern renderer_t renderer;

void SCR_Init(int rendererType,int viewPortWidth,int viewPortHeight);
void SCR_RenderFrame(void);
void SCR_GetColorBuffer(uchar* data);

/* TextureMagFilter */
#define DE_GL_NEAREST                        0x2600
#define DE_GL_LINEAR                         0x2601
#define DE_DEFAULT_FILTERING				DE_GL_LINEAR

#endif
