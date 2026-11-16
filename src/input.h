/*
 *  input.h
 *  dEngine
 *
 *  Raw device layer. input.c is the only translation unit in the game layer
 *  that touches <kos.h>; everything above it, including all of game/, works in
 *  terms of the engine's own button and axis enums from dgame.h.
 */

#pragma once

#include "dgame.h"

#define DG_BTN_MASK(button) (1u << (button))

typedef struct dg_pad_state_t {
	uchar connected;
	uint  buttons;
	uint  buttonsPrev;
	float axes[DG_AXIS_COUNT];
} dg_pad_state_t;

extern dg_pad_state_t dg_pads[DG_MAX_PORTS];
