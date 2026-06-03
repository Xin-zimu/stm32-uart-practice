#include "app_protocol_practice.h"
#include "app_light.h"
#include "timing.h"
#include "stm32f10x_usart.h"
#include <stdio.h>

/*
 * Reliable binary protocol practice.
 *
 * Bytes arrive from the USART interrupt one at a time. The interrupt side only
 * validates and queues a complete frame; command execution runs in the main
 * loop so LED/app state is not changed inside the interrupt handler.
 */
#define PROTO_HEAD_1             0xAA
#define PROTO_HEAD_2             0x55
#define PROTO_MAX_DATA_LEN       16
#define PROTO_RX_TIMEOUT_MS      50u
#define PROTO_TX_TIMEOUT         100000u

/*
 * Final practice frame:
 * AA 55 SEQ LEN CMD DATA CHECKSUM
 *
 * CHECKSUM = SEQ + LEN + CMD + DATA, keep the low 8 bits.
 */
#define PROTO_CMD_PING           0x01
#define PROTO_CMD_LED            0x02
#define PROTO_CMD_STATUS         0x03
#define PROTO_CMD_BUZZER         0x04

#define PROTO_CMD_ACK            0x80
#define PROTO_CMD_STATUS_RSP     0x83

#define PROTO_ERR_OK             0x00
#define PROTO_ERR_BAD_LEN        0x01
#define PROTO_ERR_BAD_PARAM      0x02
#define PROTO_ERR_UNKNOWN_CMD    0x03
#define PROTO_ERR_RX_BUSY        0x04

typedef enum
{
    PROTO_RX_WAIT_AA = 0,
    PROTO_RX_WAIT_55,
    PROTO_RX_WAIT_SEQ,
    PROTO_RX_WAIT_LEN,
    PROTO_RX_WAIT_CMD,
    PROTO_RX_WAIT_DATA,
    PROTO_RX_WAIT_CHECKSUM
} ProtocolRxState;

typedef struct
{
    uint8_t seq;
    uint8_t len;
    uint8_t cmd;
    uint8_t data[PROTO_MAX_DATA_LEN];
} ProtocolFrame;

static ProtocolRxState s_rx_state = PROTO_RX_WAIT_AA;
static ProtocolFrame s_rx_frame;
static volatile ProtocolFrame s_pending_frame;
static volatile uint8_t s_pending_ready = 0;
static uint8_t s_data_count = 0;

static volatile uint16_t s_rx_ok_count = 0;
static volatile uint16_t s_rx_error_count = 0;
static volatile uint16_t s_rx_timeout_count = 0;
static uint32_t s_last_rx_tick = 0;
static uint8_t s_buzzer_state = 0;

static uint8_t Protocol_CalcChecksum(const ProtocolFrame *frame)
{
    uint8_t checksum;
    uint8_t i;

    checksum = 0;
    checksum = (uint8_t)(checksum + frame->seq);
    checksum = (uint8_t)(checksum + frame->len);
    checksum = (uint8_t)(checksum + frame->cmd);

    for (i = 0; i < frame->len; i++)
    {
        checksum = (uint8_t)(checksum + frame->data[i]);
    }

    return checksum;
}

static uint16_t Protocol_CalcCrc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc;
    uint8_t i;
    uint8_t bit;

    crc = 0xFFFF;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];

        for (bit = 0; bit < 8; bit++)
        {
            if ((crc & 0x0001) != 0)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }

    return crc;
}

static void Protocol_ResetRx(void)
{
    s_rx_state = PROTO_RX_WAIT_AA;
    s_rx_frame.seq = 0;
    s_rx_frame.len = 0;
    s_rx_frame.cmd = 0;
    s_data_count = 0;
}

/* Abort a half-received frame if the sender pauses too long between bytes. */
static void Protocol_CheckRxTimeout(uint32_t now_ms)
{
    if (s_rx_state == PROTO_RX_WAIT_AA)
    {
        return;
    }

    if ((uint32_t)(now_ms - s_last_rx_tick) > PROTO_RX_TIMEOUT_MS)
    {
        s_rx_timeout_count++;
        s_rx_error_count++;
        Protocol_ResetRx();
    }
}

/*
 * Move one verified frame from the parser into the main-loop pending slot.
 * If the previous frame has not been handled yet, count it as an RX busy error.
 */
