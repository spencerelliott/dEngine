/*
 *  character_controller.c
 *  dEngine example: a first person walker on the physics module
 *
 *  Replaces the free fly camera with a player that has a body: it walks, slides
 *  along walls, falls, jumps and stands on things. The camera sits at eye
 *  height on top of it.
 *
 *  Shows:
 *    - turning the debug camera off and driving the camera yourself
 *    - a kinematic player moved with PHYS_MoveAndSlide
 *    - PHYS_IsGrounded and groundNormal for jumping and slopes
 *    - building camera relative movement from Game_GetCameraForward/Right
 *    - trigger volumes, with the enter/exit tracking the engine leaves to you
 *    - layers so the trigger only reacts to the player
 *    - a raycast "use" probe that reports what you are looking at
 *
 *  Controls (port 0):
 *    stick       walk
 *    L / R       turn
 *    D-pad U/D   look up / down
 *    A           jump
 *    B           probe what is in front of you
 *    X           sprint (hold)
 *
 *  Build:  ./dc-build.sh --example character_controller
 */

#include <dgame.h>
#include <physics.h>

#include <math.h>

/* Inches, like the rest of the tech demo. */
#define PLAYER_HALF_WIDTH    14.0f
#define PLAYER_HALF_HEIGHT   34.0f
#define PLAYER_EYE_HEIGHT    62.0f     /* above the feet */

#define WALK_SPEED          140.0f
#define SPRINT_SPEED        260.0f
#define JUMP_SPEED          330.0f
#define GRAVITY            1066.0f
#define TURN_SPEED            2.2f     /* radians per second */
#define LOOK_SPEED            1.4f
#define MAX_PITCH             1.3f

#define LAYER_WORLD    0x0001
#define LAYER_PLAYER   0x0002
#define LAYER_TRIGGER  0x0004

static rigidbody_t* g_player;
static rigidbody_t* g_zone;
static float g_verticalSpeed;
static float g_head;
static float g_pitch;

/* Trigger bookkeeping. The callback says "overlapping this step"; comparing
   this step with the last one gives enter and exit. */
static int g_inZoneThisStep;
static int g_inZone;
static int g_zoneVisits;

static char g_probeText[64];

static dg_action_t g_actMoveX;
static dg_action_t g_actMoveY;
static dg_action_t g_actTurn;
static dg_action_t g_actLook;
static dg_action_t g_actJump;
static dg_action_t g_actProbe;
static dg_action_t g_actSprint;

/* -------------------------------------------------------------------------
 *  Level
 * ------------------------------------------------------------------------- */

static rigidbody_t* AddStaticBox(float x, float y, float z, float hx, float hy, float hz)
{
	collider_t collider;
	vec3_t position;
	rigidbody_t* body;

	COL_MakeBox(&collider, hx, hy, hz);
	vectorSet(position, x, y, z);

	body = PHYS_CreateBody(PHYS_STATIC, position, &collider);

	if (body)
		PHYS_SetLayer(body, LAYER_WORLD, PHYS_LAYER_ALL);

	return body;
}

static void OnTrigger(rigidbody_t* a, rigidbody_t* b, const contact_t* contact)
{
	(void)contact;

	if ((a == g_zone && b == g_player) || (a == g_player && b == g_zone))
		g_inZoneThisStep = 1;
}

