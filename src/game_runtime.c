/*
 *  game_runtime.c
 *  dEngine
 *
 *  Drives the game layer: frame dispatch, deferred engine events, and the thin
 *  accessors that let script code read and write engine state without reaching
 *  into engine globals directly.
 */

#include "dgame.h"
#include "game_internal.h"
#include "input.h"
#include "world.h"
#include "camera.h"
#include "commands.h"
#include "timer.h"
#include "renderer.h"
#include "font.h"

#include <stdarg.h>

/* Longest string HUD_DrawText will emit. Font_GenerateStringVertexArray returns
   a ubyte vertex count and produces four vertices per glyph, so anything past
   63 characters would silently wrap the return value. */
#define DG_HUD_MAX_CHARS 60

/* A load can stall for hundreds of milliseconds. Without a ceiling the first
   frame afterwards hands game logic a dt large enough to teleport anything
   integrating against it. */
#define DG_MAX_DELTA_SECONDS 0.1f

static char  dg_pendingScene[256];
static int   dg_quitRequested;
static float dg_timeScale = 1.0f;
static uchar dg_debugCamera = 1;
static char  dg_bootScene[256];

static ushort dg_hudIndices[DG_HUD_MAX_CHARS * 6];
static svertex_t dg_hudVertices[DG_HUD_MAX_CHARS * 4];

/* ------------------------------------------------------------------ *
 *  Configuration and startup
 * ------------------------------------------------------------------ */

void DG_ApplyConfig(dg_config_t* cfg)
{
	Game_Configure(cfg);

	dg_debugCamera = cfg->debugCamera;

	if (cfg->bootScene && cfg->bootScene[0])
	{
		strncpy(dg_bootScene, cfg->bootScene, sizeof(dg_bootScene) - 1);
		dg_bootScene[sizeof(dg_bootScene) - 1] = '\0';
	}
	else
		dg_bootScene[0] = '\0';
}

const char* DG_GetBootScene(void)
{
	return dg_bootScene[0] ? dg_bootScene : 0;
}

void DG_Startup(void)
{
	int i;

	/* Four vertices per glyph, two triangles, wound the same way the stats
	   overlay does it. */
	for (i = 0; i < DG_HUD_MAX_CHARS; i++)
	{
		dg_hudIndices[i * 6 + 0] = (ushort)(i * 4 + 0);
		dg_hudIndices[i * 6 + 1] = (ushort)(i * 4 + 1);
		dg_hudIndices[i * 6 + 2] = (ushort)(i * 4 + 2);
		dg_hudIndices[i * 6 + 3] = (ushort)(i * 4 + 0);
		dg_hudIndices[i * 6 + 4] = (ushort)(i * 4 + 2);
		dg_hudIndices[i * 6 + 5] = (ushort)(i * 4 + 3);
	}

	DG_ObjectsInit();
	DG_AdoptSceneEntities();

	printf("[Game] %d behavior(s) registered.\n", DG_RegisteredBehaviorCount());

	INP_InstallDefaultBindings();
	Game_ConfigureInput();

	Game_Init();
	Game_SceneLoaded(World_CurrentScene());
}

void DG_ShutdownAll(void)
{
	Game_Shutdown();
	DG_DetachAllBehaviors();
}

/* ------------------------------------------------------------------ *
 *  Frame
 * ------------------------------------------------------------------ */

float Game_DeltaTime(void)
{
	float dt = timediff * 0.001f * dg_timeScale;

	if (dt < 0.0f)
		dt = 0.0f;
	if (dt > DG_MAX_DELTA_SECONDS)
		dt = DG_MAX_DELTA_SECONDS;

	return dt;
}

float Game_Time(void)
{
	return simulationTime * 0.001f;
}

int Game_FPS(void)
{
	return fps;
}

void DG_FrameBegin(void)
{
	float dt = Game_DeltaTime();

	INP_Poll();
	INP_UpdateActions();

	if (dg_debugCamera)
		DG_UpdateDebugCamera(dt);

	DG_UpdateBehaviors(dt);
	Game_Update(dt);
}

void DG_FrameLate(void)
{
	Game_LateUpdate(Game_DeltaTime());
}

void DG_FrameEnd(void)
{
	DG_FlushDestroys();

	if (!dg_pendingScene[0])
		return;

	Game_SceneUnloading(World_CurrentScene());

	/* Detach against the outgoing scene's slots, before the table is rebuilt. */
	DG_DetachAllBehaviors();

	World_LoadScene(dg_pendingScene);
	dg_pendingScene[0] = '\0';

	DG_ObjectsInit();
	DG_AdoptSceneEntities();

	/* Swallow the load stall so the first frame back gets a sane dt. */
	Timer_Skip();

	Game_SceneLoaded(World_CurrentScene());
}

int DG_QuitRequested(void)
{
	return dg_quitRequested;
}

/* ------------------------------------------------------------------ *
 *  Engine events
 * ------------------------------------------------------------------ */

