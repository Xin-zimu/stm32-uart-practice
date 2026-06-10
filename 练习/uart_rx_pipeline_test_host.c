#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
    APP_LIGHT_TRAFFIC_AUTO = 0,
    APP_LIGHT_TRAFFIC_RED,
    APP_LIGHT_TRAFFIC_YELLOW,
    APP_LIGHT_TRAFFIC_GREEN,
    APP_LIGHT_TRAFFIC_OFF
} AppLightTrafficMode;

static uint32_t g_tick;
static AppLightTrafficMode g_light_mode;
static uint8_t g_tx_packets[16][32];
static uint16_t g_tx_lengths[16];
static uint8_t g_tx_count;

/*
 * Return the simulated millisecond tick used by protocol timeout checks.
 *
 * Parameters: None.
 * Return value: Current host-test tick.
 */
uint32_t Timing_GetTick(void)
{
    return g_tick;
}

/*
 * Record the traffic-light mode selected by a text or binary command.
 *
 * Parameters: mode is the requested application light mode.
 * Return value: None.
 */
void App_Light_SetTrafficMode(AppLightTrafficMode mode)
{
    g_light_mode = mode;
}

/*
 * Return the simulated current traffic-light mode.
 *
 * Parameters: None.
 * Return value: Last mode written by App_Light_SetTrafficMode().
 */
AppLightTrafficMode App_Light_GetTrafficMode(void)
{
    return g_light_mode;
}

/*
 * Return a stable simulated analog light value.
 *
 * Parameters: None.
 * Return value: Fixed AO value used by status responses.
 */
uint16_t App_Light_GetAO(void)
{
    return 1234u;
}

/*
 * Return the simulated light/dark decision.
 *
 * Parameters: None.
 * Return value: Zero, representing a bright environment.
 */
uint8_t App_Light_IsDark(void)
{
    return 0;
}

/*
 * Return the simulated light threshold.
 *
 * Parameters: None.
 * Return value: Fixed threshold used by text status output.
 */
uint16_t App_Light_GetThreshold(void)
{
    return 2000u;
}

/*
 * Accept a threshold write without changing unrelated pipeline state.
 *
 * Parameters: threshold is the value requested by the text command.
 * Return value: None.
 */
void App_Light_SetThreshold(uint16_t threshold)
{
    (void)threshold;
}

/*
 * Report that the simulated temperature sample is unavailable.
 *
 * Parameters: None.
 * Return value: Zero so status output uses the NA path.
 */
uint8_t App_Temp_IsValid(void)
{
    return 0;
}

/*
 * Return a placeholder simulated temperature.
 *
 * Parameters: None.
 * Return value: Zero; ignored while App_Temp_IsValid() is false.
 */
int16_t App_Temp_GetTemp10(void)
{
    return 0;
}

/*
 * Capture one complete protocol response written by the production code.
 *
 * Parameters:
 * data: Response bytes.
 * len: Number of response bytes.
 *
 * Return value:
 * 1 when captured, otherwise 0 when the capture array is full.
 */
uint8_t UartTx_TryWrite(const uint8_t *data, uint16_t len)
{
    if ((g_tx_count >= 16u) || (len > sizeof(g_tx_packets[0])))
    {
        return 0;
    }

    memcpy(g_tx_packets[g_tx_count], data, len);
    g_tx_lengths[g_tx_count] = len;
    g_tx_count++;
    return 1;
}

#define APP_PROTOCOL_FEED_TEXT          0u
#define APP_PROTOCOL_FEED_CONSUMED      1u
#define APP_PROTOCOL_FEED_FRAME_END     2u
#define APP_UART_TEXT_FEED_NONE         0u
#define APP_UART_TEXT_FEED_LINE_END     1u

#define UART_RX_HOST_TEST
#include "../SYSTEM/usart/uart_rx.c"

#define UART_RX_PIPELINE_HOST_TEST
#include "../User/app_protocol_practice.c"
#include "../User/app_uart_practice.c"
#include "../User/app_uart_rx_dispatch.c"

