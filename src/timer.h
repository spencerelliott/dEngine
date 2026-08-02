
/*
 *  timer.h
 *  dEngine
 *
 *  Created by fabien sanglard on 15/08/09.
 *
 */

#ifndef FS_TIMER
#define FS_TIMER

#include "globals.h"

void Timer_resetTime(void);
void Timer_tick(void);

void Timer_Pause(void);
void Timer_Resume(void);

void Timer_ForceTimeIncrement(int ms);

/* Discards the time that has elapsed since the last tick, so the next frame
   starts from now. Use it after a long stall such as a scene load.
   Timer_Pause/Timer_Resume cannot do this: Timer_tick assigns
   lastTime = currentTime before it samples the clock, which immediately undoes
   whatever Timer_Resume set. */
void Timer_Skip(void);

extern int fps;
extern unsigned int simulationTime;
extern int timediff;

#endif