
/*
 *  world.c
 *  dEngine
 *
 *  Created by fabien sanglard on 24/08/09.
 *
 */

#include "world.h"
#include "md5model.h"
#include "renderer.h"
#include "timer.h"
#include "dEngine.h"
#include "camera.h"
#include "timer.h"
#include "filesystem.h"
#include "lexer.h"
#include "obj.h"
#include "game_internal.h"

light_t light;

entity_t map[MAX_MAP_ENTITIES];
ushort num_map_entities;

static char world_currentScene[256];

/* Frees only the geometry owned by this entity. The material is borrowed from
   MATLIB and is deliberately not touched here -- see World_UnloadScene. */
static void World_FreeEntityModel(entity_t* entity)
{
	md5_object_t* md5Object;
	int i;

	if (!entity->model)
		return;

	if (entity->type == ENTITY_OBJ)
	{
		/* OBJ_Free frees the obj_t itself, so do not free(model) after it. */
		OBJ_Free((obj_t*)entity->model);
	}
	else
	{
		md5Object = (md5_object_t*)entity->model;

		/* FreeModel does not release the GPU-ready arrays, and it frees the
		   meshes[] array they live in -- so drain them first. */
		if (md5Object->md5Model.meshes)
		{
			for (i = 0; i < md5Object->md5Model.num_meshes; i++)
			{
				free(md5Object->md5Model.meshes[i].vertexArray);
				free(md5Object->md5Model.meshes[i].vertexIndices);
				md5Object->md5Model.meshes[i].vertexArray = 0;
				md5Object->md5Model.meshes[i].vertexIndices = 0;
			}
		}

		FreeAnim(&md5Object->md5Anim);
		FreeModel(&md5Object->md5Model);
		free(md5Object);
	}

	entity->model = 0;
	entity->material = 0;
}

entity_t* World_AllocEntity(void)
{
	entity_t* entity = 0;
	int i;

	/* Reuse a hole before growing. Holes are never compacted away: object
	   handles encode a slot index and must stay valid. */
	for (i = 0; i < num_map_entities; i++)
	{
		if (!(map[i].flags & ENTITY_FLAG_ACTIVE))
		{
			entity = &map[i];
			break;
		}
	}

	if (!entity)
	{
		if (num_map_entities >= MAX_MAP_ENTITIES)
		{
			printf("[World] Entity overflow (max %d), ignoring spawn.\n", MAX_MAP_ENTITIES);
			return 0;
		}

		entity = &map[num_map_entities++];
	}

	entity->type = ENTITY_OBJ;
	entity->model = 0;
	entity->material = 0;
	entity->flags = ENTITY_FLAG_ACTIVE | ENTITY_FLAG_VISIBLE;
	matrixLoadIdentity(entity->matrix);

	return entity;
}

void World_FreeEntity(entity_t* entity)
{
	if (!entity || !(entity->flags & ENTITY_FLAG_ACTIVE))
		return;

	World_FreeEntityModel(entity);

	entity->flags = 0;
	/* Invalidate every handle that still points here. */
	entity->id++;
}

void World_ReadMatrix(de_matrix_t target)
{
	int i,j ;
	
	LE_readToken(); //{
	for(i=0 ; i < 4 ; i++)
		for(j=0 ; j < 4 ; j++)
			target[j*4+i] = LE_readReal();
	LE_readToken(); //}
}


void World_ReadOBJs(de_matrix_t currentMatrix)
{
	entity_t* currentEntity;
	obj_t* obj;
	
	
	LE_readToken(); // {
	LE_readToken();
	while (strcmp(LE_getCurrentToken(), "}"))
	{
		currentEntity = World_AllocEntity();

		if (!currentEntity)
		{
			/* Table full. Keep consuming so the token stream stays aligned. */
			LE_readToken();
		}
		else
		{
			currentEntity->type = ENTITY_OBJ;
			obj = calloc(1, sizeof(obj_t));
			currentEntity->model = obj ;

			if (!OBJ_Load(LE_readToken(), currentEntity))
			{
				free(currentEntity->model);
				currentEntity->model = 0;
				World_FreeEntity(currentEntity);
			}
			else
			{
				matrixCopy(currentMatrix, currentEntity->matrix);
				if (currentEntity->material)
					MATLIB_MakeAvailable(currentEntity->material);
			}
		}

		LE_readToken();
	}
}

