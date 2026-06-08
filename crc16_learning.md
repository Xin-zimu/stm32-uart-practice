# CRC16 专属学习笔记

本文只讲当前 STM32 UART 工程实际使用的 CRC16，包括算法参数、代码实现、发送格式、接收校验和调试方法。

相关代码：

- `User/app_protocol_practice.c`：STM32 正式协议实现
- `Tools/uart_protocol_host.py`：Python 上位机实现
- `练习/practice`：适合单独阅读的简化实现
- `练习/protocol_test_host.c`：PC 端自动测试

## 1. 当前使用的 CRC16 是什么

当前协议使用：

```text
CRC-16/MODBUS
```

完整参数如下：

| 参数 | 当前值 | 含义 |
| --- | --- | --- |
| CRC 宽度 | 16 bit | 最终结果范围为 `0x0000~0xFFFF` |
| 初始值 Init | `0xFFFF` | 计算前 CRC 寄存器的初值 |
| 标准多项式 Poly | `0x8005` | CRC-16/MODBUS 的标准表示 |
| 反射多项式 | `0xA001` | 当前右移算法实际使用的表示 |
| 输入反射 RefIn | `true` | 每个字节按低位优先处理 |
| 输出反射 RefOut | `true` | 输出与反射算法一致 |
| 结果异或 XorOut | `0x0000` | 计算完成后不再异或其他值 |
| 标准校验值 Check | `0x4B37` | 字符串 `123456789` 的结果 |

注意：

```text
“CRC16”不是唯一算法。
```

CRC-16/MODBUS、CRC-16/CCITT-FALSE、CRC-16/XMODEM 等都产生 16 位结果，但初值、多项式、移位方向等参数不同，结果也不同。通信双方必须使用完全相同的 CRC 变体。

## 2. CRC16 有什么作用

CRC 用于发现串口传输中的数据错误，例如：

- 某一位由 0 变成 1
- 某一位由 1 变成 0
- 连续多位受到干扰
- 某些字节内容损坏
- 某些字节顺序发生变化

CRC 不是：

- 加密算法
- 身份认证
- 防篡改签名
- 数据压缩

攻击者如果知道算法，可以修改数据后重新计算 CRC。因此 CRC 只能用于错误检测，不能用于安全防护。

## 3. 当前协议帧格式

正式协议帧为：

```text
AA 55 SEQ LEN CMD DATA CRC_LO CRC_HI
```

字段说明：

| 字段 | 是否参与 CRC |
| --- | --- |
| `AA 55` 帧头 | 否 |
| `SEQ` 序列号 | 是 |
| `LEN` 数据长度 | 是 |
| `CMD` 命令字 | 是 |
| `DATA` 数据区 | 是 |
| `CRC_LO CRC_HI` | 它们是计算结果，不参与自身计算 |

所以 CRC 输入顺序固定为：

```text
SEQ -> LEN -> CMD -> DATA[0] -> DATA[1] -> ...
```

不能随意改变顺序，也不能遗漏 `LEN` 或 `CMD`。

帧头 `AA 55` 不参加 CRC，是当前协议的人为设计选择。只要 STM32 和上位机遵守同一规则即可。

## 4. 一个完整例子

PING 请求参数：

```text
SEQ  = 0x01
LEN  = 0x00
CMD  = 0x01
DATA = 空
```

参加 CRC 计算的字节是：

```text
01 00 01
```

CRC-16/MODBUS 计算结果是：

```text
0xC0E1
```

16 位数值可以拆成：

```text
高字节 CRC_HI = 0xC0
低字节 CRC_LO = 0xE1
```

当前协议按照 Modbus 常用传输顺序，低字节先发送：

```text
CRC_LO CRC_HI = E1 C0
```

因此完整 PING 帧是：

```text
AA 55 01 00 01 E1 C0
```

这里很容易混淆：

```text
CRC 数值写作：0xC0E1
串口发送顺序：E1 C0
```

## 5. 核心算法怎么工作

STM32 代码中的单字节更新函数：

```c
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
```

处理一个字节时分为三步。

### 5.1 当前字节与 CRC 异或

```c
crc ^= byte;
```

新字节先进入 CRC 寄存器的低 8 位。

例如：

```text
初始 CRC = FFFF
输入字节 = 01

FFFF XOR 0001 = FFFE
```

### 5.2 每个字节处理 8 次

一个字节有 8 bit，因此循环 8 次：

```c
for (bit = 0; bit < 8; bit++)
```

### 5.3 根据最低位决定是否异或多项式

如果最低位为 1：

