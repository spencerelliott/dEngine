/*
 *  entities.h
 *  dEngine
 *
 *  Created by fabien sanglard on 11/09/09.
 *
 */


#ifndef ED_ENTITIES
#define ED_ENTITIES

#define ENTITY_MD5 0
#define ENTITY_OBJ 1

/* Slot is in use. Cleared slots stay in place so that object handles keep
   pointing at a stable index. */
#define ENTITY_FLAG_ACTIVE  (1 << 0)
/* Submitted to the renderer. */
#define ENTITY_FLAG_VISIBLE (1 << 1)

#define MAX_MAP_ENTITIES 256

#include "globals.h"
#include "internal_math.h"
#include "matrix.h"
#include "material.h"

typedef struct entity_t {
	uchar type;
	uchar flags;
	void* model;

	material_t* material;

	de_matrix_t matrix;
	de_matrix_t cachePVMShadow;
	/* Handle generation counter. Bumped every time the slot is released so
	   that stale dg_object_t handles fail validation instead of aliasing a
	   recycled entity. */
	ushort id;

} entity_t;



#endif
