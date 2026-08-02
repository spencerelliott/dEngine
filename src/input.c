/*
 *  input.c
 *  dEngine
 *
 *  Reads the Dreamcast maple bus into engine-neutral pad state.
 *
 *  The KOS button constants are mapped through an explicit table rather than
 *  being used directly, so the values in dgame.h stay stable no matter what
 *  CONT_* happens to be defined as, and so no game file ever needs a KOS
 *  header.
 */

#include "input.h"
#include "game_internal.h"

#ifdef DREAMCAST
#include <dc/maple.h>
#include <dc/maple/controller.h>
#endif

dg_pad_state_t dg_pads[DG_MAX_PORTS];

static float inp_deadzone = 0.15f;

/* Analog triggers double as digital buttons past this point. */
#define INP_TRIGGER_THRESHOLD 0.5f

void Input_SetDeadzone(float deadzone)
{
	if (deadzone < 0.0f)
		deadzone = 0.0f;
	if (deadzone > 0.9f)
		deadzone = 0.9f;

	inp_deadzone = deadzone;
}

/* Rescales so the live part of the throw still reaches 1.0 instead of losing
   the bottom of the range to the deadzone. */
static float INP_ApplyDeadzone(float value)
{
	float magnitude = value < 0.0f ? -value : value;

	if (magnitude <= inp_deadzone)
		return 0.0f;

	magnitude = (magnitude - inp_deadzone) / (1.0f - inp_deadzone);

	if (magnitude > 1.0f)
		magnitude = 1.0f;

	return value < 0.0f ? -magnitude : magnitude;
}

void INP_Init(void)
{
	memset(dg_pads, 0, sizeof(dg_pads));
}

#ifdef DREAMCAST

static const struct {
	uint32 kos;
	dg_button_t button;
} inp_buttonMap[] = {
	{ CONT_A,           DG_BTN_A     },
	{ CONT_B,           DG_BTN_B     },
	{ CONT_X,           DG_BTN_X     },
	{ CONT_Y,           DG_BTN_Y     },
	{ CONT_START,       DG_BTN_START },
	{ CONT_DPAD_UP,     DG_BTN_UP    },
	{ CONT_DPAD_DOWN,   DG_BTN_DOWN  },
	{ CONT_DPAD_LEFT,   DG_BTN_LEFT  },
	{ CONT_DPAD_RIGHT,  DG_BTN_RIGHT }
};

#define INP_BUTTON_MAP_SIZE ((int)(sizeof(inp_buttonMap) / sizeof(inp_buttonMap[0])))

void INP_Poll(void)
{
	int port;
	int i;
	maple_device_t* device;
	cont_state_t* state;
	dg_pad_state_t* pad;

	for (port = 0; port < DG_MAX_PORTS; port++)
	{
		pad = &dg_pads[port];

		pad->buttonsPrev = pad->buttons;
		pad->buttons = 0;

		/* Nth attached controller rather than Nth physical port, which is the
		   behaviour a game wants out of "player N". */
		device = maple_enum_type(port, MAPLE_FUNC_CONTROLLER);

		if (!device)
		{
			pad->connected = 0;
			memset(pad->axes, 0, sizeof(pad->axes));
			continue;
		}

		state = (cont_state_t*)maple_dev_status(device);

		if (!state)
		{
			pad->connected = 0;
			memset(pad->axes, 0, sizeof(pad->axes));
			continue;
		}

		pad->connected = 1;

		for (i = 0; i < INP_BUTTON_MAP_SIZE; i++)
		{
			if (state->buttons & inp_buttonMap[i].kos)
				pad->buttons |= DG_BTN_MASK(inp_buttonMap[i].button);
		}

		/* Sticks arrive as roughly -128..127; Y is inverted so that up is
		   positive, which is what game code expects. */
		pad->axes[DG_AXIS_LX] = INP_ApplyDeadzone(state->joyx / 128.0f);
		pad->axes[DG_AXIS_LY] = INP_ApplyDeadzone(state->joyy / -128.0f);

		pad->axes[DG_AXIS_LTRIG] = INP_ApplyDeadzone(state->ltrig / 255.0f);
		pad->axes[DG_AXIS_RTRIG] = INP_ApplyDeadzone(state->rtrig / 255.0f);

		if (pad->axes[DG_AXIS_LTRIG] > INP_TRIGGER_THRESHOLD)
			pad->buttons |= DG_BTN_MASK(DG_BTN_LTRIG);
		if (pad->axes[DG_AXIS_RTRIG] > INP_TRIGGER_THRESHOLD)
			pad->buttons |= DG_BTN_MASK(DG_BTN_RTRIG);
	}
}

#else

/* Host builds have no maple bus. Keeping this branch compilable is what lets
   the rest of the game layer be syntax checked with a normal gcc. */
void INP_Poll(void)
{
	int port;

	for (port = 0; port < DG_MAX_PORTS; port++)
	{
		dg_pads[port].buttonsPrev = dg_pads[port].buttons;
		dg_pads[port].buttons = 0;
		dg_pads[port].connected = 0;
		memset(dg_pads[port].axes, 0, sizeof(dg_pads[port].axes));
	}
}

#endif

int Input_PadConnected(int port)
{
	if (port < 0 || port >= DG_MAX_PORTS)
		return 0;

	return dg_pads[port].connected;
}

int Input_RawHeld(int port, dg_button_t button)
{
	if (port < 0 || port >= DG_MAX_PORTS || button < 0 || button >= DG_BTN_COUNT)
		return 0;

	return (dg_pads[port].buttons & DG_BTN_MASK(button)) != 0;
}

int Input_RawPressed(int port, dg_button_t button)
{
	uint mask;

	if (port < 0 || port >= DG_MAX_PORTS || button < 0 || button >= DG_BTN_COUNT)
		return 0;

	mask = DG_BTN_MASK(button);

	return (dg_pads[port].buttons & mask) && !(dg_pads[port].buttonsPrev & mask);
}

float Input_RawAxis(int port, dg_axis_t axis)
{
	if (port < 0 || port >= DG_MAX_PORTS || axis < 0 || axis >= DG_AXIS_COUNT)
		return 0.0f;

	return dg_pads[port].axes[axis];
}
