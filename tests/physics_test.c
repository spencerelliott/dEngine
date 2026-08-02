/*
 *  physics_test.c
 *  dEngine
 *
 *  Host side test suite for the collision and physics modules. It does not
 *  need a Dreamcast, a renderer or any asset: it only links collision.c and
 *  physics.c, which is exactly the point, the simulation has no dependency on
 *  the rest of the engine.
 *
 *      cmake -DBUILD_PHYSICS_TESTS=ON .. && make dEngine_physics_test
 *      ./dEngine_physics_test
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "collision.h"
#include "physics.h"

static int checksRun;
static int checksFailed;
static const char* currentTest;

#define CHECK(condition, message)											\
	do {																	\
		checksRun++;														\
		if (!(condition)) {													\
			checksFailed++;													\
			printf("  FAIL [%s] %s (line %d)\n", currentTest, message, __LINE__);	\
		}																	\
	} while(0)

#define CHECK_NEAR(value, expected, tolerance, message)						\
	do {																	\
		checksRun++;														\
		if (!(fabsf((value) - (expected)) <= (tolerance))) {					\
			checksFailed++;													\
			printf("  FAIL [%s] %s: got %.4f expected %.4f +/- %.4f (line %d)\n",	\
				   currentTest, message, (double)(value), (double)(expected),		\
				   (double)(tolerance), __LINE__);							\
		}																	\
	} while(0)

static const float step = 1.0f / 60.0f;

static void RunSteps(int count)
{
	int i;

	for(i = 0 ; i < count ; i++)
		PHYS_StepOnce(step);
}

/* A 100x2x100 static slab whose top face sits at y = 0. */
static rigidbody_t* MakeGround(void)
{
	collider_t collider;
	vec3_t position = { 0.0f, -1.0f, 0.0f };

	COL_MakeBox(&collider, 50.0f, 1.0f, 50.0f);

	return PHYS_CreateBody(PHYS_STATIC, position, &collider);
}

static rigidbody_t* MakeBox(uchar type, float x, float y, float z, float half)
{
	collider_t collider;
	vec3_t position;

	vectorSet(position, x, y, z);
	COL_MakeBox(&collider, half, half, half);

	return PHYS_CreateBody(type, position, &collider);
}

static rigidbody_t* MakeSphere(uchar type, float x, float y, float z, float radius)
{
	collider_t collider;
	vec3_t position;

	vectorSet(position, x, y, z);
	COL_MakeSphere(&collider, radius);

	return PHYS_CreateBody(type, position, &collider);
}

/* ========================================================================= */
/* Collision primitives                                                      */
/* ========================================================================= */

static void Test_Bounds(void)
{
	collider_t sphere, box;
	aabb_t sphereBounds, boxBounds;
	vec3_t position = { 1.0f, 2.0f, 3.0f };

	currentTest = "bounds";

	COL_MakeSphere(&sphere, 2.0f);
	COL_GetAABB(&sphere, position, &sphereBounds);

	CHECK_NEAR(sphereBounds.min[X], -1.0f, 0.001f, "sphere min x");
	CHECK_NEAR(sphereBounds.max[Y], 4.0f, 0.001f, "sphere max y");

	COL_MakeBox(&box, 1.0f, 2.0f, 3.0f);
	COL_GetAABB(&box, position, &boxBounds);

	CHECK_NEAR(boxBounds.min[Z], 0.0f, 0.001f, "box min z");
	CHECK_NEAR(boxBounds.max[Z], 6.0f, 0.001f, "box max z");

	CHECK(COL_AABBOverlap(&sphereBounds, &boxBounds), "bounds should overlap");
	CHECK(COL_AABBContainsPoint(&boxBounds, position), "center is inside the box");

	sphereBounds.min[X] += 100.0f;
	sphereBounds.max[X] += 100.0f;
	CHECK(!COL_AABBOverlap(&sphereBounds, &boxBounds), "moved bounds should not overlap");
}

