#ifndef __UART_RX_H
#define __UART_RX_H

#include "stm32f10x.h"

#define UART_RX_BUFFER_SIZE          256u    // Ring storage size; must be a power of two
#define UART_RX_BUFFER_CAPACITY      255u    // One slot remains empty to distinguish full from empty

void UartRx_Init(void);
uint8_t UartRx_PushFromIrq(uint8_t byte);
uint8_t UartRx_TryRead(uint8_t *byte);
uint16_t UartRx_GetAvailable(void);
uint16_t UartRx_GetDropCount(void);

#endif
