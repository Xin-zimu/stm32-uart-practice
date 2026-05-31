#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define PROTO_MAX_DATA_LEN 32

void Protocol_Dispatch(const uint8_t *payload, uint8_t len);
uint8_t HandleLedSet(const uint8_t *data, uint8_t data_len);

static uint8_t g_uart_tx[128];
static uint8_t g_uart_tx_count;
static uint8_t g_led_id;
static bool g_led_state;
static uint8_t g_led_set_count;

void Uart_SendByte(uint8_t byte)
{
    if (g_uart_tx_count < sizeof(g_uart_tx))
    {
        g_uart_tx[g_uart_tx_count++] = byte;
    }
}

void Led_SetState(uint8_t led_id, bool state)
{
    g_led_id = led_id;
    g_led_state = state;
    g_led_set_count++;
}

#define PROTOCOL_HOST_TEST
#include "practice"

static void ResetCapture(void)
{
    g_uart_tx_count = 0;
    g_led_id = 0;
    g_led_state = false;
    g_led_set_count = 0;
}

static void ResetProtocol(void)
{
    s_rx_len = 0;
    s_rx_index = 0;
    s_last_rx_tick = 0;
    s_rx_state = PROTO_STATE_WAIT_AA;
}

static void FeedFrame(const uint8_t *frame, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
    {
        Protocol_InputByte(frame[i]);
    }
}

static void FeedFrameWithTime(const uint8_t *frame, uint8_t len, uint32_t start_ms, uint32_t step_ms)
{
    for (uint8_t i = 0; i < len; i++)
    {
        Protocol_InputByteWithTime(frame[i], start_ms + ((uint32_t)i * step_ms));
    }
}

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

static int ExpectTxFrame(const uint8_t *expected, uint8_t len, const char *message)
{
    if (g_uart_tx_count != len)
    {
        printf("FAIL: %s\n", message);
        printf("      expected tx length %u, got %u\n", len, g_uart_tx_count);
        return 1;
    }

    for (uint8_t i = 0; i < len; i++)
    {
        if (g_uart_tx[i] != expected[i])
        {
            printf("FAIL: %s\n", message);
            printf("      tx[%u] expected 0x%02X, got 0x%02X\n", i, expected[i], g_uart_tx[i]);
            return 1;
        }
    }

    printf("PASS: %s\n", message);
    return 0;
}

int main(void)
{
    int failed = 0;

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x17};
        FeedFrame(frame, sizeof(frame));
        {
            const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x10, 0x00, 0x94};
            failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "valid frame sends OK response");
        }
        failed += Expect(g_led_set_count == 1, "valid frame calls Led_SetState once");
        failed += Expect(g_led_id == 1, "valid frame uses LED id 1");
        failed += Expect(g_led_state == true, "valid frame turns LED on");
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "state returns to WAIT_AA after valid frame");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x18};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad checksum frame is ignored");
        failed += Expect(g_uart_tx_count == 0, "bad checksum frame sends no response");
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "state returns to WAIT_AA after bad checksum");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x17};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 1, "AA AA 55 sequence resynchronizes");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x11, 0x02, 0x01, 0x19};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x11, 0x02, 0x97};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad LED id does not set LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "bad LED id sends LED_ID_ERROR response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x12, 0x01, 0x02, 0x1A};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x12, 0x03, 0x99};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad ON_OFF does not set LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "bad ON_OFF sends ON_OFF_ERROR response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x02, 0x02, 0x13, 0x17};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x82, 0x13, 0x00, 0x98};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "ping does not touch LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "ping sends OK response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x02, 0x09, 0x14, 0x1F};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x04, 0xFF, 0x14, 0x04, 0x09, 0x24};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "unknown command does not touch LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "unknown command sends error response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x01, 0x02, 0x03};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "len without SEQ is ignored");
        failed += Expect(g_uart_tx_count == 0, "len without SEQ sends no response");
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "len without SEQ returns to WAIT_AA");
    }

    ResetProtocol();
    ResetCapture();
    {
        Protocol_InputByteWithTime(0xAA, 100);
        Protocol_InputByteWithTime(0x55, 110);
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_LEN, "partial frame enters WAIT_LEN before timeout");
        Protocol_CheckTimeout(200);
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "partial frame timeout resets parser");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t late_tail[] = {0x55, 0x03, 0x01, 0x01, 0x01, 0x06};
        Protocol_InputByteWithTime(0xAA, 100);
        FeedFrameWithTime(late_tail, sizeof(late_tail), 200, 1);
        failed += Expect(g_led_set_count == 0, "late bytes after timeout do not complete stale frame");
        failed += Expect(g_uart_tx_count == 0, "late bytes after timeout send no stale response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t payload[] = {0x01, 0x01, 0x00};
        Protocol_SendFrame(payload, sizeof(payload));
        failed += Expect(g_uart_tx_count == 7, "send frame length is correct");
        failed += Expect(g_uart_tx[0] == 0xAA && g_uart_tx[1] == 0x55, "send frame header is AA 55");
        failed += Expect(g_uart_tx[2] == 0x03, "send frame LEN is correct");
        failed += Expect(g_uart_tx[6] == 0x05, "send frame checksum is correct");
    }

    if (failed == 0)
    {
        printf("ALL TESTS PASSED\n");
    }
    else
    {
        printf("%d TEST CHECKS FAILED\n", failed);
    }

    return failed == 0 ? 0 : 1;
}
