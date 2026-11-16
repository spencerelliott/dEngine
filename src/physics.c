/*
 *  physics.c
 *  dEngine
 *
 *  Impulse based rigid body simulation for sphere and box colliders.
 *
 *  The pipeline of a single fixed step is:
 *
 *      forces      gravity, damping and user forces are integrated into
 *                  the velocities
 *      integrate   velocities move the bodies, optionally swept against
 *                  static geometry when continuous detection is enabled
 *      broad phase sweep and prune on the X axis produces candidate pairs
 *      narrow phase sphere/box tests turn pairs into contacts
 *      solve       sequential impulses with accumulated clamping resolve
 *                  the contact velocities, then a positional pass removes
 *                  the leftover penetration
 *      sleep       bodies that stopped moving are parked
 */

#include "physics.h"

/* Number of iterations of the velocity solver. More is stabler and slower. */
#define PHYS_DEFAULT_ITERATIONS 4

/* Number of passes of the positional pass that removes leftover penetration. */
#define PHYS_DEFAULT_POSITION_ITERATIONS 3

/* Penetration left untouched by the positional pass. Stops resting jitter. */
#define PHYS_DEFAULT_SLOP 0.01f

/* Fraction of the remaining penetration removed every step. */
#define PHYS_DEFAULT_CORRECTION 0.8f

/* Approach speed under which a collision does not bounce. */
#define PHYS_RESTITUTION_THRESHOLD 1.0f

/* A contact whose normal points this much up counts as ground. ~45 degrees. */
#define PHYS_GROUND_DOT 0.7f

/* Never simulate more than this many steps for a single frame. */
#define PHYS_MAX_STEPS 5

/* Longest frame the accumulator will take in, in milliseconds. */
#define PHYS_MAX_FRAME_MS 250

/* Backs the swept motion off the surface so the body does not end up touching. */
#define PHYS_SKIN 0.001f

/* Iterations of PHYS_MoveAndSlide(). */
#define PHYS_MAX_SLIDES 4

typedef struct phys_pair_t {
	ushort a;
	ushort b;
} phys_pair_t;

typedef struct phys_contact_t {
	ushort a;
	ushort b;
	uint key;			/* identifies the pair across steps, for warm starting */
	contact_t manifold;
	float restitutionTarget;
	float normalImpulse;
	float tangentImpulse;
	float normalMass;	/* 1 / (invMassA + invMassB), 0 when both are infinite */
} phys_contact_t;

/*
 * Warm starting cache. Replaying the impulse a contact needed last step is
 * what lets a four iteration solver hold a stack up: without it every step
 * starts from zero and the pile slowly sinks into itself. Open addressed,
 * statically sized, rebuilt at the end of every step.
 */
#define PHYS_CACHE_SIZE 1024	/* power of two, at least twice PHYS_MAX_CONTACTS */
#define PHYS_CACHE_EMPTY 0xFFFFFFFFu
#define PHYS_CACHE_PROBES 8

typedef struct phys_cache_t {
	uint key;
	float normalImpulse;
} phys_cache_t;

static rigidbody_t bodies[PHYS_MAX_BODIES];
static aabb_t bodyBounds[PHYS_MAX_BODIES];
static ushort freeList[PHYS_MAX_BODIES];
static ushort sorted[PHYS_MAX_BODIES];		/* body ids sorted by bounds.min[X] */
static phys_pair_t pairs[PHYS_MAX_PAIRS];
static phys_contact_t contacts[PHYS_MAX_CONTACTS];
static phys_cache_t impulseCache[PHYS_CACHE_SIZE];

static int numFree;
static int numSorted;
static int numPairs;
static int numContacts;

static vec3_t gravity = { 0.0f, -9.81f, 0.0f };
static const vec3_t upVector = { 0.0f, 1.0f, 0.0f };

static float timestep = 1.0f / 60.0f;
static float accumulator;
static float interpolation;
static float penetrationSlop = PHYS_DEFAULT_SLOP;
static float correctionFactor = PHYS_DEFAULT_CORRECTION;
static float sleepThreshold = 0.15f;
static float sleepTime = 0.5f;
static float maxVelocity = 4000.0f;
static int solverIterations = PHYS_DEFAULT_ITERATIONS;
static int positionIterations = PHYS_DEFAULT_POSITION_ITERATIONS;

static phys_callback_t contactCallback;
static phys_callback_t triggerCallback;

static phys_stats_t stats;
static uchar initialized;

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static uint PHYS_ContactKey(ushort a, ushort b)
{
	if (a < b)
		return ((uint)a << 16) | (uint)b;

	return ((uint)b << 16) | (uint)a;
}

static uint PHYS_CacheSlot(uint key)
{
	/* Knuth's multiplicative hash, the top bits are the well mixed ones. */
	return (uint)((key * 2654435761u) >> 16) & (PHYS_CACHE_SIZE - 1);
}