```c
crc = (crc >> 1) ^ 0xA001;
```

如果最低位为 0：

```c
crc = crc >> 1;
```

这里检查的是最低位：

```c
crc & 0x0001
```

所以算法采用右移和反射多项式 `0xA001`。

## 6. 为什么代码使用 0xA001

CRC-16/MODBUS 的标准多项式通常写作：

```text
0x8005
```

但当前实现按最低位优先处理，每次向右移动，因此使用其位反射形式：

```text
0xA001
```

可以简单记忆：

| 算法形式 | 移位方向 | 常见多项式写法 |
| --- | --- | --- |
| 非反射实现 | 左移 | `0x8005` |
| 反射实现 | 右移 | `0xA001` |

不能在当前右移代码中直接把 `0xA001` 换成 `0x8005`，否则计算结果会改变。

## 7. 一帧数据如何计算 CRC

正式协议使用：

```c
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
```

执行顺序是：

1. 将 CRC 初始化为 `0xFFFF`。
2. 计算 `SEQ`。
3. 计算 `LEN`。
4. 计算 `CMD`。
5. 按顺序计算全部 `DATA`。
6. 返回 16 位结果。

每个字段都继续使用前一个字段计算后的 CRC，不能给每个字段重新设置 `0xFFFF`。

## 8. 发送端怎么加入 CRC

发送端先发送帧头和业务字段：

```c
Protocol_SendByte(PROTO_HEAD_1);
Protocol_SendByte(PROTO_HEAD_2);
Protocol_SendByte(frame.seq);
Protocol_SendByte(frame.len);
Protocol_SendByte(frame.cmd);
```

然后发送 `DATA`，最后计算并发送 CRC：

```c
crc = Protocol_CalcFrameCrc16(&frame);
Protocol_SendByte((uint8_t)(crc & 0xFF));
Protocol_SendByte((uint8_t)(crc >> 8));
```

拆分方法：

```text
crc & 0xFF  -> 取低字节 CRC_LO
crc >> 8    -> 取高字节 CRC_HI
```

假设：

```text
crc = 0xC0E1
```

则：

```text
crc & 0xFF = 0xE1
crc >> 8   = 0xC0
```

发送结果：

```text
E1 C0
```

## 9. 接收端怎么验证 CRC

状态机增加了两个状态：

```c
PROTO_RX_WAIT_CRC_LO
PROTO_RX_WAIT_CRC_HI
```

收到低字节时先保存：

```c
case PROTO_RX_WAIT_CRC_LO:
    s_rx_crc_low = byte;
    s_rx_state = PROTO_RX_WAIT_CRC_HI;
    return 1;
```

收到高字节后重新组成 16 位数：

```c
received_crc = (uint16_t)s_rx_crc_low | ((uint16_t)byte << 8);
```

例如串口收到：

```text
CRC_LO = E1
CRC_HI = C0
```

组合过程：

```text
00E1 OR C000 = C0E1
```

然后接收端对已经保存的 `SEQ + LEN + CMD + DATA` 重新计算：

```c
if (received_crc == Protocol_CalcFrameCrc16(&s_rx_frame))
{
    Protocol_PushFrameFromIrq();
}
else
{
    s_rx_error_count++;
}
```

只有两个 CRC 相等，帧才会进入命令处理流程。校验失败的帧会被丢弃。

## 10. Python 上位机为什么能和 STM32 通信

Python 使用相同参数和相同字节顺序：

```python
def calc_crc16(seq: int, cmd: int, data: bytes) -> int:
    crc = 0xFFFF

    for byte in bytes([seq, len(data), cmd]) + data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1

    return crc
```

组帧时同样低字节在前：

```python
crc_bytes = bytes([crc & 0xFF, crc >> 8])
```

STM32 和 Python 必须同时满足以下条件：

- 初值都是 `0xFFFF`
- 多项式都是 `0xA001`
- 都采用右移算法
- 覆盖字段都是 `SEQ + LEN + CMD + DATA`
- 字段计算顺序一致
- 都发送 `CRC_LO CRC_HI`

只要其中一项不同，就会出现 CRC 校验失败。

## 11. CRC16 和 checksum 的区别

之前的 checksum 是简单累加：

```text
CHECKSUM = SEQ + LEN + CMD + DATA 的低 8 位
```

它的优点：

- 实现简单
- 计算快
- 只占 1 字节

它的缺点：

- 某些错误会互相抵消
- 字节交换后结果可能不变
- 对连续多位错误的检测能力较弱

例如两个字节：

```text
01 02
```

交换为：

```text
02 01
```

