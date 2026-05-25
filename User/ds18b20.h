#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f10x.h"

void DS18B20_Init(void);
uint8_t DS18B20_Check(void);
uint8_t DS18B20_StartConvert(void);
uint8_t DS18B20_ReadTemp10(int16_t *temp10);

#endif