static void PHYS_ClearCache(void)
{
	int i;

	for(i = 0 ; i < PHYS_CACHE_SIZE ; i++)
	{
		impulseCache[i].key = PHYS_CACHE_EMPTY;
		impulseCache[i].normalImpulse = 0.0f;
	}
}

static float PHYS_LookupImpulse(uint key)
{
	uint slot;
	int probe;

	slot = PHYS_CacheSlot(key);

	for(probe = 0 ; probe < PHYS_CACHE_PROBES ; probe++)
	{
		if (impulseCache[slot].key == PHYS_CACHE_EMPTY)
			return 0.0f;

		if (impulseCache[slot].key == key)
			return impulseCache[slot].normalImpulse;

		slot = (slot + 1) & (PHYS_CACHE_SIZE - 1);
	}

	return 0.0f;
}

static void PHYS_StoreImpulse(uint key, float impulse)
{
	uint slot;
	int probe;

	slot = PHYS_CacheSlot(key);

	for(probe = 0 ; probe < PHYS_CACHE_PROBES ; probe++)
	{
		if (impulseCache[slot].key == PHYS_CACHE_EMPTY || impulseCache[slot].key == key)
		{
			impulseCache[slot].key = key;
			impulseCache[slot].normalImpulse = impulse;
			return;
		}

		slot = (slot + 1) & (PHYS_CACHE_SIZE - 1);
	}

	/* Table too crowded: this contact simply starts cold next step. */
}

static uchar PHYS_IsSimulated(const rigidbody_t* body)
{
	return (uchar)(body->inUse && !(body->flags & PHYS_FLAG_DISABLED));
}

/* Bodies that can move on their own this step. */
static uchar PHYS_IsMover(const rigidbody_t* body)
{
	if (body->type == PHYS_STATIC)
		return 0;

	return (uchar)((body->flags & PHYS_FLAG_SLEEPING) == 0);
}

static void PHYS_ClampVelocity(rigidbody_t* body)
{
	float speedSquared, scale;

	speedSquared = DotProduct(body->velocity, body->velocity);

	if (speedSquared > maxVelocity * maxVelocity)
	{
		scale = maxVelocity * DE_RECIP(DE_SQRT(speedSquared));
		vectorScale(body->velocity, scale, body->velocity);
	}
}

/* ========================================================================= */
/* World                                                                     */
/* ========================================================================= */

void PHYS_Init(void)
{
	int i;

	memset(bodies, 0, sizeof(bodies));

	numFree = 0;
	numSorted = 0;
	numPairs = 0;
	numContacts = 0;
	accumulator = 0.0f;
	interpolation = 0.0f;

	/* Hand out low ids first so debugging stays readable. */
	for(i = PHYS_MAX_BODIES - 1 ; i >= 0 ; i--)
		freeList[numFree++] = (ushort)i;

	PHYS_ClearCache();

	memset(&stats, 0, sizeof(stats));
	initialized = 1;
}

void PHYS_Reset(void)
{
	PHYS_Init();
}

void PHYS_SetGravity(float x, float y, float z)
{
	vectorSet(gravity, x, y, z);
}

void PHYS_GetGravity(vec3_t out)
{
	vectorCopy(gravity, out);
}

void PHYS_SetTimestep(float seconds)
{
	if (seconds > 0.0f)
		timestep = seconds;
}

float PHYS_GetTimestep(void)
{
	return timestep;
}

void PHYS_SetSolverParams(int velocityIterations, int passes, float slop, float correction)
{
	if (velocityIterations > 0)
		solverIterations = velocityIterations;
	if (passes > 0)
		positionIterations = passes;
	if (slop >= 0.0f)
		penetrationSlop = slop;
	if (correction >= 0.0f && correction <= 1.0f)
		correctionFactor = correction;
}

void PHYS_SetSleepParams(float linearThreshold, float seconds)
{
	if (linearThreshold >= 0.0f)
		sleepThreshold = linearThreshold;
	if (seconds >= 0.0f)
		sleepTime = seconds;
}

void PHYS_SetMaxVelocity(float unitsPerSecond)
{
	if (unitsPerSecond > 0.0f)
		maxVelocity = unitsPerSecond;
}

void PHYS_SetContactCallback(phys_callback_t callback)
{
	contactCallback = callback;
}

void PHYS_SetTriggerCallback(phys_callback_t callback)
{
	triggerCallback = callback;
}

const phys_stats_t* PHYS_GetStats(void)
{
	return &stats;
}

/* ========================================================================= */
/* Bodies                                                                    */
/* ========================================================================= */

