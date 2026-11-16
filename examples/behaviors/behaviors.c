/*
 *  behaviors.c
 *  dEngine example: composing objects out of behaviors
 *
 *  A ring of hell knights, each built from small reusable behaviors that know
 *  nothing about each other. The game code on top only adds, tweaks and
 *  removes them.
 *
 *  Shows:
 *    - DG_BEHAVIOR with on_attach / on_update / on_detach and per object state
 *    - stacking several behaviors on one object (up to four)
 *    - reaching into a behavior's state from game code with GetBehavior
 *    - removing a behavior at runtime, and destroying an object
 *    - GameObject_FindByBehavior and walking every object with GameObject_Next
 *    - holding handles safely: they die with their object and with the scene
 *    - Engine_SetTimeScale for slow motion
 *
 *  Behaviors, each in a few lines:
 *    orbit    circles a centre point, facing along its path
 *    bob      bounces up and down on top of whatever sets its height
 *    pulse    breathes its scale
 *    blink    hides and shows itself
 *
 *  Controls (port 0), on top of the free fly camera:
 *    A      add a knight to the ring
 *    B      destroy the most recently added knight
 *    X      toggle "bob" on every knight
 *    Y      speed every orbit up (wraps back to slow)
 *    START  toggle slow motion
 *
 *  Build:  ./dc-build.sh --example behaviors
 */

#include <dgame.h>

#include <math.h>

#define KNIGHT_MESH "data/models/monsters/hellKnight/hellknight.pvr.2bpp.md5mesh"
#define KNIGHT_ANIM "data/models/monsters/hellKnight/hellknight_idle2.md5anim"

#define MAX_KNIGHTS 6
#define RING_RADIUS 160.0f

/* -------------------------------------------------------------------------
 *  orbit
 *
 *  Owns the object's X/Z and yaw. Writes Y from its own baseHeight so other
 *  behaviors can add on top through it (see bob).
 * ------------------------------------------------------------------------- */

typedef struct orbit_state_t {
	float angle;         /* radians around the centre */
	float speed;         /* radians per second */
	float radius;
	float baseHeight;
	float heightOffset;  /* written by other behaviors each frame */
} orbit_state_t;

static void Orbit_Attach(dg_object_t self, void* state)
{
	orbit_state_t* orbit = (orbit_state_t*)state;
	vec3_t position;

	/* Start from wherever the object already is. State arrives zeroed. */
	GameObject_GetPosition(self, position);

	orbit->radius = (float)sqrt(position[0] * position[0] + position[2] * position[2]);
	orbit->angle = (float)atan2(position[2], position[0]);
	orbit->speed = 0.6f;
	orbit->baseHeight = position[1];
}

static void Orbit_Update(dg_object_t self, void* state, float dt)
{
	orbit_state_t* orbit = (orbit_state_t*)state;
	float head, pitch, roll;

	orbit->angle += orbit->speed * dt;

	GameObject_SetPosition(self,
	                       (float)cos(orbit->angle) * orbit->radius,
	                       orbit->baseHeight + orbit->heightOffset,
	                       (float)sin(orbit->angle) * orbit->radius);

	/* Face along the circle. Doom models look down +X, and the tangent at
	   'angle' is (-sin, cos), which a yaw of -(angle + 90 degrees) turns +X
	   onto. Keep the pitch the object was given (the knight mesh needs one to
	   stand up) and only steer the yaw. */
	GameObject_GetRotation(self, &head, &pitch, &roll);
	GameObject_SetRotation(self, -(orbit->angle + 1.5707963f), pitch, roll);

	/* Consumed: whoever wants an offset next frame writes it again. */
	orbit->heightOffset = 0.0f;
}

DG_BEHAVIOR(orbit, {
	"orbit",
	sizeof(orbit_state_t),
	Orbit_Attach,
	Orbit_Update,
	0
});

/* -------------------------------------------------------------------------
 *  bob
 *
 *  Talks to orbit through its state when both are attached, and moves the
 *  object directly otherwise. The two behaviors are still independent: bob
 *  only looks orbit up by name.
 * ------------------------------------------------------------------------- */

typedef struct bob_state_t {
	float time;
	float height;
	float rate;
} bob_state_t;

static void Bob_Attach(dg_object_t self, void* state)
{
	bob_state_t* bob = (bob_state_t*)state;

	(void)self;

	bob->height = 30.0f;
	bob->rate = 4.0f;
}

static void Bob_Update(dg_object_t self, void* state, float dt)
{
	bob_state_t* bob = (bob_state_t*)state;
	orbit_state_t* orbit;
	float offset;

	bob->time += dt;
	offset = (float)fabs(sin(bob->time * bob->rate)) * bob->height;

	orbit = (orbit_state_t*)GameObject_GetBehavior(self, "orbit");

	if (orbit)
		orbit->heightOffset += offset;
	else
	{
		vec3_t position;

		GameObject_GetPosition(self, position);
		GameObject_SetPosition(self, position[0], offset, position[2]);
	}
}

