#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f10x.h"

/* Low-level DS18B20 1-Wire temperature sensor driver on PB0. */
void DS18B20_Init(void);

/* Returns 1 when the sensor responds to a reset pulse. */
uint8_t DS18B20_Check(void);

/* Starts a conversion; call DS18B20_ReadTemp10 after the conversion time. */
uint8_t DS18B20_StartConvert(void);

/* Reads the latest temperature in 0.1C units, for example 256 means 25.6C. */
uint8_t DS18B20_ReadTemp10(int16_t *temp10);

#endif

