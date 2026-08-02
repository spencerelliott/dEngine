/*
 *  game_object.c
 *  dEngine
 *
 *  Handle based access to the world's entity table, plus the behavior registry.
 *
 *  map[] is a flat array that the map loader rebuilds from scratch on every
 *  scene, so a raw entity_t* held by game code across frames is a dangling
 *  pointer waiting to happen. A dg_object_t instead packs a slot index and the
 *  slot's generation counter. Destroying an object bumps that counter, so every
 *  outstanding handle to it stops resolving and every accessor quietly turns
 *  into a no-op rather than writing into a recycled entity.
 *
 *  Transform and behavior data live in arrays parallel to map[] rather than
 *  inside entity_t, which keeps the loaders and the renderer looking at exactly
 *  the struct they always did.
 */

#include "dgame.h"
#include "game_internal.h"
#include "world.h"
#include "entities.h"
#include "obj.h"
#include "md5model.h"
#include "material.h"
#include "filesystem.h"

#include <math.h>

#define DG_MAX_BEHAVIOR_DEFS         32
#define DG_MAX_BEHAVIORS_PER_OBJECT   4

/* Slot is stored biased by one so that a valid handle is never zero, even for
   slot 0 at generation 0. */
#define DG_HANDLE_MAKE(slot, generation) \
	((((uint)(generation)) << 16) | ((uint)((slot) + 1)))
#define DG_HANDLE_SLOT(handle) ((int)((handle) & 0xFFFFu) - 1)
#define DG_HANDLE_GENERATION(handle) ((ushort)((handle) >> 16))

typedef struct dg_transform_t {
	vec3_t position;
	float  head;
	float  pitch;
	float  roll;
	float  scale;
	/* Set when game code wrote a matrix directly; the TRS fields are then
	   stale and are not recomposed until one of them is set again. */
	uchar  rawMatrix;
} dg_transform_t;

typedef struct dg_behavior_inst_t {
	const dg_behavior_t* def;
	void* state;
} dg_behavior_inst_t;

static dg_transform_t     dg_transforms[MAX_MAP_ENTITIES];
static dg_behavior_inst_t dg_instances[MAX_MAP_ENTITIES][DG_MAX_BEHAVIORS_PER_OBJECT];
static uchar              dg_pendingDestroy[MAX_MAP_ENTITIES];

static const dg_behavior_t* dg_behaviorDefs[DG_MAX_BEHAVIOR_DEFS];
static int dg_numBehaviorDefs;

/* ------------------------------------------------------------------ *
 *  Handles
 * ------------------------------------------------------------------ */

static entity_t* DG_Resolve(dg_object_t obj)
{
	int slot;

	if (obj == DG_INVALID_OBJECT)
		return 0;

	slot = DG_HANDLE_SLOT(obj);

	if (slot < 0 || slot >= (int)num_map_entities)
		return 0;

	if (!(map[slot].flags & ENTITY_FLAG_ACTIVE))
		return 0;

	if (map[slot].id != DG_HANDLE_GENERATION(obj))
		return 0;

	return &map[slot];
}

static int DG_SlotOf(dg_object_t obj)
{
	return DG_Resolve(obj) ? DG_HANDLE_SLOT(obj) : -1;
}

static dg_object_t DG_HandleForSlot(int slot)
{
	return DG_HANDLE_MAKE(slot, map[slot].id);
}

int GameObject_IsValid(dg_object_t obj)
{
	return DG_Resolve(obj) != 0;
}

/* ------------------------------------------------------------------ *
 *  Transforms
 * ------------------------------------------------------------------ */

static void DG_ResetTransform(int slot)
{
	dg_transform_t* transform = &dg_transforms[slot];

	memset(transform, 0, sizeof(*transform));
	transform->scale = 1.0f;
}

/* Builds entity->matrix from the slot's TRS. Column major, translation at
   12..14, matching glMultMatrixf and the matrix blocks in .map files. */
