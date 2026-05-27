#include "app_light_control.h"
#include "app_light.h"

void App_LightControl_Init(void)
{
    App_Light_Init();
    App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_AUTO);
}

void App_LightControl_Task(void)
{
    App_Light_Task();
}
