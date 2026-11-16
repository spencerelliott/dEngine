/*
 *  physics_playground.c
 *  dEngine example: rigid bodies
 *
 *  Hell knights rain onto the floor of the tech demo and pile up. The physics
 *  module (physics.h) owns where they are; the game object layer (dgame.h) only
 *  draws them where physics says.
 *
 *  Shows:
 *    - setting the world up for a map authored in inches (gravity, slop, sleep)
 *    - static, dynamic and kinematic bodies
 *    - mass, friction, restitution
 *    - copying the interpolated body position onto a game object every frame
 *    - PHYS_Raycast and PHYS_ApplyImpulse to shove whatever is in front of you
 *    - PHYS_OverlapSphere for a blast
 *    - the contact callback and PHYS_GetStats
 *    - resetting the simulation around a scene swap
 *
 *  Controls (port 0), on top of the free fly camera:
 *    A      drop a knight in front of the camera
 *    B      shove whatever the camera is looking at
 *    X      blast: knock everything near the camera away
 *    Y      remove every knight
 *
 *  Build:  ./dc-build.sh --example physics_playground
 */

#include <dgame.h>
#include <physics.h>

#include <math.h>

#define KNIGHT_MESH "data/models/monsters/hellKnight/hellknight.pvr.2bpp.md5mesh"
#define KNIGHT_ANIM "data/models/monsters/hellKnight/hellknight_idle2.md5anim"

/* Every knight is skinned on the CPU, so keep the crowd small. */
#define MAX_KNIGHTS 6

/* The knight's origin is at its feet. Rough bounds, in inches. */
#define KNIGHT_HALF_WIDTH   24.0f
#define KNIGHT_HALF_HEIGHT  48.0f

/* Layers. Every body has one, and a mask of what it collides with. */
#define LAYER_WORLD    0x0001
#define LAYER_KNIGHT   0x0002
#define LAYER_PLATFORM 0x0004

typedef struct knight_t {
	dg_object_t  object;
	rigidbody_t* body;
} knight_t;

static knight_t     g_knights[MAX_KNIGHTS];
static rigidbody_t* g_floor;
static rigidbody_t* g_platform;
static float        g_platformTime;
static int          g_impactsThisFrame;

static dg_action_t g_actDrop;
static dg_action_t g_actShove;
static dg_action_t g_actBlast;
static dg_action_t g_actClear;

/* -------------------------------------------------------------------------
 *  Callbacks
 * ------------------------------------------------------------------------- */

/* Fires once per step for every touching pair, after the solver. The physics
   module reports "touching", not "started touching", so this simply counts
   contacts; see character_controller for enter/exit tracking. Never create or
   destroy bodies from in here. */
static void OnContact(rigidbody_t* a, rigidbody_t* b, const contact_t* contact)
{
	(void)a;
	(void)b;

	if (contact->depth > 0.0f)
		g_impactsThisFrame++;
}

/* -------------------------------------------------------------------------
 *  Knights
 * ------------------------------------------------------------------------- */

static knight_t* FreeKnightSlot(void)
{
	int i;

	for (i = 0; i < MAX_KNIGHTS; i++)
	{
		if (!g_knights[i].body)
			return &g_knights[i];
	}

	return 0;
}

static void DropKnight(float x, float y, float z)
{
	knight_t* knight = FreeKnightSlot();
	collider_t collider;
	vec3_t position;

	if (!knight)
	{
		Game_Log("[physics] all %d knights are out already\n", MAX_KNIGHTS);
		return;
	}

	knight->object = GameObject_SpawnAnimated(KNIGHT_MESH, KNIGHT_ANIM, x, y, z);

	if (knight->object == DG_INVALID_OBJECT)
		return;

	/* The mesh is authored Z up. Pitch it onto Y up once; position updates
	   below keep this rotation because they go through SetPosition. */
	GameObject_SetRotation(knight->object, 0.0f, (float)(-90.0f * DEG_TO_RAD), 0.0f);

	/* Box collider, offset so the body position stays at the feet like the
	   mesh origin. Bodies never rotate, so an axis aligned box is the whole
	   story. */
	COL_MakeBox(&collider, KNIGHT_HALF_WIDTH, KNIGHT_HALF_HEIGHT, KNIGHT_HALF_WIDTH);
	COL_SetOffset(&collider, 0.0f, KNIGHT_HALF_HEIGHT, 0.0f);

	position[0] = x;
	position[1] = y;
	position[2] = z;

	knight->body = PHYS_CreateBody(PHYS_DYNAMIC, position, &collider);

	if (!knight->body)
	{
		GameObject_Destroy(knight->object);
		knight->object = DG_INVALID_OBJECT;
		return;
	}

	PHYS_SetMass(knight->body, 300.0f);
	PHYS_SetLayer(knight->body, LAYER_KNIGHT, LAYER_WORLD | LAYER_KNIGHT | LAYER_PLATFORM);
	knight->body->friction    = 0.7f;
	knight->body->restitution = 0.2f;
	knight->body->damping     = 0.1f;

	/* userData gets us back from a body (a raycast hit, a callback) to the
	   game side. Store the slot rather than the handle so it is never stale. */
	knight->body->userData = knight;
}

