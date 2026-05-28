#include "app_light_control.h"
#include "app_light.h"

/*
 * 兼容旧工程文件中可能仍然编译/调用 App_LightControl_* 的情况。
 * 实际业务逻辑已经收口到 app_light，避免两套模块同时控制交通灯。
 */
void App_LightControl_Init(void)
{
    App_Light_Init();
    App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_AUTO);
}

void App_LightControl_Task(void)
{
    App_Light_Task();
}
