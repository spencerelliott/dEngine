/*
 *  example_spinner.c
 *  dEngine sample game script
 *
 *  A worked example of the whole game API surface in one file. Delete it, or
 *  strip it down, when you start your own game -- nothing in the engine refers
 *  to it by name.
 */

#include <dgame.h>

/* -------------------------------------------------------------------------
 *  A behavior: reusable per object logic, with its own state
 * ------------------------------------------------------------------------- */

typedef struct spin_state_t {
	float angle;
	float degreesPerSecond;
} spin_state_t;

static void Spin_Attach(dg_object_t self, void* state)
{
	spin_state_t* spin = (spin_state_t*)state;

	spin->angle = 0.0f;
	spin->degreesPerSecond = 90.0f;

	Game_Log("[example] spinner attached to object %u\n", self);
}

static void Spin_Update(dg_object_t self, void* state, float dt)
{
	spin_state_t* spin = (spin_state_t*)state;

	spin->angle += spin->degreesPerSecond * dt;

	GameObject_SetYaw(self, spin->angle * (float)DEG_TO_RAD);
}

DG_BEHAVIOR(spinner, {
	"spinner",
	sizeof(spin_state_t),
	Spin_Attach,
	Spin_Update,
	0
});

/* -------------------------------------------------------------------------
 *  Lifecycle hooks. Define only the ones you need; the engine covers the rest.
 * ------------------------------------------------------------------------- */

static dg_object_t g_monster;
static dg_action_t g_actionJump;
static dg_action_t g_actionNextScene;
static dg_action_t g_actionMoveX;

void Game_Configure(dg_config_t* cfg)
{
	cfg->bootScene = "data/scenes/techDemo.scene";
	cfg->debugCamera = 1;
}

void Game_ConfigureInput(void)
{
	g_actionJump      = Input_DefineAction("jump");
	g_actionNextScene = Input_DefineAction("next_scene");
	g_actionMoveX     = Input_DefineAction("move_x");

	Input_BindButton(g_actionJump, 0, DG_BTN_A);
	Input_BindButton(g_actionNextScene, 0, DG_BTN_START);

	/* Either the stick or the D-pad drives move_x; whichever is pushed
	   further wins. */
	Input_BindAxis(g_actionMoveX, 0, DG_AXIS_LX, 1.0f);
	Input_BindButtonPair(g_actionMoveX, 0, DG_BTN_LEFT, DG_BTN_RIGHT);
}

/* One-time setup only. Anything that has to exist per scene goes in
   Game_SceneLoaded, which fires for the boot scene too. */
void Game_Init(void)
{
	Game_Log("[example] game starting\n");
}

static void Example_PopulateScene(void)
{
	g_monster = GameObject_SpawnAnimated(
		"data/models/monsters/hellknight/hellknight.md5mesh",
		"data/models/monsters/hellknight/idle2.md5anim",
		0.0f, 0.0f, 0.0f);

	if (g_monster == DG_INVALID_OBJECT)
	{
		Game_Log("[example] could not spawn the monster; carrying on without it\n");
		return;
	}

	/* By name, which needs the behavior to have registered itself. The pointer
	   form -- GameObject_AddBehaviorPtr(g_monster, &spinner) -- works even if
	   your toolchain never runs the constructor. */
	GameObject_AddBehavior(g_monster, "spinner");
}

void Game_Update(float dt)
{
	vec3_t position;
	float moveX;

	if (Input_Pressed(g_actionJump))
		Game_Log("[example] jump at t=%.2f\n", Game_Time());

	if (Input_Pressed(g_actionNextScene))
	{
		/* Deferred: the swap happens after this frame has been drawn, and every
		   handle you are holding dies with the old scene. */
		if (!strcmp(Game_CurrentScene(), "data/scenes/techDemo.scene"))
			Game_RequestScene("data/scenes/ikaruga.scene");
		else
			Game_RequestScene("data/scenes/techDemo.scene");
	}

	moveX = Input_Value(g_actionMoveX);

	if (g_monster != DG_INVALID_OBJECT && moveX != 0.0f)
	{
		GameObject_GetPosition(g_monster, position);
		GameObject_SetPosition(g_monster,
		                       position[0] + moveX * 120.0f * dt,
		                       position[1],
		                       position[2]);
	}
}

void Game_RenderHUD(void)
{
	char line[64];

	sprintf(line, "objects:%d fps:%d", GameObject_Count(), Game_FPS());
	HUD_DrawText(20, 400, line);
}

void Game_SceneLoaded(const char* scene)
{
	Game_Log("[example] scene loaded: %s\n", scene);

	/* Fires for the boot scene as well as every switch, so this is the one
	   place the scene gets populated. Handles never survive a swap. */
	g_monster = DG_INVALID_OBJECT;
	Example_PopulateScene();
}