static void DG_ComposeMatrix(int slot)
{
	dg_transform_t* transform = &dg_transforms[slot];
	float* m = map[slot].matrix;
	float ch, sh, cp, sp, cr, sr, s;

	if (transform->rawMatrix)
		return;

	ch = (float)cos(transform->head);
	sh = (float)sin(transform->head);
	cp = (float)cos(transform->pitch);
	sp = (float)sin(transform->pitch);
	cr = (float)cos(transform->roll);
	sr = (float)sin(transform->roll);
	s  = transform->scale;

	/* R = Ry(head) * Rx(pitch) * Rz(roll) */
	m[0]  = (ch * cr + sh * sp * sr) * s;
	m[1]  = (cp * sr) * s;
	m[2]  = (-sh * cr + ch * sp * sr) * s;
	m[3]  = 0.0f;

	m[4]  = (-ch * sr + sh * sp * cr) * s;
	m[5]  = (cp * cr) * s;
	m[6]  = (sh * sr + ch * sp * cr) * s;
	m[7]  = 0.0f;

	m[8]  = (sh * cp) * s;
	m[9]  = (-sp) * s;
	m[10] = (ch * cp) * s;
	m[11] = 0.0f;

	m[12] = transform->position[0];
	m[13] = transform->position[1];
	m[14] = transform->position[2];
	m[15] = 1.0f;
}

/* Moves a slot from matrix-authoritative back to TRS-authoritative. Position
   and uniform scale survive; orientation does not -- a general matrix has no
   unique head/pitch/roll and guessing one would be worse than resetting. This
   is why rotating an entity that came from a .map file drops the rotation the
   map baked in, and why GameObject_SetPosition below is careful to stay in raw
   mode instead of forcing the conversion. */
static void DG_LeaveRawMatrix(int slot)
{
	dg_transform_t* transform = &dg_transforms[slot];
	const float* m = map[slot].matrix;
	float scale;

	if (!transform->rawMatrix)
		return;

	transform->position[0] = m[12];
	transform->position[1] = m[13];
	transform->position[2] = m[14];

	scale = (float)sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	transform->scale = scale > 0.0f ? scale : 1.0f;

	transform->head = 0.0f;
	transform->pitch = 0.0f;
	transform->roll = 0.0f;
	transform->rawMatrix = 0;
}

void GameObject_SetPosition(dg_object_t obj, float x, float y, float z)
{
	entity_t* entity = DG_Resolve(obj);
	int slot;

	if (!entity)
		return;

	slot = DG_HANDLE_SLOT(obj);

	dg_transforms[slot].position[0] = x;
	dg_transforms[slot].position[1] = y;
	dg_transforms[slot].position[2] = z;

	if (dg_transforms[slot].rawMatrix)
	{
		/* Translation is the one component that can be edited in place without
		   losing whatever rotation and scale the matrix carries. */
		entity->matrix[12] = x;
		entity->matrix[13] = y;
		entity->matrix[14] = z;
		return;
	}

	DG_ComposeMatrix(slot);
}

void GameObject_GetPosition(dg_object_t obj, vec3_t out)
{
	entity_t* entity = DG_Resolve(obj);
	int slot;

	if (!entity)
	{
		vectorClear(out);
		return;
	}

	slot = DG_HANDLE_SLOT(obj);

	/* When a matrix was written directly the TRS copy is stale, so read the
	   translation back out of the matrix instead. */
	if (dg_transforms[slot].rawMatrix)
	{
		out[0] = entity->matrix[12];
		out[1] = entity->matrix[13];
		out[2] = entity->matrix[14];
		return;
	}

	vectorCopy(dg_transforms[slot].position, out);
}

void GameObject_Translate(dg_object_t obj, float dx, float dy, float dz)
{
	vec3_t position;

	if (!GameObject_IsValid(obj))
		return;

	GameObject_GetPosition(obj, position);
	GameObject_SetPosition(obj, position[0] + dx, position[1] + dy, position[2] + dz);
}

void GameObject_SetRotation(dg_object_t obj, float head, float pitch, float roll)
{
	int slot = DG_SlotOf(obj);

	if (slot < 0)
		return;

	DG_LeaveRawMatrix(slot);

	dg_transforms[slot].head = head;
	dg_transforms[slot].pitch = pitch;
	dg_transforms[slot].roll = roll;

	DG_ComposeMatrix(slot);
}

void GameObject_GetRotation(dg_object_t obj, float* head, float* pitch, float* roll)
{
	int slot = DG_SlotOf(obj);

	if (head)
		*head = slot < 0 ? 0.0f : dg_transforms[slot].head;
	if (pitch)
		*pitch = slot < 0 ? 0.0f : dg_transforms[slot].pitch;
	if (roll)
		*roll = slot < 0 ? 0.0f : dg_transforms[slot].roll;
}

void GameObject_SetYaw(dg_object_t obj, float head)
{
	int slot = DG_SlotOf(obj);

	if (slot < 0)
		return;

	GameObject_SetRotation(obj, head, dg_transforms[slot].pitch, dg_transforms[slot].roll);
}