rigidbody_t* PHYS_CreateBody(uchar type, const vec3_t position, const collider_t* collider)
{
	rigidbody_t* body;
	ushort id;

	if (!initialized)
		PHYS_Init();

	if (!numFree)
	{
		printf("[PHYS] Body pool exhausted (%d bodies).\n", PHYS_MAX_BODIES);
		return NULL;
	}

	id = freeList[--numFree];
	body = &bodies[id];

	memset(body, 0, sizeof(rigidbody_t));

	body->id = id;
	body->inUse = 1;
	body->type = type;
	body->restitution = 0.0f;
	body->friction = 0.5f;
	body->damping = 0.0f;
	body->gravityScale = 1.0f;
	body->layer = PHYS_LAYER_DEFAULT;
	body->mask = PHYS_LAYER_ALL;

	if (position)
	{
		vectorCopy(position, body->position);
		vectorCopy(position, body->prevPosition);
	}

	if (collider)
		body->collider = *collider;
	else
		COL_MakeSphere(&body->collider, 1.0f);

	PHYS_SetMass(body, (type == PHYS_DYNAMIC) ? 1.0f : 0.0f);

	sorted[numSorted++] = id;
	stats.numBodies++;

	return body;
}

void PHYS_DestroyBody(rigidbody_t* body)
{
	int i;

	if (!body || !body->inUse)
		return;

	for(i = 0 ; i < numSorted ; i++)
	{
		if (sorted[i] == body->id)
		{
			numSorted--;
			sorted[i] = sorted[numSorted];
			break;
		}
	}

	body->inUse = 0;
	freeList[numFree++] = body->id;
	stats.numBodies--;

	/* The id can come back on a different body, so drop its cached impulses. */
	PHYS_ClearCache();
}

rigidbody_t* PHYS_GetBody(ushort id)
{
	if (id >= PHYS_MAX_BODIES || !bodies[id].inUse)
		return NULL;

	return &bodies[id];
}

void PHYS_SetMass(rigidbody_t* body, float mass)
{
	if (body->type != PHYS_DYNAMIC || mass <= 0.0f)
	{
		body->invMass = 0.0f;
		return;
	}

	body->invMass = DE_RECIP(mass);
}

float PHYS_GetMass(const rigidbody_t* body)
{
	if (body->invMass <= 0.0f)
		return 0.0f;

	return DE_RECIP(body->invMass);
}

void PHYS_SetPosition(rigidbody_t* body, const vec3_t position)
{
	vectorCopy(position, body->position);
	vectorCopy(position, body->prevPosition);
	PHYS_Wake(body);
}

void PHYS_SetVelocity(rigidbody_t* body, const vec3_t velocity)
{
	vectorCopy(velocity, body->velocity);
	PHYS_Wake(body);
}

void PHYS_AddVelocity(rigidbody_t* body, const vec3_t velocity)
{
	vectorAdd(body->velocity, velocity, body->velocity);
	PHYS_Wake(body);
}

void PHYS_ApplyForce(rigidbody_t* body, const vec3_t force)
{
	vectorAdd(body->force, force, body->force);
	PHYS_Wake(body);
}

void PHYS_ApplyImpulse(rigidbody_t* body, const vec3_t impulse)
{
	if (body->invMass <= 0.0f)
		return;

	vectorMA(body->velocity, body->invMass, impulse, body->velocity);
	PHYS_Wake(body);
}

void PHYS_SetLayer(rigidbody_t* body, ushort layer, ushort mask)
{
	body->layer = layer;
	body->mask = mask;
}

void PHYS_Wake(rigidbody_t* body)
{
	body->flags &= (uchar)~PHYS_FLAG_SLEEPING;
	body->sleepTimer = 0.0f;
}

void PHYS_Sleep(rigidbody_t* body)
{
	if (body->flags & PHYS_FLAG_NO_SLEEP)
		return;

	body->flags |= PHYS_FLAG_SLEEPING;
	body->sleepTimer = 0.0f;
	vectorClear(body->velocity);
}

uchar PHYS_IsGrounded(const rigidbody_t* body)
{
	return (uchar)((body->flags & PHYS_FLAG_GROUNDED) != 0);
}

/* ========================================================================= */
/* Integration                                                               */
/* ========================================================================= */

static uchar PHYS_SweepAgainstWorld(const collider_t* collider, const vec3_t from, const vec3_t to,
									ushort mask, const rigidbody_t* ignore, uchar staticOnly,
									raycast_hit_t* hit);

static void PHYS_IntegrateForces(float dt)
{
	rigidbody_t* body;
	vec3_t acceleration;
	float damping;
	int i, id;

	for(i = 0 ; i < numSorted ; i++)
	{
		id = sorted[i];
		body = &bodies[id];

		/* Contacts of the previous step are stale from here on. */
		body->flags &= (uchar)~PHYS_FLAG_GROUNDED;
		vectorClear(body->groundNormal);
		vectorCopy(body->position, body->prevPosition);

		if (!PHYS_IsSimulated(body) || body->type != PHYS_DYNAMIC)
			continue;

		if (body->flags & PHYS_FLAG_SLEEPING)
		{
			vectorClear(body->force);
			continue;
		}

		vectorScale(gravity, body->gravityScale, acceleration);
		vectorMA(acceleration, body->invMass, body->force, acceleration);
		vectorMA(body->velocity, dt, acceleration, body->velocity);

		if (body->damping > 0.0f)
		{
			damping = DE_RECIP(1.0f + body->damping * dt);
			vectorScale(body->velocity, damping, body->velocity);
		}

		vectorClear(body->force);
		PHYS_ClampVelocity(body);
	}
}