static void RemoveKnight(knight_t* knight)
{
	if (knight->body)
		PHYS_DestroyBody(knight->body);

	GameObject_Destroy(knight->object);

	knight->body = 0;
	knight->object = DG_INVALID_OBJECT;
}

static void RemoveAllKnights(void)
{
	int i;

	for (i = 0; i < MAX_KNIGHTS; i++)
		RemoveKnight(&g_knights[i]);
}

/* -------------------------------------------------------------------------
 *  The level
 * ------------------------------------------------------------------------- */

static void BuildLevel(void)
{
	collider_t collider;
	vec3_t position;

	/* Doom 3 units are inches. Real gravity would be 386 in/s^2, which feels
	   floaty at game scale; 1066 is what Doom 3 itself uses. */
	PHYS_SetGravity(0.0f, -1066.0f, 0.0f);

	/* Slop and sleep threshold have to follow the scale of the world. */
	PHYS_SetSolverParams(4, 3, 0.5f, 0.8f);
	PHYS_SetSleepParams(8.0f, 0.5f);

	PHYS_SetContactCallback(OnContact);

	/* An invisible static slab whose top face is the tech demo floor. */
	COL_MakeBox(&collider, 600.0f, 8.0f, 600.0f);
	position[0] = 0.0f;
	position[1] = -8.0f;
	position[2] = 0.0f;
	g_floor = PHYS_CreateBody(PHYS_STATIC, position, &collider);
	PHYS_SetLayer(g_floor, LAYER_WORLD, PHYS_LAYER_ALL);

	/* A kinematic slab sliding back and forth along the floor. It pushes the
	   knights and nothing pushes it back. Invisible too -- watch the knights
	   get shoved. */
	COL_MakeBox(&collider, 40.0f, 6.0f, 40.0f);
	position[0] = 0.0f;
	position[1] = 6.0f;
	position[2] = 150.0f;
	g_platform = PHYS_CreateBody(PHYS_KINEMATIC, position, &collider);
	PHYS_SetLayer(g_platform, LAYER_PLATFORM, LAYER_KNIGHT);

	g_platformTime = 0.0f;
}

static void MovePlatform(float dt)
{
	vec3_t velocity;

	if (!g_platform)
		return;

	g_platformTime += dt;

	/* Drive a kinematic body through its velocity rather than teleporting it,
	   so the solver knows how hard it is hitting things. */
	velocity[0] = (float)cos(g_platformTime * 0.8f) * 160.0f;
	velocity[1] = 0.0f;
	velocity[2] = 0.0f;
	PHYS_SetVelocity(g_platform, velocity);
}

/* -------------------------------------------------------------------------
 *  Player actions
 * ------------------------------------------------------------------------- */

static void DropInFrontOfCamera(void)
{
	vec3_t eye, forward;

	Game_GetCameraPosition(eye);
	Game_GetCameraForward(forward);

	/* 150 inches ahead, 200 above the floor, whatever the camera pitch. */
	DropKnight(eye[0] + forward[0] * 150.0f,
	           200.0f,
	           eye[2] + forward[2] * 150.0f);
}

static void Shove(void)
{
	raycast_hit_t hit;
	vec3_t eye, forward, impulse;

	Game_GetCameraPosition(eye);
	Game_GetCameraForward(forward);

	/* Only knights: the floor would swallow every miss otherwise. */
	if (!PHYS_Raycast(eye, forward, 2000.0f, LAYER_KNIGHT, 0, &hit))
	{
		Game_Log("[physics] shove: nothing there\n");
		return;
	}

	/* Mostly along the look direction, plus a little lift so it hops. */
	vectorScale(forward, 90000.0f, impulse);
	impulse[1] += 60000.0f;

	PHYS_ApplyImpulse(hit.body, impulse);
	PHYS_Wake(hit.body);

	Game_Log("[physics] shoved knight %d at %.0f inches\n",
	         (int)((knight_t*)hit.body->userData - g_knights), hit.distance);
}