/*
 * Check one host-test condition and print a readable result.
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
 * Reset all receive, parser, queue, business-state, and capture data.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
static void ResetPipeline(void)
{
    g_tick = 0;
    g_light_mode = APP_LIGHT_TRAFFIC_AUTO;
    g_tx_count = 0;
    memset(g_tx_packets, 0, sizeof(g_tx_packets));
    memset(g_tx_lengths, 0, sizeof(g_tx_lengths));

    UartRx_Init();
    App_UARTPractice_Init();
    App_ProtocolPractice_Init();
}

/*
 * Build one binary protocol frame using the production CRC implementation.
 *
 * Parameters:
 * seq: Sequence number written into the frame.
 * cmd: Protocol command byte.
 * data: Optional DATA bytes.
 * len: Number of DATA bytes.
 * packet: Output buffer with at least PROTO_MAX_FRAME_LEN bytes.
 *
 * Return value:
 * Number of bytes written to packet.
 */
static uint8_t BuildFrame(uint8_t seq,
                          uint8_t cmd,
                          const uint8_t *data,
                          uint8_t len,
                          uint8_t *packet)
{
    ProtocolFrame frame;
    uint16_t crc;
    uint8_t packet_len;
    uint8_t i;

    frame.seq = seq;
    frame.len = len;
    frame.cmd = cmd;

    for (i = 0; i < len; i++)
    {
        frame.data[i] = data[i];
    }

    packet_len = 0;
    packet[packet_len++] = PROTO_HEAD_1;
    packet[packet_len++] = PROTO_HEAD_2;
    packet[packet_len++] = seq;
    packet[packet_len++] = len;
    packet[packet_len++] = cmd;

    for (i = 0; i < len; i++)
    {
        packet[packet_len++] = data[i];
    }

    crc = Protocol_CalcFrameCrc16(&frame);
    packet[packet_len++] = (uint8_t)(crc & 0xFFu);
    packet[packet_len++] = (uint8_t)(crc >> 8);
    return packet_len;
}

/*
 * Feed one complete protocol frame directly into the production parser.
 *
 * Parameters:
 * seq: Sequence number used by a PING frame.
 *
 * Return value:
 * Final parser result returned for the CRC high byte.
 */
static uint8_t FeedPingFrame(uint8_t seq)
{
    uint8_t packet[PROTO_MAX_FRAME_LEN];
    uint8_t packet_len;
    uint8_t result;
    uint8_t i;

    packet_len = BuildFrame(seq, PROTO_CMD_PING, 0, 0, packet);
    result = APP_PROTOCOL_FEED_TEXT;

    for (i = 0; i < packet_len; i++)
    {
        result = App_ProtocolPractice_ReceiveByte(packet[i]);
    }

    return result;
}

/*
 * Queue a byte block through the production USART RX ring buffer.
 *
 * Parameters:
 * data: Input byte block.
 * len: Number of bytes to queue.
 *
 * Return value:
 * 1: Every byte was queued.
 * 0: At least one byte was rejected.
 */
static uint8_t QueueRxBytes(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++)
    {
        if (UartRx_PushFromIrq(data[i]) == 0)
        {
            return 0;
        }
    }

    return 1;
}

/*
 * Run the RX dispatcher until the byte ring becomes empty.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
static void DrainDispatcher(void)
{
    uint16_t guard;

    guard = 1000;
    while ((UartRx_GetAvailable() > 0) && (guard > 0))
    {
        App_UartRxDispatch_Task();
        guard--;
    }
}

/*
 * Run protocol-queue, text-queue, and mixed-stream integration checks.
 *
 * Parameters: None.
 * Return value: Zero when every check passes, otherwise one.
 */
