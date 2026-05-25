#include "stm32f10x.h"
#include "led.h"
#include "timing.h"
#include "light_sensor.h"
#include "oled.h"
#include "ds18b20.h"
#include "delay.h"
#include "app_light.h"
#include "app_temp.h"
#include "joystick.h"
#include "app_ui.h"

int main(void)
{
    delay_init();

    LED_Init();
    Timing_Init();

    LightSensor_Init();
    LightSensor_ADC_Init();

    OLED_Init();
    DS18B20_Init();
    Joystick_Init();

    OLED_Clear();

    App_Light_Init();
    App_Temp_Init();
    App_UI_Init();

    while (1)
    {
        App_Light_Task();
        App_Temp_Task();
        Joystick_Task();
        App_UI_Task();
    }
}
