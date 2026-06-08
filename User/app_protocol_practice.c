/*
 * 文件名称：app_protocol_practice.c
 *
 * 功能说明：
 * 本文件实现 USART1 二进制通信协议。串口接收中断每收到一个字节，
 * 就调用 App_ProtocolPractice_ReceiveByte() 推进接收状态机。状态机
 * 找到帧头、接收各字段并完成 CRC16 校验后，只把完整帧放入 pending
 * 邮箱；具体命令由主循环中的 App_ProtocolPractice_Task() 执行。
 *
 * 正式帧格式：
 *
 *     AA 55 SEQ LEN CMD DATA CRC_LO CRC_HI
 *
 * 字段含义：
 * - AA 55：固定帧头，用于从连续字节流中寻找一帧的起点。
 * - SEQ：上位机分配的序列号，STM32 响应时原样带回。
 * - LEN：DATA 区的字节数，不包括 CMD、帧头和 CRC。
 * - CMD：命令字。
 * - DATA：命令参数，可以为空，实际长度由 LEN 决定。
 * - CRC_LO、CRC_HI：CRC-16/MODBUS 结果，低字节先发送。
 *
 * CRC 只覆盖以下字段：
 *
 *     SEQ + LEN + CMD + DATA
 *
 * 固定帧头 AA 55 和 CRC 自身不参加计算。
 *
 * 中断与主循环分工：
 * - 中断：逐字节解析、长度检查、CRC 校验、完整帧入队。
 * - 主循环：命令分发、外设控制、状态读取和响应发送。
 *
 * 这种分工可以缩短 USART 接收中断的执行时间，避免在中断中执行
 * 较慢的业务逻辑，同时通过单帧 pending 邮箱保护帧数据的一致性。
 */

#include "app_protocol_practice.h"    // 本模块对外接口和 STM32 基础类型
#include "app_light.h"                // 交通灯控制、AO 值和亮暗状态接口
#include "timing.h"                   // 毫秒节拍，用于接收半包超时判断
#include "uart_tx.h"                  // USART1 TX 环形缓冲和整块入队接口
#include <stdio.h>                    // printf 初始化提示

#define PROTO_HEAD_1             0xAA        // 固定帧头第一个字节
#define PROTO_HEAD_2             0x55        // 固定帧头第二个字节
#define PROTO_MAX_DATA_LEN       16          // DATA 最大长度，也是接收数组容量
#define PROTO_MAX_FRAME_LEN      (7u + PROTO_MAX_DATA_LEN) // 完整帧最大字节数
#define PROTO_RX_TIMEOUT_MS      50u         // 相邻接收字节最大允许间隔，单位 ms

#define PROTO_CMD_PING           0x01        // 通信测试，DATA 必须为空
#define PROTO_CMD_LED            0x02        // 设置交通灯模式
#define PROTO_CMD_STATUS         0x03        // 读取设备状态
#define PROTO_CMD_BUZZER         0x04        // 设置蜂鸣器软件状态

#define PROTO_CMD_ACK            0x80        // 通用 ACK 响应
#define PROTO_CMD_STATUS_RSP     0x83        // 设备状态响应

#define PROTO_ERR_OK             0x00        // 命令执行成功
#define PROTO_ERR_BAD_LEN        0x01        // DATA 长度不符合命令要求
#define PROTO_ERR_BAD_PARAM      0x02        // DATA 参数值非法
#define PROTO_ERR_UNKNOWN_CMD    0x03        // 未定义命令
#define PROTO_ERR_RX_BUSY        0x04        // 接收邮箱忙，保留给后续扩展

/*
 * 接收状态枚举。
 *
 * 每个状态表示当前正在等待哪个字段。状态机一次只处理一个串口字节，
 * 收到合法字段后切换到下一状态；一帧结束、发生错误或超时后，统一
 * 回到 PROTO_RX_WAIT_AA，重新寻找下一帧。
 */
typedef enum
{
    PROTO_RX_WAIT_AA = 0,                     // 等待第一个帧头 0xAA
    PROTO_RX_WAIT_55,                         // 已收到 0xAA，等待 0x55
    PROTO_RX_WAIT_SEQ,                        // 等待序列号
    PROTO_RX_WAIT_LEN,                        // 等待 DATA 长度
    PROTO_RX_WAIT_CMD,                        // 等待命令字
    PROTO_RX_WAIT_DATA,                       // 按 LEN 接收 DATA
    PROTO_RX_WAIT_CRC_LO,                     // 等待 CRC 低字节
    PROTO_RX_WAIT_CRC_HI                      // 等待 CRC 高字节并执行校验
} ProtocolRxState;

