#ifndef __APP_UART_PRACTICE_H
#define __APP_UART_PRACTICE_H

#include "stm32f10x.h"

/* 打印串口练习欢迎信息。USART1 初始化由 main.c 调用 uart_init() 完成。 */
void App_UARTPractice_Init(void);

/*
 * 串口命令任务。
 * 接收中断只把字节写入 RX 环形缓冲，本函数在主循环中完成协议分发
 * 和文本命令组行。
 */
void App_UARTPractice_Task(void);

#endif
