/*
 *  game_hooks.c
 *  dEngine
 *
 *  Default implementations of every game lifecycle hook.
 *
 *  Each definition here is weak, so a plain definition of the same function in
 *  any file under game/ replaces it at link time. The attribute lives on the
 *  definition and never on the declaration in dgame.h -- that asymmetry is
 *  load bearing. If the header carried it, the game's own definition would be
 *  marked weak too, the linker would face two weak symbols and pick whichever
 *  it saw first, and which one won would depend on link order.
 *
 *  This also relies on the game objects being handed straight to
 *  add_executable rather than bundled into a static library: an archive member
 *  is only pulled in to satisfy an undefined symbol, and these defaults mean
 *  nothing is ever undefined.
 */

#include "dgame.h"
#include "game_internal.h"

#define DE_WEAK __attribute__((weak))

DE_WEAK void Game_Configure(dg_config_t* cfg)
{
	(void)cfg;
}

DE_WEAK void Game_ConfigureInput(void)
{
	/* The engine has already installed its default bindings by this point. */
}

DE_WEAK void Game_Init(void)
{
}

DE_WEAK void Game_Update(float dt)
{
	(void)dt;
}

DE_WEAK void Game_LateUpdate(float dt)
{
	(void)dt;
}

DE_WEAK void Game_RenderHUD(void)
{
}

DE_WEAK void Game_SceneLoaded(const char* scene)
{
	(void)scene;
}

DE_WEAK void Game_SceneUnloading(const char* scene)
{
	(void)scene;
}

DE_WEAK void Game_Shutdown(void)
{
}
