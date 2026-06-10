#include <stdint.h>
#include <stdio.h>

#define UART_RX_HOST_TEST
#include "../SYSTEM/usart/uart_rx.c"

/*
 * Check one host-test condition and report a readable result.
 *
 * Parameters:
 * condition: Nonzero when the tested behavior is correct.
 * message: Description printed with the result.
 *
 * Return value:
 * 0: The condition passed.
 * 1: The condition failed.
 */
static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        return 1;
    }

    printf("PASS: %s\n", message);
    return 0;
}

/*
 * Exercise FIFO order, wraparound, overflow handling, and producer-consumer
 * interleaving for the USART RX ring buffer.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 0: Every check passed.
 * 1: At least one check failed.
 */
int main(void)
{
    int failed;
    uint16_t i;
    uint8_t byte;
    uint8_t block_ok;

    failed = 0;
    UartRx_Init();

    failed += Expect(UartRx_GetAvailable() == 0,
                     "queue starts empty");
    failed += Expect(UartRx_GetDropCount() == 0,
                     "drop counter starts at zero");
    failed += Expect(UartRx_TryRead(&byte) == 0,
                     "empty queue returns no byte");
    failed += Expect(UartRx_TryRead(0) == 0,
                     "null output pointer is rejected");

    block_ok = 1;
    for (i = 0; i < UART_RX_BUFFER_CAPACITY; i++)
    {
        if (UartRx_PushFromIrq((uint8_t)i) == 0)
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0, "all capacity bytes enter queue");

    failed += Expect(UartRx_GetAvailable() == UART_RX_BUFFER_CAPACITY,
                     "queue reports full usable capacity");
    failed += Expect(UartRx_PushFromIrq(0xEE) == 0,
                     "new byte is rejected when queue is full");
    failed += Expect(UartRx_GetDropCount() == 1,
                     "full queue increments drop counter");

    block_ok = 1;
    for (i = 0; i < UART_RX_BUFFER_CAPACITY; i++)
    {
        if ((UartRx_TryRead(&byte) == 0) || (byte != (uint8_t)i))
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0, "full queue drains in FIFO order");

    failed += Expect(UartRx_GetAvailable() == 0,
                     "queue is empty after draining");

    block_ok = 1;
    for (i = 0; i < 200; i++)
    {
        if (UartRx_PushFromIrq((uint8_t)i) == 0)
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0, "first wraparound block enters queue");

    block_ok = 1;
    for (i = 0; i < 180; i++)
    {
        if ((UartRx_TryRead(&byte) == 0) || (byte != (uint8_t)i))
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0,
                     "first wraparound block partly drains in order");

    block_ok = 1;
    for (i = 0; i < 180; i++)
    {
        if (UartRx_PushFromIrq((uint8_t)(200u + i)) == 0)
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0,
                     "second block crosses the physical buffer end");

    block_ok = 1;
    for (i = 180; i < 380; i++)
    {
        if ((UartRx_TryRead(&byte) == 0) || (byte != (uint8_t)i))
        {
            block_ok = 0;
        }
    }
    failed += Expect(block_ok != 0, "wrapped data drains in FIFO order");

    failed += Expect(UartRx_GetAvailable() == 0,
                     "wrapped queue returns to empty");
    failed += Expect(UartRx_GetDropCount() == 1,
                     "successful wraparound does not add drops");

    if (failed == 0)
    {
        printf("ALL UART RX TESTS PASSED\n");
    }
    else
    {
        printf("%d UART RX CHECKS FAILED\n", failed);
    }

    return failed == 0 ? 0 : 1;
}
