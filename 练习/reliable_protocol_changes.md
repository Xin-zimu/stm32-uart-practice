# 可靠通信改动总结

本文档只总结本轮围绕下面四个目标做的改动：

```text
1. MCU 接收超时
2. SEQ 序号
3. 上位机超时重试
4. 响应按 SEQ 匹配
```

相关文件：

```text
练习/practice
练习/protocol_test_host.c
练习/protocol_document.md
```

## 1. MCU 接收超时

### 1.1 新增状态变量

在 `practice` 里新增：

```c
static uint32_t s_last_rx_tick;
```

作用：

```text
记录最近一次收到 UART 字节的时间。
单位由调用者决定，目前测试里按毫秒 now_ms 传入。
```

为什么需要它：

```text
状态机可能已经收到 AA 55 LEN，但后面的 PAYLOAD 或 CRC16 丢了。
如果没有超时机制，状态机会一直停在 RECV_PAYLOAD、WAIT_CRC_LO 或 WAIT_CRC_HI。
s_last_rx_tick 用来判断“距离上一次收到字节已经过去多久”。
```

### 1.2 新增超时时间宏

新增：

```c
#define PROTO_RX_TIMEOUT_MS  50u
```

含义：

```text
如果状态机已经进入半包状态，并且超过 50 ms 没有收到下一个字节，
就认为当前帧接收失败，丢弃半包。
```

这里的 `50u`：

```text
50 表示数值。
u 表示 unsigned，无符号整数。
```

### 1.3 新增接收复位函数

新增：

```c
static void Protocol_ResetRx(void)
{
    s_rx_len = 0;
    s_rx_index = 0;
    s_rx_state = PROTO_STATE_WAIT_AA;
}
```

作用：

```text
清空当前半包接收信息。
把状态机重置到等待帧头 AA。
```

为什么做成 `static`：

```text
这个函数只给 protocol 内部使用。
外部模块不应该随便重置协议状态机。
```

### 1.4 新增超时检查函数

新增：

```c
void Protocol_CheckTimeout(uint32_t now_ms)
{
    if(s_rx_state == PROTO_STATE_WAIT_AA)
    {
        return;
    }

    if((uint32_t)(now_ms - s_last_rx_tick) > PROTO_RX_TIMEOUT_MS)
    {
        Protocol_ResetRx();
    }
}
```

逻辑解释：

```text
1. 如果当前状态是 WAIT_AA，说明没有正在接收半包，不需要超时处理。
2. 如果当前状态不是 WAIT_AA，说明已经开始接收一帧。
3. 用 now_ms - s_last_rx_tick 计算距离上次收字节过去多久。
4. 如果超过 PROTO_RX_TIMEOUT_MS，就丢弃半包。
```

为什么写成：

```c
(uint32_t)(now_ms - s_last_rx_tick)
```

原因：

```text
无符号计数器溢出后，这种写法仍然能正常计算时间差。
这是嵌入式里常见的 tick 时间差写法。
```

### 1.5 新增带时间参数的输入函数

新增：

```c
void Protocol_InputByteWithTime(uint8_t byte, uint32_t now_ms)
{
    Protocol_CheckTimeout(now_ms);
    s_last_rx_tick = now_ms;

    switch(s_rx_state)
    {
        ...
    }
}
```

作用：

```text
每次收到一个 UART 字节时，同时告诉协议层当前时间。
协议层先检查是否已经超时，再处理当前新字节。
```

为什么先检查超时，再更新 `s_last_rx_tick`：

```text
当前字节到来之前，可能已经和上一个字节间隔太久。
如果先更新 s_last_rx_tick，就会把超时掩盖掉。
所以必须先用旧的 s_last_rx_tick 判断是否超时。
```

### 1.6 保留旧接口

保留：

```c
void Protocol_InputByte(uint8_t byte)
{
    Protocol_InputByteWithTime(byte, s_last_rx_tick);
}
```

作用：

```text
兼容之前的调用方式。
旧测试或旧代码仍然可以调用 Protocol_InputByte(byte)。
```

注意：

```text
真实工程里如果要启用超时，应该优先调用 Protocol_InputByteWithTime(byte, now_ms)，
并在主循环或定时器里周期调用 Protocol_CheckTimeout(now_ms)。
```

## 2. SEQ 序号

