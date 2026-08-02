/*
 *  physics.h
 *  dEngine
 *
 *  A small impulse based rigid body simulation sized for the Dreamcast.
 *
 *  Design notes
 *  ------------
 *  - Linear dynamics only. Bodies have a position and a velocity, no
 *    orientation and no angular velocity. Combined with sphere and axis
 *    aligned box colliders this removes inertia tensors, contact manifolds and
 *    matrix inversions from the hot path, which is what makes the whole thing
 *    fit comfortably in an SH4 frame budget.
 *  - Everything lives in statically allocated pools. Nothing is malloc'd after
 *    PHYS_Init(), so the simulation cannot fragment the 16MB heap and its cost
 *    is known up front (roughly 80KB of BSS with the default limits).
 *  - The world is advanced with a fixed timestep and an accumulator, so the
 *    simulation stays stable whether the renderer runs at 60, 30 or 12 fps.
 *    PHYS_GetRenderPosition() interpolates between the two last steps so the
 *    visuals stay smooth even when physics and display rates disagree.
 *
 *  Units
 *  -----
 *  The engine is unit agnostic: gravity defaults to -9.81 per second squared,
 *  which assumes one world unit is one meter. dEngine's Doom 3 maps are
 *  authored in inches, so a game using them wants something closer to
 *  PHYS_SetGravity(0, -1066, 0) along with a larger slop and sleep threshold
 *  (see PHYS_SetSolverParams() and PHYS_SetSleepParams()).
 */

#ifndef DE_PHYSICS
#define DE_PHYSICS

#include "globals.h"
#include "internal_math.h"
#include "matrix.h"
#include "collision.h"

/* Pool sizes. ~80KB of BSS with these defaults. */
#ifndef PHYS_MAX_BODIES
	#define PHYS_MAX_BODIES 256
#endif
#ifndef PHYS_MAX_PAIRS
	#define PHYS_MAX_PAIRS 1024
#endif
#ifndef PHYS_MAX_CONTACTS
	#define PHYS_MAX_CONTACTS 512
#endif

/* Body types */
#define PHYS_STATIC		0	/* never moves, infinite mass (level geometry) */
#define PHYS_DYNAMIC	1	/* moved by forces and contacts */
#define PHYS_KINEMATIC	2	/* moved by the game, pushes dynamic bodies, unstoppable */

/* Body flags */
#define PHYS_FLAG_TRIGGER	(1 << 0)	/* detects overlaps but never resolves them */
#define PHYS_FLAG_CCD		(1 << 1)	/* sweeps against static geometry, for fast movers */
#define PHYS_FLAG_SLEEPING	(1 << 2)	/* idle, skipped by the solver until touched */
#define PHYS_FLAG_GROUNDED	(1 << 3)	/* set by the solver when standing on something */
#define PHYS_FLAG_NO_SLEEP	(1 << 4)	/* never put this body to sleep */
#define PHYS_FLAG_DISABLED	(1 << 5)	/* ignored by the whole simulation */

/* Collision layers. Every body has one layer bit and a mask of layers it hits. */
#define PHYS_LAYER_DEFAULT	0x0001
#define PHYS_LAYER_ALL		0xFFFF

typedef struct rigidbody_t {
	vec3_t position;
	vec3_t prevPosition;	/* position at the start of the current step */
	vec3_t velocity;
	vec3_t force;			/* accumulated forces, cleared every step */
	vec3_t groundNormal;	/* valid when PHYS_FLAG_GROUNDED is set */

	collider_t collider;

	float invMass;			/* 0 means infinite mass */
	float restitution;		/* 0 = no bounce, 1 = perfectly elastic */
	float friction;			/* Coulomb friction coefficient */
	float damping;			/* linear drag, per second */
	float gravityScale;		/* 0 disables gravity, 1 is normal, -1 floats up */
	float sleepTimer;

	void* userData;			/* game side payload, an entity_t* for instance */

	ushort layer;
	ushort mask;
	ushort id;				/* index in the body pool */
	uchar type;
	uchar flags;
	uchar inUse;
	uchar spare[3];
} rigidbody_t;

typedef struct raycast_hit_t {
	rigidbody_t* body;
	vec3_t point;
	vec3_t normal;
	float distance;		/* along the ray, or of the swept motion */
	float fraction;		/* [0,1] of the requested distance or motion */
} raycast_hit_t;

typedef struct phys_stats_t {
	int numBodies;
	int numAwakeBodies;
	int numPairs;		/* broad phase pairs of the last step */
	int numContacts;	/* narrow phase contacts of the last step */
	int numSteps;		/* fixed steps run by the last PHYS_Update() */
} phys_stats_t;

