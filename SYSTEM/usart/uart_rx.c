#ifdef UART_RX_HOST_TEST
#include <stdint.h>

#define UART_RX_BUFFER_SIZE          256u
#define UART_RX_BUFFER_CAPACITY      255u
#else
#include "uart_rx.h"
#endif

#define UART_RX_BUFFER_MASK          (UART_RX_BUFFER_SIZE - 1u)

/*
 * USART1 RX ring buffer.
 *
 * The RXNE interrupt is the only producer and the main loop is the only
 * consumer. The producer publishes a byte by updating head after the byte is
 * stored. The consumer releases a byte by updating tail after it is copied.
 */
static uint8_t s_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint16_t s_rx_drop_count = 0;

/*
 * Advance one ring-buffer index.
 *
 * The buffer size is a power of two, so masking wraps the index from the final
 * slot back to zero without division.
 *
 * Parameters:
 * index: Current buffer index.
 *
 * Return value:
 * The next wrapped buffer index.
 */
static uint16_t UartRx_NextIndex(uint16_t index)
{
    return (uint16_t)((index + 1u) & UART_RX_BUFFER_MASK);
}

/*
 * Initialize the USART1 receive queue.
 *
 * This function is called before RXNE interrupts are enabled. It discards any
 * stale software-buffer contents and clears the overflow counter.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
void UartRx_Init(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_drop_count = 0;
}

/*
 * Store one byte received by the USART1 RXNE interrupt.
 *
 * The interrupt is the only producer. The byte is written before head is
 * advanced, so the main-loop consumer never observes an unpublished slot. If
 * the queue is full, the new byte is discarded and the drop counter advances.
 *
 * Parameters:
 * byte: Byte read from the USART1 data register.
 *
 * Return value:
 * 1: The byte was queued.
 * 0: The queue was full and the byte was discarded.
 *
 * Side effects:
 * Updates the producer index and may increment the overflow counter.
 */
uint8_t UartRx_PushFromIrq(uint8_t byte)
{
    uint16_t head;
    uint16_t next_head;

    head = s_rx_head;
    next_head = UartRx_NextIndex(head);

    if (next_head == s_rx_tail)
    {
        s_rx_drop_count++;
        return 0;
    }

    s_rx_buffer[head] = byte;
    s_rx_head = next_head;
    return 1;
}

/*
 * Try to remove one byte from the USART1 receive queue.
 *
 * The main loop is the only consumer. A byte is copied before tail advances,
 * allowing the RXNE producer to reuse the released slot only after the copy is
 * complete.
 *
 * Parameters:
 * byte: Output pointer that receives the oldest queued byte.
 *
 * Return value:
 * 1: A byte was returned.
 * 0: The output pointer was null or the queue was empty.
 */
uint8_t UartRx_TryRead(uint8_t *byte)
{
    uint16_t tail;

    if (byte == 0)
    {
        return 0;
    }

    tail = s_rx_tail;
    if (tail == s_rx_head)
    {
        return 0;
    }

    *byte = s_rx_buffer[tail];
    s_rx_tail = UartRx_NextIndex(tail);
    return 1;
}

/*
 * Return the number of bytes currently waiting in the receive queue.
 *
 * The value is a momentary snapshot because RXNE may append another byte after
 * the indexes are read.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Number of queued bytes from 0 through UART_RX_BUFFER_CAPACITY.
 */
uint16_t UartRx_GetAvailable(void)
{
    uint16_t head;
    uint16_t tail;

    head = s_rx_head;
    tail = s_rx_tail;
    return (uint16_t)((head - tail) & UART_RX_BUFFER_MASK);
}

/*
 * Return the number of bytes discarded because the receive queue was full.
 *
 * The counter wraps naturally after 65535. It is intended for diagnostics and
 * does not alter queue state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * Current receive-overflow count.
 */
uint16_t UartRx_GetDropCount(void)
{
    return s_rx_drop_count;
}
