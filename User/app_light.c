#include "app_light.h"
#include "timing.h"
#include "light_sensor.h"
#include "led.h"

#define LIGHT_HYSTERESIS      300
#define LIGHT_CHECK_MS        50
#define STATE_STABLE_MS       300

#define STATE_BRIGHT          0
#define STATE_DARK            1
#define STATE_UNKNOWN         2

static uint32_t s_last_light_time = 0;
static uint32_t s_pending_start_time = 0;
static uint16_t s_light_value = 0;
static uint16_t s_light_threshold = 2000;
static uint8_t s_light_state = STATE_UNKNOWN;
static uint8_t s_candidate_state = STATE_UNKNOWN;
static uint8_t s_pending_state = STATE_UNKNOWN;
static uint8_t s_auto_traffic = 1;

static void App_Light_ApplyTraffic(uint8_t state)
{
    if (s_auto_traffic == 0)
    {
        return;
    }

    if (state == STATE_DARK)
    {
        Traffic_RedOn();
    }
    else
    {
        Traffic_GreenOn();
    }
}

void App_Light_Init(void)
{
    s_light_value = LightSensor_ReadAO();

    if (s_light_value < s_light_threshold)
    {
        s_light_state = STATE_BRIGHT;
    }
    else
    {
        s_light_state = STATE_DARK;
    }

    s_pending_state = s_light_state;
    s_candidate_state = s_light_state;
    s_pending_start_time = Timing_GetTick();
    App_Light_ApplyTraffic(s_light_state);
}

void App_Light_Task(void)
{
    uint32_t now;

    now = Timing_GetTick();

    if (now - s_last_light_time < LIGHT_CHECK_MS)
    {
        return;
    }

    s_last_light_time = now;
    s_light_value = LightSensor_ReadAO();
    s_candidate_state = s_light_state;

    if (s_light_state == STATE_BRIGHT)
    {
        if (s_light_value > (s_light_threshold + LIGHT_HYSTERESIS))
        {
            s_candidate_state = STATE_DARK;
        }
    }
    else if (s_light_state == STATE_DARK)
    {
        if (s_light_value < (s_light_threshold - LIGHT_HYSTERESIS))
        {
            s_candidate_state = STATE_BRIGHT;
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
            App_Light_ApplyTraffic(s_light_state);
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
    if (s_light_state == STATE_DARK)
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

    s_light_threshold = threshold;
}

uint8_t App_Light_IsAutoTraffic(void)
{
    return s_auto_traffic;
}

void App_Light_SetAutoTraffic(uint8_t enable)
{
    if (enable)
    {
        s_auto_traffic = 1;
        App_Light_ApplyTraffic(s_light_state);
    }
    else
    {
        s_auto_traffic = 0;
    }
}