### 2.1 PAYLOAD 格式变化

旧格式：

```text
PAYLOAD = CMD DATA...
```

新格式：

```text
PAYLOAD = CMD SEQ DATA...
```

含义：

```text
CMD：命令号
SEQ：本次请求序号
DATA：命令参数
```

为什么要加 SEQ：

```text
让上位机知道某个响应对应哪一次请求。
如果发生延迟、重发、响应乱序，SEQ 可以用来匹配请求和响应。
```

### 2.2 LEN 最小值变化

旧规则：

```text
LEN 至少为 1，因为至少要包含 CMD。
```

新规则：

```text
LEN 至少为 2，因为必须包含 CMD + SEQ。
```

代码变化：

```c
if(byte < 2 || byte > PROTO_MAX_DATA_LEN)
{
    s_rx_state = PROTO_STATE_WAIT_AA;
}
```

作用：

```text
如果 LEN = 1，说明只有 CMD，没有 SEQ。
这种帧在新协议里不合法，直接丢弃。
```

### 2.3 Dispatch 解析方式变化

新增解析：

```c
uint8_t cmd = payload[0];
uint8_t seq = payload[1];
```

含义：

```text
payload[0] 是命令号。
payload[1] 是请求序号。
```

LED_SET 的参数起始位置也变化了。

旧代码：

```c
HandleLedSet(&payload[1], len - 1);
```

新代码：

```c
HandleLedSet(&payload[2], len - 2);
```

原因：

```text
payload[0] 是 CMD。
payload[1] 是 SEQ。
真正的业务参数从 payload[2] 开始。
```

### 2.4 响应格式变化

旧响应：

```text
RSP_CMD STATUS
```

新响应：

```text
RSP_CMD SEQ STATUS
```

代码变化：

```c
static void Protocol_SendStatus(uint8_t rsp_cmd, uint8_t seq, uint8_t status)
{
    uint8_t rsp_payload[3];

    rsp_payload[0] = rsp_cmd;
    rsp_payload[1] = seq;
    rsp_payload[2] = status;

    Protocol_SendFrame(rsp_payload, 3);
}
```

关键点：

```text
MCU 不生成新的 SEQ。
MCU 只把请求里的 SEQ 原样带回去。
```

例如：

```text
请求：CMD_LED_SET, SEQ=0x10
响应：CMD_LED_SET_RSP, SEQ=0x10, STATUS
```

### 2.5 未知命令响应变化

旧格式：

```text
CMD_ERROR_RSP STATUS ORIGINAL_CMD
```

新格式：

```text
CMD_ERROR_RSP SEQ STATUS ORIGINAL_CMD
```

代码：

```c
uint8_t rsp_payload[4];

rsp_payload[0] = CMD_ERROR_RSP;
rsp_payload[1] = seq;
rsp_payload[2] = PROTO_STATUS_UNKNOWN_CMD;
rsp_payload[3] = cmd;

Protocol_SendFrame(rsp_payload, 4);
```

为什么未知命令也要带 SEQ：

```text
未知命令也是对某一次请求的响应。
上位机仍然需要知道这个错误响应对应哪个请求。
```

## 3. 上位机超时重试

这一部分目前在 `protocol_test_host.c` 里用测试代码模拟。

它不是正式 PC 串口程序，但已经把可靠通信规则练出来了。

### 3.1 新增上位机侧配置

新增：

```c
#define HOST_TX_TIMEOUT_MS 100u
#define HOST_MAX_RETRIES   2u
```

含义：

```text
HOST_TX_TIMEOUT_MS：上位机等待响应的超时时间。
HOST_MAX_RETRIES：最多重试次数。
```

### 3.2 新增上位机请求状态结构体

新增：

```c
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
```

字段解释：

```text
pending：
    当前是否有一条请求正在等待响应。

failed：
    是否已经超过最大重试次数并失败。

seq：
    当前请求的 SEQ。

retry_count：
    已经重试了几次。

send_count：
    实际发送了几次。第一次发送也算一次。

status：
    收到匹配响应后记录的状态码。

payload：
    保存本次请求的 PAYLOAD。
    超时重试时可以重新发送同一份 payload。

payload_len：
    payload 长度。

last_send_ms：
    上一次发送请求的时间。
```

为什么要保存 payload：