void World_ReadMD5s(de_matrix_t currentMatrix)
{
    printf("[World_ReadMD5s] Loading MD5 models\n");
	md5_object_t* md5Object = 0;
	entity_t* currentEntity;
	md5_mesh_t* currentMesh;
	int i;

	LE_readToken(); // {
	LE_readToken();
	while (strcmp(LE_getCurrentToken(), "}"))
	{
		if (!strcmp("model",LE_getCurrentToken()))
		{
			LE_readToken();

			md5Object = 0;
			currentEntity = World_AllocEntity();

			if (currentEntity)
			{
				currentEntity->type = ENTITY_MD5;
				md5Object = calloc(1, sizeof(md5_object_t));
				currentEntity->model = md5Object ;

				if (!ReadMD5Model(LE_getCurrentToken(),currentEntity ))
				{
					free(currentEntity->model);
					currentEntity->model = 0;
					md5Object = 0;
					World_FreeEntity(currentEntity);
				}
				else
				{
					matrixCopy(currentMatrix, currentEntity->matrix);
					for(i = 0; i < 1; i++)
					{
						currentMesh = &md5Object->md5Model.meshes[i];
						if (currentEntity->material)
							MATLIB_MakeAvailable(currentEntity->material);
					}
				}
			}
		}

		if (!strcmp("anim",LE_getCurrentToken()))
		{
			LE_readToken();

			/* Applies to the model declared just above. If that load failed
			   there is nothing to animate. */
			if (md5Object)
			{
				ReadMD5Anim (LE_getCurrentToken(), &md5Object->md5Anim);

				 for(i = 0; i < 1; i++)
				 {
					 currentMesh = &md5Object->md5Model.meshes[i];
					 GenerateGPUVertices (currentMesh,md5Object->md5Model.baseSkel);
					 GenerateLightingInfo (currentMesh,md5Object->md5Model.baseSkel);
				 }
			}
		}

		LE_readToken();
	}
}

void World_Loadmap(char* mapFileName)
{
	filehandle_t* mapFile ;
	de_matrix_t currentMatrix;
	
	mapFile = FS_OpenFile(mapFileName, "rt");
	
	if (!mapFile)
	{
		printf("Map file '%s' cannot be opened.\n",mapFileName);
		return;
	}
	
	printf("[World_Loadmap] Found map: '%s'.\n",mapFileName);
	
	LE_pushLexer();
	LE_init(mapFile);

    printf("[World_Loadmap] Initialized lexer\n");
	
	while(LE_hasMoreData())
	{
        printf("[World_Loadmap] lexer has more data\n");
		LE_readToken();
		
		if (!strcmp(LE_getCurrentToken(), "matrix")) {
            printf("[World_Loadmap] Reading world matrix\n");
            World_ReadMatrix(currentMatrix);
        }
		
		if (!strcmp(LE_getCurrentToken(), "entities"))
		{
            printf("[World_Loadmap] Reading entity: %s\n", LE_getCurrentToken());
			LE_readToken(); // OBJ or MD5 ?
			
			if (!strcmp("OBJ",LE_getCurrentToken())) {
                printf("[World_Loadmap] Reading object file: %s\n", LE_getCurrentToken());
                World_ReadOBJs(currentMatrix);
            }
				
			if (!strcmp("MD5",LE_getCurrentToken())) {
                printf("[World_Loadmap] Reading MD5s\n");
                World_ReadMD5s(currentMatrix);
            }

		}
	}
	
	LE_popLexer();
}



void World_OpenScene(char* filename)
{
	filehandle_t* sceneFile;
	
	sceneFile = FS_OpenFile(filename, "rt");
	
	if (!sceneFile)
	{
		printf("Could not find scene file: %s.\n",filename);
		return;
	}
	
	printf("[World_OpenScene] Found scene: '%s'.\n",filename);
	
	LE_pushLexer();
	LE_init(sceneFile);
	
	
	while (LE_hasMoreData()) 
	{
		LE_readToken();
		
		if (!strcmp("map", LE_getCurrentToken()))
		{
			LE_readToken();	//{
			LE_readToken();
			while (strcmp("}", LE_getCurrentToken()))
			{
				if (!strcmp("filename", LE_getCurrentToken()))
				{
					LE_readToken();
					/* The entity table is cleared by World_UnloadScene, which
					   World_LoadScene always runs first. Resetting the count
					   here would orphan the previous scene's geometry. */
					World_Loadmap(LE_getCurrentToken());
				}
				LE_readToken();
			}
		}
		else 
		if (!strcmp("light", LE_getCurrentToken()))
		{
			LE_readToken(); // {
			LE_readToken();
			while (strcmp("}", LE_getCurrentToken()))
			{
				if (!strcmp("position", LE_getCurrentToken()))
				{
					light.position[0] = LE_readReal();
					light.position[1] = LE_readReal();
					light.position[2] = LE_readReal();
					light.position[3] = 1;
				}else 
				if (!strcmp("lookAt", LE_getCurrentToken()))
				{
					light.lookAt[0] = LE_readReal();
					light.lookAt[1] = LE_readReal();
					light.lookAt[2] = LE_readReal();
				}else 
				if (!strcmp("upVector", LE_getCurrentToken()))
				{
					light.upVector[0] = LE_readReal();
					light.upVector[1] = LE_readReal();
					light.upVector[2] = LE_readReal();
				}else 
				if (!strcmp("fov", LE_getCurrentToken()))
				{
					light.fov = LE_readReal();
				}else 
				if (!strcmp("ambientColor", LE_getCurrentToken()))
				{
					light.ambient[0] = LE_readReal();
					light.ambient[1] = LE_readReal();
					light.ambient[2] = LE_readReal();
				}else 
				if (!strcmp("diffuseColor", LE_getCurrentToken()))
				{
					light.diffuse[0] = LE_readReal();
					light.diffuse[1] = LE_readReal();
					light.diffuse[2] = LE_readReal();
				}else 
				if (!strcmp("speculaColor", LE_getCurrentToken()))
				{
					light.specula[0] = LE_readReal();
					light.specula[1] = LE_readReal();
					light.specula[2] = LE_readReal();
				}else 
				if (!strcmp("constantAttenuation", LE_getCurrentToken()))
				{
					light.constantAttenuation = LE_readReal();
				}else 
				if (!strcmp("linearAttenuation", LE_getCurrentToken()))
				{
					light.linearAttenuation = LE_readReal();
				}
				else 
					if (!strcmp("attachedToCamera", LE_getCurrentToken()))
					{
						LE_readToken();
						light.attachedToCamera = !strcmp("1", LE_getCurrentToken());
					}

				LE_readToken();
				//printf("lgiht token=%s\n",LE_getCurrentToken());
			}
		}
	}

	LE_popLexer();
}

