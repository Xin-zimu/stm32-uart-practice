#include "led.h"

#define TRAFFIC_GPIO_PORT      GPIOA
#define TRAFFIC_GPIO_CLK       RCC_APB2Periph_GPIOA

#define TRAFFIC_RED_PIN        GPIO_Pin_5
#define TRAFFIC_YELLOW_PIN     GPIO_Pin_6
#define TRAFFIC_GREEN_PIN      GPIO_Pin_7

/* Configure PA5/PA6/PA7 as push-pull outputs for the three traffic LEDs. */
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(TRAFFIC_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = TRAFFIC_RED_PIN | TRAFFIC_YELLOW_PIN | TRAFFIC_GREEN_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(TRAFFIC_GPIO_PORT, &GPIO_InitStructure);

    Traffic_AllOff();
}

/* Turn off every traffic LED before selecting a new single-light state. */
void Traffic_AllOff(void)
{
    GPIO_ResetBits(TRAFFIC_GPIO_PORT, TRAFFIC_RED_PIN);
    GPIO_ResetBits(TRAFFIC_GPIO_PORT, TRAFFIC_YELLOW_PIN);
    GPIO_ResetBits(TRAFFIC_GPIO_PORT, TRAFFIC_GREEN_PIN);
}

void Traffic_RedOn(void)
{
    /* Keep traffic light output mutually exclusive. */
    Traffic_AllOff();
    GPIO_SetBits(TRAFFIC_GPIO_PORT, TRAFFIC_RED_PIN);
}

void Traffic_YellowOn(void)
{
    Traffic_AllOff();
    GPIO_SetBits(TRAFFIC_GPIO_PORT, TRAFFIC_YELLOW_PIN);
}

void Traffic_GreenOn(void)
{
    Traffic_AllOff();
    GPIO_SetBits(TRAFFIC_GPIO_PORT, TRAFFIC_GREEN_PIN);
}
