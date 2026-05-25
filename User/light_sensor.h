#ifndef __LIGHT_SENSOR_H
#define __LIGHT_SENSOR_H

#include "stm32f10x.h"

void LightSensor_Init(void);
uint8_t LightSensor_ReadDO(void);
uint8_t LightSensor_IsDark(void);

void LightSensor_ADC_Init(void);
uint16_t LightSensor_ReadAO(void);

#endif