static void Test_SphereVsSphere(void)
{
	collider_t a, b;
	contact_t contact;
	vec3_t posA = { 0.0f, 0.0f, 0.0f };
	vec3_t posB = { 1.5f, 0.0f, 0.0f };
	vec3_t posFar = { 3.0f, 0.0f, 0.0f };

	currentTest = "sphere vs sphere";

	COL_MakeSphere(&a, 1.0f);
	COL_MakeSphere(&b, 1.0f);

	CHECK(COL_Collide(&a, posA, &b, posB, &contact), "overlapping spheres collide");
	CHECK_NEAR(contact.depth, 0.5f, 0.001f, "penetration depth");
	CHECK_NEAR(contact.normal[X], 1.0f, 0.001f, "normal points from a to b");
	CHECK_NEAR(contact.point[X], 0.75f, 0.001f, "contact point sits between the centers");

	CHECK(!COL_Collide(&a, posA, &b, posFar, &contact), "separated spheres do not collide");
}

static void Test_BoxVsBox(void)
{
	collider_t a, b;
	contact_t contact;
	vec3_t posA = { 0.0f, 0.0f, 0.0f };
	vec3_t posB = { 0.4f, 1.8f, 0.0f };

	currentTest = "box vs box";

	COL_MakeBox(&a, 1.0f, 1.0f, 1.0f);
	COL_MakeBox(&b, 1.0f, 1.0f, 1.0f);

	CHECK(COL_Collide(&a, posA, &b, posB, &contact), "overlapping boxes collide");
	CHECK_NEAR(contact.normal[Y], 1.0f, 0.001f, "separates along the shallowest axis");
	CHECK_NEAR(contact.depth, 0.2f, 0.001f, "penetration depth");
}

static void Test_SphereVsBox(void)
{
	collider_t sphere, box;
	contact_t contact;
	vec3_t boxPosition = { 0.0f, 0.0f, 0.0f };
	vec3_t above = { 0.0f, 1.4f, 0.0f };
	vec3_t inside = { 0.0f, 0.3f, 0.0f };
	vec3_t corner = { 1.8f, 1.8f, 0.0f };

	currentTest = "sphere vs box";

	COL_MakeSphere(&sphere, 0.5f);
	COL_MakeBox(&box, 1.0f, 1.0f, 1.0f);

	CHECK(COL_Collide(&sphere, above, &box, boxPosition, &contact), "sphere resting on the face");
	CHECK_NEAR(contact.normal[Y], -1.0f, 0.001f, "normal points down into the box");
	CHECK_NEAR(contact.depth, 0.1f, 0.001f, "face penetration depth");

	CHECK(COL_Collide(&sphere, inside, &box, boxPosition, &contact), "sphere center inside the box");
	CHECK_NEAR(contact.normal[Y], -1.0f, 0.001f, "pushed out through the closest face");
	CHECK_NEAR(contact.depth, 1.2f, 0.001f, "deep penetration depth");

	/* The nearest corner is at (1,1,0), which is sqrt(1.28) ~ 1.13 away. */
	CHECK(!COL_Collide(&sphere, corner, &box, boxPosition, &contact), "corner gap is respected");
}

static void Test_Raycast(void)
{
	collider_t sphere, box;
	trace_t trace;
	vec3_t origin = { -10.0f, 0.0f, 0.0f };
	vec3_t direction = { 1.0f, 0.0f, 0.0f };
	vec3_t away = { -1.0f, 0.0f, 0.0f };
	vec3_t position = { 0.0f, 0.0f, 0.0f };
	vec3_t offset = { 0.0f, 5.0f, 0.0f };

	currentTest = "raycast";

	COL_MakeSphere(&sphere, 1.0f);

	CHECK(COL_RayVsSphere(origin, direction, 20.0f, position, 1.0f, &trace), "ray hits the sphere");
	CHECK_NEAR(trace.point[X], -1.0f, 0.001f, "impact on the near side");
	CHECK_NEAR(trace.normal[X], -1.0f, 0.001f, "normal faces the ray");
	CHECK_NEAR(trace.fraction * 20.0f, 9.0f, 0.001f, "distance travelled");

	CHECK(!COL_RayVsSphere(origin, away, 20.0f, position, 1.0f, &trace), "ray pointing away misses");
	CHECK(!COL_RayVsSphere(origin, direction, 5.0f, position, 1.0f, &trace), "short ray falls short");
	CHECK(!COL_RayVsSphere(offset, direction, 20.0f, position, 1.0f, &trace), "ray above misses");

	COL_MakeBox(&box, 1.0f, 1.0f, 1.0f);
	CHECK(COL_RayVsCollider(origin, direction, 20.0f, &box, position, &trace), "ray hits the box");
	CHECK_NEAR(trace.point[X], -1.0f, 0.001f, "impact on the near face");
	CHECK_NEAR(trace.normal[X], -1.0f, 0.001f, "face normal faces the ray");
	CHECK(!COL_RayVsCollider(offset, direction, 20.0f, &box, position, &trace), "ray above the box misses");
}

