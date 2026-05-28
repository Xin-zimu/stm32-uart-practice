#ifndef __APP_UART_PRACTICE_H
#define __APP_UART_PRACTICE_H

#include "stm32f10x.h"

/* 打印串口练习欢迎信息。USART1 初始化由 main.c 调用 uart_init() 完成。 */
void App_UARTPractice_Init(void);

/*
 * 串口命令任务。
 * 接收中断负责收字节，本函数只在主循环中处理“完整一行”命令。
 */
void App_UARTPractice_Task(void);

#endif
