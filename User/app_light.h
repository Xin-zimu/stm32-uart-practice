#ifndef __APP_LIGHT_H
#define __APP_LIGHT_H

#include "stm32f10x.h"

typedef enum
{
    APP_LIGHT_TRAFFIC_AUTO = 0,
    APP_LIGHT_TRAFFIC_RED,
    APP_LIGHT_TRAFFIC_YELLOW,
    APP_LIGHT_TRAFFIC_GREEN,
    APP_LIGHT_TRAFFIC_OFF
} AppLightTrafficMode;

void App_Light_Init(void);
void App_Light_Task(void);
uint16_t App_Light_GetAO(void);
uint8_t App_Light_IsDark(void);
uint16_t App_Light_GetThreshold(void);
void App_Light_SetThreshold(uint16_t threshold);
void App_Light_SetTrafficMode(AppLightTrafficMode mode);
AppLightTrafficMode App_Light_GetTrafficMode(void);
uint16_t App_Light_GetVersion(void);

#endif
