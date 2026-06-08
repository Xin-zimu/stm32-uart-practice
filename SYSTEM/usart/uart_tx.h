#ifndef __UART_TX_H
#define __UART_TX_H

#include "stm32f10x.h"

/*
 * USART1 非阻塞发送模块。
 *
 * 普通协议数据可以使用环形缓冲区的全部剩余空间；调试文字会额外
 * 保留 32 字节，避免大量 printf 输出挤占协议响应所需空间。
 */
#define UART_TX_BUFFER_SIZE          256u
#define UART_TX_PROTOCOL_RESERVE     32u

void UartTx_Init(void);
uint8_t UartTx_TryWrite(const uint8_t *data, uint16_t len);
uint8_t UartTx_TryWriteDebug(const uint8_t *data, uint16_t len);
uint8_t UartTx_TryByte(uint8_t byte);
uint16_t UartTx_GetFree(void);
uint16_t UartTx_GetDropCount(void);
void UartTx_IRQHandler(void);

#endif
