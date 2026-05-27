#ifndef __APP_TEMP_H
#define __APP_TEMP_H

#include "stm32f10x.h"

void App_Temp_Init(void);
void App_Temp_Task(void);
uint8_t App_Temp_IsValid(void);
int16_t App_Temp_GetTemp10(void);
uint16_t App_Temp_GetVersion(void);

#endif