/* Put the object back down when bob goes away. */
static void Bob_Detach(dg_object_t self, void* state)
{
	vec3_t position;

	(void)state;

	if (GameObject_GetBehavior(self, "orbit"))
		return;

	GameObject_GetPosition(self, position);
	GameObject_SetPosition(self, position[0], 0.0f, position[2]);
}

DG_BEHAVIOR(bob, {
	"bob",
	sizeof(bob_state_t),
	Bob_Attach,
	Bob_Update,
	Bob_Detach
});

/* -------------------------------------------------------------------------
 *  pulse
 * ------------------------------------------------------------------------- */

typedef struct pulse_state_t {
	float time;
	float phase;
} pulse_state_t;

static void Pulse_Attach(dg_object_t self, void* state)
{
	pulse_state_t* pulse = (pulse_state_t*)state;

	/* The handle doubles as a cheap per object seed so the ring does not
	   breathe in lockstep. */
	pulse->phase = (float)(self & 0xFF) * 0.37f;
}

static void Pulse_Update(dg_object_t self, void* state, float dt)
{
	pulse_state_t* pulse = (pulse_state_t*)state;

	pulse->time += dt;
	GameObject_SetScale(self, 1.0f + 0.12f * (float)sin(pulse->time * 2.5f + pulse->phase));
}

static void Pulse_Detach(dg_object_t self, void* state)
{
	(void)state;

	GameObject_SetScale(self, 1.0f);
}

DG_BEHAVIOR(pulse, {
	"pulse",
	sizeof(pulse_state_t),
	Pulse_Attach,
	Pulse_Update,
	Pulse_Detach
});

/* -------------------------------------------------------------------------
 *  blink -- no state needed beyond a timer
 * ------------------------------------------------------------------------- */

typedef struct blink_state_t {
	float time;
} blink_state_t;

static void Blink_Update(dg_object_t self, void* state, float dt)
{
	blink_state_t* blink = (blink_state_t*)state;

	blink->time += dt;

	/* Visible 1.5 s, hidden 0.25 s. */
	GameObject_SetVisible(self, fmod(blink->time, 1.75) < 1.5);
}

static void Blink_Detach(dg_object_t self, void* state)
{
	(void)state;

	GameObject_SetVisible(self, 1);
}

DG_BEHAVIOR(blink, {
	"blink",
	sizeof(blink_state_t),
	0,
	Blink_Update,
	Blink_Detach
});

/* -------------------------------------------------------------------------
 *  Game
 * ------------------------------------------------------------------------- */

/* Handles, not pointers. A destroyed knight's handle just stops resolving. */
static dg_object_t g_knights[MAX_KNIGHTS];
static int         g_numKnights;
static int         g_bobOn;
static int         g_slowMotion;

static dg_action_t g_actAdd;
static dg_action_t g_actRemove;
static dg_action_t g_actBob;
static dg_action_t g_actFaster;
static dg_action_t g_actSlowMo;

static void AddKnight(void)
{
	dg_object_t knight;
	float angle;

	if (g_numKnights >= MAX_KNIGHTS)
	{
		Game_Log("[behaviors] ring is full\n");
		return;
	}

	angle = (float)g_numKnights * (2.0f * 3.14159265f / MAX_KNIGHTS);

	knight = GameObject_SpawnAnimated(KNIGHT_MESH, KNIGHT_ANIM,
	                                  (float)cos(angle) * RING_RADIUS, 0.0f,
	                                  (float)sin(angle) * RING_RADIUS);

	if (knight == DG_INVALID_OBJECT)
		return;

	/* Z up mesh onto Y up. orbit keeps this pitch when it steers the yaw. */
	GameObject_SetRotation(knight, 0.0f, (float)(-90.0f * DEG_TO_RAD), 0.0f);

	/* Every knight orbits and breathes. By name, which relies on DG_BEHAVIOR's
	   constructor having registered them... */
	GameObject_AddBehavior(knight, "orbit");
	GameObject_AddBehavior(knight, "pulse");

	/* ...or by pointer, which does not. Either works. Every other knight
	   blinks. */
	if (g_numKnights & 1)
		GameObject_AddBehaviorPtr(knight, &blink);

	if (g_bobOn)
		GameObject_AddBehaviorPtr(knight, &bob);

	g_knights[g_numKnights++] = knight;
}