static void Test_ShapeCast(void)
{
	collider_t moving, fixed;
	trace_t trace;
	vec3_t from = { -10.0f, 0.0f, 0.0f };
	vec3_t to = { 10.0f, 0.0f, 0.0f };
	vec3_t fixedPosition = { 0.0f, 0.0f, 0.0f };
	float impact;

	currentTest = "shape cast";

	/* A half unit box sweeping into a one unit box stops 1.5 units short. */
	COL_MakeBox(&moving, 0.5f, 0.5f, 0.5f);
	COL_MakeBox(&fixed, 1.0f, 1.0f, 1.0f);

	CHECK(COL_CastCollider(&moving, from, to, &fixed, fixedPosition, &trace), "box sweep hits");

	impact = from[X] + (to[X] - from[X]) * trace.fraction;
	CHECK_NEAR(impact, -1.5f, 0.001f, "box stops against the face");
	CHECK_NEAR(trace.normal[X], -1.0f, 0.001f, "impact normal");

	/* Sphere versus sphere is exact as well. */
	COL_MakeSphere(&moving, 0.5f);
	COL_MakeSphere(&fixed, 1.0f);

	CHECK(COL_CastCollider(&moving, from, to, &fixed, fixedPosition, &trace), "sphere sweep hits");
	impact = from[X] + (to[X] - from[X]) * trace.fraction;
	CHECK_NEAR(impact, -1.5f, 0.001f, "sphere stops when the surfaces touch");
}

/* ========================================================================= */
/* Simulation                                                                */
/* ========================================================================= */

static void Test_Gravity(void)
{
	rigidbody_t* body;
	float expected;

	currentTest = "gravity";

	PHYS_Init();
	PHYS_SetGravity(0.0f, -9.81f, 0.0f);

	body = MakeSphere(PHYS_DYNAMIC, 0.0f, 0.0f, 0.0f, 0.5f);
	body->flags |= PHYS_FLAG_NO_SLEEP;

	RunSteps(60);

	/*
	 * Semi implicit Euler accumulates g*dt^2 * n(n+1)/2 rather than the
	 * analytic 0.5*g*t^2, so allow the usual couple of percent.
	 */
	expected = -0.5f * 9.81f;
	CHECK_NEAR(body->position[Y], expected, 0.15f, "fell for one second");
	CHECK_NEAR(body->velocity[Y], -9.81f, 0.2f, "velocity after one second");
	CHECK_NEAR(body->position[X], 0.0f, 0.0001f, "no lateral drift");

	body->gravityScale = 0.0f;
	vectorClear(body->velocity);
	expected = body->position[Y];
	RunSteps(60);
	CHECK_NEAR(body->position[Y], expected, 0.0001f, "gravity scale 0 floats");
}

static void Test_RestingContact(void)
{
	rigidbody_t* box;
	float resting;

	currentTest = "resting contact";

	PHYS_Init();
	MakeGround();

	box = MakeBox(PHYS_DYNAMIC, 0.0f, 5.0f, 0.0f, 0.5f);

	RunSteps(180);

	resting = box->position[Y];
	CHECK_NEAR(resting, 0.5f, 0.05f, "box rests on top of the ground");
	CHECK(resting > 0.44f, "box did not sink through the ground");
	CHECK(PHYS_IsGrounded(box) || (box->flags & PHYS_FLAG_SLEEPING), "box is grounded or asleep");

	/* And it stays there. */
	RunSteps(600);
	CHECK_NEAR(box->position[Y], resting, 0.01f, "resting position is stable");
	CHECK_NEAR(box->position[X], 0.0f, 0.01f, "no lateral creep");
}

