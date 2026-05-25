#ifndef __TIMING_H
#define __TIMING_H

#include "stm32f10x.h"

void Timing_Init(void);
void Timing_IncTick(void);
uint32_t Timing_GetTick(void);

#endif