static void PHYS_IntegratePositions(float dt)
{
	rigidbody_t* body;
	raycast_hit_t hit;
	vec3_t target, motion;
	float fraction;
	int i, id;

	for(i = 0 ; i < numSorted ; i++)
	{
		id = sorted[i];
		body = &bodies[id];

		if (!PHYS_IsSimulated(body) || !PHYS_IsMover(body))
			continue;

		vectorMA(body->position, dt, body->velocity, target);

		if ((body->flags & PHYS_FLAG_CCD) && !(body->flags & PHYS_FLAG_TRIGGER))
		{
			/*
			 * Fast movers would tunnel through thin geometry in a single step.
			 * Sweep the collider against the static world and stop the body on
			 * the first surface it would have crossed. The solver picks the
			 * contact up on the next step.
			 */
			if (PHYS_SweepAgainstWorld(&body->collider, body->position, target,
									   body->mask, body, 1, &hit))
			{
				fraction = hit.fraction;
				if (fraction < 0.0f)
					fraction = 0.0f;

				vectorSubtract(target, body->position, motion);
				vectorMA(body->position, fraction, motion, target);

				/*
				 * Bite a hair into the surface. The overlap stays well below
				 * the solver slop, but it guarantees the narrow phase sees the
				 * touch on the next step, so the impact is solved and reported
				 * like any other contact instead of the body just freezing.
				 */
				vectorMA(target, -PHYS_SKIN * 2.0f, hit.normal, target);
			}
		}

		vectorCopy(target, body->position);
	}
}

/* ========================================================================= */
/* Broad phase: sweep and prune along X                                      */
/* ========================================================================= */

static uchar PHYS_ShouldCollide(const rigidbody_t* a, const rigidbody_t* b)
{
	if (!(a->layer & b->mask) || !(b->layer & a->mask))
		return 0;

	/* Two bodies that cannot move cannot start touching. */
	if (!PHYS_IsMover(a) && !PHYS_IsMover(b))
		return 0;

	return 1;
}

static void PHYS_Broadphase(void)
{
	rigidbody_t *a, *b;
	float keyMin, reach;
	ushort key;
	int i, j, idA, idB;

	numPairs = 0;

	for(i = 0 ; i < numSorted ; i++)
	{
		idA = sorted[i];
		COL_GetAABB(&bodies[idA].collider, bodies[idA].position, &bodyBounds[idA]);
	}

	/* Insertion sort: the list is almost sorted already from the last step. */
	for(i = 1 ; i < numSorted ; i++)
	{
		key = sorted[i];
		keyMin = bodyBounds[key].min[X];

		j = i - 1;
		while (j >= 0 && bodyBounds[sorted[j]].min[X] > keyMin)
		{
			sorted[j + 1] = sorted[j];
			j--;
		}

		sorted[j + 1] = key;
	}

	for(i = 0 ; i < numSorted ; i++)
	{
		idA = sorted[i];
		a = &bodies[idA];

		if (!PHYS_IsSimulated(a))
			continue;

		reach = bodyBounds[idA].max[X];

		for(j = i + 1 ; j < numSorted ; j++)
		{
			idB = sorted[j];

			/* Sorted on min X: nothing further along can overlap either. */
			if (bodyBounds[idB].min[X] > reach)
				break;

			b = &bodies[idB];

			if (!PHYS_IsSimulated(b))
				continue;

			if (!PHYS_ShouldCollide(a, b))
				continue;

			if (!COL_AABBOverlap(&bodyBounds[idA], &bodyBounds[idB]))
				continue;

			if (numPairs >= PHYS_MAX_PAIRS)
			{
				printf("[PHYS] Broad phase pair overflow (%d).\n", PHYS_MAX_PAIRS);
				return;
			}

			pairs[numPairs].a = (ushort)idA;
			pairs[numPairs].b = (ushort)idB;
			numPairs++;
		}
	}
}

/* ========================================================================= */
/* Narrow phase                                                              */
/* ========================================================================= */

static void PHYS_MarkGround(rigidbody_t* body, const vec3_t normal, float sign)
{
	vec3_t oriented;

	if (body->type != PHYS_DYNAMIC)
		return;

	vectorScale(normal, sign, oriented);

	if (DotProduct(oriented, upVector) < PHYS_GROUND_DOT)
		return;

	body->flags |= PHYS_FLAG_GROUNDED;
	vectorCopy(oriented, body->groundNormal);
}