static void Test_Restitution(void)
{
	rigidbody_t* bouncy;
	rigidbody_t* dead;
	float peak;
	int i;

	currentTest = "restitution";

	PHYS_Init();
	MakeGround();

	bouncy = MakeSphere(PHYS_DYNAMIC, -2.0f, 3.0f, 0.0f, 0.5f);
	bouncy->restitution = 0.8f;
	bouncy->flags |= PHYS_FLAG_NO_SLEEP;

	dead = MakeSphere(PHYS_DYNAMIC, 2.0f, 3.0f, 0.0f, 0.5f);
	dead->restitution = 0.0f;

	/* Let both land. */
	RunSteps(60);

	peak = 0.0f;
	for(i = 0 ; i < 120 ; i++)
	{
		PHYS_StepOnce(step);

		if (bouncy->position[Y] > peak)
			peak = bouncy->position[Y];
	}

	CHECK(peak > 1.2f, "elastic sphere bounced back up");
	CHECK(peak < 3.0f, "elastic sphere did not gain energy");
	CHECK_NEAR(dead->position[Y], 0.5f, 0.05f, "inelastic sphere stayed put");
}

static void Test_Friction(void)
{
	rigidbody_t* rough;
	rigidbody_t* slippery;
	rigidbody_t* ground;
	vec3_t velocity = { 5.0f, 0.0f, 0.0f };

	currentTest = "friction";

	PHYS_Init();
	ground = MakeGround();
	ground->friction = 1.0f;

	rough = MakeBox(PHYS_DYNAMIC, -20.0f, 0.5f, 0.0f, 0.5f);
	rough->friction = 1.0f;
	PHYS_SetVelocity(rough, velocity);

	slippery = MakeBox(PHYS_DYNAMIC, 20.0f, 0.5f, 0.0f, 0.5f);
	slippery->friction = 0.0f;
	slippery->flags |= PHYS_FLAG_NO_SLEEP;
	PHYS_SetVelocity(slippery, velocity);

	RunSteps(180);

	CHECK(fabsf(rough->velocity[X]) < 0.2f, "friction brought the box to a stop");
	CHECK_NEAR(slippery->velocity[X], 5.0f, 0.2f, "frictionless box kept its speed");
	CHECK(slippery->position[X] > 33.0f, "frictionless box kept sliding");
}

static void Test_Stacking(void)
{
	rigidbody_t* stack[3];
	int i;

	currentTest = "stacking";

	PHYS_Init();
	MakeGround();

	for(i = 0 ; i < 3 ; i++)
		stack[i] = MakeBox(PHYS_DYNAMIC, 0.0f, 0.5f + i * 1.02f, 0.0f, 0.5f);

	RunSteps(300);

	for(i = 0 ; i < 3 ; i++)
	{
		CHECK_NEAR(stack[i]->position[Y], 0.5f + i * 1.0f, 0.08f, "box settled at its stack height");
		CHECK_NEAR(stack[i]->position[X], 0.0f, 0.1f, "stack did not drift sideways");
	}
}

static int triggerEvents;
static rigidbody_t* lastTriggerBody;

static void OnTrigger(rigidbody_t* a, rigidbody_t* b, const contact_t* contact)
{
	(void)contact;

	triggerEvents++;
	lastTriggerBody = (a->flags & PHYS_FLAG_TRIGGER) ? b : a;
}

static void Test_Trigger(void)
{
	rigidbody_t* trigger;
	rigidbody_t* faller;

	currentTest = "trigger";

	PHYS_Init();
	PHYS_SetTriggerCallback(OnTrigger);
	triggerEvents = 0;
	lastTriggerBody = NULL;

	trigger = MakeBox(PHYS_STATIC, 0.0f, 0.0f, 0.0f, 2.0f);
	trigger->flags |= PHYS_FLAG_TRIGGER;

	faller = MakeSphere(PHYS_DYNAMIC, 0.0f, 5.0f, 0.0f, 0.5f);
	faller->flags |= PHYS_FLAG_NO_SLEEP;

	RunSteps(120);

	CHECK(triggerEvents > 0, "trigger reported the overlap");
	CHECK(lastTriggerBody == faller, "trigger reported the right body");
	CHECK(faller->position[Y] < -5.0f, "trigger did not block the fall");

	PHYS_SetTriggerCallback(NULL);
}

