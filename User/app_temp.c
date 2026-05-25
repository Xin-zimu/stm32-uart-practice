#include "app_temp.h"
#include "timing.h"
#include "ds18b20.h"

#define TEMP_CONVERT_TIME_MS    750

static uint8_t s_ds18b20_busy = 0;
static uint32_t s_temp_start_time = 0;
static int16_t s_temp10 = 0;
static uint8_t s_temp_valid = 0;

void App_Temp_Init(void)
{
    if (DS18B20_StartConvert())
    {
        s_ds18b20_busy = 1;
        s_temp_start_time = Timing_GetTick();
    }
    else
    {
        s_ds18b20_busy = 0;
        s_temp_valid = 0;
    }
}

void App_Temp_Task(void)
{
    uint32_t now;

    now = Timing_GetTick();

    if (s_ds18b20_busy == 0)
    {
        if (DS18B20_StartConvert())
        {
            s_ds18b20_busy = 1;
            s_temp_start_time = now;
        }
    }
    else if (now - s_temp_start_time >= TEMP_CONVERT_TIME_MS)
    {
        s_ds18b20_busy = 0;

        if (DS18B20_ReadTemp10(&s_temp10))
        {
            s_temp_valid = 1;
        }
        else
        {
            s_temp_valid = 0;
        }

        if (DS18B20_StartConvert())
        {
            s_ds18b20_busy = 1;
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