static void Blast(void)
{
	rigidbody_t* touched[MAX_KNIGHTS];
	vec3_t eye, away, impulse;
	float distance;
	int count, i;

	Game_GetCameraPosition(eye);

	count = PHYS_OverlapSphere(eye, 400.0f, LAYER_KNIGHT, touched, MAX_KNIGHTS);

	for (i = 0; i < count; i++)
	{
		vectorSubtract(touched[i]->position, eye, away);
		away[1] = 0.0f;
		distance = (float)sqrt(away[0] * away[0] + away[2] * away[2]);

		if (distance < 1.0f)
			continue;

		/* Stronger close in. */
		vectorScale(away, (1.0f - distance / 400.0f) * 150000.0f / distance, impulse);
		impulse[1] = 80000.0f;

		PHYS_ApplyImpulse(touched[i], impulse);
		PHYS_Wake(touched[i]);
	}

	Game_Log("[physics] blast hit %d knight(s)\n", count);
}

/* -------------------------------------------------------------------------
 *  Hooks
 * ------------------------------------------------------------------------- */

void Game_Configure(dg_config_t* cfg)
{
	cfg->bootScene = "data/scenes/techDemo.scene";
	cfg->debugCamera = 1;
}

void Game_ConfigureInput(void)
{
	g_actDrop  = Input_DefineAction("drop");
	g_actShove = Input_DefineAction("shove");
	g_actBlast = Input_DefineAction("blast");
	g_actClear = Input_DefineAction("clear");

	Input_BindButton(g_actDrop,  0, DG_BTN_A);
	Input_BindButton(g_actShove, 0, DG_BTN_B);
	Input_BindButton(g_actBlast, 0, DG_BTN_X);
	Input_BindButton(g_actClear, 0, DG_BTN_Y);
}

void Game_SceneLoaded(const char* scene)
{
	int i;

	(void)scene;

	for (i = 0; i < MAX_KNIGHTS; i++)
	{
		g_knights[i].object = DG_INVALID_OBJECT;
		g_knights[i].body = 0;
	}

	BuildLevel();

	/* A short stack to start with, so there is something to knock over. */
	DropKnight(-60.0f, 20.0f, 80.0f);
	DropKnight(-60.0f, 140.0f, 80.0f);
	DropKnight(-60.0f, 260.0f, 80.0f);
}

void Game_SceneUnloading(const char* scene)
{
	(void)scene;

	/* Bodies are not tied to scenes: the physics world outlives a scene swap
	   unless you clear it. The game objects die with the scene on their own. */
	PHYS_Reset();
	g_floor = 0;
	g_platform = 0;
}

void Game_Update(float dt)
{
	g_impactsThisFrame = 0;

	MovePlatform(dt);

	if (Input_Pressed(g_actDrop))
		DropInFrontOfCamera();

	if (Input_Pressed(g_actShove))
		Shove();

	if (Input_Pressed(g_actBlast))
		Blast();

	if (Input_Pressed(g_actClear))
		RemoveAllKnights();
}

/* Physics steps between Game_Update and Game_LateUpdate, so this is where the
   frame's final positions are known. */
void Game_LateUpdate(float dt)
{
	vec3_t position;
	int i;

	(void)dt;

	for (i = 0; i < MAX_KNIGHTS; i++)
	{
		knight_t* knight = &g_knights[i];

		if (!knight->body)
			continue;

		/* Fell off the edge of the world: recycle it. */
		if (knight->body->position[1] < -500.0f)
		{
			RemoveKnight(knight);
			continue;
		}

		/* Interpolated between the last two fixed steps, so the motion stays
		   smooth when the frame rate and the 60 Hz step rate disagree. */
		PHYS_GetRenderPosition(knight->body, position);
		GameObject_SetPosition(knight->object, position[0], position[1], position[2]);
	}
}

void Game_RenderHUD(void)
{
	const phys_stats_t* stats = PHYS_GetStats();
	char line[64];

	/* HUD space is -640..640 by -480..480 with y up, and x is the RIGHT edge
	   of the string. */
	sprintf(line, "bodies %d  awake %d", stats->numBodies, stats->numAwakeBodies);
	HUD_DrawText(620, 400, line);

	sprintf(line, "pairs %d  contacts %d", stats->numPairs, stats->numContacts);
	HUD_DrawText(620, 360, line);

	sprintf(line, "steps %d  impacts %d", stats->numSteps, g_impactsThisFrame);
	HUD_DrawText(620, 320, line);

	HUD_DrawText(620, -360, "A drop  B shove  X blast");
	HUD_DrawText(620, -400, "Y clear");
}

void Game_Shutdown(void)
{
	PHYS_Reset();
}
