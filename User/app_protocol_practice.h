#ifndef __APP_PROTOCOL_PRACTICE_H
#define __APP_PROTOCOL_PRACTICE_H

#include "stm32f10x.h"

/*
 * Binary protocol practice module.
 *
 * USART1 interrupt should call App_ProtocolPractice_ReceiveByte() once for
 * every received byte. The main loop should call App_ProtocolPractice_Task().
 */
void App_ProtocolPractice_Init(void);
uint8_t App_ProtocolPractice_ReceiveByte(uint8_t byte);
void App_ProtocolPractice_Task(void);

uint16_t App_ProtocolPractice_GetRxOkCount(void);
uint16_t App_ProtocolPractice_GetRxErrorCount(void);
uint16_t App_ProtocolPractice_GetRxTimeoutCount(void);

/* The counters above are useful for OLED pages, debugging, or protocol tests. */

#endif