static void PHYS_Narrowphase(void)
{
	rigidbody_t *a, *b;
	phys_contact_t* contact;
	contact_t manifold;
	vec3_t relative;
	float approach;
	int i;

	numContacts = 0;

	for(i = 0 ; i < numPairs ; i++)
	{
		a = &bodies[pairs[i].a];
		b = &bodies[pairs[i].b];

		if (!COL_Collide(&a->collider, a->position, &b->collider, b->position, &manifold))
			continue;

		/* Anything touching a sleeping body wakes it up. */
		if ((a->flags & PHYS_FLAG_SLEEPING) && PHYS_IsMover(b))
			PHYS_Wake(a);
		if ((b->flags & PHYS_FLAG_SLEEPING) && PHYS_IsMover(a))
			PHYS_Wake(b);

		if ((a->flags | b->flags) & PHYS_FLAG_TRIGGER)
		{
			if (triggerCallback)
				triggerCallback(a, b, &manifold);

			continue;
		}

		if (numContacts >= PHYS_MAX_CONTACTS)
		{
			printf("[PHYS] Contact overflow (%d).\n", PHYS_MAX_CONTACTS);
			break;
		}

		contact = &contacts[numContacts++];
		contact->a = pairs[i].a;
		contact->b = pairs[i].b;
		contact->key = PHYS_ContactKey(pairs[i].a, pairs[i].b);
		contact->manifold = manifold;
		contact->normalImpulse = PHYS_LookupImpulse(contact->key);
		contact->tangentImpulse = 0.0f;

		/*
		 * Two infinite masses cannot be pushed apart. The contact is still
		 * recorded so the game gets its callback, the solver skips it.
		 */
		contact->normalMass = (a->invMass + b->invMass > 0.0f)
							? DE_RECIP(a->invMass + b->invMass)
							: 0.0f;

		/*
		 * Restitution is evaluated once, on the approach speed the pair had
		 * before the solver touched it. Slow contacts do not bounce at all,
		 * otherwise resting bodies buzz forever.
		 */
		vectorSubtract(b->velocity, a->velocity, relative);
		approach = DotProduct(relative, manifold.normal);

		if (approach < -PHYS_RESTITUTION_THRESHOLD)
		{
			/* The bounciest of the two wins, so a rubber ball bounces on plain ground. */
			float restitution = (a->restitution > b->restitution) ? a->restitution : b->restitution;
			contact->restitutionTarget = -restitution * approach;
		}
		else
			contact->restitutionTarget = 0.0f;

		/* The normal points from a to b, so a is the one standing on b. */
		PHYS_MarkGround(a, manifold.normal, -1.0f);
		PHYS_MarkGround(b, manifold.normal, 1.0f);
	}
}

/* ========================================================================= */
/* Solver                                                                    */
/* ========================================================================= */

/* Replays the impulses of the previous step so the solver starts warm. */
static void PHYS_WarmStart(void)
{
	rigidbody_t *a, *b;
	phys_contact_t* contact;
	vec3_t impulse;
	int i;

	for(i = 0 ; i < numContacts ; i++)
	{
		contact = &contacts[i];

		if (contact->normalMass <= 0.0f || contact->normalImpulse == 0.0f)
			continue;

		a = &bodies[contact->a];
		b = &bodies[contact->b];

		vectorScale(contact->manifold.normal, contact->normalImpulse, impulse);

		if (a->invMass > 0.0f)
			vectorMA(a->velocity, -a->invMass, impulse, a->velocity);
		if (b->invMass > 0.0f)
			vectorMA(b->velocity, b->invMass, impulse, b->velocity);
	}
}

static void PHYS_StoreImpulses(void)
{
	int i;

	PHYS_ClearCache();

	for(i = 0 ; i < numContacts ; i++)
	{
		if (contacts[i].normalImpulse > 0.0f)
			PHYS_StoreImpulse(contacts[i].key, contacts[i].normalImpulse);
	}
}

