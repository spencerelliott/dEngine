/*
 *  game_internal.h
 *  dEngine
 *
 *  Entry points the engine uses to drive the game layer. Game scripts should
 *  include dgame.h instead; nothing here is part of the scripting surface.
 */

#pragma once

#include "dgame.h"

/* ---- startup -------------------------------------------------------- */

/* Fills cfg with the engine defaults, hands it to Game_Configure, then stores
   the result. Called from main() before dEngine_Init. */
void DG_ApplyConfig(dg_config_t* cfg);

/* NULL when the game did not name one, in which case data/config.cfg wins. */
const char* DG_GetBootScene(void);

/* Runs behavior registration, Game_ConfigureInput and Game_Init, in that
   order. Called at the end of dEngine_Init, once a scene and camera exist. */
void DG_Startup(void);
void DG_ShutdownAll(void);

/* ---- per frame ------------------------------------------------------ */

void DG_FrameBegin(void);   /* poll input, behaviors, Game_Update */
void DG_FrameLate(void);    /* Game_LateUpdate */
void DG_FrameEnd(void);     /* deferred destroys, then any pending scene swap */

int  DG_QuitRequested(void);

/* ---- object layer --------------------------------------------------- */

void DG_ObjectsInit(void);
void DG_UpdateBehaviors(float dt);
void DG_FlushDestroys(void);

/* Adopts entities the map loader created; run after every scene load. */
void DG_AdoptSceneEntities(void);

/* Diagnostic: how many behaviors made it into the registry. */
int DG_RegisteredBehaviorCount(void);

/* Detaches every behavior and drops all state. Runs before a scene unload. */
void DG_DetachAllBehaviors(void);

/* ---- input layer ---------------------------------------------------- */

void INP_Init(void);
void INP_Poll(void);            /* raw devices -> port state */
void INP_UpdateActions(void);   /* port state -> action values */
void INP_InstallDefaultBindings(void);

/* Feeds the built in free fly camera actions into the Comm_ queue. */
void DG_UpdateDebugCamera(float dt);

/* ---- rendering ------------------------------------------------------ */

/* Called from SCR_RenderFrame inside the 2D pass. */
void DG_RenderHUD(void);
