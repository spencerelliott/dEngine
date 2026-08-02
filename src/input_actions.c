/*
 *  input_actions.c
 *  dEngine
 *
 *  Named actions over the raw pad state. Game code binds once and then polls
 *  by action, so gameplay never mentions a physical button.
 *
 *  An action can carry several bindings at once. They are combined by taking
 *  the largest magnitude contribution rather than summing, so binding "move_x"
 *  to both the analog stick and the D-pad lets either drive it without the two
 *  fighting when both are active.
 */

#include "input.h"
#include "game_internal.h"

#define DG_MAX_ACTIONS             32
#define DG_MAX_BINDINGS_PER_ACTION  4
#define DG_ACTION_NAME_MAX         24

/* An action counts as held past this magnitude. */
#define DG_DIGITAL_THRESHOLD 0.5f

enum {
	DG_BIND_BUTTON,
	DG_BIND_AXIS,
	DG_BIND_BUTTON_PAIR
};

typedef struct dg_binding_t {
	uchar kind;
	uchar port;
	uchar a;       /* button or axis */
	uchar b;       /* second button, for pairs */
	float scale;
} dg_binding_t;

typedef struct dg_action_entry_t {
	char         name[DG_ACTION_NAME_MAX];
	dg_binding_t bindings[DG_MAX_BINDINGS_PER_ACTION];
	uchar        numBindings;
	float        value;
	float        valuePrev;
} dg_action_entry_t;

static dg_action_entry_t inp_actions[DG_MAX_ACTIONS];
static int inp_numActions;

dg_action_t Input_FindAction(const char* name)
{
	int i;

	if (!name)
		return -1;

	for (i = 0; i < inp_numActions; i++)
	{
		if (!strcmp(inp_actions[i].name, name))
			return i;
	}

	return -1;
}

dg_action_t Input_DefineAction(const char* name)
{
	dg_action_t action;
	dg_action_entry_t* entry;

	if (!name || !name[0])
		return -1;

	action = Input_FindAction(name);

	if (action >= 0)
		return action;

	if (inp_numActions >= DG_MAX_ACTIONS)
	{
		printf("[Input] Action table full (max %d), '%s' ignored.\n", DG_MAX_ACTIONS, name);
		return -1;
	}

	entry = &inp_actions[inp_numActions];
	memset(entry, 0, sizeof(*entry));
	strncpy(entry->name, name, DG_ACTION_NAME_MAX - 1);
	entry->name[DG_ACTION_NAME_MAX - 1] = '\0';

	return inp_numActions++;
}

void Input_ClearBindings(void)
{
	memset(inp_actions, 0, sizeof(inp_actions));
	inp_numActions = 0;
}

static dg_binding_t* INP_AddBinding(dg_action_t action, int port)
{
	dg_action_entry_t* entry;
	dg_binding_t* binding;

	if (action < 0 || action >= inp_numActions)
		return 0;

	if (port < 0 || port >= DG_MAX_PORTS)
		return 0;

	entry = &inp_actions[action];

	if (entry->numBindings >= DG_MAX_BINDINGS_PER_ACTION)
	{
		printf("[Input] '%s' already has %d bindings, ignoring.\n",
		       entry->name, DG_MAX_BINDINGS_PER_ACTION);
		return 0;
	}

	binding = &entry->bindings[entry->numBindings++];
	memset(binding, 0, sizeof(*binding));
	binding->port = (uchar)port;
	binding->scale = 1.0f;

	return binding;
}

void Input_BindButton(dg_action_t action, int port, dg_button_t button)
{
	dg_binding_t* binding;

	if (button < 0 || button >= DG_BTN_COUNT)
		return;

	binding = INP_AddBinding(action, port);

	if (!binding)
		return;

	binding->kind = DG_BIND_BUTTON;
	binding->a = (uchar)button;
}

void Input_BindAxis(dg_action_t action, int port, dg_axis_t axis, float scale)
{
	dg_binding_t* binding;

	if (axis < 0 || axis >= DG_AXIS_COUNT)
		return;

	binding = INP_AddBinding(action, port);

	if (!binding)
		return;

	binding->kind = DG_BIND_AXIS;
	binding->a = (uchar)axis;
	binding->scale = scale;
}

