#include "app_temp.h"
#include "timing.h"
#include "ds18b20.h"

#define TEMP_CONVERT_TIME_MS    750
#define TEMP_ERROR_RETRY_MS     1000

/*
 * DS18B20 转换需要等待，不能在主循环里阻塞 750ms。
 * 这里用显式状态机把“启动转换、等待、读取、错误重试”拆开。
 */
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

/* 温度数据变化通知，供 UI 做按需刷新。 */
static void App_Temp_BumpVersion(void)
{
    s_temp_version++;
}

/* 统一设置有效标志，只有有效性真的变化时才递增版本号。 */
static void App_Temp_SetValid(uint8_t valid)
{
    if (valid != s_temp_valid)
    {
        s_temp_valid = valid;
        App_Temp_BumpVersion();
    }
}

/*
 * 启动一次 DS18B20 温度转换。
 * 启动失败说明总线或传感器可能异常，进入 ERROR 后稍后重试。
 */
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
        /* START 状态只负责发起一次新的转换。 */
        App_Temp_StartConvert(now);
    }
    else if (s_temp_state == APP_TEMP_STATE_WAIT)
    {
        /* 转换时间到后再进入 READ，避免阻塞等待。 */
        if (now - s_temp_start_time >= TEMP_CONVERT_TIME_MS)
        {
            s_temp_state = APP_TEMP_STATE_READ;
        }
    }
    else if (s_temp_state == APP_TEMP_STATE_READ)
    {
        /* 读取成功后立即启动下一次转换，形成连续采样。 */
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
        /* ERROR 状态不忙等，间隔一段时间后回到 START 重试。 */
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