static void PHYS_SolveVelocities(void)
{
	rigidbody_t *a, *b;
	phys_contact_t* contact;
	vec3_t relative, tangent, impulse;
	float normalVelocity, tangentVelocity, lambda, previous;
	float friction, maxFriction, length, inverse;
	int iteration, i;

	for(iteration = 0 ; iteration < solverIterations ; iteration++)
	{
		for(i = 0 ; i < numContacts ; i++)
		{
			contact = &contacts[i];

			if (contact->normalMass <= 0.0f)
				continue;

			a = &bodies[contact->a];
			b = &bodies[contact->b];

			/* --- normal impulse --- */
			vectorSubtract(b->velocity, a->velocity, relative);
			normalVelocity = DotProduct(relative, contact->manifold.normal);

			lambda = (contact->restitutionTarget - normalVelocity) * contact->normalMass;

			/*
			 * Clamping the accumulated impulse instead of the increment lets
			 * later iterations undo an earlier overshoot, which is what makes
			 * small stacks settle in only a few iterations.
			 */
			previous = contact->normalImpulse;
			contact->normalImpulse = previous + lambda;
			if (contact->normalImpulse < 0.0f)
				contact->normalImpulse = 0.0f;
			lambda = contact->normalImpulse - previous;

			vectorScale(contact->manifold.normal, lambda, impulse);

			if (a->invMass > 0.0f)
				vectorMA(a->velocity, -a->invMass, impulse, a->velocity);
			if (b->invMass > 0.0f)
				vectorMA(b->velocity, b->invMass, impulse, b->velocity);

			/* --- friction impulse --- */
			vectorSubtract(b->velocity, a->velocity, relative);
			normalVelocity = DotProduct(relative, contact->manifold.normal);
			vectorMA(relative, -normalVelocity, contact->manifold.normal, tangent);

			length = DotProduct(tangent, tangent);

			if (length < COL_EPSILON * COL_EPSILON)
				continue;

			inverse = DE_RECIP(DE_SQRT(length));
			vectorScale(tangent, inverse, tangent);

			tangentVelocity = DotProduct(relative, tangent);
			lambda = -tangentVelocity * contact->normalMass;

			friction = DE_SQRT(a->friction * b->friction);
			maxFriction = friction * contact->normalImpulse;

			previous = contact->tangentImpulse;
			contact->tangentImpulse = previous + lambda;
			if (contact->tangentImpulse > maxFriction)
				contact->tangentImpulse = maxFriction;
			if (contact->tangentImpulse < -maxFriction)
				contact->tangentImpulse = -maxFriction;
			lambda = contact->tangentImpulse - previous;

			vectorScale(tangent, lambda, impulse);

			if (a->invMass > 0.0f)
				vectorMA(a->velocity, -a->invMass, impulse, a->velocity);
			if (b->invMass > 0.0f)
				vectorMA(b->velocity, b->invMass, impulse, b->velocity);
		}
	}
}

/*
 * Removes the penetration the velocity solver could not prevent. The overlap
 * is measured again on every pass instead of being predicted, which costs one
 * cheap shape test per contact but converges in two or three passes even when
 * a tall stack presses down on its bottom box.
 */
static void PHYS_CorrectPositions(void)
{
	rigidbody_t *a, *b;
	phys_contact_t* contact;
	contact_t manifold;
	vec3_t correction;
	float depth, scale;
	int iteration, i;

	for(iteration = 0 ; iteration < positionIterations ; iteration++)
	{
		for(i = 0 ; i < numContacts ; i++)
		{
			contact = &contacts[i];

			if (contact->normalMass <= 0.0f)
				continue;

			a = &bodies[contact->a];
			b = &bodies[contact->b];

			if (!COL_Collide(&a->collider, a->position, &b->collider, b->position, &manifold))
				continue;

			depth = manifold.depth - penetrationSlop;

			if (depth <= 0.0f)
				continue;

			scale = correctionFactor * depth * contact->normalMass;
			vectorScale(manifold.normal, scale, correction);

			if (a->invMass > 0.0f)
				vectorMA(a->position, -a->invMass, correction, a->position);
			if (b->invMass > 0.0f)
				vectorMA(b->position, b->invMass, correction, b->position);
		}
	}
}

static void PHYS_UpdateSleep(float dt)
{
	rigidbody_t* body;
	float speedSquared;
	int i, id;

	stats.numAwakeBodies = 0;

	for(i = 0 ; i < numSorted ; i++)
	{
		id = sorted[i];
		body = &bodies[id];

		if (!PHYS_IsSimulated(body) || body->type != PHYS_DYNAMIC)
			continue;

		if (body->flags & PHYS_FLAG_SLEEPING)
			continue;

		stats.numAwakeBodies++;

		if (body->flags & PHYS_FLAG_NO_SLEEP)
			continue;

		speedSquared = DotProduct(body->velocity, body->velocity);

		if (speedSquared > sleepThreshold * sleepThreshold)
		{
			body->sleepTimer = 0.0f;
			continue;
		}

		body->sleepTimer += dt;

		if (body->sleepTimer >= sleepTime)
		{
			PHYS_Sleep(body);
			stats.numAwakeBodies--;
		}
	}
}

static void PHYS_FireContactCallbacks(void)
{
	int i;

	if (!contactCallback)
		return;

	for(i = 0 ; i < numContacts ; i++)
		contactCallback(&bodies[contacts[i].a], &bodies[contacts[i].b], &contacts[i].manifold);
}

