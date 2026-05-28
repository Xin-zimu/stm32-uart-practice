#ifndef __APP_LIGHT_H
#define __APP_LIGHT_H

#include "stm32f10x.h"

/*
 * 交通灯模式由 app_light 统一管理。
 * UI 和串口层只设置这里的模式，不直接调用 led.c 里的 Traffic_* 底层函数。
 */
typedef enum
{
    APP_LIGHT_TRAFFIC_AUTO = 0,
    APP_LIGHT_TRAFFIC_RED,
    APP_LIGHT_TRAFFIC_YELLOW,
    APP_LIGHT_TRAFFIC_GREEN,
    APP_LIGHT_TRAFFIC_OFF
} AppLightTrafficMode;

/* 初始化光照应用层状态，并按默认 AUTO 模式刷新交通灯。 */
void App_Light_Init(void);

/* 周期读取光敏 AO，带滞回和稳定时间判断亮/暗状态。 */
void App_Light_Task(void);

/* 获取最近一次采样到的光敏 AO 数值。 */
uint16_t App_Light_GetAO(void);

/* 返回当前稳定后的亮暗状态：1 表示暗，0 表示亮。 */
uint8_t App_Light_IsDark(void);

/* 获取/设置光照阈值，设置时会在 app_light.c 内部做范围限制。 */
uint16_t App_Light_GetThreshold(void);
void App_Light_SetThreshold(uint16_t threshold);

/* 设置/读取当前交通灯模式。 */
void App_Light_SetTrafficMode(AppLightTrafficMode mode);
AppLightTrafficMode App_Light_GetTrafficMode(void);

/*
 * 数据版本号。
 * 当 AO、亮暗状态、阈值或交通灯模式变化时递增，供 UI 判断是否需要刷新。
 */
uint16_t App_Light_GetVersion(void);

#endif