```text
重试时不能重新组一条不同 SEQ 的请求。
必须重发同一个请求，尤其 SEQ 必须保持不变。
```

### 3.3 启动一次请求

新增：

```c
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
```

作用：

```text
模拟上位机发送 LED_SET 命令。
payload 格式是 CMD SEQ LED_ID ON_OFF。
```

重点：

```text
第一次发送后 pending = true。
之后必须等待相同 SEQ 的响应。
```

### 3.4 发送或重发请求

新增：

```c
static void Host_SendPending(uint32_t now_ms)
{
    Host_FeedPayloadToMcu(g_host.payload, g_host.payload_len, now_ms);
    g_host.last_send_ms = now_ms;
    g_host.send_count++;
}
```

作用：

```text
把当前保存的 payload 重新发给 MCU。
第一次发送和超时重试都调用它。
```

为什么重试不重新分配 SEQ：

```text
同一条请求重试，必须保持同一个 SEQ。
否则上位机会把重试当成另一条新请求。
```

### 3.5 检查上位机发送超时

新增：

```c
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
```

逻辑解释：

```text
1. 如果没有等待中的请求，不处理。
2. 如果还没超过超时时间，不处理。
3. 如果已经达到最大重试次数，标记失败。
4. 否则 retry_count 加 1，然后重发同一份 payload。
```

## 4. 响应按 SEQ 匹配

### 4.1 新增响应解析函数

新增：

```c
static bool Host_ProcessCapturedResponse(void)
```

它做几件事：

```text
1. 检查响应帧长度是否足够。
2. 检查帧头是否是 AA 55。
3. 根据 LEN 判断整帧长度是否正确。
4. 重新计算 CRC16。
5. 判断当前是否有 pending 请求。
6. 检查响应 payload[1] 是否等于当前等待的 SEQ。
7. 如果 SEQ 匹配，记录 STATUS 并清除 pending。
```

### 4.2 SEQ 不匹配时忽略响应

关键代码：

```c
if(payload[1] != g_host.seq)
{
    return false;
}
```

含义：

```text
响应里的 SEQ 不是当前等待的 SEQ。
这说明它不是当前请求的响应。
上位机不能把它当作成功。
应该继续等待当前请求的正确响应。
```

### 4.3 SEQ 匹配时结束等待

关键代码：

```c
g_host.status = payload[2];
g_host.pending = false;
return true;
```

含义：

```text
响应 SEQ 和请求 SEQ 一致。
说明这就是当前请求的响应。
记录状态码，然后结束等待。
```

## 5. 新增测试场景

### 5.1 MCU 接收超时测试

测试：

```text
输入 AA 55 后停住。
超过超时时间。
检查状态机是否回到 WAIT_AA。
```

验证点：

```text
半包不会永久卡住。
```

### 5.2 超时后旧半包不再生效

测试：

```text
先输入 AA。
等待到超时。
再输入旧半包剩下的 55 03 01 01 01 06。
```

验证点：

```text
超时后的旧数据不能继续拼成一帧。
```

### 5.3 SEQ 不匹配响应测试

测试：

```text
上位机等待 SEQ=0x20。
先收到 SEQ=0x21 的响应。
上位机忽略。
再收到 SEQ=0x20 的响应。
上位机接受。
```

验证点：

```text
响应必须按 SEQ 匹配。
不能只看响应命令号和状态码。
```

### 5.4 上位机超时重试测试

测试：

```text
上位机发送 SEQ=0x30 的 LED_SET。
在超时前检查，不重试。
超过 HOST_TX_TIMEOUT_MS 后检查，触发重试。
重试后收到同 SEQ 响应。
请求结束。
```

验证点：

```text
超时后重发同一请求。
重发仍然使用同一个 SEQ。
收到匹配响应后停止等待。
```

## 6. 当前四个任务状态

```text
MCU 接收超时：已完成
SEQ 序号：已完成
上位机超时重试：已完成测试版模拟
响应按 SEQ 匹配：已完成测试版模拟
```

说明：

```text
“上位机超时重试”和“响应按 SEQ 匹配”目前是在 protocol_test_host.c 中模拟。
它不是正式上位机软件，但协议规则已经验证通过。
后续如果写真正上位机程序，可以直接照这个逻辑迁移。
```