void PHYS_StepOnce(float dt)
{
	if (dt <= 0.0f)
		return;

	PHYS_IntegrateForces(dt);
	PHYS_IntegratePositions(dt);
	PHYS_Broadphase();
	PHYS_Narrowphase();
	PHYS_WarmStart();
	PHYS_SolveVelocities();
	PHYS_StoreImpulses();
	PHYS_CorrectPositions();
	PHYS_UpdateSleep(dt);
	PHYS_FireContactCallbacks();

	stats.numPairs = numPairs;
	stats.numContacts = numContacts;
}

void PHYS_Update(int deltaMilliseconds)
{
	int steps;

	stats.numSteps = 0;

	if (!initialized || deltaMilliseconds <= 0)
		return;

	if (deltaMilliseconds > PHYS_MAX_FRAME_MS)
		deltaMilliseconds = PHYS_MAX_FRAME_MS;

	accumulator += (float)deltaMilliseconds * 0.001f;

	steps = 0;
	while (accumulator >= timestep && steps < PHYS_MAX_STEPS)
	{
		PHYS_StepOnce(timestep);
		accumulator -= timestep;
		steps++;
	}

	/* Give up on the backlog rather than spiral into slow motion. */
	if (steps >= PHYS_MAX_STEPS)
		accumulator = 0.0f;

	interpolation = accumulator * DE_RECIP(timestep);
	stats.numSteps = steps;
}

/* ========================================================================= */
/* Rendering                                                                 */
/* ========================================================================= */

float PHYS_GetInterpolation(void)
{
	return interpolation;
}

void PHYS_GetRenderPosition(const rigidbody_t* body, vec3_t position)
{
	vector_Interpolate(position, body->prevPosition, interpolation, body->position);
}

void PHYS_BodyToMatrix(const rigidbody_t* body, de_matrix_t matrix)
{
	vec3_t position;

	PHYS_GetRenderPosition(body, position);

	matrix[12] = position[X];
	matrix[13] = position[Y];
	matrix[14] = position[Z];
}

/* ========================================================================= */
/* Queries                                                                   */
/* ========================================================================= */

uchar PHYS_Raycast(const vec3_t origin, const vec3_t direction, float maxDistance,
				   ushort mask, const rigidbody_t* ignore, raycast_hit_t* hit)
{
	rigidbody_t* body;
	trace_t trace;
	float best;
	int i;
	uchar found;

	best = 1.0f;
	found = 0;

	if (hit)
	{
		hit->body = NULL;
		hit->fraction = 1.0f;
		hit->distance = maxDistance;
	}

	for(i = 0 ; i < numSorted ; i++)
	{
		body = &bodies[sorted[i]];

		if (!PHYS_IsSimulated(body) || body == ignore)
			continue;

		if (!(body->layer & mask))
			continue;

		if (!COL_RayVsCollider(origin, direction, maxDistance, &body->collider, body->position, &trace))
			continue;

		if (found && trace.fraction > best)
			continue;

		best = trace.fraction;
		found = 1;

		if (!hit)
			return 1;

		hit->body = body;
		hit->fraction = trace.fraction;
		hit->distance = trace.fraction * maxDistance;
		vectorCopy(trace.point, hit->point);
		vectorCopy(trace.normal, hit->normal);
	}

	return found;
}

static int PHYS_OverlapCollider(const collider_t* collider, const vec3_t position, ushort mask,
								rigidbody_t** results, int maxResults)
{
	rigidbody_t* body;
	int i, count;

	count = 0;

	for(i = 0 ; i < numSorted && count < maxResults ; i++)
	{
		body = &bodies[sorted[i]];

		if (!PHYS_IsSimulated(body))
			continue;

		if (!(body->layer & mask))
			continue;

		if (!COL_Collide(collider, position, &body->collider, body->position, NULL))
			continue;

		results[count++] = body;
	}

	return count;
}

int PHYS_OverlapSphere(const vec3_t center, float radius, ushort mask,
					   rigidbody_t** results, int maxResults)
{
	collider_t collider;

	COL_MakeSphere(&collider, radius);

	return PHYS_OverlapCollider(&collider, center, mask, results, maxResults);
}

int PHYS_OverlapBox(const vec3_t center, const vec3_t halfExtents, ushort mask,
					rigidbody_t** results, int maxResults)
{
	collider_t collider;

	COL_MakeBox(&collider, halfExtents[X], halfExtents[Y], halfExtents[Z]);

	return PHYS_OverlapCollider(&collider, center, mask, results, maxResults);
}