void GameObject_SetScale(dg_object_t obj, float scale)
{
	int slot = DG_SlotOf(obj);

	if (slot < 0)
		return;

	DG_LeaveRawMatrix(slot);

	dg_transforms[slot].scale = scale;

	DG_ComposeMatrix(slot);
}

float GameObject_GetScale(dg_object_t obj)
{
	int slot = DG_SlotOf(obj);

	return slot < 0 ? 0.0f : dg_transforms[slot].scale;
}

void GameObject_SetMatrix(dg_object_t obj, const de_matrix_t m)
{
	entity_t* entity = DG_Resolve(obj);
	int slot;
	int i;

	if (!entity)
		return;

	slot = DG_HANDLE_SLOT(obj);

	for (i = 0; i < 16; i++)
		entity->matrix[i] = m[i];

	dg_transforms[slot].rawMatrix = 1;
}

void GameObject_GetMatrix(dg_object_t obj, de_matrix_t out)
{
	entity_t* entity = DG_Resolve(obj);

	if (!entity)
	{
		matrixLoadIdentity(out);
		return;
	}

	matrixCopy(entity->matrix, out);
}

void GameObject_SetVisible(dg_object_t obj, int visible)
{
	entity_t* entity = DG_Resolve(obj);

	if (!entity)
		return;

	if (visible)
		entity->flags |= ENTITY_FLAG_VISIBLE;
	else
		entity->flags &= (uchar)~ENTITY_FLAG_VISIBLE;
}

int GameObject_IsVisible(dg_object_t obj)
{
	entity_t* entity = DG_Resolve(obj);

	return entity && (entity->flags & ENTITY_FLAG_VISIBLE);
}

int GameObject_SetMaterial(dg_object_t obj, const char* materialName)
{
	entity_t* entity = DG_Resolve(obj);
	material_t* material;

	if (!entity || !materialName)
		return 0;

	material = MATLIB_Get((char*)materialName);

	if (!material)
	{
		printf("[Game] No material named '%s'.\n", materialName);
		return 0;
	}

	MATLIB_MakeAvailable(material);
	entity->material = material;

	return 1;
}

/* ------------------------------------------------------------------ *
 *  Spawning
 * ------------------------------------------------------------------ */

static int DG_IsObjFile(const char* path)
{
	const char* extension = FS_GetExtensionAddress((char*)path);

	return extension &&
	       (extension[0] == 'o' || extension[0] == 'O') &&
	       (extension[1] == 'b' || extension[1] == 'B') &&
	       (extension[2] == 'j' || extension[2] == 'J') &&
	       extension[3] == '\0';
}

static dg_object_t DG_FinishSpawn(entity_t* entity, int slot, float x, float y, float z)
{
	if (entity->material)
		MATLIB_MakeAvailable(entity->material);

	DG_ResetTransform(slot);
	dg_transforms[slot].position[0] = x;
	dg_transforms[slot].position[1] = y;
	dg_transforms[slot].position[2] = z;
	DG_ComposeMatrix(slot);

	return DG_HandleForSlot(slot);
}

dg_object_t GameObject_SpawnAt(const char* modelFile, float x, float y, float z)
{
	entity_t* entity;
	int slot;

	if (!modelFile || !modelFile[0])
		return DG_INVALID_OBJECT;

	entity = World_AllocEntity();

	if (!entity)
		return DG_INVALID_OBJECT;

	slot = (int)(entity - map);

	memset(&dg_instances[slot], 0, sizeof(dg_instances[slot]));
	dg_pendingDestroy[slot] = 0;

	if (DG_IsObjFile(modelFile))
	{
		entity->type = ENTITY_OBJ;
		entity->model = calloc(1, sizeof(obj_t));

		if (!OBJ_Load((char*)modelFile, entity))
		{
			free(entity->model);
			entity->model = 0;
			World_FreeEntity(entity);
			printf("[Game] Could not load OBJ '%s'.\n", modelFile);
			return DG_INVALID_OBJECT;
		}
	}
	else
	{
		entity->type = ENTITY_MD5;
		entity->model = calloc(1, sizeof(md5_object_t));

		if (!ReadMD5Model(modelFile, entity))
		{
			free(entity->model);
			entity->model = 0;
			World_FreeEntity(entity);
			printf("[Game] Could not load MD5 '%s'.\n", modelFile);
			return DG_INVALID_OBJECT;
		}
	}

	return DG_FinishSpawn(entity, slot, x, y, z);
}

dg_object_t GameObject_Spawn(const char* modelFile)
{
	return GameObject_SpawnAt(modelFile, 0.0f, 0.0f, 0.0f);
}