/*
 * Fired once per step for every touching pair: the contact callback after the
 * solver has run, the trigger callback as soon as the overlap is detected.
 * 'contact' describes the touch, with the normal pointing from a to b. Do not
 * create or destroy bodies from inside a callback.
 */
typedef void (*phys_callback_t)(rigidbody_t* a, rigidbody_t* b, const contact_t* contact);

/* --- world ------------------------------------------------------------- */

void PHYS_Init(void);
void PHYS_Reset(void);						/* destroys every body */
void PHYS_Update(int deltaMilliseconds);	/* fixed step accumulator, call once per frame */
void PHYS_StepOnce(float deltaSeconds);		/* advance exactly one step, for tools and tests */

void PHYS_SetGravity(float x, float y, float z);
void PHYS_GetGravity(vec3_t gravity);
void PHYS_SetTimestep(float seconds);
float PHYS_GetTimestep(void);
void PHYS_SetSolverParams(int velocityIterations, int positionIterations,
						  float penetrationSlop, float correction);
void PHYS_SetSleepParams(float linearThreshold, float seconds);
void PHYS_SetMaxVelocity(float unitsPerSecond);
void PHYS_SetContactCallback(phys_callback_t callback);
void PHYS_SetTriggerCallback(phys_callback_t callback);
const phys_stats_t* PHYS_GetStats(void);

/* --- bodies ------------------------------------------------------------ */

rigidbody_t* PHYS_CreateBody(uchar type, const vec3_t position, const collider_t* collider);
void PHYS_DestroyBody(rigidbody_t* body);
rigidbody_t* PHYS_GetBody(ushort id);

void PHYS_SetMass(rigidbody_t* body, float mass);
float PHYS_GetMass(const rigidbody_t* body);
void PHYS_SetPosition(rigidbody_t* body, const vec3_t position);	/* teleport */
void PHYS_SetVelocity(rigidbody_t* body, const vec3_t velocity);
void PHYS_AddVelocity(rigidbody_t* body, const vec3_t velocity);
void PHYS_ApplyForce(rigidbody_t* body, const vec3_t force);		/* cleared every step */
void PHYS_ApplyImpulse(rigidbody_t* body, const vec3_t impulse);	/* instantaneous */
void PHYS_SetLayer(rigidbody_t* body, ushort layer, ushort mask);
void PHYS_Wake(rigidbody_t* body);
void PHYS_Sleep(rigidbody_t* body);
uchar PHYS_IsGrounded(const rigidbody_t* body);

/* --- rendering --------------------------------------------------------- */

float PHYS_GetInterpolation(void);
void PHYS_GetRenderPosition(const rigidbody_t* body, vec3_t position);
/* Writes the interpolated position into the translation of a column major matrix. */
void PHYS_BodyToMatrix(const rigidbody_t* body, de_matrix_t matrix);

/* --- queries ----------------------------------------------------------- */

/*
 * Nearest hit along a ray. 'direction' is normalized internally. Bodies are
 * filtered with 'mask' (a body is considered when body->layer & mask) and
 * 'ignore' may be NULL.
 */
uchar PHYS_Raycast(const vec3_t origin, const vec3_t direction, float maxDistance,
				   ushort mask, const rigidbody_t* ignore, raycast_hit_t* hit);

/* Bodies overlapping a sphere. Returns how many were written to 'results'. */
int PHYS_OverlapSphere(const vec3_t center, float radius, ushort mask,
					   rigidbody_t** results, int maxResults);

/* Bodies overlapping a box given by its center and half extents. */
int PHYS_OverlapBox(const vec3_t center, const vec3_t halfExtents, ushort mask,
					rigidbody_t** results, int maxResults);

/* First impact of 'collider' moved from 'from' to 'to'. */
uchar PHYS_SweepCollider(const collider_t* collider, const vec3_t from, const vec3_t to,
						 ushort mask, const rigidbody_t* ignore, raycast_hit_t* hit);

/* Same, using the collider and current position of 'body'. */
uchar PHYS_SweepBody(const rigidbody_t* body, const vec3_t delta, ushort mask, raycast_hit_t* hit);

/*
 * Character style movement: moves 'body' by 'delta', sliding along whatever it
 * hits instead of stopping dead. Bodies are moved directly, without going
 * through the solver, which is what a player controller usually wants.
 * Returns the number of surfaces the move slid along.
 */
int PHYS_MoveAndSlide(rigidbody_t* body, const vec3_t delta, ushort mask);

#endif