static void Test_Layers(void)
{
	rigidbody_t* ground;
	rigidbody_t* ghost;
	rigidbody_t* solid;

	currentTest = "layers";

	PHYS_Init();

	ground = MakeGround();
	PHYS_SetLayer(ground, 0x0001, PHYS_LAYER_ALL);

	/* Only collides with layer 4, so it ignores the ground. */
	ghost = MakeSphere(PHYS_DYNAMIC, -2.0f, 3.0f, 0.0f, 0.5f);
	PHYS_SetLayer(ghost, 0x0002, 0x0004);
	ghost->flags |= PHYS_FLAG_NO_SLEEP;

	solid = MakeSphere(PHYS_DYNAMIC, 2.0f, 3.0f, 0.0f, 0.5f);
	PHYS_SetLayer(solid, 0x0002, PHYS_LAYER_ALL);

	RunSteps(120);

	CHECK(ghost->position[Y] < -1.0f, "masked body fell through");
	CHECK_NEAR(solid->position[Y], 0.5f, 0.05f, "unmasked body landed");
}

static int contactEvents;

static void OnContact(rigidbody_t* a, rigidbody_t* b, const contact_t* contact)
{
	(void)a;
	(void)b;
	(void)contact;

	contactEvents++;
}

static void Test_ContinuousDetection(void)
{
	rigidbody_t* wall;
	rigidbody_t* bullet;
	rigidbody_t* tunneler;
	collider_t collider;
	vec3_t wallPosition = { 0.0f, 0.0f, 0.0f };
	vec3_t velocity = { 600.0f, 0.0f, 0.0f };

	currentTest = "continuous detection";

	PHYS_Init();
	PHYS_SetContactCallback(OnContact);
	contactEvents = 0;

	/* A 10cm thin wall. At 600 units per second a body crosses it in 1/6000s. */
	COL_MakeBox(&collider, 0.05f, 5.0f, 5.0f);
	wall = PHYS_CreateBody(PHYS_STATIC, wallPosition, &collider);
	CHECK(wall != NULL, "wall created");

	bullet = MakeSphere(PHYS_DYNAMIC, -5.0f, 0.0f, 0.0f, 0.1f);
	bullet->gravityScale = 0.0f;
	bullet->flags |= PHYS_FLAG_CCD | PHYS_FLAG_NO_SLEEP;
	PHYS_SetVelocity(bullet, velocity);

	tunneler = MakeSphere(PHYS_DYNAMIC, -5.0f, 2.0f, 0.0f, 0.1f);
	tunneler->gravityScale = 0.0f;
	tunneler->flags |= PHYS_FLAG_NO_SLEEP;
	PHYS_SetVelocity(tunneler, velocity);

	RunSteps(60);

	CHECK(bullet->position[X] < 0.0f, "swept body stopped at the wall");
	CHECK(bullet->position[X] > -0.5f, "swept body reached the wall");
	CHECK(contactEvents > 0, "the impact was reported to the game");
	CHECK(tunneler->position[X] > 5.0f, "without the flag a fast body tunnels through");

	PHYS_SetContactCallback(NULL);
}

static void Test_Queries(void)
{
	rigidbody_t* nearBody;
	rigidbody_t* farBody;
	rigidbody_t* results[8];
	raycast_hit_t hit;
	vec3_t origin = { -20.0f, 0.0f, 0.0f };
	vec3_t direction = { 1.0f, 0.0f, 0.0f };
	vec3_t center = { 0.0f, 0.0f, 0.0f };
	int count;

	currentTest = "queries";

	PHYS_Init();

	nearBody = MakeSphere(PHYS_STATIC, 0.0f, 0.0f, 0.0f, 1.0f);
	farBody = MakeBox(PHYS_STATIC, 10.0f, 0.0f, 0.0f, 1.0f);
	PHYS_SetLayer(farBody, 0x0004, PHYS_LAYER_ALL);

	CHECK(PHYS_Raycast(origin, direction, 100.0f, PHYS_LAYER_ALL, NULL, &hit), "ray found something");
	CHECK(hit.body == nearBody, "ray returned the nearest body");
	CHECK_NEAR(hit.distance, 19.0f, 0.01f, "hit distance");
	CHECK_NEAR(hit.normal[X], -1.0f, 0.01f, "hit normal");

	CHECK(PHYS_Raycast(origin, direction, 100.0f, PHYS_LAYER_ALL, nearBody, &hit), "ignored body is skipped");
	CHECK(hit.body == farBody, "ray fell through to the far body");

	CHECK(PHYS_Raycast(origin, direction, 100.0f, 0x0004, NULL, &hit), "mask selects the far body");
	CHECK(hit.body == farBody, "mask filtered the near body out");

	CHECK(!PHYS_Raycast(origin, direction, 5.0f, PHYS_LAYER_ALL, NULL, &hit), "short ray hits nothing");

	count = PHYS_OverlapSphere(center, 2.0f, PHYS_LAYER_ALL, results, 8);
	CHECK(count == 1, "one body overlaps the small sphere");
	CHECK(count == 1 && results[0] == nearBody, "overlap returned the near body");

	count = PHYS_OverlapSphere(center, 20.0f, PHYS_LAYER_ALL, results, 8);
	CHECK(count == 2, "both bodies overlap the big sphere");
}