int main(void)
{
    static const uint8_t red_line[] = "LED RED\r";
    static const uint8_t green_line[] = "LED GREEN\r";
    static const uint8_t off_line[] = "LED OFF\r";
    static const uint8_t ping_line[] = "PING\r";
    static const uint8_t status_line[] = "STATUS\r";
    uint8_t packet[PROTO_MAX_FRAME_LEN];
    uint8_t packet_len;
    uint8_t i;
    int failed;

    failed = 0;

    ResetPipeline();
    for (i = 1; i <= 5; i++)
    {
        failed += Expect(FeedPingFrame(i) == APP_PROTOCOL_FEED_FRAME_END,
                         "complete protocol frame reports frame end");
    }

    failed += Expect(App_ProtocolPractice_GetQueueCount() == 4,
                     "protocol queue keeps four complete frames");
    failed += Expect(App_ProtocolPractice_GetFrameDropCount() == 1,
                     "fifth protocol frame increments queue drop count");
    failed += Expect(App_ProtocolPractice_GetRxOkCount() == 4,
                     "only queued protocol frames increment success count");

    for (i = 0; i < 4; i++)
    {
        App_ProtocolPractice_Task();
    }

    failed += Expect(g_tx_count == 4,
                     "four queued protocol frames execute");
    failed += Expect((g_tx_packets[0][2] == 1) &&
                     (g_tx_packets[1][2] == 2) &&
                     (g_tx_packets[2][2] == 3) &&
                     (g_tx_packets[3][2] == 4),
                     "protocol frame queue preserves FIFO sequence order");

    ResetPipeline();
    for (i = 0; i < sizeof(red_line) - 1u; i++)
    {
        (void)App_UARTPractice_FeedByte(red_line[i]);
    }
    for (i = 0; i < sizeof(green_line) - 1u; i++)
    {
        (void)App_UARTPractice_FeedByte(green_line[i]);
    }
    for (i = 0; i < sizeof(off_line) - 1u; i++)
    {
        (void)App_UARTPractice_FeedByte(off_line[i]);
    }

    failed += Expect(App_UARTPractice_GetLineQueueCount() == 2,
                     "text queue keeps two complete lines");
    failed += Expect(App_UARTPractice_GetLineDropCount() == 1,
                     "third text line increments queue drop count");

    App_UARTPractice_Task();
    failed += Expect(g_light_mode == APP_LIGHT_TRAFFIC_RED,
                     "first text command executes first");
    App_UARTPractice_Task();
    failed += Expect(g_light_mode == APP_LIGHT_TRAFFIC_GREEN,
                     "second text command executes second");

    ResetPipeline();
    packet_len = BuildFrame(0x33u, PROTO_CMD_PING, 0, 0, packet);
    failed += Expect(QueueRxBytes(ping_line, sizeof(ping_line) - 1u) != 0,
                     "mixed stream queues first text line");
    failed += Expect(QueueRxBytes(packet, packet_len) != 0,
                     "mixed stream queues binary frame");
    failed += Expect(QueueRxBytes(status_line, sizeof(status_line) - 1u) != 0,
                     "mixed stream queues second text line");

    DrainDispatcher();
    failed += Expect(UartRx_GetAvailable() == 0,
                     "dispatcher drains mixed RX byte stream");
    failed += Expect(App_UARTPractice_GetLineQueueCount() == 2,
                     "mixed stream produces two text lines");
    failed += Expect(App_ProtocolPractice_GetQueueCount() == 1,
                     "mixed stream produces one protocol frame");
    failed += Expect((App_UARTPractice_GetLineDropCount() == 0) &&
                     (App_ProtocolPractice_GetFrameDropCount() == 0),
                     "mixed stream does not overflow message queues");

    App_ProtocolPractice_Task();
    failed += Expect((g_tx_count == 1) && (g_tx_packets[0][2] == 0x33u),
                     "mixed binary frame executes with original sequence");

    if (failed == 0)
    {
        printf("ALL UART RX PIPELINE TESTS PASSED\n");
    }
    else
    {
        printf("%d UART RX PIPELINE CHECKS FAILED\n", failed);
    }

    return failed == 0 ? 0 : 1;
}