static void BuildLevel(void)
{
	collider_t collider;
	vec3_t position;

	PHYS_SetGravity(0.0f, -GRAVITY, 0.0f);
	PHYS_SetSolverParams(4, 3, 0.5f, 0.8f);
	PHYS_SetSleepParams(8.0f, 0.5f);
	PHYS_SetTriggerCallback(OnTrigger);

	/* Collision for the tech demo room. The render geometry has none of its
	   own, so a few boxes stand in for it: a floor, four walls, the knight in
	   the middle and a step to jump onto. */
	AddStaticBox(   0.0f,  -8.0f,    0.0f, 600.0f,   8.0f, 600.0f);
	AddStaticBox(   0.0f, 100.0f,  420.0f, 600.0f, 100.0f,   8.0f);
	AddStaticBox(   0.0f, 100.0f, -420.0f, 600.0f, 100.0f,   8.0f);
	AddStaticBox( 420.0f, 100.0f,    0.0f,   8.0f, 100.0f, 600.0f);
	AddStaticBox(-420.0f, 100.0f,    0.0f,   8.0f, 100.0f, 600.0f);
	AddStaticBox(   0.0f,  48.0f,    0.0f,  26.0f,  48.0f,  26.0f);   /* hell knight */
	AddStaticBox( 150.0f,  12.0f,  -80.0f,  40.0f,  12.0f,  40.0f);   /* a step      */

	/* A trigger: overlaps are reported, never resolved, so you walk right
	   through it. Its mask only lets the player set it off. */
	COL_MakeBox(&collider, 50.0f, 50.0f, 50.0f);
	vectorSet(position, -150.0f, 50.0f, 150.0f);
	g_zone = PHYS_CreateBody(PHYS_STATIC, position, &collider);
	if (g_zone)
	{
		g_zone->flags |= PHYS_FLAG_TRIGGER;
		PHYS_SetLayer(g_zone, LAYER_TRIGGER, LAYER_PLAYER);
	}

	/* The player. Kinematic: the solver never pushes it, we move it. Its
	   origin is at its feet, so the box is offset upwards. */
	COL_MakeBox(&collider, PLAYER_HALF_WIDTH, PLAYER_HALF_HEIGHT, PLAYER_HALF_WIDTH);
	COL_SetOffset(&collider, 0.0f, PLAYER_HALF_HEIGHT, 0.0f);
	vectorSet(position, 0.0f, 10.0f, -200.0f);
	g_player = PHYS_CreateBody(PHYS_KINEMATIC, position, &collider);
	if (g_player)
	{
		g_player->flags |= PHYS_FLAG_NO_SLEEP;
		PHYS_SetLayer(g_player, LAYER_PLAYER, LAYER_WORLD | LAYER_TRIGGER);
	}

	g_verticalSpeed = 0.0f;
	g_head = 0.0f;
	g_pitch = 0.0f;
	g_inZone = 0;
	g_inZoneThisStep = 0;
	g_probeText[0] = '\0';
}

/* -------------------------------------------------------------------------
 *  Player
 * ------------------------------------------------------------------------- */

static void Probe(void)
{
	raycast_hit_t hit;
	vec3_t eye, forward;

	Game_GetCameraPosition(eye);
	Game_GetCameraForward(forward);

	/* The player is not in this mask, and is passed as 'ignore' too: either is
	   enough to keep the probe from hitting yourself. */
	if (PHYS_Raycast(eye, forward, 600.0f, LAYER_WORLD, g_player, &hit))
	{
		sprintf(g_probeText, "hit body %u at %.0f in, n=%.1f %.1f %.1f",
		        (unsigned)hit.body->id, hit.distance,
		        hit.normal[0], hit.normal[1], hit.normal[2]);
	}
	else
		sprintf(g_probeText, "nothing within 600 in");

	Game_Log("[controller] probe: %s\n", g_probeText);
}

static void MovePlayer(float dt)
{
	vec3_t forward, right, motion;
	float moveX, moveY, speed, length, startY;

	/* Looking around. Both are radians; pitch is clamped short of straight up
	   or down so forward never degenerates. */
	g_head  += Input_Value(g_actTurn) * TURN_SPEED * dt;
	g_pitch += Input_Value(g_actLook) * LOOK_SPEED * dt;
	if (g_pitch >  MAX_PITCH) g_pitch =  MAX_PITCH;
	if (g_pitch < -MAX_PITCH) g_pitch = -MAX_PITCH;
	Game_SetCameraAngles(g_head, g_pitch);

	/* Flatten the camera axes onto the floor so looking down does not slow
	   you down or push you into the ground. */
	Game_GetCameraForward(forward);
	Game_GetCameraRight(right);
	forward[1] = 0.0f;
	right[1] = 0.0f;

	length = (float)sqrt(DotProduct(forward, forward));
	if (length > 0.001f)
		vectorScale(forward, 1.0f / length, forward);
	length = (float)sqrt(DotProduct(right, right));
	if (length > 0.001f)
		vectorScale(right, 1.0f / length, right);

	moveX = Input_Value(g_actMoveX);
	moveY = Input_Value(g_actMoveY);
	speed = Input_Held(g_actSprint) ? SPRINT_SPEED : WALK_SPEED;

	vectorClear(motion);
	vectorMA(motion, moveY * speed * dt, forward, motion);
	vectorMA(motion, moveX * speed * dt, right, motion);

	/* Gravity and jumping are ours to integrate: kinematic bodies ignore the
	   world's gravity. */
	if (PHYS_IsGrounded(g_player))
	{
		g_verticalSpeed = Input_Pressed(g_actJump) ? JUMP_SPEED : -1.0f;
	}
	else
	{
		g_verticalSpeed -= GRAVITY * dt;
	}

	motion[1] = g_verticalSpeed * dt;
	startY = g_player->position[1];

	PHYS_MoveAndSlide(g_player, motion, LAYER_WORLD);

	/* Rose less than half of what was asked: we hit our head. Stop rising. */
	if (motion[1] > 0.0f && g_player->position[1] - startY < motion[1] * 0.5f)
		g_verticalSpeed = 0.0f;
}