static void Protocol_PushFrameFromIrq(void)
{
    uint8_t i;

    if (s_pending_ready != 0)
    {
        s_rx_error_count++;
        return;
    }

    s_pending_frame.seq = s_rx_frame.seq;
    s_pending_frame.len = s_rx_frame.len;
    s_pending_frame.cmd = s_rx_frame.cmd;

    for (i = 0; i < s_rx_frame.len; i++)
    {
        s_pending_frame.data[i] = s_rx_frame.data[i];
    }

    s_pending_ready = 1;
    s_rx_ok_count++;
}

static void Protocol_SendByte(uint8_t byte)
{
    uint32_t timeout;

    USART_SendData(USART1, byte);

    timeout = PROTO_TX_TIMEOUT;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
        if (timeout == 0)
        {
            return;
        }

        timeout--;
    }
}

/* Build and send one complete protocol frame, including header and checksum. */
static void Protocol_SendFrame(uint8_t seq, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    ProtocolFrame frame;
    uint8_t i;

    if (len > PROTO_MAX_DATA_LEN)
    {
        return;
    }

    frame.seq = seq;
    frame.len = len;
    frame.cmd = cmd;

    for (i = 0; i < len; i++)
    {
        frame.data[i] = data[i];
    }

    Protocol_SendByte(PROTO_HEAD_1);
    Protocol_SendByte(PROTO_HEAD_2);
    Protocol_SendByte(frame.seq);
    Protocol_SendByte(frame.len);
    Protocol_SendByte(frame.cmd);

    for (i = 0; i < frame.len; i++)
    {
        Protocol_SendByte(frame.data[i]);
    }

    Protocol_SendByte(Protocol_CalcChecksum(&frame));
}

static void Protocol_SendAck(uint8_t seq, uint8_t source_cmd, uint8_t err_code)
{
    uint8_t data[2];

    data[0] = source_cmd;
    data[1] = err_code;

    Protocol_SendFrame(seq, PROTO_CMD_ACK, data, 2);
}

static void Protocol_SendStatus(uint8_t seq)
{
    uint8_t data[5];
    uint16_t ao;

    ao = App_Light_GetAO();

    data[0] = (uint8_t)App_Light_GetTrafficMode();
    data[1] = App_Light_IsDark();
    data[2] = (uint8_t)(ao >> 8);
    data[3] = (uint8_t)(ao & 0xFF);
    data[4] = s_buzzer_state;

    Protocol_SendFrame(seq, PROTO_CMD_STATUS_RSP, data, 5);
}

static uint8_t Protocol_HandleLedCommand(const ProtocolFrame *frame)
{
    if (frame->len != 1)
    {
        return PROTO_ERR_BAD_LEN;
    }

    if (frame->data[0] == 0x00)
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_OFF);
    }
    else if (frame->data[0] == 0x01)
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_RED);
    }
    else if (frame->data[0] == 0x02)
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_YELLOW);
    }
    else if (frame->data[0] == 0x03)
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_GREEN);
    }
    else if (frame->data[0] == 0x04)
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_AUTO);
    }
    else
    {
        return PROTO_ERR_BAD_PARAM;
    }

    return PROTO_ERR_OK;
}

static uint8_t Protocol_HandleBuzzerCommand(const ProtocolFrame *frame)
{
    if (frame->len != 1)
    {
        return PROTO_ERR_BAD_LEN;
    }

    if ((frame->data[0] != 0x00) && (frame->data[0] != 0x01))
    {
        return PROTO_ERR_BAD_PARAM;
    }

    /*
     * This board project has no buzzer driver yet. Keep the state in software
     * so the protocol command can still be practiced and queried.
     */
    s_buzzer_state = frame->data[0];
    return PROTO_ERR_OK;
}

/* Decode one complete frame and send either ACK, status, or an error ACK. */
static void Protocol_HandleFrame(const ProtocolFrame *frame)
{
    uint8_t err_code;

    err_code = PROTO_ERR_OK;

    if (frame->cmd == PROTO_CMD_PING)
    {
        if (frame->len != 0)
        {
            err_code = PROTO_ERR_BAD_LEN;
        }

        Protocol_SendAck(frame->seq, frame->cmd, err_code);
    }
    else if (frame->cmd == PROTO_CMD_LED)
    {
        err_code = Protocol_HandleLedCommand(frame);
        Protocol_SendAck(frame->seq, frame->cmd, err_code);
    }
    else if (frame->cmd == PROTO_CMD_STATUS)
    {
        if (frame->len != 0)
        {
            Protocol_SendAck(frame->seq, frame->cmd, PROTO_ERR_BAD_LEN);
        }
        else
        {
            Protocol_SendAck(frame->seq, frame->cmd, PROTO_ERR_OK);
            Protocol_SendStatus(frame->seq);
        }
    }
    else if (frame->cmd == PROTO_CMD_BUZZER)
    {
        err_code = Protocol_HandleBuzzerCommand(frame);
        Protocol_SendAck(frame->seq, frame->cmd, err_code);
    }
    else
    {
        Protocol_SendAck(frame->seq, frame->cmd, PROTO_ERR_UNKNOWN_CMD);
    }
}

