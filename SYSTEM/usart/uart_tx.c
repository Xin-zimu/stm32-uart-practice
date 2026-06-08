#ifdef UART_TX_HOST_TEST
#include <stdint.h>

extern void UartTx_TestEnableTxeInterrupt(void);
extern void UartTx_TestDisableTxeInterrupt(void);
extern uint8_t UartTx_TestIsTxeInterruptEnabled(void);
extern uint8_t UartTx_TestIsTxeSet(void);
extern void UartTx_TestSendData(uint8_t byte);

#define UART_TX_ENABLE_TXE_INTERRUPT()       UartTx_TestEnableTxeInterrupt()
#define UART_TX_DISABLE_TXE_INTERRUPT()      UartTx_TestDisableTxeInterrupt()
#define UART_TX_IS_TXE_INTERRUPT_ENABLED()   UartTx_TestIsTxeInterruptEnabled()
#define UART_TX_IS_TXE_SET()                 UartTx_TestIsTxeSet()
#define UART_TX_SEND_DATA(byte)              UartTx_TestSendData(byte)

#define UART_TX_BUFFER_SIZE                  256u
#define UART_TX_PROTOCOL_RESERVE             32u
#else
#include "uart_tx.h"

#define UART_TX_ENABLE_TXE_INTERRUPT()       (USART1->CR1 |= USART_CR1_TXEIE)
#define UART_TX_DISABLE_TXE_INTERRUPT()      (USART1->CR1 &= (uint16_t)(~USART_CR1_TXEIE))
#define UART_TX_IS_TXE_INTERRUPT_ENABLED()   ((USART1->CR1 & USART_CR1_TXEIE) != 0)
#define UART_TX_IS_TXE_SET()                 ((USART1->SR & USART_SR_TXE) != 0)
#define UART_TX_SEND_DATA(byte)              (USART1->DR = (uint16_t)(byte))
#endif

#define UART_TX_BUFFER_MASK          (UART_TX_BUFFER_SIZE - 1u)
#define UART_TX_BUFFER_CAPACITY      (UART_TX_BUFFER_SIZE - 1u)

/*
 * head 由主循环生产者推进，tail 由 TXE 中断消费者推进。
 * 留出一个空槽用于区分“缓冲区为空”和“缓冲区已满”。
 */
static uint8_t s_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t s_tx_head = 0;
static volatile uint16_t s_tx_tail = 0;
static volatile uint16_t s_tx_drop_count = 0;

static uint16_t UartTx_NextIndex(uint16_t index)
{
    return (uint16_t)((index + 1u) & UART_TX_BUFFER_MASK);
}

static uint16_t UartTx_FreeFromSnapshot(uint16_t head, uint16_t tail)
{
    return (uint16_t)((tail - head - 1u) & UART_TX_BUFFER_MASK);
}

/*
 * 将一个完整数据块原子地放入环形缓冲区。
 *
 * 入队期间只关闭 USART1 TXE 中断，不关闭 RXNE 和其他系统中断。
 * 数据全部复制完成后才更新 head，因此中断不会看到半写入的数据。
 * 空间不足时整个数据块都会失败，不会写入部分内容。
 */
static uint8_t UartTx_TryWriteInternal(const uint8_t *data,
                                       uint16_t len,
                                       uint16_t reserve)
{
    uint16_t head;
    uint16_t tail;
    uint16_t free_count;
    uint16_t i;
    uint8_t txe_was_enabled;

    if (len == 0)
    {
        return 1;
    }

    if (data == 0)
    {
        s_tx_drop_count++;
        return 0;
    }

    if ((len > UART_TX_BUFFER_CAPACITY) ||
        (reserve > UART_TX_BUFFER_CAPACITY) ||
        (len > (uint16_t)(UART_TX_BUFFER_CAPACITY - reserve)))
    {
        s_tx_drop_count++;
        return 0;
    }

    txe_was_enabled = UART_TX_IS_TXE_INTERRUPT_ENABLED() ? 1u : 0u;
    UART_TX_DISABLE_TXE_INTERRUPT();

    head = s_tx_head;
    tail = s_tx_tail;
    free_count = UartTx_FreeFromSnapshot(head, tail);

    if (free_count < (uint16_t)(len + reserve))
    {
        s_tx_drop_count++;

        if (txe_was_enabled != 0)
        {
            UART_TX_ENABLE_TXE_INTERRUPT();
        }

        return 0;
    }

    for (i = 0; i < len; i++)
    {
        s_tx_buffer[head] = data[i];
        head = UartTx_NextIndex(head);
    }

    s_tx_head = head;
    UART_TX_ENABLE_TXE_INTERRUPT();
    return 1;
}

void UartTx_Init(void)
{
    UART_TX_DISABLE_TXE_INTERRUPT();
    s_tx_head = 0;
    s_tx_tail = 0;
    s_tx_drop_count = 0;
}

uint8_t UartTx_TryWrite(const uint8_t *data, uint16_t len)
{
    return UartTx_TryWriteInternal(data, len, 0);
}

uint8_t UartTx_TryWriteDebug(const uint8_t *data, uint16_t len)
{
    return UartTx_TryWriteInternal(data, len, UART_TX_PROTOCOL_RESERVE);
}

uint8_t UartTx_TryByte(uint8_t byte)
{
    return UartTx_TryWriteDebug(&byte, 1);
}

uint16_t UartTx_GetFree(void)
{
    uint16_t head;
    uint16_t tail;

    head = s_tx_head;
    tail = s_tx_tail;
    return UartTx_FreeFromSnapshot(head, tail);
}

uint16_t UartTx_GetDropCount(void)
{
    return s_tx_drop_count;
}

/*
 * 每次 TXE 置位时最多向 DR 写入一个字节。
 * 写入最后一个待发送字节后立即关闭 TXE 中断，避免空中断循环。
 */
void UartTx_IRQHandler(void)
{
    uint16_t tail;

    if ((!UART_TX_IS_TXE_INTERRUPT_ENABLED()) || (!UART_TX_IS_TXE_SET()))
    {
        return;
    }

    tail = s_tx_tail;
    if (tail == s_tx_head)
    {
        UART_TX_DISABLE_TXE_INTERRUPT();
        return;
    }

    UART_TX_SEND_DATA(s_tx_buffer[tail]);
    tail = UartTx_NextIndex(tail);
    s_tx_tail = tail;

    if (tail == s_tx_head)
    {
        UART_TX_DISABLE_TXE_INTERRUPT();
    }
}
