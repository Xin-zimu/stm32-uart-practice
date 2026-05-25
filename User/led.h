#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

void LED_Init(void);

void Traffic_AllOff(void);
void Traffic_RedOn(void);
void Traffic_YellowOn(void);
void Traffic_GreenOn(void);

#endif