static void Test_MoveAndSlide(void)
{
	rigidbody_t* player;
	vec3_t wall = { 2.0f, 0.0f, 0.0f };
	vec3_t straight = { 3.0f, 0.0f, 0.0f };
	vec3_t diagonal = { 3.0f, 0.0f, 3.0f };
	collider_t collider;
	int slides;

	currentTest = "move and slide";

	PHYS_Init();

	COL_MakeBox(&collider, 0.5f, 5.0f, 20.0f);
	PHYS_CreateBody(PHYS_STATIC, wall, &collider);

	player = MakeSphere(PHYS_KINEMATIC, 0.0f, 0.0f, 0.0f, 0.5f);

	slides = PHYS_MoveAndSlide(player, straight, PHYS_LAYER_ALL);
	CHECK(slides > 0, "the move was blocked");
	CHECK_NEAR(player->position[X], 1.0f, 0.02f, "stopped against the wall");
	CHECK_NEAR(player->position[Z], 0.0f, 0.001f, "no sideways motion");

	PHYS_SetPosition(player, (vec3_t){ 0.0f, 0.0f, -3.0f });
	PHYS_MoveAndSlide(player, diagonal, PHYS_LAYER_ALL);
	CHECK_NEAR(player->position[X], 1.0f, 0.02f, "diagonal move stopped at the wall");
	CHECK(player->position[Z] > -0.5f, "diagonal move slid along the wall");
}

static void Test_Sleeping(void)
{
	rigidbody_t* sleeper;
	rigidbody_t* hammer;
	vec3_t push = { 6.0f, 0.0f, 0.0f };

	currentTest = "sleeping";

	PHYS_Init();
	MakeGround();

	sleeper = MakeBox(PHYS_DYNAMIC, 0.0f, 2.0f, 0.0f, 0.5f);

	RunSteps(300);
	CHECK(sleeper->flags & PHYS_FLAG_SLEEPING, "settled body went to sleep");

	hammer = MakeBox(PHYS_DYNAMIC, -3.0f, 0.5f, 0.0f, 0.5f);
	hammer->friction = 0.0f;
	PHYS_SetVelocity(hammer, push);

	RunSteps(60);

	CHECK(!(sleeper->flags & PHYS_FLAG_SLEEPING), "impact woke the sleeping body");
	CHECK(sleeper->position[X] > 0.1f, "the sleeping body was pushed");
}

static void Test_Kinematic(void)
{
	rigidbody_t* platform;
	rigidbody_t* crate;
	vec3_t velocity = { 0.0f, 1.0f, 0.0f };

	currentTest = "kinematic";

	PHYS_Init();

	platform = MakeBox(PHYS_KINEMATIC, 0.0f, 0.0f, 0.0f, 1.0f);
	PHYS_SetVelocity(platform, velocity);

	crate = MakeBox(PHYS_DYNAMIC, 0.0f, 1.6f, 0.0f, 0.5f);
	crate->flags |= PHYS_FLAG_NO_SLEEP;
	crate->gravityScale = 0.0f;

	RunSteps(60);

	CHECK_NEAR(platform->position[Y], 1.0f, 0.02f, "platform moved on its own");
	CHECK(crate->position[Y] > 1.9f, "the crate was pushed up by the platform");
	CHECK(platform->position[Y] < 1.02f, "the crate did not slow the platform down");
}