void World_ReadSceneBlock(void)
{

	LE_readToken(); // {
	LE_readToken(); // scene fileName;

	/* Boot and runtime scene switching share one path. */
	World_LoadScene(LE_getCurrentToken());

	LE_readToken(); // }

}

void World_UnloadScene(void)
{
	int i;

	for (i = 0; i < num_map_entities; i++)
		World_FreeEntity(&map[i]);

	num_map_entities = 0;

	/* Materials and textures are deliberately kept alive for the lifetime of
	   the process. TEX_LoadTexture returns cache hits by struct copy
	   (texture.c), so several material_t instances share one data pointer, one
	   path allocation and one GL texture name -- freeing per scene would double
	   free. The material library is loaded once from data/materials.lbr and is
	   scene independent, so retaining it costs a few KB of hash buckets while
	   the geometry freed above is what actually dominates RAM. Reclaiming GPU
	   texture memory needs refcounting in MATLIB/TEXT plus de-aliasing
	   TEX_LoadTexture; until then, cycling through scenes with disjoint texture
	   sets can exhaust VRAM. */
}

int World_LoadScene(const char* sceneFilename)
{
	World_UnloadScene();

	/* Copy first: World_OpenScene takes char* and callers routinely pass a
	   pointer into the lexer's token buffer, which LE_init is about to reuse. */
	strncpy(world_currentScene, sceneFilename, sizeof(world_currentScene) - 1);
	world_currentScene[sizeof(world_currentScene) - 1] = '\0';

	World_OpenScene(world_currentScene);

	return num_map_entities > 0;
}

const char* World_CurrentScene(void)
{
	return world_currentScene;
}

void World_Init(void)
{
	filehandle_t* config;
	const char* bootScene;

	/* A game script can name its own start scene from Game_Configure, in which
	   case data/config.cfg is not consulted at all. */
	bootScene = DG_GetBootScene();

	if (bootScene && bootScene[0])
	{
		World_LoadScene(bootScene);
		return;
	}

	config = FS_OpenFile("data/config.cfg", "rt");


	if (!config)
	{
		printf("Conf file: data/config.cfg not found");
		exit(0);
	}
	
	LE_init(config);
	
	while (LE_hasMoreData()) 
	{
		LE_readToken();
		
		if (!strcmp("scene", LE_getCurrentToken()))
		{
			World_ReadSceneBlock();
		}
	}
	
			
	FS_CloseFile(config);
	
	
}


void World_Update(void)
{
	int i;
	md5_object_t* md5Object;
	entity_t* entity;
	
	for(i=0;i<num_map_entities;i++)
	{
		entity = &map[i];

		/* Destroyed entities leave holes behind so that object handles keep
		   resolving; skip them or MD5_Update dereferences a freed model. */
		if (!(entity->flags & ENTITY_FLAG_ACTIVE))
			continue;

		if (entity->type == ENTITY_OBJ)
			continue;

		md5Object = (md5_object_t*)entity->model;

		if (!md5Object)
			continue;

		MD5_Update(md5Object);
	}
	
	
	

	//simulationTime = 25000;
	if (light.attachedToCamera)
	{
		//light.position = camera.position + camera.right * 5;
		vectorCopy(camera.up,light.position);
		vectorScale(light.position,-20,light.position);
		vectorAdd(camera.position,light.position,light.position);
		
		//light.lookAt = camera.forward * 10;
		vectorCopy(camera.forward,light.lookAt);
		vectorScale(light.lookAt,100,light.lookAt);
		
		vectorCopy(camera.up,light.upVector);
		
		
	}
	//camera.vEye[0] = -cos(simulationTime*0.0001f)*170	;
	//camera.vEye[2] = -sin(simulationTime*0.0001f)*170	;
	
	//light.position[0] = cos(simulationTime*0.001f)*200	;
	//light.position[2] = sin(simulationTime*0.001f)*200	;
	
	
	//light.position[0] = cos(simulationTime*0.002f)*80	;
	//light.position[1] = cos(simulationTime*0.001f)*10	+120;

	
}