void Input_BindButtonPair(dg_action_t action, int port,
                          dg_button_t negative, dg_button_t positive)
{
	dg_binding_t* binding;

	if (negative < 0 || negative >= DG_BTN_COUNT)
		return;
	if (positive < 0 || positive >= DG_BTN_COUNT)
		return;

	binding = INP_AddBinding(action, port);

	if (!binding)
		return;

	binding->kind = DG_BIND_BUTTON_PAIR;
	binding->a = (uchar)negative;
	binding->b = (uchar)positive;
}

static float INP_EvaluateBinding(const dg_binding_t* binding)
{
	const dg_pad_state_t* pad = &dg_pads[binding->port];
	float value;

	switch (binding->kind)
	{
		case DG_BIND_BUTTON:
			return (pad->buttons & DG_BTN_MASK(binding->a)) ? binding->scale : 0.0f;

		case DG_BIND_AXIS:
			return pad->axes[binding->a] * binding->scale;

		case DG_BIND_BUTTON_PAIR:
			value = 0.0f;
			if (pad->buttons & DG_BTN_MASK(binding->a))
				value -= 1.0f;
			if (pad->buttons & DG_BTN_MASK(binding->b))
				value += 1.0f;
			return value * binding->scale;

		default:
			return 0.0f;
	}
}

void INP_UpdateActions(void)
{
	int i;
	int b;
	dg_action_entry_t* entry;
	float value;
	float best;
	float magnitude;
	float bestMagnitude;

	for (i = 0; i < inp_numActions; i++)
	{
		entry = &inp_actions[i];
		entry->valuePrev = entry->value;

		best = 0.0f;
		bestMagnitude = 0.0f;

		for (b = 0; b < entry->numBindings; b++)
		{
			value = INP_EvaluateBinding(&entry->bindings[b]);
			magnitude = value < 0.0f ? -value : value;

			if (magnitude > bestMagnitude)
			{
				bestMagnitude = magnitude;
				best = value;
			}
		}

		entry->value = best;
	}
}

float Input_Value(dg_action_t action)
{
	if (action < 0 || action >= inp_numActions)
		return 0.0f;

	return inp_actions[action].value;
}

int Input_Held(dg_action_t action)
{
	float value;

	if (action < 0 || action >= inp_numActions)
		return 0;

	value = inp_actions[action].value;

	return (value > DG_DIGITAL_THRESHOLD) || (value < -DG_DIGITAL_THRESHOLD);
}

static int INP_WasHeld(dg_action_t action)
{
	float value = inp_actions[action].valuePrev;

	return (value > DG_DIGITAL_THRESHOLD) || (value < -DG_DIGITAL_THRESHOLD);
}

int Input_Pressed(dg_action_t action)
{
	if (action < 0 || action >= inp_numActions)
		return 0;

	return Input_Held(action) && !INP_WasHeld(action);
}

int Input_Released(dg_action_t action)
{
	if (action < 0 || action >= inp_numActions)
		return 0;

	return !Input_Held(action) && INP_WasHeld(action);
}

/* The built in free fly camera. Prefixed so a game defining "move_x" can never
   collide with it, and so Input_ClearBindings followed by the game's own setup
   drops the debug camera cleanly. */
void INP_InstallDefaultBindings(void)
{
	dg_action_t move   = Input_DefineAction("__cam_move");
	dg_action_t strafe = Input_DefineAction("__cam_strafe");
	dg_action_t lookX  = Input_DefineAction("__cam_look_x");
	dg_action_t lookY  = Input_DefineAction("__cam_look_y");

	Input_BindAxis(move, 0, DG_AXIS_LY, 1.0f);
	Input_BindAxis(strafe, 0, DG_AXIS_LX, 1.0f);

	/* One analog stick, so the triggers do the yawing. */
	Input_BindAxis(lookX, 0, DG_AXIS_RTRIG, 1.0f);
	Input_BindAxis(lookX, 0, DG_AXIS_LTRIG, -1.0f);

	Input_BindButtonPair(lookY, 0, DG_BTN_DOWN, DG_BTN_UP);
}