简单累加结果都等于 `0x03`，无法发现顺序错误。CRC 会受到处理顺序影响，更可能发现此类问题。

CRC16 的代价是：

- 帧尾由 1 字节增加到 2 字节
- 逐位实现需要更多计算
- 通信双方必须明确具体 CRC 参数

对于当前 STM32F103 和最大 16 字节 DATA，这点计算开销很小。

## 12. 为什么没有使用 STM32F103 硬件 CRC

STM32F103 自带 CRC 外设，但它主要提供固定的 32 位 CRC 计算，不是当前协议需要的 CRC-16/MODBUS。

当前采用纯软件实现，优点是：

- 算法参数明确
- MCU 和 Python 容易保持一致
- 不依赖硬件 CRC 外设配置
- 方便移植到其他单片机
- 适合作为 CRC 学习代码

工程中虽然包含 `stm32f10x_crc.c`，但当前 UART CRC16 没有调用它。

## 13. 常见错误

### 13.1 把 CRC16 当成一种固定算法

错误想法：

```text
只要都叫 CRC16，结果就应该一样。
```

实际必须同时确认：

- 多项式
- 初值
- 输入是否反射
- 输出是否反射
- 结果异或值
- 数据覆盖范围
- CRC 字节发送顺序

### 13.2 把 AA 55 加入计算

当前协议不计算帧头。如果上位机把 `AA 55` 加入 CRC，而 STM32 不加入，结果一定不同。

### 13.3 高低字节顺序写反

计算结果：

```text
0xC0E1
```

当前线上顺序：

```text
E1 C0
```

如果发送成 `C0 E1`，接收端组合后会得到 `0xE1C0`，校验失败。

### 13.4 忘记计算 LEN

当前 CRC 覆盖 `LEN`。只计算 `SEQ + CMD + DATA` 会得到不同结果。

### 13.5 发送端和接收端字段顺序不同

CRC 与输入顺序有关：

```text
SEQ LEN CMD DATA
```

不能改成：

```text
LEN SEQ CMD DATA
```

### 13.6 每处理一个字段都重新初始化

一整帧只能在开始时设置一次：

```c
crc = 0xFFFF;
```

后续字段必须连续更新同一个 CRC。

## 14. 如何验证实现正确

### 14.1 标准测试向量

CRC-16/MODBUS 的常用标准测试：

```text
输入 ASCII：123456789
预期 CRC：  0x4B37
线上字节：  37 4B
```

如果算不出 `0x4B37`，说明算法参数或实现有误。

### 14.2 当前协议测试向量

| 用途 | CRC 输入 | CRC 数值 | 线上 CRC |
| --- | --- | --- | --- |
| PING | `01 00 01` | `0xC0E1` | `E1 C0` |
| LED RED | `03 01 02 01` | `0x0091` | `91 00` |
| STATUS | `07 00 03` | `0x0080` | `80 00` |

对应完整帧：

```text
PING:     AA 55 01 00 01 E1 C0
LED RED:  AA 55 03 01 02 01 91 00
STATUS:   AA 55 07 00 03 80 00
```

### 14.3 故意破坏一个字节

正确帧：

```text
AA 55 03 01 02 01 91 00
```

把 DATA 从 `01` 改成 `02`，但不修改 CRC：

```text
AA 55 03 01 02 02 91 00
```

STM32 重新计算出的 CRC 不再是 `0x0091`，因此会丢弃该帧并增加接收错误计数。

## 15. 建议学习顺序

1. 先记住当前 CRC 参数和覆盖范围。
2. 手工拆解 PING 帧中的 CRC 高低字节。
3. 阅读 `Protocol_UpdateCrc16()`，理解一个字节为什么循环 8 次。
4. 阅读 `Protocol_CalcFrameCrc16()`，理解整帧如何连续计算。
5. 阅读 `Protocol_SendFrame()`，理解 CRC 如何加入帧尾。
6. 阅读接收状态机的 `WAIT_CRC_LO` 和 `WAIT_CRC_HI`。
7. 对照 Python 代码，确认两端算法完全一致。
8. 修改一个测试帧字节，观察错误帧被丢弃。

## 16. 最终记忆卡片

```text
算法：CRC-16/MODBUS
初值：0xFFFF
右移多项式：0xA001
结果异或：0x0000
覆盖：SEQ + LEN + CMD + DATA
不覆盖：AA 55 和 CRC 自身
发送：CRC_LO 在前，CRC_HI 在后
标准向量："123456789" -> 0x4B37
PING：01 00 01 -> CRC 0xC0E1 -> 发送 E1 C0
```
