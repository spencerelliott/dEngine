/*
 *  world.h
 *  dEngine
 *
 *  Created by fabien sanglard on 24/08/09.
 *
 */

#ifndef DE_WORLD
#define DE_WORLD

#include "md5model.h"
#include "entities.h"


typedef struct light_t {
	
	vec4_t position;
	vec3_t lookAt;
	vec3_t upVector;
	float fov;
	
	vec4_t ambient;
	vec4_t diffuse;
	vec4_t specula;
	float constantAttenuation;
	float linearAttenuation;
	//float quadraticAttenuation;
	
	char attachedToCamera;
} light_t;

void World_Init(void);
void World_Update(void);

/* Defined in world.c but historically undeclared. */
void World_OpenScene(char* filename);
void World_Loadmap(char* mapFileName);

/* Scene lifecycle. World_LoadScene tears the current scene down first, so it is
   safe to call repeatedly. Never call it mid-frame: the renderer walks map[]
   directly. Game code should use Game_RequestScene() instead, which defers to a
   frame boundary. */
int         World_LoadScene(const char* sceneFilename);
void        World_UnloadScene(void);
const char* World_CurrentScene(void);

/* Slot management shared by the map parser and the game object layer, so there
   is one code path by which an entity comes into being. */
entity_t* World_AllocEntity(void);
void      World_FreeEntity(entity_t* entity);

extern light_t light;

extern entity_t map[MAX_MAP_ENTITIES];
extern ushort num_map_entities;

#endif