dg_object_t GameObject_SpawnAnimated(const char* meshFile, const char* animFile,
                                     float x, float y, float z)
{
	dg_object_t obj;
	entity_t* entity;
	md5_object_t* md5Object;
	md5_mesh_t* mesh;

	obj = GameObject_SpawnAt(meshFile, x, y, z);

	if (obj == DG_INVALID_OBJECT || !animFile || !animFile[0])
		return obj;

	entity = DG_Resolve(obj);

	if (!entity || entity->type != ENTITY_MD5 || !entity->model)
		return obj;

	md5Object = (md5_object_t*)entity->model;

	if (!ReadMD5Anim(animFile, &md5Object->md5Anim))
	{
		printf("[Game] Could not load anim '%s'.\n", animFile);
		return obj;
	}

	/* The renderer only ever walks mesh 0, matching the scene loader. */
	if (md5Object->md5Model.num_meshes > 0)
	{
		mesh = &md5Object->md5Model.meshes[0];
		GenerateGPUVertices(mesh, md5Object->md5Model.baseSkel);
		GenerateLightingInfo(mesh, md5Object->md5Model.baseSkel);
	}

	return obj;
}

/* ------------------------------------------------------------------ *
 *  Behaviors
 * ------------------------------------------------------------------ */

void DG_RegisterBehavior(const dg_behavior_t* behavior)
{
	int i;

	if (!behavior || !behavior->name)
		return;

	/* Idempotent, so the constructor path and an explicit call from Game_Init
	   can coexist. */
	for (i = 0; i < dg_numBehaviorDefs; i++)
	{
		if (dg_behaviorDefs[i] == behavior)
			return;

		if (!strcmp(dg_behaviorDefs[i]->name, behavior->name))
		{
			printf("[Game] Two behaviors named '%s'; keeping the first.\n", behavior->name);
			return;
		}
	}

	if (dg_numBehaviorDefs >= DG_MAX_BEHAVIOR_DEFS)
	{
		printf("[Game] Behavior registry full (max %d), '%s' ignored.\n",
		       DG_MAX_BEHAVIOR_DEFS, behavior->name);
		return;
	}

	dg_behaviorDefs[dg_numBehaviorDefs++] = behavior;
}

int DG_RegisteredBehaviorCount(void)
{
	return dg_numBehaviorDefs;
}

static const dg_behavior_t* DG_FindBehavior(const char* name)
{
	int i;

	if (!name)
		return 0;

	for (i = 0; i < dg_numBehaviorDefs; i++)
	{
		if (!strcmp(dg_behaviorDefs[i]->name, name))
			return dg_behaviorDefs[i];
	}

	return 0;
}

void* GameObject_AddBehaviorPtr(dg_object_t obj, const dg_behavior_t* behavior)
{
	int slot = DG_SlotOf(obj);
	int i;
	dg_behavior_inst_t* instance = 0;

	if (slot < 0 || !behavior)
		return 0;

	/* Attaching by pointer works whether or not the definition was ever
	   registered, which keeps scripts working even if the constructor based
	   registration does not run on a given toolchain. */
	DG_RegisterBehavior(behavior);

	for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
	{
		if (dg_instances[slot][i].def == behavior)
			return dg_instances[slot][i].state;

		if (!dg_instances[slot][i].def && !instance)
			instance = &dg_instances[slot][i];
	}

	if (!instance)
	{
		printf("[Game] Object already has %d behaviors, '%s' ignored.\n",
		       DG_MAX_BEHAVIORS_PER_OBJECT, behavior->name);
		return 0;
	}

	instance->def = behavior;
	instance->state = behavior->state_size ? calloc(1, behavior->state_size) : 0;

	if (behavior->on_attach)
		behavior->on_attach(obj, instance->state);

	return instance->state;
}

void* GameObject_AddBehavior(dg_object_t obj, const char* behaviorName)
{
	const dg_behavior_t* behavior = DG_FindBehavior(behaviorName);

	if (!behavior)
	{
		printf("[Game] No behavior named '%s' is registered.\n",
		       behaviorName ? behaviorName : "(null)");
		return 0;
	}

	return GameObject_AddBehaviorPtr(obj, behavior);
}

void* GameObject_GetBehavior(dg_object_t obj, const char* behaviorName)
{
	int slot = DG_SlotOf(obj);
	int i;

	if (slot < 0 || !behaviorName)
		return 0;

	for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
	{
		if (dg_instances[slot][i].def &&
		    !strcmp(dg_instances[slot][i].def->name, behaviorName))
			return dg_instances[slot][i].state;
	}

	return 0;
}

