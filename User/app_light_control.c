#include "app_light_control.h"
#include "timing.h"
#include "led.h"
#include "light_sensor.h"

#define LIGHT_CHECK_INTERVAL_MS     100

static uint32_t s_last_check_time = 0;

void App_LightControl_Init(void)
{
    s_last_check_time = Timing_GetTick();

    if (LightSensor_IsDark())
    {
        Traffic_RedOn();
    }
    else
    {
        Traffic_GreenOn();
    }
}

void App_LightControl_Task(void)
{
    uint32_t now = Timing_GetTick();

    if (now - s_last_check_time >= LIGHT_CHECK_INTERVAL_MS)
    {
        s_last_check_time = now;

        if (LightSensor_IsDark())
        {
            Traffic_RedOn();      // ╟╣ -> ╨Л╣ф
        }
        else
        {
            Traffic_GreenOn();    // аа -> бл╣ф
        }
    }
}
