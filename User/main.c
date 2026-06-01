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
#include "usart.h"
#include "app_uart_practice.h"
#include "app_protocol_practice.h"

/*
 * Project entry point.
 *
 * Hardware drivers are initialized first, then application modules are started.
 * The main loop stays non-blocking: every module exposes a small Task function
 * and decides internally whether it is time to do work.
 */
int main(void)
{
    delay_init();

    /* Basic board services used by later application modules. */
    LED_Init();
    Timing_Init();
    uart_init(115200);

    /* Sensor and display drivers. */
    LightSensor_Init();
    LightSensor_ADC_Init();

    OLED_Init();
    DS18B20_Init();
    Joystick_Init();

    OLED_Clear();

    /* Application modules. Init order matters because UI/protocol read app state. */
    App_Light_Init();
    App_Temp_Init();
    App_UI_Init();
    App_UARTPractice_Init();
    App_ProtocolPractice_Init();

    while (1)
    {
        /* Cooperative scheduler: each task returns quickly and never blocks. */
        App_Light_Task();
        App_Temp_Task();
        Joystick_Task();
        App_UI_Task();
        App_UARTPractice_Task();
        App_ProtocolPractice_Task();
    }
}