void Game_RequestScene(const char* sceneFilename)
{
	if (!sceneFilename || !sceneFilename[0])
		return;

	strncpy(dg_pendingScene, sceneFilename, sizeof(dg_pendingScene) - 1);
	dg_pendingScene[sizeof(dg_pendingScene) - 1] = '\0';
}

const char* Game_CurrentScene(void)
{
	return World_CurrentScene();
}

void Game_RequestQuit(void)
{
	dg_quitRequested = 1;
}

void Engine_SetDebugCameraEnabled(int enabled)
{
	dg_debugCamera = enabled ? 1 : 0;
}

int Engine_IsDebugCameraEnabled(void)
{
	return dg_debugCamera;
}

void Engine_SetTimeScale(float scale)
{
	dg_timeScale = scale < 0.0f ? 0.0f : scale;
}

float Engine_GetTimeScale(void)
{
	return dg_timeScale;
}

void Engine_SetStatsEnabled(int enabled)
{
	renderer.statsEnabled = enabled ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 *  Camera and light
 * ------------------------------------------------------------------ */

void Game_GetCameraPosition(vec3_t out)
{
	vectorCopy(camera.position, out);
}

void Game_SetCameraPosition(float x, float y, float z)
{
	camera.position[0] = x;
	camera.position[1] = y;
	camera.position[2] = z;
}

void Game_GetCameraForward(vec3_t out)
{
	vectorCopy(camera.forward, out);
}

void Game_GetCameraRight(vec3_t out)
{
	vectorCopy(camera.right, out);
}

void Game_SetCameraAngles(float head, float pitch)
{
	camera.head = head;
	camera.pitch = pitch;
}

void Game_GetCameraAngles(float* head, float* pitch)
{
	if (head)
		*head = camera.head;
	if (pitch)
		*pitch = camera.pitch;
}

void Game_SetCameraFov(float degrees)
{
	camera.fov = degrees;
}

void Game_SetLightPosition(float x, float y, float z)
{
	light.position[0] = x;
	light.position[1] = y;
	light.position[2] = z;
	light.position[3] = 1.0f;
}

void Game_GetLightPosition(vec3_t out)
{
	vectorCopy(light.position, out);
}

void Game_SetLightAttachedToCamera(int attached)
{
	light.attachedToCamera = attached ? 1 : 0;
}

/* The stock free fly camera. Everything is routed through the command queue in
   commands.c rather than writing to the camera directly, which keeps the dt
   scaling in one place -- and gives that queue its first producer. */
void Game_DefaultFlyCamera(float dt)
{
	command_t command;
	dg_action_t move;
	dg_action_t strafe;
	dg_action_t lookX;
	dg_action_t lookY;
	float value;

	(void)dt;   /* Comm_* already scales by timediff. */

	move   = Input_FindAction("__cam_move");
	strafe = Input_FindAction("__cam_strafe");
	lookX  = Input_FindAction("__cam_look_x");
	lookY  = Input_FindAction("__cam_look_y");

	value = Input_Value(move);
	if (value != 0.0f)
	{
		command.type = COMMAND_TYPE_MOVE_NORTH_SOUTH;
		command.value = value;
		Comm_AddCommand(&command);
	}

	value = Input_Value(strafe);
	if (value != 0.0f)
	{
		command.type = COMMAND_TYPE_MOVE_EAST_WEST;
		command.value = value;
		Comm_AddCommand(&command);
	}

	/* The queue's angular constant is tuned for a mouse delta, so a stick held
	   at full throw needs a healthy multiplier to turn at a usable rate. */
	value = Input_Value(lookX);
	if (value != 0.0f)
	{
		command.type = COMMAND_TYPE_HEAD;
		command.value = value * 20.0f;
		Comm_AddCommand(&command);
	}

	value = Input_Value(lookY);
	if (value != 0.0f)
	{
		command.type = COMMAND_TYPE_PITCH;
		command.value = value * 20.0f;
		Comm_AddCommand(&command);
	}
}

void DG_UpdateDebugCamera(float dt)
{
	Game_DefaultFlyCamera(dt);
}

/* ------------------------------------------------------------------ *
 *  HUD and logging
 * ------------------------------------------------------------------ */

void HUD_DrawText(int x, int y, const char* text)
{
	char clipped[DG_HUD_MAX_CHARS + 1];
	ubyte numVertices;

	if (!text || !text[0] || !font.texfont)
		return;

	strncpy(clipped, text, DG_HUD_MAX_CHARS);
	clipped[DG_HUD_MAX_CHARS] = '\0';

	renderer.SetTexture(font.texfont->textureId);

	numVertices = Font_GenerateStringVertexArray(dg_hudVertices, clipped, x, y);

	if (!numVertices)
		return;

	renderer.RenderString(dg_hudVertices, dg_hudIndices, numVertices / 4 * 6);
}

void DG_RenderHUD(void)
{
	Game_RenderHUD();
}

void Game_Log(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}