/*
 * 协议帧的内部表示。
 *
 * 帧头只用于字节流同步，CRC 只用于完整性校验，因此二者不保存在
 * ProtocolFrame 中。data 数组只有前 len 个字节有效。
 */
typedef struct
{
    uint8_t seq;                              // 请求序列号，响应时原样带回
    uint8_t len;                              // DATA 实际长度
    uint8_t cmd;                              // 命令字
    uint8_t data[PROTO_MAX_DATA_LEN];         // 命令参数缓冲区
} ProtocolFrame;

static ProtocolRxState s_rx_state = PROTO_RX_WAIT_AA;    // 当前接收状态
static ProtocolFrame s_rx_frame;                         // 中断侧正在组装的帧
static volatile ProtocolFrame s_pending_frame;           // 中断交给主循环的完整帧
static volatile uint8_t s_pending_ready = 0;              // 1 表示 pending 中有待处理帧
static uint8_t s_data_count = 0;                          // 当前已接收的 DATA 字节数
static uint8_t s_rx_crc_low = 0;                          // 暂存线上先到达的 CRC 低字节

static volatile uint16_t s_rx_ok_count = 0;               // 成功进入 pending 的帧数
static volatile uint16_t s_rx_error_count = 0;            // 所有接收错误的累计次数
static volatile uint16_t s_rx_timeout_count = 0;          // 半包接收超时次数
static uint32_t s_last_rx_tick = 0;                        // 最近一次收到字节的毫秒节拍
static uint8_t s_buzzer_state = 0;                         // 蜂鸣器软件模拟状态

/*
 * 使用一个输入字节更新 CRC-16/MODBUS 中间值。
 *
 * 参数 crc 是处理当前字节之前的 CRC，参数 byte 是本次加入计算的
 * 字节。该算法采用右移形式，反射多项式为 0xA001。新字节先异或
 * 到 CRC 低 8 位，然后固定处理 8 个二进制位。
 *
 * 最低位为 1 时，右移后异或 0xA001；最低位为 0 时只右移。
 * 返回值继续作为下一个输入字节的 CRC 中间值。
 */
static uint16_t Protocol_UpdateCrc16(uint16_t crc, uint8_t byte)
{
    uint8_t bit;

    crc ^= byte;
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

    return crc;
}

/*
 * 计算一帧业务字段的 CRC-16/MODBUS。
 *
 * 初值固定为 0xFFFF，字段输入顺序固定为：
 *
 *     SEQ -> LEN -> CMD -> DATA[0] -> ... -> DATA[len - 1]
 *
 * 每个字段都在前一个字段的 CRC 结果上继续计算，不能为每个字段
 * 重新初始化。帧头 AA 55 和 CRC 字节不参与计算。
 */
static uint16_t Protocol_CalcFrameCrc16(const ProtocolFrame *frame)
{
    uint16_t crc;
    uint8_t i;

    crc = 0xFFFF;
    crc = Protocol_UpdateCrc16(crc, frame->seq);
    crc = Protocol_UpdateCrc16(crc, frame->len);
    crc = Protocol_UpdateCrc16(crc, frame->cmd);

    for (i = 0; i < frame->len; i++)
    {
        crc = Protocol_UpdateCrc16(crc, frame->data[i]);
    }

    return crc;
}

/*
 * 丢弃当前半包并复位接收状态机。
 *
 * 该函数会清除当前帧的关键字段、DATA 计数和暂存的 CRC 低字节，
 * 然后重新等待 0xAA。初始化、长度错误、CRC 校验结束、接收超时
 * 和非法状态都会调用此函数。
 */
static void Protocol_ResetRx(void)
{
    s_rx_state = PROTO_RX_WAIT_AA;
    s_rx_frame.seq = 0;
    s_rx_frame.len = 0;
    s_rx_frame.cmd = 0;
    s_data_count = 0;
    s_rx_crc_low = 0;
}

/*
 * 检查正在接收的半包是否超时。
 *
 * 等待 0xAA 时说明当前没有半包，不需要检查。其他状态下，如果
 * 当前节拍与最近一次接收节拍的差值超过 50ms，就认为发送中断、
 * 字节丢失或帧不完整，记录超时和接收错误后复位状态机。
 *
 * 使用无符号减法计算时间差，可以正确处理 32 位节拍自然回绕。
 */
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
 * 将 CRC 校验成功的接收帧放入主循环 pending 邮箱。
 *
 * 本函数位于串口中断调用路径中，只进行短时间内存复制，不执行
 * 命令和串口回复。当前邮箱只有一个槽；如果上一帧还没有被主循环
 * 取走，新帧不会覆盖旧帧，而是增加错误计数并被丢弃。
 *
 * 所有字段复制完成后才设置 s_pending_ready，避免主循环读取到
 * 只复制了一部分的帧。
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