static uchar PHYS_SweepAgainstWorld(const collider_t* collider, const vec3_t from, const vec3_t to,
									ushort mask, const rigidbody_t* ignore, uchar staticOnly,
									raycast_hit_t* hit)
{
	rigidbody_t* body;
	trace_t trace;
	aabb_t sweptBounds, endBounds, bodyBox;
	vec3_t motion;
	float best;
	int i;
	uchar found;

	found = 0;
	best = 1.0f;

	if (hit)
	{
		hit->body = NULL;
		hit->fraction = 1.0f;
	}

	vectorSubtract(to, from, motion);

	/* Bounds of the whole motion, used to reject most of the world cheaply. */
	COL_GetAABB(collider, from, &sweptBounds);
	COL_GetAABB(collider, to, &endBounds);
	COL_EncloseAABB(&sweptBounds, &endBounds);

	for(i = 0 ; i < numSorted ; i++)
	{
		body = &bodies[sorted[i]];

		if (!PHYS_IsSimulated(body) || body == ignore)
			continue;

		if (body->flags & PHYS_FLAG_TRIGGER)
			continue;

		if (!(body->layer & mask))
			continue;

		if (staticOnly && body->type == PHYS_DYNAMIC)
			continue;

		COL_GetAABB(&body->collider, body->position, &bodyBox);

		if (!COL_AABBOverlap(&sweptBounds, &bodyBox))
			continue;

		if (!COL_CastCollider(collider, from, to, &body->collider, body->position, &trace))
			continue;

		if (trace.fraction >= best)
			continue;

		best = trace.fraction;
		found = 1;

		if (!hit)
			return 1;

		hit->body = body;
		hit->fraction = trace.fraction;
		hit->distance = trace.fraction * DE_SQRT(DotProduct(motion, motion));
		vectorCopy(trace.point, hit->point);
		vectorCopy(trace.normal, hit->normal);
	}

	return found;
}

uchar PHYS_SweepCollider(const collider_t* collider, const vec3_t from, const vec3_t to,
						 ushort mask, const rigidbody_t* ignore, raycast_hit_t* hit)
{
	return PHYS_SweepAgainstWorld(collider, from, to, mask, ignore, 0, hit);
}

uchar PHYS_SweepBody(const rigidbody_t* body, const vec3_t delta, ushort mask, raycast_hit_t* hit)
{
	vec3_t target;

	vectorAdd(body->position, delta, target);

	return PHYS_SweepAgainstWorld(&body->collider, body->position, target, mask, body, 0, hit);
}

/*
 * Pushes a body out of anything it is already stuck in. Without this a
 * character that spawned inside geometry, or that got squeezed by a moving
 * platform, would sweep against a surface it is behind and refuse to move.
 */
static void PHYS_ResolvePenetration(rigidbody_t* body, ushort mask)
{
	rigidbody_t* other;
	contact_t manifold;
	int i;

	for(i = 0 ; i < numSorted ; i++)
	{
		other = &bodies[sorted[i]];

		if (!PHYS_IsSimulated(other) || other == body)
			continue;

		if (other->flags & PHYS_FLAG_TRIGGER)
			continue;

		if (!(other->layer & mask))
			continue;

		if (!COL_Collide(&body->collider, body->position, &other->collider, other->position, &manifold))
			continue;

		/* The normal points from the body to the obstacle, so back out of it. */
		vectorMA(body->position, -(manifold.depth + PHYS_SKIN), manifold.normal, body->position);
	}
}

int PHYS_MoveAndSlide(rigidbody_t* body, const vec3_t delta, ushort mask)
{
	raycast_hit_t hit;
	vec3_t remaining, target, motion;
	float fraction, into;
	int slides, iteration;

	vectorCopy(delta, remaining);
	slides = 0;

	body->flags &= (uchar)~PHYS_FLAG_GROUNDED;

	PHYS_ResolvePenetration(body, mask);

	for(iteration = 0 ; iteration < PHYS_MAX_SLIDES ; iteration++)
	{
		if (DotProduct(remaining, remaining) < COL_EPSILON * COL_EPSILON)
			break;

		vectorAdd(body->position, remaining, target);

		if (!PHYS_SweepAgainstWorld(&body->collider, body->position, target, mask, body, 0, &hit))
		{
			vectorCopy(target, body->position);
			break;
		}

		/* Advance up to the surface, keeping a thin gap. */
		fraction = hit.fraction - PHYS_SKIN;
		if (fraction < 0.0f)
			fraction = 0.0f;

		vectorSubtract(target, body->position, motion);
		vectorMA(body->position, fraction, motion, body->position);

		/* Project what is left of the motion onto the surface. */
		vectorScale(motion, 1.0f - fraction, remaining);
		into = DotProduct(remaining, hit.normal);
		vectorMA(remaining, -into, hit.normal, remaining);

		/* Kill the velocity that went into the surface as well. */
		into = DotProduct(body->velocity, hit.normal);
		if (into < 0.0f)
			vectorMA(body->velocity, -into, hit.normal, body->velocity);

		if (DotProduct(hit.normal, upVector) >= PHYS_GROUND_DOT)
		{
			body->flags |= PHYS_FLAG_GROUNDED;
			vectorCopy(hit.normal, body->groundNormal);
		}

		slides++;
	}

	PHYS_Wake(body);

	return slides;
}
