/*
 *  input_actions.c
 *  dEngine example: the input layer
 *
 *  A live readout of the pad, and two control schemes you can swap between at
 *  runtime. A hell knight stands in for "the player" so the bindings have
 *  something to drive.
 *
 *  Shows:
 *    - named actions bound to buttons, axes and button pairs
 *    - several bindings on one action (stick and D-pad both steer)
 *    - Input_Value / Held / Pressed / Released
 *    - throwing every binding away with Input_ClearBindings and rebinding,
 *      including the engine's own __cam_* camera actions
 *    - Input_SetDeadzone
 *    - raw access: which ports have a pad, and their buttons and axes
 *    - a second player on port 1, if one is plugged in
 *    - Game_RequestScene to swap scenes, and handles dying with the scene
 *
 *  Controls (port 0):
 *    START        swap the control scheme (A/B)
 *    Y            cycle the deadzone 0.05 -> 0.15 -> 0.30
 *
 *    Scheme A     stick or D-pad moves the knight, A hops, triggers turn it,
 *                 B (hold) charges, release to dash
 *    Scheme B     the free fly camera is back on the stick; D-pad moves the
 *                 knight; X swaps scenes
 *
 *  Port 1 (scheme A only): its stick moves a second knight.
 *
 *  Build:  ./dc-build.sh --example input_actions
 */

#include <dgame.h>

#include <math.h>

#define KNIGHT_MESH "data/models/monsters/hellKnight/hellknight.pvr.2bpp.md5mesh"
#define KNIGHT_ANIM "data/models/monsters/hellKnight/hellknight_idle2.md5anim"

#define MOVE_SPEED   120.0f
#define TURN_SPEED     2.0f
#define HOP_SPEED    220.0f
#define GRAVITY      700.0f
#define DASH_MAX     400.0f

typedef struct player_t {
	dg_object_t object;
	float       heading;
	float       height;
	float       verticalSpeed;
} player_t;

static player_t g_players[2];

static int   g_scheme;          /* 0 = A, 1 = B */
static int   g_deadzoneIndex = 1;
static const float g_deadzones[3] = { 0.05f, 0.15f, 0.30f };
static float g_charge;          /* seconds B has been held */
static char  g_lastEvent[64];

/* Actions. Input_ClearBindings drops the actions themselves, not only their
   bindings, so every id is re-fetched after a rebind: an old one may now name
   a different action, or none. */
static dg_action_t g_actMoveX, g_actMoveY, g_actTurn, g_actHop, g_actDash;
static dg_action_t g_actDeadzone, g_actScene;
static dg_action_t g_actP2MoveX, g_actP2MoveY;

/* -------------------------------------------------------------------------
 *  Schemes
 * ------------------------------------------------------------------------- */

/* Bindings both schemes share. The scheme swap itself is read raw (see
   Game_Update), so it has no action. */
static void BindCommon(void)
{
	g_actDeadzone = Input_DefineAction("deadzone");

	Input_BindButton(g_actDeadzone, 0, DG_BTN_Y);
}

static void BindSchemeA(void)
{
	/* Drops every binding the engine or an earlier scheme made, the free fly
	   camera's __cam_* actions included -- so the camera stops moving. */
	Input_ClearBindings();
	Engine_SetDebugCameraEnabled(0);

	BindCommon();

	g_actMoveX = Input_DefineAction("move_x");
	g_actMoveY = Input_DefineAction("move_y");
	g_actTurn  = Input_DefineAction("turn");
	g_actHop   = Input_DefineAction("hop");
	g_actDash  = Input_DefineAction("dash");
	g_actScene = Input_DefineAction("scene");   /* defined, left unbound */

	/* The bigger of the two wins each frame, so the stick and the D-pad both
	   work without either one having to be "primary". */
	Input_BindAxis(g_actMoveX, 0, DG_AXIS_LX, 1.0f);
	Input_BindButtonPair(g_actMoveX, 0, DG_BTN_LEFT, DG_BTN_RIGHT);
	Input_BindAxis(g_actMoveY, 0, DG_AXIS_LY, 1.0f);
	Input_BindButtonPair(g_actMoveY, 0, DG_BTN_DOWN, DG_BTN_UP);

	/* One analog axis per direction: negative scale flips it. */
	Input_BindAxis(g_actTurn, 0, DG_AXIS_RTRIG,  1.0f);
	Input_BindAxis(g_actTurn, 0, DG_AXIS_LTRIG, -1.0f);

	Input_BindButton(g_actHop,  0, DG_BTN_A);
	Input_BindButton(g_actDash, 0, DG_BTN_B);

	/* Player two gets its own actions on port 1. With nothing plugged in
	   they simply read zero. */
	g_actP2MoveX = Input_DefineAction("p2_move_x");
	g_actP2MoveY = Input_DefineAction("p2_move_y");
	Input_BindAxis(g_actP2MoveX, 1, DG_AXIS_LX, 1.0f);
	Input_BindAxis(g_actP2MoveY, 1, DG_AXIS_LY, 1.0f);
}