static void RemoveNewestKnight(void)
{
	if (!g_numKnights)
		return;

	/* Deferred to the end of the frame; the object's behaviors are detached
	   then. Our handle is dead from here on either way. */
	GameObject_Destroy(g_knights[--g_numKnights]);
	g_knights[g_numKnights] = DG_INVALID_OBJECT;
}

static void ToggleBob(void)
{
	int i;

	g_bobOn = !g_bobOn;

	for (i = 0; i < g_numKnights; i++)
	{
		if (g_bobOn)
			GameObject_AddBehaviorPtr(g_knights[i], &bob);
		else
			GameObject_RemoveBehavior(g_knights[i], "bob");
	}
}

static void SpeedUpOrbits(void)
{
	dg_object_t obj;

	/* Walk every live object -- including the ones the map loader made -- and
	   tweak the ones that orbit. GetBehavior is NULL for the rest. */
	for (obj = GameObject_Next(DG_INVALID_OBJECT); obj != DG_INVALID_OBJECT;
	     obj = GameObject_Next(obj))
	{
		orbit_state_t* orbit = (orbit_state_t*)GameObject_GetBehavior(obj, "orbit");

		if (!orbit)
			continue;

		orbit->speed *= 1.6f;
		if (orbit->speed > 4.0f)
			orbit->speed = 0.6f;
	}
}

void Game_Configure(dg_config_t* cfg)
{
	cfg->bootScene = "data/scenes/techDemo.scene";
	cfg->debugCamera = 1;
}

void Game_ConfigureInput(void)
{
	g_actAdd    = Input_DefineAction("add");
	g_actRemove = Input_DefineAction("remove");
	g_actBob    = Input_DefineAction("bob");
	g_actFaster = Input_DefineAction("faster");
	g_actSlowMo = Input_DefineAction("slow_motion");

	Input_BindButton(g_actAdd,    0, DG_BTN_A);
	Input_BindButton(g_actRemove, 0, DG_BTN_B);
	Input_BindButton(g_actBob,    0, DG_BTN_X);
	Input_BindButton(g_actFaster, 0, DG_BTN_Y);
	Input_BindButton(g_actSlowMo, 0, DG_BTN_START);
}

void Game_SceneLoaded(const char* scene)
{
	int i;

	(void)scene;

	/* Handles from the last scene are dead. Start clean. */
	for (i = 0; i < MAX_KNIGHTS; i++)
		g_knights[i] = DG_INVALID_OBJECT;
	g_numKnights = 0;

	/* The scene's own knight sits at the origin, right where the ring is
	   centred. Hide it rather than destroy it, to show both are options. */
	{
		dg_object_t obj;

		for (obj = GameObject_Next(DG_INVALID_OBJECT); obj != DG_INVALID_OBJECT;
		     obj = GameObject_Next(obj))
		{
			vec3_t position;

			GameObject_GetPosition(obj, position);

			if (position[0] == 0.0f && position[1] == 0.0f && position[2] == 0.0f)
				GameObject_SetVisible(obj, 0);
		}
	}

	for (i = 0; i < 4; i++)
		AddKnight();
}

void Game_Update(float dt)
{
	(void)dt;

	if (Input_Pressed(g_actAdd))
		AddKnight();

	if (Input_Pressed(g_actRemove))
		RemoveNewestKnight();

	if (Input_Pressed(g_actBob))
		ToggleBob();

	if (Input_Pressed(g_actFaster))
		SpeedUpOrbits();

	if (Input_Pressed(g_actSlowMo))
	{
		/* Scales the dt handed to behaviors and Game_Update. */
		g_slowMotion = !g_slowMotion;
		Engine_SetTimeScale(g_slowMotion ? 0.25f : 1.0f);
	}
}

void Game_RenderHUD(void)
{
	orbit_state_t* orbit = 0;
	char line[64];

	if (g_numKnights)
		orbit = (orbit_state_t*)GameObject_GetBehavior(g_knights[0], "orbit");

	sprintf(line, "knights %d/%d  objects %d", g_numKnights, MAX_KNIGHTS, GameObject_Count());
	HUD_DrawText(620, 400, line);

	sprintf(line, "orbit %.2f rad/s  bob %s", orbit ? orbit->speed : 0.0f,
	        g_bobOn ? "on" : "off");
	HUD_DrawText(620, 360, line);

	sprintf(line, "time scale %.2f", Engine_GetTimeScale());
	HUD_DrawText(620, 320, line);

	/* FindByBehavior answers "is there one?" without keeping a list. */
	HUD_DrawText(620, 280, GameObject_FindByBehavior("blink") != DG_INVALID_OBJECT
	             ? "someone is blinking" : "nobody blinks");

	HUD_DrawText(620, -360, "A add  B remove  X bob");
	HUD_DrawText(620, -400, "Y faster  START slow-mo");
}
