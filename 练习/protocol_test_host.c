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

/*
 * Host-side doubles for the MCU hardware functions.
 * The protocol exercise calls these names, and the tests inspect the captured
 * buffers/states instead of requiring a real STM32 board.
 */
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

#define HOST_TX_TIMEOUT_MS 100u
#define HOST_MAX_RETRIES   2u

typedef struct
{
    bool pending;
    bool failed;
    uint8_t seq;
    uint8_t retry_count;
    uint8_t send_count;
    uint8_t status;
    uint8_t payload[PROTO_MAX_DATA_LEN];
    uint8_t payload_len;
    uint32_t last_send_ms;
} HostRequest_t;

static HostRequest_t g_host;

/* Clear captured UART output and fake LED state between independent tests. */
static void ResetCapture(void)
{
    g_uart_tx_count = 0;
    g_led_id = 0;
    g_led_state = false;
    g_led_set_count = 0;
}

static void Host_Reset(void)
{
    g_host.pending = false;
    g_host.failed = false;
    g_host.seq = 0;
    g_host.retry_count = 0;
    g_host.send_count = 0;
    g_host.status = 0xFF;
    g_host.payload_len = 0;
    g_host.last_send_ms = 0;
}

static void ResetProtocol(void)
{
    s_rx_len = 0;
    s_rx_index = 0;
    s_rx_crc_low = 0;
    s_last_rx_tick = 0;
    s_rx_state = PROTO_STATE_WAIT_AA;
}

/* Feed a whole frame into the parser one byte at a time. */
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

static void Host_FeedPayloadToMcu(const uint8_t *payload, uint8_t len, uint32_t now_ms)
{
    uint16_t crc = Protocol_CalcCrc16(len, payload);

    Protocol_InputByteWithTime(0xAA, now_ms);
    Protocol_InputByteWithTime(0x55, now_ms);
    Protocol_InputByteWithTime(len, now_ms);

    for(uint8_t i = 0; i < len; i++)
    {
        Protocol_InputByteWithTime(payload[i], now_ms);
    }

    Protocol_InputByteWithTime((uint8_t)(crc & 0xFF), now_ms);
    Protocol_InputByteWithTime((uint8_t)(crc >> 8), now_ms);
}

static void Host_SendPending(uint32_t now_ms)
{
    Host_FeedPayloadToMcu(g_host.payload, g_host.payload_len, now_ms);
    g_host.last_send_ms = now_ms;
    g_host.send_count++;
}

/* Simulate a PC-side request with sequence matching and retry bookkeeping. */
static void Host_StartLedSet(uint8_t seq, uint8_t led_id, uint8_t on_off, uint32_t now_ms)
{
    g_host.pending = true;
    g_host.failed = false;
    g_host.seq = seq;
    g_host.retry_count = 0;
    g_host.send_count = 0;
    g_host.status = 0xFF;
    g_host.payload[0] = CMD_LED_SET;
    g_host.payload[1] = seq;
    g_host.payload[2] = led_id;
    g_host.payload[3] = on_off;
    g_host.payload_len = 4;

    Host_SendPending(now_ms);
}

static bool Host_ProcessCapturedResponse(void)
{
    uint8_t len;
    uint16_t received_crc;
    uint8_t *payload;

    if(g_uart_tx_count < 6)
    {
        return false;
    }

    if(g_uart_tx[0] != 0xAA || g_uart_tx[1] != 0x55)
    {
        return false;
    }

    len = g_uart_tx[2];
    if(g_uart_tx_count != (uint8_t)(len + 5))
    {
        return false;
    }

    payload = &g_uart_tx[3];
    received_crc = (uint16_t)g_uart_tx[3 + len] |
                   ((uint16_t)g_uart_tx[4 + len] << 8);
    if(received_crc != Protocol_CalcCrc16(len, payload))
    {
        return false;
    }

    if(!g_host.pending || len < 3)
    {
        return false;
    }

    if(payload[1] != g_host.seq)
    {
        return false;
    }

    g_host.status = payload[2];
    g_host.pending = false;
    return true;
}