static void BindSchemeB(void)
{
	Input_ClearBindings();

	/* Bring the engine's camera bindings back exactly as they were... */
	{
		dg_action_t move   = Input_DefineAction("__cam_move");
		dg_action_t strafe = Input_DefineAction("__cam_strafe");
		dg_action_t lookX  = Input_DefineAction("__cam_look_x");

		Input_BindAxis(move,   0, DG_AXIS_LY, 1.0f);
		Input_BindAxis(strafe, 0, DG_AXIS_LX, 1.0f);
		Input_BindAxis(lookX,  0, DG_AXIS_RTRIG,  1.0f);
		Input_BindAxis(lookX,  0, DG_AXIS_LTRIG, -1.0f);
		/* __cam_look_y is left unbound: the D-pad belongs to the knight now. */
	}
	Engine_SetDebugCameraEnabled(1);

	BindCommon();

	/* ...and give the knight the D-pad only. */
	g_actMoveX = Input_DefineAction("move_x");
	g_actMoveY = Input_DefineAction("move_y");
	Input_BindButtonPair(g_actMoveX, 0, DG_BTN_LEFT, DG_BTN_RIGHT);
	Input_BindButtonPair(g_actMoveY, 0, DG_BTN_DOWN, DG_BTN_UP);

	g_actScene = Input_DefineAction("scene");
	Input_BindButton(g_actScene, 0, DG_BTN_X);

	/* Defined but left unbound in this scheme: they read 0 and are never
	   pressed, so the code using them needs no special case. */
	g_actTurn    = Input_DefineAction("turn");
	g_actHop     = Input_DefineAction("hop");
	g_actDash    = Input_DefineAction("dash");
	g_actP2MoveX = Input_DefineAction("p2_move_x");
	g_actP2MoveY = Input_DefineAction("p2_move_y");
}

static void ApplyScheme(int scheme)
{
	g_scheme = scheme;
	g_charge = 0.0f;

	if (scheme == 0)
		BindSchemeA();
	else
		BindSchemeB();

	sprintf(g_lastEvent, "scheme %c", scheme == 0 ? 'A' : 'B');
	Game_Log("[input] %s\n", g_lastEvent);
}

/* -------------------------------------------------------------------------
 *  Players
 * ------------------------------------------------------------------------- */

static void SpawnPlayer(player_t* player, float x, float z)
{
	player->object = GameObject_SpawnAnimated(KNIGHT_MESH, KNIGHT_ANIM, x, 0.0f, z);
	player->heading = 0.0f;
	player->height = 0.0f;
	player->verticalSpeed = 0.0f;

	GameObject_SetRotation(player->object, 0.0f, (float)(-90.0f * DEG_TO_RAD), 0.0f);
}

/* Moves in world X/Z: simple and camera independent, which is the point here.
   See character_controller for camera relative movement. */
static void DrivePlayer(player_t* player, float moveX, float moveY, float turn,
                        int hop, float dash, float dt)
{
	vec3_t position;
	float head, pitch, roll;

	if (!GameObject_IsValid(player->object))
		return;

	GameObject_GetPosition(player->object, position);

	position[0] += moveX * MOVE_SPEED * dt;
	position[2] += moveY * MOVE_SPEED * dt;

	if (dash > 0.0f)
	{
		/* Along the heading. Doom models look down +X. */
		position[0] += (float)cos(player->heading) * dash;
		position[2] -= (float)sin(player->heading) * dash;
	}

	if (hop && player->height <= 0.0f)
		player->verticalSpeed = HOP_SPEED;

	player->verticalSpeed -= GRAVITY * dt;
	player->height += player->verticalSpeed * dt;
	if (player->height < 0.0f)
	{
		player->height = 0.0f;
		player->verticalSpeed = 0.0f;
	}

	GameObject_SetPosition(player->object, position[0], player->height, position[2]);

	if (turn != 0.0f)
	{
		player->heading += turn * TURN_SPEED * dt;
		GameObject_GetRotation(player->object, &head, &pitch, &roll);
		GameObject_SetRotation(player->object, player->heading, pitch, roll);
	}
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
	Input_SetDeadzone(g_deadzones[g_deadzoneIndex]);
	ApplyScheme(0);
}

