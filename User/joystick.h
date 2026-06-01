#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f10x.h"

/*
   Joystick module pins:
   COM -> GND
   UP  -> PA2
   DOWN-> PA3
   LFT -> PA4
   RHT -> PB5
   MID -> PB1
   SET/RST are not used by the current UI.
*/

#define JOY_UP_PORT        GPIOA
#define JOY_UP_CLK         RCC_APB2Periph_GPIOA
#define JOY_UP_PIN         GPIO_Pin_2

#define JOY_DOWN_PORT      GPIOA
#define JOY_DOWN_CLK       RCC_APB2Periph_GPIOA
#define JOY_DOWN_PIN       GPIO_Pin_3

#define JOY_LEFT_PORT      GPIOA
#define JOY_LEFT_CLK       RCC_APB2Periph_GPIOA
#define JOY_LEFT_PIN       GPIO_Pin_4

#define JOY_RIGHT_PORT     GPIOB
#define JOY_RIGHT_CLK      RCC_APB2Periph_GPIOB
#define JOY_RIGHT_PIN      GPIO_Pin_5

#define JOY_PRESS_PORT     GPIOB
#define JOY_PRESS_CLK      RCC_APB2Periph_GPIOB
#define JOY_PRESS_PIN      GPIO_Pin_1

#define JOY_EVENT_UP       0x01
#define JOY_EVENT_DOWN     0x02
#define JOY_EVENT_LEFT     0x04
#define JOY_EVENT_RIGHT    0x08
#define JOY_EVENT_PRESS    0x10

/* Initialize GPIO pins and the debounce state. */
void Joystick_Init(void);

/* Poll the joystick. Call this periodically from the main loop. */
void Joystick_Task(void);

/* Returns newly pressed directions since the last call, then clears them. */
uint8_t Joystick_GetEvents(void);

#endif
