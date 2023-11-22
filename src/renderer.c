/*
 *  renderer.c
 *  dEngine
 *
 *  Created by fabien sanglard on 10/08/09.
 *
 */

#include "renderer.h"
#include <internal_math.h>
#include <malloc.h>
#include "renderer_fixed.h"
#include "renderer_progr.h"
#include "stats.h"
#include "timer.h"

int renderWidth;
int renderHeight;
float renderAspect;

renderer_t renderer;



//2D text and stats
char* textSwitchStr = "Texture Switches: %d";
char* shadSwitchStr = "Shader Switches: %d";
char* blendSwitchStr = "Blending Switches: %d";
char* polyCount = "Poly Count: %d";
char* msCount = "ms: %d";
char* allocStr = "allocated: %.2fMB";
char* freeStr = "free: %.2fKB";

char fpsText[80]; 
char teSwText[80]; 
char shSwText[80]; 
char bldwText[80]; 
char polCnText[80]; 
char msText[80];
char allocText[80];
char freeText[80];


ushort indicesString[256]  ;


svertex_t verticesSprite[4];
ushort indicesSprite[6] =  {1,2,3,1,3,4};

void SCR_Init(int rendererType,int viewPortWidth,int viewPortHeight)
{
	int i;
	svertex_t* v;
	
	//renderWidth =  768;
	//renderHeight = 1024;
	
	//renderWidth =  320;
	//renderHeight = 480;
	
    renderWidth =  viewPortWidth;
    renderHeight = viewPortHeight;
    
	if (rendererType == GL_11_RENDERER)
	{
		printf("Running as openGL ES 1.1\n");
		initFixedRenderer(&renderer);
	}
	
	//Generate indices for text rendition
	for(i=0; i < 42 ; i++)
	{
		indicesString[i*6] = i*4;
		indicesString[i*6+1] = i*4+1;
		indicesString[i*6+2] = i*4+2;
		indicesString[i*6+3] = i*4;
		indicesString[i*6+4] = i*4+2;
		indicesString[i*6+5] = i*4+3;
		
	}
	
	
	//Sprite stuff
	v = verticesSprite;
	//	4 3
	//	1 2
	/*1*/v->text[0] = 0 ; 	v->text[1] = 0; 	v->pos[0] = 0;				v->pos[1] = 0;				v++;
	/*2*/v->text[0] = 1;	v->text[1] = 0;		v->pos[0] = renderWidth ;	v->pos[1] = 0;				v++;
	/*3*/v->text[0] = 1;	v->text[1] = 1;		v->pos[0] = renderWidth ;	v->pos[1] = renderHeight;	v++;
	/*4*/v->text[0] = 0;	v->text[1] = 1;		v->pos[0] =  0;				v->pos[1] = renderHeight;	v++;
	
}



void DrawStats()
{
	static svertex_t vertexString[256];
	uint numVertices;
    const int statsX = -315;
    const int statsYStart = 400;

    struct mallinfo malloc_info = mallinfo();
	
	if (fps > 99)
		sprintf( fpsText, "fps:%d",fps );
	else if (fps > 9) 
		sprintf( fpsText, "fps: %d",fps );
	else
		sprintf( fpsText, "fps:  %d",fps );
	
	sprintf( teSwText, textSwitchStr,STATS_GetTexSwitchCount() );
	sprintf( polCnText, polyCount,STATS_GetTrianglesCount() );
	sprintf(msText, msCount,simulationTime);
	sprintf(shSwText, shadSwitchStr, STATS_GetShaderSwitchCount());
	sprintf(bldwText, blendSwitchStr, STATS_GetBlendingSwitchCount());
    sprintf(allocText, allocStr, (float)malloc_info.uordblks/1024.0f/1024.0f);
    sprintf(freeText, freeStr, (float)malloc_info.fordblks/1024.0f);
	
	renderer.SetTexture(font.texfont->textureId);
	
	numVertices = Font_GenerateStringVertexArray(vertexString,fpsText,statsX,statsYStart);
	renderer.RenderString(vertexString,indicesString,numVertices/4*6);
	
	numVertices = Font_GenerateStringVertexArray(vertexString,teSwText,statsX, statsYStart - 40);
	renderer.RenderString(vertexString,indicesString,numVertices/4*6);
	
	numVertices = Font_GenerateStringVertexArray(vertexString,polCnText,statsX, statsYStart - 80);
	renderer.RenderString(vertexString,indicesString,numVertices/4*6);
	
	numVertices = Font_GenerateStringVertexArray(vertexString,msText,statsX, statsYStart - 120);
	renderer.RenderString(vertexString,indicesString,numVertices/4*6);

    numVertices = Font_GenerateStringVertexArray(vertexString,allocText,statsX, statsYStart - 160);
    renderer.RenderString(vertexString,indicesString,numVertices/4*6);

    numVertices = Font_GenerateStringVertexArray(vertexString,freeText,statsX, statsYStart - 200);
    renderer.RenderString(vertexString,indicesString,numVertices/4*6);
	
}

void SCR_RenderFrame(void)
{
	STATS_Begin();
		
	renderer.Set3D();
	
	renderer.RenderEntities();
	
	
	
	if (renderer.statsEnabled)
	{
		renderer.Set2D();
		DrawStats();
	}
	
	renderer.StopRendition();
}

void SCR_GetColorBuffer(uchar* data)
{
	renderer.GetColorBuffer(data);
}