/* Retry a pending request after timeout, then mark failure after max retries. */
static void Host_CheckTimeout(uint32_t now_ms)
{
    if(!g_host.pending)
    {
        return;
    }

    if((uint32_t)(now_ms - g_host.last_send_ms) <= HOST_TX_TIMEOUT_MS)
    {
        return;
    }

    if(g_host.retry_count >= HOST_MAX_RETRIES)
    {
        g_host.pending = false;
        g_host.failed = true;
        return;
    }

    g_host.retry_count++;
    Host_SendPending(now_ms);
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

    /* Valid LED_SET request: frame parsing, command dispatch, and response. */
    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x15, 0xA9};
        FeedFrame(frame, sizeof(frame));
        {
            const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x10, 0x00, 0x5D, 0x88};
            failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "valid frame sends OK response");
        }
        failed += Expect(g_led_set_count == 1, "valid frame calls Led_SetState once");
        failed += Expect(g_led_id == 1, "valid frame uses LED id 1");
        failed += Expect(g_led_state == true, "valid frame turns LED on");
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "state returns to WAIT_AA after valid frame");
    }

    /* Bad CRC16 must be dropped without side effects. */
    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x14, 0xA9};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad CRC16 frame is ignored");
        failed += Expect(g_uart_tx_count == 0, "bad CRC16 frame sends no response");
        failed += Expect(s_rx_state == PROTO_STATE_WAIT_AA, "state returns to WAIT_AA after bad CRC16");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0xAA, 0x55, 0x04, 0x01, 0x10, 0x01, 0x01, 0x15, 0xA9};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 1, "AA AA 55 sequence resynchronizes");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x11, 0x02, 0x01, 0x44, 0x99};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x11, 0x02, 0xDD, 0xD9};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad LED id does not set LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "bad LED id sends LED_ID_ERROR response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x04, 0x01, 0x12, 0x01, 0x02, 0xF4, 0x68};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x81, 0x12, 0x03, 0x1C, 0xE9};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "bad ON_OFF does not set LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "bad ON_OFF sends ON_OFF_ERROR response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x02, 0x02, 0x13, 0x90, 0xAD};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x03, 0x82, 0x13, 0x00, 0xAD, 0x78};
        FeedFrame(frame, sizeof(frame));
        failed += Expect(g_led_set_count == 0, "ping does not touch LED");
        failed += ExpectTxFrame(expected_rsp, sizeof(expected_rsp), "ping sends OK response");
    }

    ResetProtocol();
    ResetCapture();
    {
        const uint8_t frame[] = {0xAA, 0x55, 0x02, 0x09, 0x14, 0xD6, 0x5F};
        const uint8_t expected_rsp[] = {0xAA, 0x55, 0x04, 0xFF, 0x14, 0x04, 0x09, 0x67, 0x16};
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
    Host_Reset();
    {
        const uint8_t wrong_seq_rsp[] = {CMD_LED_SET_RSP, 0x21, PROTO_STATUS_OK};
        const uint8_t correct_seq_rsp[] = {CMD_LED_SET_RSP, 0x20, PROTO_STATUS_OK};

        Host_StartLedSet(0x20, 1, 1, 100);
        failed += Expect(g_host.pending == true, "host waits for response after sending request");
        ResetCapture();

        Protocol_SendFrame(wrong_seq_rsp, sizeof(wrong_seq_rsp));
        failed += Expect(Host_ProcessCapturedResponse() == false, "host ignores response with wrong SEQ");
        failed += Expect(g_host.pending == true, "host keeps waiting after wrong SEQ");

        ResetCapture();
        Protocol_SendFrame(correct_seq_rsp, sizeof(correct_seq_rsp));
        failed += Expect(Host_ProcessCapturedResponse() == true, "host accepts response with matching SEQ");
        failed += Expect(g_host.pending == false, "host clears pending request after matching SEQ");
        failed += Expect(g_host.status == PROTO_STATUS_OK, "host records response status");
    }

    ResetProtocol();
    ResetCapture();
    Host_Reset();
    {
        Host_StartLedSet(0x30, 1, 1, 100);
        failed += Expect(g_host.send_count == 1, "host sends first request once");

        ResetCapture();
        Host_CheckTimeout(150);
        failed += Expect(g_host.send_count == 1, "host does not retry before timeout");

        Host_CheckTimeout(201);
        failed += Expect(g_host.retry_count == 1, "host increments retry count after timeout");
        failed += Expect(g_host.send_count == 2, "host resends request after timeout");
        failed += Expect(g_uart_tx_count > 0, "retry reaches MCU and captures response");
        failed += Expect(Host_ProcessCapturedResponse() == true, "host matches retry response by SEQ");
        failed += Expect(g_host.pending == false, "host stops waiting after retry response");
        failed += Expect(g_host.failed == false, "host retry succeeds without failure");
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
        failed += Expect(g_uart_tx_count == 8, "send frame length is correct");
        failed += Expect(g_uart_tx[0] == 0xAA && g_uart_tx[1] == 0x55, "send frame header is AA 55");
        failed += Expect(g_uart_tx[2] == 0x03, "send frame LEN is correct");
        failed += Expect(g_uart_tx[6] == 0x50 && g_uart_tx[7] == 0x30,
                         "send frame CRC16 is correct and low byte is first");
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