/*
 * 组装并发送一个完整协议帧。
 *
 * 参数：
 * - seq：响应序列号，通常直接使用请求中的 SEQ。
 * - cmd：本次发送帧的命令字。
 * - data：DATA 区首地址；len 为 0 时不会访问该指针。
 * - len：DATA 字节数，不能超过 PROTO_MAX_DATA_LEN。
 *
 * 发送顺序固定为：
 *
 *     AA 55 SEQ LEN CMD DATA CRC_LO CRC_HI
 *
 * CRC 数值的低 8 位先发送，高 8 位后发送。
 */
static void Protocol_SendFrame(uint8_t seq, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    ProtocolFrame frame;
    uint8_t packet[PROTO_MAX_FRAME_LEN];
    uint8_t packet_len;
    uint16_t crc;
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

    packet_len = 0;
    packet[packet_len++] = PROTO_HEAD_1;
    packet[packet_len++] = PROTO_HEAD_2;
    packet[packet_len++] = frame.seq;
    packet[packet_len++] = frame.len;
    packet[packet_len++] = frame.cmd;

    for (i = 0; i < frame.len; i++)
    {
        packet[packet_len++] = frame.data[i];
    }

    crc = Protocol_CalcFrameCrc16(&frame);
    packet[packet_len++] = (uint8_t)(crc & 0xFF);         // CRC 低字节先发送
    packet[packet_len++] = (uint8_t)(crc >> 8);           // CRC 高字节后发送

    /*
     * 整帧一次性入队。空间不足时整个帧都失败，不会发送残缺协议帧。
     * 失败次数由 uart_tx 模块的 drop_count 统一记录。
     */
    (void)UartTx_TryWrite(packet, packet_len);
}

/*
 * 发送通用 ACK 响应。
 *
 * ACK 的 DATA 固定为两个字节：
 *
 *     ORIGIN_CMD ERR_CODE
 *
 * ORIGIN_CMD 表示被应答的请求命令，ERR_CODE 表示执行结果。
 * 响应使用请求中的 SEQ，供上位机匹配请求和响应。
 */
static void Protocol_SendAck(uint8_t seq, uint8_t source_cmd, uint8_t err_code)
{
    uint8_t data[2];

    data[0] = source_cmd;
    data[1] = err_code;

    Protocol_SendFrame(seq, PROTO_CMD_ACK, data, 2);
}

/*
 * 读取并发送当前设备状态。
 *
 * 状态响应 DATA 固定为 5 个字节：
 *
 *     TRAFFIC_MODE IS_DARK AO_H AO_L BUZZER
 *
 * AO 是 16 位值，在业务 DATA 中采用高字节在前的顺序。这里的
 * AO 字节序与帧尾 CRC 的低字节优先规则是两个独立约定。
 */
static void Protocol_SendStatus(uint8_t seq)
{
    uint8_t data[5];
    uint16_t ao;

    ao = App_Light_GetAO();

    data[0] = (uint8_t)App_Light_GetTrafficMode();         // 当前交通灯模式
    data[1] = App_Light_IsDark();                         // 当前亮暗判断
    data[2] = (uint8_t)(ao >> 8);                         // AO 高字节
    data[3] = (uint8_t)(ao & 0xFF);                       // AO 低字节
    data[4] = s_buzzer_state;                             // 蜂鸣器软件状态

    Protocol_SendFrame(seq, PROTO_CMD_STATUS_RSP, data, 5);
}

/*
 * 处理交通灯控制命令。
 *
 * 请求 DATA 必须只有一个 MODE 字节：
 * - 0x00：关闭全部交通灯。
 * - 0x01：红灯模式。
 * - 0x02：黄灯模式。
 * - 0x03：绿灯模式。
 * - 0x04：恢复光照自动控制。
 *
 * 本函数只返回协议错误码，不直接发送 ACK。
 */
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

/*
 * 处理蜂鸣器控制命令。
 *
 * 请求 DATA 必须只有一个 STATE 字节，0 表示关闭，1 表示打开。
 * 当前工程没有实际蜂鸣器驱动，因此这里只更新软件状态；后续增加
 * 硬件驱动时，可以在参数检查通过后同步控制蜂鸣器引脚。
 */
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

    s_buzzer_state = frame->data[0];
    return PROTO_ERR_OK;
}

/*
 * 分发并执行一帧已经通过 CRC 校验的命令。
 *
 * 本函数只在主循环中调用。PING、LED 和 BUZZER 返回一个 ACK；
 * STATUS 在请求合法时先返回成功 ACK，再发送独立状态响应帧。
 * 未定义命令不执行任何业务操作，只返回未知命令错误。
 */
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

