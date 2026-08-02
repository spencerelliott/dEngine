/*
 *  dgame.h
 *  dEngine
 *
 *  The dEngine game logic API.
 *
 *  Drop a .c file into the game/ directory and include this one header. CMake
 *  picks the file up automatically, so there is nothing to register and no
 *  build file to edit.
 *
 *  Every lifecycle hook below is optional. Define the ones you want; the engine
 *  supplies a default for the rest. With an empty game/ directory the engine
 *  behaves exactly as it did before this layer existed.
 */

#pragma once

#include "globals.h"
#include "internal_math.h"
#include "matrix.h"

/* -------------------------------------------------------------------------
 *  Startup configuration
 * ------------------------------------------------------------------------- */

typedef struct dg_config_t {
	uchar       rendererType;   /* GL_11_RENDERER */
	int         width;
	int         height;
	const char* bootScene;      /* NULL => read data/config.cfg as before */
	uchar       debugCamera;    /* 1 => the built in free fly camera is bound */
} dg_config_t;

/* -------------------------------------------------------------------------
 *  Lifecycle hooks -- you define these, all optional
 * ------------------------------------------------------------------------- */

/* Runs before any engine subsystem is initialised. Only touch cfg here; no
   engine service exists yet. */
void Game_Configure(dg_config_t* cfg);

/* Define actions and bindings. Runs after the engine has installed its own
   defaults, so you can rebind or call Input_ClearBindings() and start over. */
void Game_ConfigureInput(void);

/* Runs exactly once, after the boot scene has loaded and the camera exists.
   Use it for one-time setup only: Game_SceneLoaded fires immediately after it
   for the boot scene, and again for every scene after that, so anything that
   has to be rebuilt per scene belongs there rather than here. */
void Game_Init(void);

/* Every frame, before the world updates. dt is in seconds. */
void Game_Update(float dt);

/* Every frame, after the world updates and before anything is drawn. */
void Game_LateUpdate(float dt);

/* Called inside the 2D pass. Use HUD_DrawText. */
void Game_RenderHUD(void);

/* Scene transitions. Game_SceneLoaded fires for every scene including the boot
   scene, which makes it the right place for per-scene setup. Handles from the
   previous scene are already dead by the time it runs. */
void Game_SceneLoaded(const char* scene);
void Game_SceneUnloading(const char* scene);

/* Runs on the way out, before the world is torn down. */
void Game_Shutdown(void);

/* -------------------------------------------------------------------------
 *  Game objects
 *
 *  Objects are addressed by handle, never by pointer. map[] is a flat array
 *  that is rebuilt on every scene load, so a stored entity_t* dangles. A handle
 *  encodes a slot plus a generation counter: once an object is destroyed every
 *  handle to it fails validation and every accessor becomes a safe no-op.
 * ------------------------------------------------------------------------- */

typedef uint dg_object_t;
#define DG_INVALID_OBJECT ((dg_object_t)0)

/* The file extension picks the loader: .obj goes through OBJ_Load, anything
   else through ReadMD5Model. Returns DG_INVALID_OBJECT on failure. */
dg_object_t GameObject_Spawn(const char* modelFile);
dg_object_t GameObject_SpawnAt(const char* modelFile, float x, float y, float z);
dg_object_t GameObject_SpawnAnimated(const char* meshFile, const char* animFile,
                                     float x, float y, float z);

/* Deferred: the slot is released at the end of the frame, after rendering. */
void GameObject_Destroy(dg_object_t obj);
int  GameObject_IsValid(dg_object_t obj);

/* Every object created by the scene loader is enumerable too. Pass
   DG_INVALID_OBJECT to start; returns DG_INVALID_OBJECT when done. */
dg_object_t GameObject_Next(dg_object_t obj);
int         GameObject_Count(void);

void GameObject_SetPosition(dg_object_t obj, float x, float y, float z);
void GameObject_GetPosition(dg_object_t obj, vec3_t out);
void GameObject_Translate  (dg_object_t obj, float dx, float dy, float dz);

/* Radians. head is yaw about Y, pitch about X, roll about Z, applied in that
   order. */
void GameObject_SetRotation(dg_object_t obj, float head, float pitch, float roll);
void GameObject_GetRotation(dg_object_t obj, float* head, float* pitch, float* roll);
void GameObject_SetYaw     (dg_object_t obj, float head);
void GameObject_SetScale   (dg_object_t obj, float scale);
float GameObject_GetScale  (dg_object_t obj);

/* Writing a matrix directly takes over from the position/rotation/scale
   helpers for that object until one of them is called again. Column major,
   16 floats, same convention as glMultMatrixf. */
void GameObject_SetMatrix(dg_object_t obj, const de_matrix_t m);
void GameObject_GetMatrix(dg_object_t obj, de_matrix_t out);

void GameObject_SetVisible(dg_object_t obj, int visible);
int  GameObject_IsVisible (dg_object_t obj);

/* Looks the material up by name in the library loaded from data/materials.lbr.
   Returns 0 if there is no such material. */
int GameObject_SetMaterial(dg_object_t obj, const char* materialName);

/* -------------------------------------------------------------------------
 *  Behaviors -- reusable per object logic, so several scripts can compose
 * ------------------------------------------------------------------------- */

typedef struct dg_behavior_t {
	const char* name;
	uint        state_size;    /* zeroed and handed to the callbacks; may be 0 */

	void (*on_attach)(dg_object_t self, void* state);
	void (*on_update)(dg_object_t self, void* state, float dt);
	void (*on_detach)(dg_object_t self, void* state);
} dg_behavior_t;

/* Idempotent: registering the same definition twice is harmless. */
void DG_RegisterBehavior(const dg_behavior_t* behavior);