static void DG_DetachInstance(dg_object_t obj, dg_behavior_inst_t* instance)
{
	if (!instance->def)
		return;

	if (instance->def->on_detach)
		instance->def->on_detach(obj, instance->state);

	free(instance->state);
	instance->state = 0;
	instance->def = 0;
}

void GameObject_RemoveBehavior(dg_object_t obj, const char* behaviorName)
{
	int slot = DG_SlotOf(obj);
	int i;

	if (slot < 0 || !behaviorName)
		return;

	for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
	{
		if (dg_instances[slot][i].def &&
		    !strcmp(dg_instances[slot][i].def->name, behaviorName))
		{
			DG_DetachInstance(obj, &dg_instances[slot][i]);
			return;
		}
	}
}

dg_object_t GameObject_FindByBehavior(const char* behaviorName)
{
	int slot;
	int i;

	if (!behaviorName)
		return DG_INVALID_OBJECT;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		if (!(map[slot].flags & ENTITY_FLAG_ACTIVE))
			continue;

		for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
		{
			if (dg_instances[slot][i].def &&
			    !strcmp(dg_instances[slot][i].def->name, behaviorName))
				return DG_HandleForSlot(slot);
		}
	}

	return DG_INVALID_OBJECT;
}

/* ------------------------------------------------------------------ *
 *  Iteration and destruction
 * ------------------------------------------------------------------ */

dg_object_t GameObject_Next(dg_object_t obj)
{
	int slot;

	if (obj == DG_INVALID_OBJECT)
		slot = 0;
	else
	{
		slot = DG_HANDLE_SLOT(obj);

		if (slot < 0)
			return DG_INVALID_OBJECT;

		slot++;
	}

	for (; slot < (int)num_map_entities; slot++)
	{
		if (map[slot].flags & ENTITY_FLAG_ACTIVE)
			return DG_HandleForSlot(slot);
	}

	return DG_INVALID_OBJECT;
}

int GameObject_Count(void)
{
	int slot;
	int count = 0;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		if (map[slot].flags & ENTITY_FLAG_ACTIVE)
			count++;
	}

	return count;
}

void GameObject_Destroy(dg_object_t obj)
{
	int slot = DG_SlotOf(obj);

	if (slot < 0)
		return;

	/* Deferred: the renderer may still be walking map[] this frame. */
	dg_pendingDestroy[slot] = 1;
}

void DG_FlushDestroys(void)
{
	int slot;
	int i;
	dg_object_t obj;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		if (!dg_pendingDestroy[slot])
			continue;

		dg_pendingDestroy[slot] = 0;

		if (!(map[slot].flags & ENTITY_FLAG_ACTIVE))
			continue;

		obj = DG_HandleForSlot(slot);

		for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
			DG_DetachInstance(obj, &dg_instances[slot][i]);

		World_FreeEntity(&map[slot]);
		DG_ResetTransform(slot);
	}
}

void DG_DetachAllBehaviors(void)
{
	int slot;
	int i;
	dg_object_t obj;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		obj = DG_HandleForSlot(slot);

		for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
			DG_DetachInstance(obj, &dg_instances[slot][i]);

		dg_pendingDestroy[slot] = 0;
	}
}

void DG_UpdateBehaviors(float dt)
{
	int slot;
	int i;
	dg_object_t obj;
	dg_behavior_inst_t* instance;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		if (!(map[slot].flags & ENTITY_FLAG_ACTIVE))
			continue;

		if (dg_pendingDestroy[slot])
			continue;

		obj = DG_HandleForSlot(slot);

		for (i = 0; i < DG_MAX_BEHAVIORS_PER_OBJECT; i++)
		{
			instance = &dg_instances[slot][i];

			if (instance->def && instance->def->on_update)
				instance->def->on_update(obj, instance->state, dt);
		}
	}
}

void DG_ObjectsInit(void)
{
	int slot;

	memset(dg_instances, 0, sizeof(dg_instances));
	memset(dg_pendingDestroy, 0, sizeof(dg_pendingDestroy));

	for (slot = 0; slot < MAX_MAP_ENTITIES; slot++)
		DG_ResetTransform(slot);
}

/* Entities built by the map loader already carry a world matrix from the file.
   Mark them matrix-authoritative so reads report where they actually are
   instead of the empty TRS record. Run after every scene load. */
void DG_AdoptSceneEntities(void)
{
	int slot;

	for (slot = 0; slot < (int)num_map_entities; slot++)
	{
		DG_ResetTransform(slot);

		if (map[slot].flags & ENTITY_FLAG_ACTIVE)
			dg_transforms[slot].rawMatrix = 1;
	}
}