void App_ProtocolPractice_Init(void)
{
    Protocol_ResetRx();
    s_pending_ready = 0;
    s_rx_ok_count = 0;
    s_rx_error_count = 0;
    s_rx_timeout_count = 0;
    s_last_rx_tick = Timing_GetTick();
    s_buzzer_state = 0;

    printf("Binary protocol practice ready. See protocol_practice_protocol.md\r\n");

    /*
     * Keep this call in the module so the CRC16 lesson has real code to read.
     * The current practice frame still uses the simpler 8-bit checksum.
     */
    (void)Protocol_CalcCrc16((const uint8_t *)"CRC", 3);
}

uint8_t App_ProtocolPractice_ReceiveByte(uint8_t byte)
{
    uint32_t now_ms;

    now_ms = Timing_GetTick();
    Protocol_CheckRxTimeout(now_ms);
    s_last_rx_tick = now_ms;

    switch (s_rx_state)
    {
        case PROTO_RX_WAIT_AA:
            if (byte == PROTO_HEAD_1)
            {
                s_rx_state = PROTO_RX_WAIT_55;
                return 1;
            }
            break;

        case PROTO_RX_WAIT_55:
            if (byte == PROTO_HEAD_2)
            {
                s_rx_state = PROTO_RX_WAIT_SEQ;
                return 1;
            }
            else if (byte == PROTO_HEAD_1)
            {
                s_rx_state = PROTO_RX_WAIT_55;
                return 1;
            }
            else
            {
                Protocol_ResetRx();
            }
            break;

        case PROTO_RX_WAIT_SEQ:
            s_rx_frame.seq = byte;
            s_rx_state = PROTO_RX_WAIT_LEN;
            return 1;

        case PROTO_RX_WAIT_LEN:
            s_rx_frame.len = byte;
            s_data_count = 0;

            if (s_rx_frame.len > PROTO_MAX_DATA_LEN)
            {
                s_rx_error_count++;
                Protocol_ResetRx();
            }
            else
            {
                s_rx_state = PROTO_RX_WAIT_CMD;
            }
            return 1;

        case PROTO_RX_WAIT_CMD:
            s_rx_frame.cmd = byte;

            if (s_rx_frame.len == 0)
            {
                s_rx_state = PROTO_RX_WAIT_CHECKSUM;
            }
            else
            {
                s_rx_state = PROTO_RX_WAIT_DATA;
            }
            return 1;

        case PROTO_RX_WAIT_DATA:
            s_rx_frame.data[s_data_count] = byte;
            s_data_count++;

            if (s_data_count >= s_rx_frame.len)
            {
                s_rx_state = PROTO_RX_WAIT_CHECKSUM;
            }
            return 1;

        case PROTO_RX_WAIT_CHECKSUM:
            if (byte == Protocol_CalcChecksum(&s_rx_frame))
            {
                /* Keep command handling out of the IRQ; queue for Task(). */
                Protocol_PushFrameFromIrq();
            }
            else
            {
                s_rx_error_count++;
            }

            Protocol_ResetRx();
            return 1;

        default:
            Protocol_ResetRx();
            break;
    }

    return 0;
}

void App_ProtocolPractice_Task(void)
{
    ProtocolFrame frame;
    uint8_t i;

    Protocol_CheckRxTimeout(Timing_GetTick());

    if (s_pending_ready == 0)
    {
        return;
    }

    /* Copy the volatile pending frame while interrupts are closed briefly. */
    __disable_irq();
    frame.seq = s_pending_frame.seq;
    frame.len = s_pending_frame.len;
    frame.cmd = s_pending_frame.cmd;

    for (i = 0; i < s_pending_frame.len; i++)
    {
        frame.data[i] = s_pending_frame.data[i];
    }

    s_pending_ready = 0;
    __enable_irq();

    Protocol_HandleFrame(&frame);
}

uint16_t App_ProtocolPractice_GetRxOkCount(void)
{
    return s_rx_ok_count;
}

uint16_t App_ProtocolPractice_GetRxErrorCount(void)
{
    return s_rx_error_count;
}

uint16_t App_ProtocolPractice_GetRxTimeoutCount(void)
{
    return s_rx_timeout_count;
}
