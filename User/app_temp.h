#ifndef __APP_TEMP_H
#define __APP_TEMP_H

#include "stm32f10x.h"

/* 初始化温度采样状态机，并启动第一次 DS18B20 温度转换。 */
void App_Temp_Init(void);

/*
 * 非阻塞温度任务。
 * 主循环中反复调用，内部用状态机等待 DS18B20 转换完成。
 */
void App_Temp_Task(void);

/* 返回当前温度数据是否有效。 */
uint8_t App_Temp_IsValid(void);

/* 获取温度值，单位为 0.1 摄氏度，例如 256 表示 25.6C。 */
int16_t App_Temp_GetTemp10(void);

/*
 * 数据版本号。
 * 当温度值或有效性变化时递增，供 UI 判断是否需要刷新温度区域。
 */
uint16_t App_Temp_GetVersion(void);

#endif
