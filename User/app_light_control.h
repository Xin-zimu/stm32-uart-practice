#ifndef __APP_LIGHT_CONTROL_H
#define __APP_LIGHT_CONTROL_H

#include "stm32f10x.h"

/*
 * 旧版光照控制接口兼容层。
 * 新代码应优先使用 app_light.h，不再直接维护第二套光照控制逻辑。
 */
void App_LightControl_Init(void);
void App_LightControl_Task(void);

#endif
