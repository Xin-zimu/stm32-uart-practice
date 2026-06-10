#ifdef UART_RX_PIPELINE_HOST_TEST
#include <stdint.h>
#else
#include "app_uart_rx_dispatch.h"
#include "app_uart_practice.h"
#include "app_protocol_practice.h"
#include "uart_rx.h"
#endif

#define UART_RX_DISPATCH_BYTE_BUDGET      32u

/*
 * Dispatch queued USART1 bytes to the binary and text parsers.
 *
 * Each byte is offered to the binary protocol parser first. Bytes that do not
 * belong to a binary frame are then passed to the text-line parser. The task
 * stops after a complete protocol attempt or text line, or after a fixed byte
 * budget, so command execution tasks get regular CPU time.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
void App_UartRxDispatch_Task(void)
{
    uint8_t byte;
    uint8_t protocol_result;
    uint8_t processed;

    processed = 0;

    while ((processed < UART_RX_DISPATCH_BYTE_BUDGET) &&
           (UartRx_TryRead(&byte) != 0))
    {
        processed++;
        protocol_result = App_ProtocolPractice_ReceiveByte(byte);

        if (protocol_result == APP_PROTOCOL_FEED_TEXT)
        {
            if (App_UARTPractice_FeedByte(byte) == APP_UART_TEXT_FEED_LINE_END)
            {
                return;
            }
        }
        else if (protocol_result == APP_PROTOCOL_FEED_FRAME_END)
        {
            return;
        }
    }
}
