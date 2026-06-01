# 二进制协议设计练习

代码文件：

- `User/app_protocol_practice.c`
- `User/app_protocol_practice.h`

串口参数：

```text
USART1, 115200, 8N1
串口助手必须使用 HEX 发送
```

## 第 1 次练习：最简单帧格式

最初学习时使用这个基础格式理解字段：

```text
AA 55 LEN CMD DATA CHECKSUM
```

字段：

```text
AA 55      帧头
LEN        DATA 长度
CMD        命令
DATA       参数，可以为空
CHECKSUM   LEN + CMD + DATA，只保留低 8 位
```

例子：

```text
PING：      AA 55 00 01 01
LED 开：    AA 55 01 02 01 04
LED 关：    AA 55 01 02 00 03
```

## 第 2 次练习：UART 接收状态机

状态机思路：

```text
等待 AA
等待 55
接收 SEQ
接收 LEN
接收 CMD
接收 DATA
接收 CHECKSUM
```

代码入口：

```c
void App_ProtocolPractice_ReceiveByte(uint8_t byte);
```

USART1 中断每收到 1 个字节，就调用一次这个函数。

## 第 3 次练习：加入 checksum 或 CRC16

当前代码实际使用 8 位累加和：

```text
CHECKSUM = SEQ + LEN + CMD + DATA
```

代码位置：

```c
static uint8_t Protocol_CalcChecksum(const ProtocolFrame *frame);
```

代码里也保留了 CRC16 计算函数，后续可以把帧尾从 1 字节 checksum 升级成 2 字节 CRC16：

```c
static uint16_t Protocol_CalcCrc16(const uint8_t *data, uint8_t len);
```

## 第 4 次练习：命令分发

命令表：

```text
01：PING
02：LED 控制
03：读取状态
04：蜂鸣器控制
80：ACK 应答
83：状态响应
```

代码位置：

```c
static void Protocol_HandleFrame(const ProtocolFrame *frame);
```

## 第 5 次练习：处理粘包、半包、错误帧

状态机天然支持：

```text
垃圾数据：一直等 AA
半包：状态停在当前字段，等待后续字节
粘包：一帧处理完后回到等待 AA，继续解析下一帧
长度错误：LEN > PROTO_MAX_DATA_LEN 时丢弃
校验错误：不进入命令分发
```

当前最大 DATA 长度：

```text
16 字节
```

## 第 6 次练习：加入 ACK、错误码、序列号

最终练习帧格式比第 1 次多了 `SEQ`：

```text
AA 55 SEQ LEN CMD DATA CHECKSUM
```

`SEQ` 是序列号。主机发送命令时自己递增，STM32 回复时带回同一个 `SEQ`。

SEQ 使用规则：

```text
上位机负责生成 SEQ。
STM32 不生成新的 SEQ，只把请求里的 SEQ 原样带回响应。
同一条命令超时重试时，必须继续使用同一个 SEQ。
只有发送新命令时，SEQ 才递增。
```

ACK 帧：

```text
AA 55 SEQ 02 80 ORIGIN_CMD ERR CHECKSUM
```

错误码：

```text
00：OK
01：长度错误
02：参数错误
03：未知命令
04：接收忙
```

## 第 7 次练习：协议文档

正式协议如下。

### 帧格式

```text
AA 55 SEQ LEN CMD DATA CHECKSUM
```

### 字段说明

```text
AA 55      固定帧头
SEQ        序列号，0x00~0xFF
LEN        DATA 长度，不包含帧头、SEQ、LEN、CMD、CHECKSUM
CMD        命令
DATA       参数
CHECKSUM   SEQ + LEN + CMD + DATA，只保留低 8 位
```

### 可发送命令

PING：

```text
AA 55 01 00 01 02
```

LED 关：

```text
AA 55 02 01 02 00 05
```

LED 红灯：

```text
AA 55 03 01 02 01 07
```

LED 黄灯：

```text
AA 55 04 01 02 02 09
```

LED 绿灯：

```text
AA 55 05 01 02 03 0B
```

LED 自动：

```text
AA 55 06 01 02 04 0D
```

读取状态：

```text
AA 55 07 00 03 0A
```

蜂鸣器关：

```text
AA 55 08 01 04 00 0D
```

蜂鸣器开：

```text
AA 55 09 01 04 01 0F
```

### 响应示例

PING 成功 ACK：

```text
AA 55 01 02 80 01 00 84
```

LED 红灯成功 ACK：

```text
AA 55 03 02 80 02 00 87
```

未知命令 ACK：

```text
AA 55 SEQ 02 80 ORIGIN_CMD 03 CHECKSUM
```

读取状态会先回复 ACK，再回复状态帧：

```text
AA 55 SEQ 05 83 TRAFFIC_MODE IS_DARK AO_H AO_L BUZZER CHECKSUM
```

## 第 8 次练习：可靠通信规则

本项目已经把下面四个可靠通信点接入工程：

```text
1. MCU 接收超时
2. SEQ 序号
3. 上位机超时重试
4. 响应按 SEQ 匹配
```

### MCU 接收超时

代码位置：

```text
User/app_protocol_practice.c
```

当前超时时间：

```text
PROTO_RX_TIMEOUT_MS = 50 ms
```

规则：

```text
如果 MCU 已经开始接收一帧，但超过 50 ms 没有收到后续字节，
就丢弃当前半包，并把状态机重置到等待 AA。
```

作用：

```text
防止丢字节后状态机长期卡在 WAIT_LEN、WAIT_DATA 或 WAIT_CHECKSUM。
```

相关统计：

```c
uint16_t App_ProtocolPractice_GetRxTimeoutCount(void);
```

### SEQ 序号

当前帧格式：

```text
AA 55 SEQ LEN CMD DATA CHECKSUM
```

SEQ 含义：

```text
SEQ 是上位机给每条请求分配的序号。
STM32 响应时必须带回相同 SEQ。
上位机用 SEQ 判断响应属于哪条请求。
```

示例：

```text
上位机发送：
AA 55 03 01 02 01 07

含义：
SEQ = 03
LEN = 01
CMD = 02
DATA = 01

STM32 回复：
AA 55 03 02 80 02 00 87

含义：
SEQ = 03
CMD = 80 ACK
DATA = 02 00，表示原命令 02 执行成功
```

### 上位机超时重试

参考脚本：

```text
Tools/uart_protocol_host.py
```

规则：

```text
1. 上位机发送请求并记录当前 SEQ。
2. 在超时时间内等待响应。
3. 如果没有收到相同 SEQ 的响应，就重发同一帧。
4. 重试时不能换 SEQ。
5. 超过最大重试次数后，认为通信失败。
```

默认参数：

```text
timeout = 0.2 秒
retries = 2
```

示例：

```powershell
python Tools/uart_protocol_host.py COM3 ping
python Tools/uart_protocol_host.py COM3 led --value 1
python Tools/uart_protocol_host.py COM3 status
python Tools/uart_protocol_host.py COM3 buzzer --value 1
```

如果电脑没有安装 pyserial：

```powershell
pip install pyserial
```

### 响应按 SEQ 匹配

上位机收到响应后，不只看 CMD 和 STATUS，还必须检查 SEQ。

规则：

```text
如果响应 SEQ != 当前等待的请求 SEQ：
    忽略该响应，继续等待。

如果响应 SEQ == 当前等待的请求 SEQ：
    接受该响应，读取状态码，结束等待。
```

为什么必须这样：

```text
串口通信可能出现延迟、重发、旧响应晚到。
如果不检查 SEQ，上位机可能把旧响应误认为当前命令的响应。
```

