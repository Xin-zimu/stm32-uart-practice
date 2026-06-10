#ifndef __APP_UART_PRACTICE_H
#define __APP_UART_PRACTICE_H

#include "stm32f10x.h"

#define APP_UART_TEXT_FEED_NONE         0u      // No complete text line was produced
#define APP_UART_TEXT_FEED_LINE_END     1u      // A complete or discarded line ended

/* 打印串口练习欢迎信息。USART1 初始化由 main.c 调用 uart_init() 完成。 */
void App_UARTPractice_Init(void);
uint8_t App_UARTPractice_FeedByte(uint8_t byte);
void App_UARTPractice_Task(void);
uint8_t App_UARTPractice_GetLineQueueCount(void);
uint16_t App_UARTPractice_GetLineDropCount(void);

#endif
