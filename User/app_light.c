#include "app_light.h"
#include "timing.h"
#include "light_sensor.h"
#include "led.h"

#define LIGHT_HYSTERESIS      300
#define LIGHT_CHECK_MS        50
#define STATE_STABLE_MS       300

typedef enum
{
    APP_LIGHT_STATE_BRIGHT = 0,
    APP_LIGHT_STATE_DARK,
    APP_LIGHT_STATE_UNKNOWN
} AppLightState;

static uint32_t s_last_light_time = 0;
static uint32_t s_pending_start_time = 0;
static uint16_t s_light_value = 0;
static uint16_t s_light_threshold = 2000;
static AppLightState s_light_state = APP_LIGHT_STATE_UNKNOWN;
static AppLightState s_candidate_state = APP_LIGHT_STATE_UNKNOWN;
static AppLightState s_pending_state = APP_LIGHT_STATE_UNKNOWN;
static AppLightTrafficMode s_traffic_mode = APP_LIGHT_TRAFFIC_AUTO;
static uint16_t s_version = 0;

static void App_Light_BumpVersion(void)
{
    s_version++;
}

static void App_Light_ApplyTraffic(void)
{
    if (s_traffic_mode == APP_LIGHT_TRAFFIC_AUTO)
    {
        if (s_light_state == APP_LIGHT_STATE_DARK)
        {
            Traffic_RedOn();
        }
        else
        {
            Traffic_GreenOn();
        }
    }
    else if (s_traffic_mode == APP_LIGHT_TRAFFIC_RED)
    {
        Traffic_RedOn();
    }
    else if (s_traffic_mode == APP_LIGHT_TRAFFIC_YELLOW)
    {
        Traffic_YellowOn();
    }
    else if (s_traffic_mode == APP_LIGHT_TRAFFIC_GREEN)
    {
        Traffic_GreenOn();
    }
    else
    {
        Traffic_AllOff();
    }
}

void App_Light_Init(void)
{
    s_light_value = LightSensor_ReadAO();

    if (s_light_value < s_light_threshold)
    {
        s_light_state = APP_LIGHT_STATE_BRIGHT;
    }
    else
    {
        s_light_state = APP_LIGHT_STATE_DARK;
    }

    s_pending_state = s_light_state;
    s_candidate_state = s_light_state;
    s_pending_start_time = Timing_GetTick();
    App_Light_ApplyTraffic();
    App_Light_BumpVersion();
}

void App_Light_Task(void)
{
    uint32_t now;
    uint16_t light_value;

    now = Timing_GetTick();

    if (now - s_last_light_time < LIGHT_CHECK_MS)
    {
        return;
    }

    s_last_light_time = now;
    light_value = LightSensor_ReadAO();

    if (light_value != s_light_value)
    {
        s_light_value = light_value;
        App_Light_BumpVersion();
    }

    s_candidate_state = s_light_state;

    if (s_light_state == APP_LIGHT_STATE_BRIGHT)
    {
        if (s_light_value > (s_light_threshold + LIGHT_HYSTERESIS))
        {
            s_candidate_state = APP_LIGHT_STATE_DARK;
        }
    }
    else if (s_light_state == APP_LIGHT_STATE_DARK)
    {
        if (s_light_value < (s_light_threshold - LIGHT_HYSTERESIS))
        {
            s_candidate_state = APP_LIGHT_STATE_BRIGHT;
        }
    }

    if (s_candidate_state != s_light_state)
    {
        if (s_candidate_state != s_pending_state)
        {
            s_pending_state = s_candidate_state;
            s_pending_start_time = now;
        }
        else if (now - s_pending_start_time >= STATE_STABLE_MS)
        {
            s_light_state = s_candidate_state;
            App_Light_ApplyTraffic();
            App_Light_BumpVersion();
        }
    }
    else
    {
        s_pending_state = s_light_state;
        s_pending_start_time = now;
    }
}

uint16_t App_Light_GetAO(void)
{
    return s_light_value;
}

uint8_t App_Light_IsDark(void)
{
    if (s_light_state == APP_LIGHT_STATE_DARK)
    {
        return 1;
    }

    return 0;
}

uint16_t App_Light_GetThreshold(void)
{
    return s_light_threshold;
}

void App_Light_SetThreshold(uint16_t threshold)
{
    if (threshold < 300)
    {
        threshold = 300;
    }

    if (threshold > 3800)
    {
        threshold = 3800;
    }

    if (threshold != s_light_threshold)
    {
        s_light_threshold = threshold;
        App_Light_BumpVersion();
    }
}

void App_Light_SetTrafficMode(AppLightTrafficMode mode)
{
    if ((mode != APP_LIGHT_TRAFFIC_AUTO) &&
        (mode != APP_LIGHT_TRAFFIC_RED) &&
        (mode != APP_LIGHT_TRAFFIC_YELLOW) &&
        (mode != APP_LIGHT_TRAFFIC_GREEN) &&
        (mode != APP_LIGHT_TRAFFIC_OFF))
    {
        mode = APP_LIGHT_TRAFFIC_AUTO;
    }

    if (mode != s_traffic_mode)
    {
        s_traffic_mode = mode;
        App_Light_ApplyTraffic();
        App_Light_BumpVersion();
    }
}

AppLightTrafficMode App_Light_GetTrafficMode(void)
{
    return s_traffic_mode;
}

uint16_t App_Light_GetVersion(void)
{
    return s_version;
}