/*
 * 初始化二进制协议模块。
 *
 * 应在系统节拍和 USART 初始化完成后调用一次。该函数复位状态机、
 * 清空 pending 标志和所有统计计数，并将蜂鸣器软件状态初始化为
 * 关闭。初始化结束后通过 printf 输出协议模块就绪提示。
 */
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
}

/*
 * 向协议状态机输入一个 USART 接收字节。
 *
 * 该函数通常由 USART1 接收中断调用。返回 1 表示当前字节已被
 * 二进制协议状态机接收或处理；返回 0 表示该字节不属于当前
 * 二进制帧，上层可以继续交给文本命令接收逻辑。
 *
 * 处理步骤：
 * 1. 先使用上一次接收时间检查半包是否超时。
 * 2. 更新最近一次接收时间。
 * 3. 根据当前状态解释本次字节并切换到下一状态。
 * 4. CRC 高字节到达后组合线上 CRC，与本地计算结果比较。
 * 5. CRC 正确时把完整帧放入 pending，随后复位接收状态机。
 *
 * WAIT_55 状态支持 AA AA 55 重同步：第二个 AA 可以作为新帧头
 * 的起点。LEN 在写入 DATA 数组之前会先检查最大长度，避免越界。
 */
uint8_t App_ProtocolPractice_ReceiveByte(uint8_t byte)
{
    uint32_t now_ms;

    now_ms = Timing_GetTick();
    Protocol_CheckRxTimeout(now_ms);                        // 必须在更新时间前检查间隔
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
                s_rx_state = PROTO_RX_WAIT_55;              // AA AA 55 重同步
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
                s_rx_state = PROTO_RX_WAIT_CRC_LO;          // 无 DATA，直接接收 CRC
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
                s_rx_state = PROTO_RX_WAIT_CRC_LO;
            }
            return 1;

        case PROTO_RX_WAIT_CRC_LO:
            s_rx_crc_low = byte;
            s_rx_state = PROTO_RX_WAIT_CRC_HI;
            return 1;

        case PROTO_RX_WAIT_CRC_HI:
        {
            uint16_t received_crc;

            received_crc = (uint16_t)s_rx_crc_low | ((uint16_t)byte << 8);
            if (received_crc == Protocol_CalcFrameCrc16(&s_rx_frame))
            {
                Protocol_PushFrameFromIrq();                // 中断中只入队，不执行命令
            }
            else
            {
                s_rx_error_count++;
            }

            Protocol_ResetRx();
            return 1;
        }

        default:
            Protocol_ResetRx();                            // 非法状态保护
            break;
    }

    return 0;
}

/*
 * 在主循环中处理一帧已经通过 CRC 校验的数据。
 *
 * 主循环应持续调用本函数。没有 pending 帧时立即返回，不会阻塞
 * 其他任务。复制 volatile pending 邮箱时短暂关闭中断，确保 SEQ、
 * LEN、CMD、DATA 和 ready 标志作为一个整体被取走。
 *
 * 本地副本完成后立即清除 ready 并恢复中断，具体命令处理和串口
 * 回复都在中断开启状态下执行，避免长时间阻塞 USART 接收。
 */
void App_ProtocolPractice_Task(void)
{
    ProtocolFrame frame;
    uint8_t i;

    Protocol_CheckRxTimeout(Timing_GetTick());              // 无新字节时也能清理超时半包

    if (s_pending_ready == 0)
    {
        return;
    }

    __disable_irq();
    frame.seq = s_pending_frame.seq;
    frame.len = s_pending_frame.len;
    frame.cmd = s_pending_frame.cmd;

    for (i = 0; i < s_pending_frame.len; i++)
    {
        frame.data[i] = s_pending_frame.data[i];
    }

    s_pending_ready = 0;                                   // 本地副本完成，释放单帧邮箱
    __enable_irq();

    Protocol_HandleFrame(&frame);
}

/*
 * 返回成功接收帧数。
 *
 * 只有 CRC 正确且成功放入 pending 邮箱的帧才会计数。CRC 正确但
 * pending 邮箱忙而被丢弃的帧不会计入成功数。
 */
uint16_t App_ProtocolPractice_GetRxOkCount(void)
{
    return s_rx_ok_count;
}

/*
 * 返回接收错误总数。
 *
 * 当前包括 LEN 超限、CRC 校验失败、半包超时和 pending 邮箱忙。
 */
uint16_t App_ProtocolPractice_GetRxErrorCount(void)
{
    return s_rx_error_count;
}

/*
 * 返回半包接收超时次数。
 *
 * 超时也包含在接收错误总数中，单独提供该接口便于判断问题是否
 * 主要由发送间隔过长、字节丢失或串口参数不匹配造成。
 */
uint16_t App_ProtocolPractice_GetRxTimeoutCount(void)
{
    return s_rx_timeout_count;
}