static void UpdateZone(void)
{
	/* The trigger callback only runs inside a physics step. On a frame that
	   ran none (rendering faster than 60 Hz) there is no news, and treating
	   the silence as "not overlapping" would report a false exit. */
	if (PHYS_GetStats()->numSteps == 0)
		return;

	/* Edge detection over the last physics step. */
	if (g_inZoneThisStep && !g_inZone)
	{
		g_zoneVisits++;
		Game_Log("[controller] entered the zone (visit %d)\n", g_zoneVisits);
	}
	else if (!g_inZoneThisStep && g_inZone)
	{
		Game_Log("[controller] left the zone\n");
	}

	g_inZone = g_inZoneThisStep;
	g_inZoneThisStep = 0;
}

/* -------------------------------------------------------------------------
 *  Hooks
 * ------------------------------------------------------------------------- */

void Game_Configure(dg_config_t* cfg)
{
	cfg->bootScene = "data/scenes/techDemo.scene";

	/* We own the camera. */
	cfg->debugCamera = 0;
}

void Game_ConfigureInput(void)
{
	/* Drop the engine's __cam_* bindings: none of them are wanted here. */
	Input_ClearBindings();

	g_actMoveX  = Input_DefineAction("move_x");
	g_actMoveY  = Input_DefineAction("move_y");
	g_actTurn   = Input_DefineAction("turn");
	g_actLook   = Input_DefineAction("look");
	g_actJump   = Input_DefineAction("jump");
	g_actProbe  = Input_DefineAction("probe");
	g_actSprint = Input_DefineAction("sprint");

	Input_BindAxis(g_actMoveX, 0, DG_AXIS_LX, 1.0f);
	Input_BindAxis(g_actMoveY, 0, DG_AXIS_LY, 1.0f);

	/* Two bindings on one action: the bigger magnitude wins, so one trigger
	   turns left and the other right. */
	Input_BindAxis(g_actTurn, 0, DG_AXIS_LTRIG,  1.0f);
	Input_BindAxis(g_actTurn, 0, DG_AXIS_RTRIG, -1.0f);

	Input_BindButtonPair(g_actLook, 0, DG_BTN_DOWN, DG_BTN_UP);

	Input_BindButton(g_actJump,   0, DG_BTN_A);
	Input_BindButton(g_actProbe,  0, DG_BTN_B);
	Input_BindButton(g_actSprint, 0, DG_BTN_X);
}

void Game_SceneLoaded(const char* scene)
{
	(void)scene;

	/* The light follows the camera, like a head lamp. */
	Game_SetLightAttachedToCamera(1);

	BuildLevel();
}

void Game_SceneUnloading(const char* scene)
{
	(void)scene;

	PHYS_Reset();
	g_player = 0;
	g_zone = 0;
}

void Game_Update(float dt)
{
	if (!g_player)
		return;

	MovePlayer(dt);

	if (Input_Pressed(g_actProbe))
		Probe();
}

/* After the physics step: settle the trigger state and put the camera on the
   player's interpolated position. */
void Game_LateUpdate(float dt)
{
	(void)dt;

	if (!g_player)
		return;

	UpdateZone();

	/* MoveAndSlide moved the body this very frame, outside the fixed step, so
	   its current position is already the one to show. (Dynamic bodies want
	   PHYS_GetRenderPosition instead; see physics_playground.) */
	Game_SetCameraPosition(g_player->position[0],
	                       g_player->position[1] + PLAYER_EYE_HEIGHT,
	                       g_player->position[2]);
}

void Game_RenderHUD(void)
{
	char line[64];

	if (!g_player)
		return;

	sprintf(line, "pos %.0f %.0f %.0f", g_player->position[0],
	        g_player->position[1], g_player->position[2]);
	HUD_DrawText(620, 400, line);

	if (PHYS_IsGrounded(g_player))
		sprintf(line, "grounded, slope n.y %.2f", g_player->groundNormal[1]);
	else
		sprintf(line, "airborne, vy %.0f", g_verticalSpeed);
	HUD_DrawText(620, 360, line);

	sprintf(line, "%s  visits %d", g_inZone ? "IN ZONE" : "outside", g_zoneVisits);
	HUD_DrawText(620, 320, line);

	if (g_probeText[0])
		HUD_DrawText(620, 280, g_probeText);

	HUD_DrawText(620, -400, "A jump  B probe  X sprint");
}

void Game_Shutdown(void)
{
	PHYS_Reset();
}
