#ifndef __APP_LIGHT_H
#define __APP_LIGHT_H

#include "stm32f10x.h"

void App_Light_Init(void);
void App_Light_Task(void);
uint16_t App_Light_GetAO(void);
uint8_t App_Light_IsDark(void);
uint16_t App_Light_GetThreshold(void);
void App_Light_SetThreshold(uint16_t threshold);
uint8_t App_Light_IsAutoTraffic(void);
void App_Light_SetAutoTraffic(uint8_t enable);

#endif
