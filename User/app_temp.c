#include "app_temp.h"
#include "timing.h"
#include "ds18b20.h"

#define TEMP_CONVERT_TIME_MS    750
#define TEMP_ERROR_RETRY_MS     1000

typedef enum
{
    APP_TEMP_STATE_START = 0,
    APP_TEMP_STATE_WAIT,
    APP_TEMP_STATE_READ,
    APP_TEMP_STATE_ERROR
} AppTempState;

static AppTempState s_temp_state = APP_TEMP_STATE_START;
static uint32_t s_temp_start_time = 0;
static int16_t s_temp10 = 0;
static uint8_t s_temp_valid = 0;
static uint16_t s_temp_version = 0;

static void App_Temp_BumpVersion(void)
{
    s_temp_version++;
}

static void App_Temp_SetValid(uint8_t valid)
{
    if (valid != s_temp_valid)
    {
        s_temp_valid = valid;
        App_Temp_BumpVersion();
    }
}

static void App_Temp_StartConvert(uint32_t now)
{
    if (DS18B20_StartConvert())
    {
        s_temp_start_time = now;
        s_temp_state = APP_TEMP_STATE_WAIT;
    }
    else
    {
        App_Temp_SetValid(0);
        s_temp_start_time = now;
        s_temp_state = APP_TEMP_STATE_ERROR;
    }
}

void App_Temp_Init(void)
{
    s_temp_state = APP_TEMP_STATE_START;
    s_temp_start_time = Timing_GetTick();
    App_Temp_StartConvert(s_temp_start_time);
}

void App_Temp_Task(void)
{
    uint32_t now;
    int16_t temp10;

    now = Timing_GetTick();

    if (s_temp_state == APP_TEMP_STATE_START)
    {
        App_Temp_StartConvert(now);
    }
    else if (s_temp_state == APP_TEMP_STATE_WAIT)
    {
        if (now - s_temp_start_time >= TEMP_CONVERT_TIME_MS)
        {
            s_temp_state = APP_TEMP_STATE_READ;
        }
    }
    else if (s_temp_state == APP_TEMP_STATE_READ)
    {
        if (DS18B20_ReadTemp10(&temp10))
        {
            if ((s_temp_valid == 0) || (temp10 != s_temp10))
            {
                s_temp10 = temp10;
                s_temp_valid = 1;
                App_Temp_BumpVersion();
            }
        }
        else
        {
            App_Temp_SetValid(0);
        }

        App_Temp_StartConvert(now);
    }
    else
    {
        if (now - s_temp_start_time >= TEMP_ERROR_RETRY_MS)
        {
            s_temp_state = APP_TEMP_STATE_START;
            s_temp_start_time = now;
        }
    }
}

uint8_t App_Temp_IsValid(void)
{
    return s_temp_valid;
}

int16_t App_Temp_GetTemp10(void)
{
    return s_temp10;
}

uint16_t App_Temp_GetVersion(void)
{
    return s_temp_version;
}
