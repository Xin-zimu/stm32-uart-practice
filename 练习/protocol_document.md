# UART 二进制协议文档

## 1. 帧格式

当前帧格式：

```text
AA 55 LEN PAYLOAD CHECK
```

示例请求：

```text
AA 55 04 01 10 01 01 17
```

含义：

```text
AA 55         帧头
04            LEN
01 10 01 01   PAYLOAD
17            CHECK
```

## 2. 字段含义

| 字段 | 长度 | 含义 |
|---|---:|---|
| `AA 55` | 2 字节 | 固定帧头 |
| `LEN` | 1 字节 | PAYLOAD 长度 |
| `PAYLOAD` | `LEN` 字节 | 命令号和命令参数 |
| `CHECK` | 1 字节 | 校验和 |

规则：

```text
PAYLOAD[0] 固定是命令号 CMD。
PAYLOAD[1] 固定是序号 SEQ。
PAYLOAD[2...] 是命令参数 DATA。
LEN 包含 CMD、SEQ 和 DATA。
```

## 3. 命令号

| 命令 | 值 | 方向 | 含义 |
|---|---:|---|---|
| `CMD_LED_SET` | `0x01` | 上位机 -> MCU | 设置 LED 状态 |
| `CMD_PING` | `0x02` | 上位机 -> MCU | 测试 MCU 是否有响应 |
| `CMD_LED_SET_RSP` | `0x81` | MCU -> 上位机 | LED_SET 响应 |
| `CMD_PING_RSP` | `0x82` | MCU -> 上位机 | PING 响应 |
| `CMD_ERROR_RSP` | `0xFF` | MCU -> 上位机 | 通用错误响应 |

当前命令号约定：

```text
0x01 ~ 0x7F：请求命令
0x80 ~ 0xFE：响应命令
0xFF：通用错误响应
```

## 4. 参数格式

### 4.1 LED_SET

请求 PAYLOAD：

```text
CMD_LED_SET SEQ LED_ID ON_OFF
```

示例：

```text
01 10 01 01
```

含义：

| 字节 | 含义 |
|---:|---|
| `0x01` | `CMD_LED_SET` |
| `0x10` | `SEQ = 0x10` |
| `0x01` | `LED_ID = 1` |
| `0x01` | `ON_OFF = 1`，打开 |

规则：

```text
LED_ID：当前只支持 1
ON_OFF：0 = 关闭，1 = 打开
```

### 4.2 PING

请求 PAYLOAD：

```text
CMD_PING SEQ
```

示例：

```text
02 13
```

PING 没有额外参数，但仍然必须携带 SEQ。

## 5. 回复格式

大部分正常响应使用 3 字节 PAYLOAD：

```text
RSP_CMD SEQ STATUS
```

### 5.1 LED_SET 回复

成功示例：

```text
AA 55 03 81 10 00 94
```

含义：

```text
03      LEN
81      CMD_LED_SET_RSP
10      SEQ
00      STATUS_OK
94      CHECK
```

### 5.2 PING 回复

成功示例：

```text
AA 55 03 82 13 00 98
```

含义：

```text
03      LEN
82      CMD_PING_RSP
13      SEQ
00      STATUS_OK
98      CHECK
```

### 5.3 未知命令回复

未知命令响应 PAYLOAD：

```text
CMD_ERROR_RSP SEQ STATUS ORIGINAL_CMD
```

示例请求：

```text
AA 55 02 09 14 1F
```

响应：

```text
AA 55 04 FF 14 04 09 24
```

含义：

```text
04      LEN
FF      CMD_ERROR_RSP
14      SEQ
04      PROTO_STATUS_UNKNOWN_CMD
09      原始未知命令
24      CHECK
```

## 6. 错误码

| 状态码 | 值 | 含义 |
|---|---:|---|
| `PROTO_STATUS_OK` | `0x00` | 成功 |
| `PROTO_STATUS_LEN_ERROR` | `0x01` | 参数长度错误 |
| `PROTO_STATUS_LED_ID_ERROR` | `0x02` | LED_ID 不支持 |
| `PROTO_STATUS_ON_OFF_ERROR` | `0x03` | ON_OFF 参数错误 |
| `PROTO_STATUS_UNKNOWN_CMD` | `0x04` | 未知命令 |

## 7. 校验方式

校验规则：

```text
CHECK = LEN + PAYLOAD 所有字节相加，只保留低 8 位
```

等价 C 逻辑：

```c
uint8_t check = (uint8_t)((len + payload[0] + payload[1] + ...) & 0xFF);
```

