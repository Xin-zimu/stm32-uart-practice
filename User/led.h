#ifndef __LED_H
#define __LED_H

#include "stm32f10x.h"

/* Traffic-light LED driver: red=PA5, yellow=PA6, green=PA7. */
void LED_Init(void);

/* Low-level LED helpers. Higher application layers choose when to call them. */
void Traffic_AllOff(void);
void Traffic_RedOn(void);
void Traffic_YellowOn(void);
void Traffic_GreenOn(void);

#endif