void Game_SceneLoaded(const char* scene)
{
	/* Bindings survive a scene swap; objects do not. */
	SpawnPlayer(&g_players[0], -80.0f, -120.0f);
	SpawnPlayer(&g_players[1],  80.0f, -120.0f);

	sprintf(g_lastEvent, "loaded %.50s", scene);
}

void Game_Update(float dt)
{
	float dash = 0.0f;

	/* Raw on purpose. A rebind recreates every action with no history, so an
	   action-based START would read as freshly pressed again on the very next
	   frame (it is still held) and flip the scheme straight back. The raw pad
	   state keeps its own previous frame and is untouched by a rebind. */
	if (Input_RawPressed(0, DG_BTN_START))
		ApplyScheme(!g_scheme);

	if (Input_Pressed(g_actDeadzone))
	{
		g_deadzoneIndex = (g_deadzoneIndex + 1) % 3;
		Input_SetDeadzone(g_deadzones[g_deadzoneIndex]);
		sprintf(g_lastEvent, "deadzone %.2f", g_deadzones[g_deadzoneIndex]);
	}

	/* Held accumulates, Released fires once on the falling edge. */
	if (Input_Held(g_actDash))
		g_charge += dt;

	if (Input_Released(g_actDash))
	{
		dash = g_charge * DASH_MAX;
		if (dash > DASH_MAX * 0.5f)
			dash = DASH_MAX * 0.5f;

		sprintf(g_lastEvent, "dash after %.2fs", g_charge);
		g_charge = 0.0f;
	}

	if (Input_Pressed(g_actHop))
		sprintf(g_lastEvent, "hop");

	DrivePlayer(&g_players[0],
	            Input_Value(g_actMoveX), Input_Value(g_actMoveY), Input_Value(g_actTurn),
	            Input_Pressed(g_actHop), dash, dt);

	DrivePlayer(&g_players[1],
	            Input_Value(g_actP2MoveX), Input_Value(g_actP2MoveY), 0.0f, 0, 0.0f, dt);

	if (Input_Pressed(g_actScene))
	{
		/* Deferred to the end of the frame. Both players' handles die with the
		   old scene; Game_SceneLoaded respawns them. */
		if (!strcmp(Game_CurrentScene(), "data/scenes/techDemo.scene"))
			Game_RequestScene("data/scenes/ikaruga.scene");
		else
			Game_RequestScene("data/scenes/techDemo.scene");
	}
}

void Game_RenderHUD(void)
{
	static const char* names[DG_BTN_COUNT] = {
		"A", "B", "X", "Y", "S", "U", "D", "L", "R", "Lt", "Rt"
	};
	char line[64];
	int port, b, length;

	sprintf(line, "scheme %c  deadzone %.2f", g_scheme == 0 ? 'A' : 'B',
	        g_deadzones[g_deadzoneIndex]);
	HUD_DrawText(620, 400, line);

	/* Actions: what game code sees. */
	sprintf(line, "move %+.2f %+.2f  turn %+.2f", Input_Value(g_actMoveX),
	        Input_Value(g_actMoveY), Input_Value(g_actTurn));
	HUD_DrawText(620, 360, line);

	if (Input_Held(g_actDash))
		sprintf(line, "charging %.2fs", g_charge);
	else
		sprintf(line, "%.40s", g_lastEvent);
	HUD_DrawText(620, 320, line);

	/* Raw devices: what the hardware says, before any binding. */
	for (port = 0; port < 2; port++)
	{
		int y = 240 - port * 80;

		if (!Input_PadConnected(port))
		{
			sprintf(line, "port %d: no pad", port);
			HUD_DrawText(620, y, line);
			continue;
		}

		sprintf(line, "port %d stick %+.2f %+.2f trig %.2f %.2f", port,
		        Input_RawAxis(port, DG_AXIS_LX), Input_RawAxis(port, DG_AXIS_LY),
		        Input_RawAxis(port, DG_AXIS_LTRIG), Input_RawAxis(port, DG_AXIS_RTRIG));
		HUD_DrawText(620, y, line);

		length = sprintf(line, "held:");
		for (b = 0; b < DG_BTN_COUNT; b++)
		{
			if (Input_RawHeld(port, (dg_button_t)b))
				length += sprintf(line + length, " %s", names[b]);
		}
		HUD_DrawText(620, y - 40, line);
	}

	HUD_DrawText(620, -400, g_scheme == 0 ? "START scheme B  Y deadzone"
	                                      : "START scheme A  Y deadzone  X scene");
}