示例：

```text
AA 55 04 01 10 01 01 17
```

计算：

```text
04 + 01 + 10 + 01 + 01 = 17
```

溢出示例：

```text
03 + FF + 04 + 09 = 10F
低 8 位 = 0F
```

## 8. 大小端规则

当前协议只使用 1 字节字段，所以目前不涉及大小端问题。

后续如果加入多字节整数，建议统一使用：

```text
大端序。
高字节在前，低字节在后。
```

示例：

```text
uint16_t value = 0x1234
编码为：12 34
```

这样在串口工具里直接看 HEX 数据时更直观。

## 9. 最大长度

`LEN` 是 1 字节，所以理论最大值是 255。

当前实现使用：

```text
PROTO_MAX_DATA_LEN
```

当前练习测试值：

```text
PROTO_MAX_DATA_LEN = 32
```

规则：

```text
LEN 必须 >= 2
LEN 必须 <= PROTO_MAX_DATA_LEN
LEN 包含 CMD、SEQ 和 DATA
```

长度非法的帧会被丢弃，并且当前不返回响应。

## 10. 超时规则

当前练习代码已经实现接收超时。

当前超时时间：

```text
PROTO_RX_TIMEOUT_MS = 50 ms
```

超时规则：

```text
如果一帧已经开始接收，但超过 RX_TIMEOUT_MS 还没有收到下一个字节，
就丢弃当前半包，并把状态机重置到 WAIT_AA。
```

当前提供两个相关接口：

```text
Protocol_InputByteWithTime(byte, now_ms)
Protocol_CheckTimeout(now_ms)
```

使用方式：

```text
1. 每次收到 UART 字节时，调用 Protocol_InputByteWithTime(byte, now_ms)。
2. 主循环或定时器里周期调用 Protocol_CheckTimeout(now_ms)。
3. 如果当前状态不是 WAIT_AA，并且 now_ms - last_rx_tick > PROTO_RX_TIMEOUT_MS：
       重置状态机到 WAIT_AA。
```

超时的作用：

```text
当中间丢字节时，状态机不会一直卡在 RECV_PAYLOAD 或 WAIT_CHECK。
```

## 11. 可靠通信规则

当前协议已经具备基础可靠性：

```text
帧头：用于同步帧起点
LEN：用于确定帧边界
CHECK：用于检测数据损坏
SEQ：用于匹配请求和响应
STATUS：用于反馈命令执行结果
UNKNOWN_CMD：用于反馈未知命令
```

当前还没有实现：

```text
重试
上位机侧命令超时
重复命令检测
```

当前命令格式：

```text
PAYLOAD = CMD SEQ DATA...
```

响应格式：

```text
PAYLOAD = RSP_CMD SEQ STATUS DATA...
```

上位机规则：

```text
发送命令时带 SEQ。
等待带相同 SEQ 的响应。
如果超时没有响应，重试 N 次。
如果仍然失败，报告通信失败。
```

MCU 规则：

```text
只处理校验正确的帧。
响应里带回相同 SEQ。
必要时记录最近 SEQ，用于识别重复命令。
```

## 12. 如何从字节流中恢复结构化数据

当前代码已经实现了这个核心能力。

UART 输入本质上只是连续字节流：

```text
AA 55 03 01 01 01 06 AA 55 01 02 03 ...
```

状态机把字节流恢复成一帧一帧的数据：

```text
WAIT_AA
WAIT_55
WAIT_LEN
RECV_PAYLOAD
WAIT_CHECK
```

解析依赖：

```text
帧头：找到一帧的开始
LEN：知道 PAYLOAD 要收几个字节
CHECK：确认这一帧是否有效
Dispatch：把 PAYLOAD 转换成具体命令
```

重要规则：

```text
PAYLOAD 是二进制数据。
不能用 '\0'、'\r'、'\n' 判断 PAYLOAD 结束。
PAYLOAD 是否结束只能看 LEN。
```

## 13. 如何让 MCU 和上位机稳定交互

当前交互流程：

```text
上位机发送请求帧
MCU 解析帧
MCU 校验 CHECK
MCU 分发命令
MCU 执行命令
MCU 返回响应帧
上位机检查响应
```

当前稳定性级别：

```text
适合基础 UART 命令练习。
还不是完整可靠传输协议。
```

后续要更稳定，可以继续加：

```text
MCU 接收超时
SEQ 序号
上位机超时重试
响应按 SEQ 匹配
校验失败统计
未知命令统计
接收成功统计
```