static void Test_PoolAndDeterminism(void)
{
	rigidbody_t* body;
	rigidbody_t* recycled;
	float first, second;
	int i, created;

	currentTest = "pool and determinism";

	PHYS_Init();

	created = 0;
	for(i = 0 ; i < PHYS_MAX_BODIES + 2 ; i++)
	{
		body = MakeSphere(PHYS_DYNAMIC, (float)i, 0.0f, 0.0f, 0.1f);
		if (body)
			created++;
	}

	CHECK(created == PHYS_MAX_BODIES, "the pool handed out exactly its capacity");

	body = PHYS_GetBody(0);
	CHECK(body != NULL, "body 0 is alive");
	PHYS_DestroyBody(body);
	CHECK(PHYS_GetBody(0) == NULL, "destroyed body is gone");

	recycled = MakeSphere(PHYS_DYNAMIC, 0.0f, 0.0f, 0.0f, 0.1f);
	CHECK(recycled != NULL, "the freed slot was recycled");

	/* Same setup, same steps, same answer. */
	PHYS_Init();
	MakeGround();
	body = MakeBox(PHYS_DYNAMIC, 0.3f, 4.0f, -0.2f, 0.5f);
	RunSteps(240);
	first = body->position[Y];

	PHYS_Init();
	MakeGround();
	body = MakeBox(PHYS_DYNAMIC, 0.3f, 4.0f, -0.2f, 0.5f);
	RunSteps(240);
	second = body->position[Y];

	CHECK_NEAR(first, second, 0.0f, "the simulation is deterministic");
	CHECK(first == first, "no NaN crept in");
}

static void Test_FixedStepAccumulator(void)
{
	rigidbody_t* body;
	vec3_t position;
	const phys_stats_t* physStats;

	currentTest = "fixed step";

	PHYS_Init();
	PHYS_SetTimestep(1.0f / 60.0f);

	body = MakeSphere(PHYS_DYNAMIC, 0.0f, 0.0f, 0.0f, 0.5f);
	body->flags |= PHYS_FLAG_NO_SLEEP;

	/* A 33ms frame is two steps of 1/60 with a bit left over. */
	PHYS_Update(33);
	physStats = PHYS_GetStats();
	CHECK(physStats->numSteps == 1, "a 33ms frame runs one step and banks the rest");

	PHYS_Update(33);
	physStats = PHYS_GetStats();
	CHECK(physStats->numSteps == 2, "the banked time produces a second step");

	/* A huge hitch must not explode into hundreds of steps. */
	PHYS_Update(100000);
	physStats = PHYS_GetStats();
	CHECK(physStats->numSteps <= 5, "a long hitch is clamped");

	CHECK(PHYS_GetInterpolation() >= 0.0f && PHYS_GetInterpolation() <= 1.0f, "interpolation stays in range");

	PHYS_GetRenderPosition(body, position);
	CHECK(position[Y] <= body->prevPosition[Y] && position[Y] >= body->position[Y],
		  "the render position sits between the two last steps");
}

static void Test_BodyToMatrix(void)
{
	rigidbody_t* body;
	de_matrix_t matrix;

	currentTest = "body to matrix";

	PHYS_Init();

	body = MakeSphere(PHYS_DYNAMIC, 1.0f, 2.0f, 3.0f, 0.5f);
	matrixLoadIdentity(matrix);
	matrix[0] = 2.0f;

	PHYS_BodyToMatrix(body, matrix);

	CHECK_NEAR(matrix[12], 1.0f, 0.001f, "translation x");
	CHECK_NEAR(matrix[13], 2.0f, 0.001f, "translation y");
	CHECK_NEAR(matrix[14], 3.0f, 0.001f, "translation z");
	CHECK_NEAR(matrix[0], 2.0f, 0.001f, "the rest of the matrix is untouched");
}

/* matrixLoadIdentity lives in matrix.c, which pulls in the SH4 math header.
   Keep the test binary standalone with a local copy. */
void matrixLoadIdentity(de_matrix_t m)
{
	int i;

	for(i = 0 ; i < 16 ; i++)
		m[i] = 0.0f;

	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

int main(void)
{
	printf("dEngine physics tests\n");
	printf("---------------------\n");

	Test_Bounds();
	Test_SphereVsSphere();
	Test_BoxVsBox();
	Test_SphereVsBox();
	Test_Raycast();
	Test_ShapeCast();

	Test_Gravity();
	Test_RestingContact();
	Test_Restitution();
	Test_Friction();
	Test_Stacking();
	Test_Trigger();
	Test_Layers();
	Test_ContinuousDetection();
	Test_Queries();
	Test_MoveAndSlide();
	Test_Sleeping();
	Test_Kinematic();
	Test_PoolAndDeterminism();
	Test_FixedStepAccumulator();
	Test_BodyToMatrix();

	printf("---------------------\n");
	printf("%d checks, %d failures\n", checksRun, checksFailed);

	return checksFailed ? 1 : 0;
}