/* Declares a behavior and registers it before main() runs, so a script never
   has to be wired up from anywhere central:
 *
 *      DG_BEHAVIOR(spinner, {
 *          .name = "spinner", .state_size = sizeof(spin_t),
 *          .on_update = Spin_Update
 *      });
 *
 * If your toolchain turns out not to run constructors, the escape hatch is to
 * call DG_RegisterBehavior(&spinner) from Game_Init -- it is idempotent, so
 * doing both is fine.
 */
#define DG_BEHAVIOR(ident, ...)                                              \
	static const dg_behavior_t ident = __VA_ARGS__;                          \
	__attribute__((constructor, used))                                       \
	static void ident##_dg_register(void) { DG_RegisterBehavior(&ident); }

/* Returns the behavior's state block, or NULL if it could not be attached.
   The pointer form works whether or not the behavior was ever registered. */
void* GameObject_AddBehavior   (dg_object_t obj, const char* behaviorName);
void* GameObject_AddBehaviorPtr(dg_object_t obj, const dg_behavior_t* behavior);
void* GameObject_GetBehavior   (dg_object_t obj, const char* behaviorName);
void  GameObject_RemoveBehavior(dg_object_t obj, const char* behaviorName);

dg_object_t GameObject_FindByBehavior(const char* behaviorName);

/* -------------------------------------------------------------------------
 *  Input
 *
 *  Game code talks about named actions, not hardware. Bind once, then poll.
 * ------------------------------------------------------------------------- */

#define DG_MAX_PORTS 4

typedef enum dg_button_t {
	DG_BTN_A = 0,
	DG_BTN_B,
	DG_BTN_X,
	DG_BTN_Y,
	DG_BTN_START,
	DG_BTN_UP,
	DG_BTN_DOWN,
	DG_BTN_LEFT,
	DG_BTN_RIGHT,
	DG_BTN_LTRIG,      /* synthesised from the analog trigger */
	DG_BTN_RTRIG,
	DG_BTN_COUNT
} dg_button_t;

typedef enum dg_axis_t {
	DG_AXIS_LX = 0,    /* -1 left .. +1 right */
	DG_AXIS_LY,        /* -1 down .. +1 up    */
	DG_AXIS_LTRIG,     /*  0 .. 1             */
	DG_AXIS_RTRIG,     /*  0 .. 1             */
	DG_AXIS_COUNT
} dg_axis_t;

typedef int dg_action_t;   /* -1 when the action table is full */

/* Get or create by name. Cheap enough to call every frame, but caching the id
   in a static is free. */
dg_action_t Input_DefineAction(const char* name);
dg_action_t Input_FindAction(const char* name);
void        Input_ClearBindings(void);

/* An action may carry several bindings; the one with the largest magnitude
   wins, so "move_x" can sit on the stick and the D-pad at once. */
void Input_BindButton    (dg_action_t action, int port, dg_button_t button);
void Input_BindAxis      (dg_action_t action, int port, dg_axis_t axis, float scale);
void Input_BindButtonPair(dg_action_t action, int port,
                          dg_button_t negative, dg_button_t positive);

float Input_Value   (dg_action_t action);   /* -1 .. 1, deadzoned */
int   Input_Held    (dg_action_t action);
int   Input_Pressed (dg_action_t action);   /* rising edge, this frame only  */
int   Input_Released(dg_action_t action);   /* falling edge, this frame only */

/* Raw device access, for when an action is the wrong abstraction. */
int   Input_PadConnected(int port);
int   Input_RawHeld     (int port, dg_button_t button);
int   Input_RawPressed  (int port, dg_button_t button);
float Input_RawAxis     (int port, dg_axis_t axis);
void  Input_SetDeadzone (float deadzone);   /* default 0.15 */

/* -------------------------------------------------------------------------
 *  Camera and light
 * ------------------------------------------------------------------------- */

void Game_GetCameraPosition(vec3_t out);
void Game_SetCameraPosition(float x, float y, float z);
void Game_GetCameraForward (vec3_t out);
void Game_GetCameraRight   (vec3_t out);
void Game_SetCameraAngles  (float head, float pitch);
void Game_GetCameraAngles  (float* head, float* pitch);
void Game_SetCameraFov     (float degrees);

/* The stock free fly camera. The default Game_Update is exactly this call, so
   if you want your own logic plus the debug camera, call it yourself. */
void Game_DefaultFlyCamera(float dt);

void Game_SetLightPosition(float x, float y, float z);
void Game_GetLightPosition(vec3_t out);
void Game_SetLightAttachedToCamera(int attached);

/* -------------------------------------------------------------------------
 *  Engine events
 * ------------------------------------------------------------------------- */

/* Applied at the end of the frame, after rendering -- never mid frame. All
   handles from the outgoing scene become invalid. */
void        Game_RequestScene(const char* sceneFilename);
const char* Game_CurrentScene(void);
void        Game_RequestQuit(void);

void  Engine_SetDebugCameraEnabled(int enabled);
int   Engine_IsDebugCameraEnabled(void);
void  Engine_SetTimeScale(float scale);
float Engine_GetTimeScale(void);
void  Engine_SetStatsEnabled(int enabled);

/* -------------------------------------------------------------------------
 *  Time, text, logging
 * ------------------------------------------------------------------------- */

float Game_DeltaTime(void);   /* seconds, scaled, clamped to 0.1 */
float Game_Time(void);        /* seconds since start */
int   Game_FPS(void);

/* Only valid from Game_RenderHUD. Truncates past 60 characters: the underlying
   Font_GenerateStringVertexArray returns a ubyte vertex count. */
void HUD_DrawText(int x, int y, const char* text);

void Game_Log(const char* fmt, ...);
