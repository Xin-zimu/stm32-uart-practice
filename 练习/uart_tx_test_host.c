#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_txe_interrupt_enabled;
static uint8_t g_txe_set = 1;
static uint8_t g_sent[1024];
static uint16_t g_sent_count;

void UartTx_TestEnableTxeInterrupt(void)
{
    g_txe_interrupt_enabled = 1;
}

void UartTx_TestDisableTxeInterrupt(void)
{
    g_txe_interrupt_enabled = 0;
}

uint8_t UartTx_TestIsTxeInterruptEnabled(void)
{
    return g_txe_interrupt_enabled;
}

uint8_t UartTx_TestIsTxeSet(void)
{
    return g_txe_set;
}

void UartTx_TestSendData(uint8_t byte)
{
    if (g_sent_count < sizeof(g_sent))
    {
        g_sent[g_sent_count++] = byte;
    }
}

#define UART_TX_HOST_TEST
#include "../SYSTEM/usart/uart_tx.c"

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

static void ResetTest(void)
{
    memset(g_sent, 0, sizeof(g_sent));
    g_sent_count = 0;
    g_txe_set = 1;
    UartTx_Init();
}

static void DrainTx(void)
{
    uint16_t guard = 1000;

    while ((g_txe_interrupt_enabled != 0) && (guard > 0))
    {
        UartTx_IRQHandler();
        guard--;
    }
}

int main(void)
{
    int failed = 0;

    ResetTest();
    {
        const uint8_t data[] = {0xAA, 0x55, 0x01, 0x02};

        failed += Expect(UartTx_GetFree() == 255, "initial capacity is 255 bytes");
        failed += Expect(UartTx_TryWrite(data, sizeof(data)) == 1,
                         "protocol block enters queue");
        failed += Expect(g_txe_interrupt_enabled == 1,
                         "enqueue enables TXE interrupt");

        DrainTx();
        failed += Expect(g_sent_count == sizeof(data), "all queued bytes are sent");
        failed += Expect(memcmp(g_sent, data, sizeof(data)) == 0,
                         "sent byte order matches enqueue order");
        failed += Expect(g_txe_interrupt_enabled == 0,
                         "TXE interrupt stops when queue becomes empty");
        failed += Expect(UartTx_GetFree() == 255,
                         "capacity returns after draining queue");
    }

    ResetTest();
    {
        const uint8_t data[] = {0x31, 0x32};

        failed += Expect(UartTx_TryWrite(data, sizeof(data)) == 1,
                         "data queues while transmitter is busy");
        g_txe_set = 0;
        UartTx_IRQHandler();
        failed += Expect(g_sent_count == 0,
                         "handler does not consume data before TXE is set");
        failed += Expect(g_txe_interrupt_enabled == 1,
                         "TXE interrupt remains armed while hardware is busy");

        g_txe_set = 1;
        DrainTx();
        failed += Expect(g_sent_count == sizeof(data),
                         "queued data sends after TXE becomes ready");
    }

    ResetTest();
    {
        uint8_t debug_data[224];
        uint8_t protocol_data[23];

        memset(debug_data, 0x44, sizeof(debug_data));
        memset(protocol_data, 0xA5, sizeof(protocol_data));

        failed += Expect(UartTx_TryWriteDebug(debug_data, 223) == 1,
                         "debug output may use space above reserve");
        failed += Expect(UartTx_TryWriteDebug(debug_data, 1) == 0,
                         "debug output cannot consume protocol reserve");
        failed += Expect(UartTx_TryWrite(protocol_data, sizeof(protocol_data)) == 1,
                         "protocol frame can use reserved space");
        failed += Expect(UartTx_GetDropCount() == 1,
                         "rejected debug write increments drop count");
    }

    ResetTest();
    {
        uint8_t first[250];
        const uint8_t frame[] = {1, 2, 3, 4, 5, 6};
        uint16_t free_before;

        memset(first, 0x11, sizeof(first));
        failed += Expect(UartTx_TryWrite(first, sizeof(first)) == 1,
                         "large protocol block enters queue");
        free_before = UartTx_GetFree();
        failed += Expect(UartTx_TryWrite(frame, sizeof(frame)) == 0,
                         "oversized remaining frame is rejected");
        failed += Expect(UartTx_GetFree() == free_before,
                         "failed frame write leaves queue unchanged");
    }

    ResetTest();
    {
        uint8_t first[250];
        uint8_t second[20];
        uint16_t i;

        for (i = 0; i < sizeof(first); i++)
        {
            first[i] = (uint8_t)i;
        }
        for (i = 0; i < sizeof(second); i++)
        {
            second[i] = (uint8_t)(0xE0u + i);
        }

        failed += Expect(UartTx_TryWrite(first, sizeof(first)) == 1,
                         "queue accepts data before wraparound");

        for (i = 0; i < 240; i++)
        {
            UartTx_IRQHandler();
        }

        failed += Expect(UartTx_TryWrite(second, sizeof(second)) == 1,
                         "queue accepts data across buffer end");
        DrainTx();
        failed += Expect(g_sent_count == 270,
                         "wraparound sends every byte exactly once");
        failed += Expect(memcmp(&g_sent[250], second, sizeof(second)) == 0,
                         "wraparound preserves second block order");
    }

    if (failed == 0)
    {
        printf("ALL UART TX TESTS PASSED\n");
    }
    else
    {
        printf("%d UART TX CHECKS FAILED\n", failed);
    }

    return failed == 0 ? 0 : 1;
}
