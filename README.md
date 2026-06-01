# STM32F103 串口通信练习工程

这是一个以 **USART1 串口通信练习** 为主线的 STM32F103 工程。OLED、摇杆、光敏传感器、DS18B20 温度传感器和交通灯 LED 都是串口练习的控制对象和状态来源；项目重点不是单独点亮某个外设，而是练习如何通过串口完成命令解析、协议封包、状态查询、错误处理和上位机交互。

工程同时保留两种串口通信方式：

- 文本命令行：适合用串口助手直接输入 `HELP`、`STATUS`、`LED RED`、`TH 2000` 等命令。
- 二进制协议帧：适合练习真实设备通信中的帧头、长度、命令字、数据区、校验、ACK、错误码、序列号和超时重试。

## 串口练习重点

本项目围绕 USART1 练习了这些内容：

| 练习点 | 对应文件 | 说明 |
| --- | --- | --- |
| USART1 初始化 | `SYSTEM/usart/usart.c` | PA9 为 TX，PA10 为 RX，115200 8N1，开启接收中断 |
| `printf` 重定向 | `SYSTEM/usart/usart.c` | 将 `printf` 输出重定向到 USART1，方便调试 |
| 中断接收字节 | `SYSTEM/usart/usart.c` | 每收到 1 字节进入 `USART1_IRQHandler` |
| 文本行接收 | `SYSTEM/usart/usart.c`、`User/app_uart_practice.c` | 以 CR/LF 作为一行结束，主循环解析完整命令 |
| 文本命令解析 | `User/app_uart_practice.c` | 支持大小写兼容、参数解析、命令表分发 |
| 二进制协议状态机 | `User/app_protocol_practice.c` | 按 `AA 55 SEQ LEN CMD DATA CHECKSUM` 逐字节解析 |
| 校验和 | `User/app_protocol_practice.c` | 使用 8 位累加和校验帧内容 |
| ACK 和错误码 | `User/app_protocol_practice.c` | 每条命令返回 ACK，携带原命令和执行结果 |
| SEQ 序列号 | `User/app_protocol_practice.c`、`Tools/uart_protocol_host.py` | 上位机发起 SEQ，STM32 原样带回，防止响应错配 |
| 接收超时 | `User/app_protocol_practice.c` | 半包超过 50ms 未完成时丢弃并复位状态机 |
| 上位机重试 | `Tools/uart_protocol_host.py` | 超时后使用同一 SEQ 重发请求 |
| 协议测试 | `练习/protocol_test_host.c` | 在 PC 上模拟 MCU/主机通信，验证状态机、校验、重试和 SEQ 匹配 |

## 串口硬件连接

串口参数：

```text
USART1
TX: PA9
RX: PA10
波特率: 115200
数据格式: 8N1
```

USB-TTL 连接方式：

| USB-TTL | STM32F103 |
| --- | --- |
| TXD | PA10 / USART1_RX |
| RXD | PA9 / USART1_TX |
| GND | GND |

注意：

- USB-TTL 必须使用 3.3V TTL 电平。
- TXD/RXD 要交叉连接。
- STM32 和 USB-TTL 必须共地。

## 文本串口命令

文本命令由 `User/app_uart_practice.c` 处理。串口助手可以用普通文本发送，换行可以是 CRLF、CR 或 LF。

上电后会输出：

```text
STM32F103 UART practice ready.
USART1: PA9=TX, PA10=RX, 115200 8N1.
Send HELP for commands.
Binary protocol practice ready. See protocol_practice_protocol.md
```

支持命令：

| 命令 | 说明 |
| --- | --- |
| `HELP` | 显示帮助 |
| `PING` | 回复 `PONG` |
| `ECHO text` | 回显文本 |
| `STATUS` | 输出 AO、亮暗状态、阈值、LED 模式和温度 |
| `LED AUTO` | 交通灯恢复光照自动控制 |
| `LED RED` | 红灯模式 |
| `LED YELLOW` | 黄灯模式 |
| `LED GREEN` | 绿灯模式 |
| `LED OFF` | 关闭交通灯 |
| `TH?` | 查询光照阈值 |
| `TH +` | 阈值增加 50 |
| `TH -` | 阈值减少 50 |
| `TH 2000` | 设置阈值为 2000 |

文本命令的作用是练习“人能读懂”的串口调试接口，适合初期调试和快速控制外设。

## 二进制协议练习

二进制协议由 `User/app_protocol_practice.c` 处理。串口助手需要使用 HEX 发送，或者使用 `Tools/uart_protocol_host.py`。

最终帧格式：

```text
AA 55 SEQ LEN CMD DATA CHECKSUM
```

字段含义：

| 字段 | 说明 |
| --- | --- |
| `AA 55` | 固定帧头 |
| `SEQ` | 序列号，由上位机生成，STM32 响应时原样带回 |
| `LEN` | DATA 长度，不包含 CMD |
| `CMD` | 命令字 |
| `DATA` | 参数区，可为空 |
| `CHECKSUM` | `SEQ + LEN + CMD + DATA` 的低 8 位 |

命令字：

| CMD | 名称 | DATA |
| --- | --- | --- |
| `0x01` | PING | 无 |
| `0x02` | LED 控制 | `00=OFF`，`01=RED`，`02=YELLOW`，`03=GREEN`，`04=AUTO` |
| `0x03` | 读取状态 | 无 |
| `0x04` | 蜂鸣器控制 | `00=OFF`，`01=ON`，当前工程用软件状态模拟 |
| `0x80` | ACK | `原命令 + 错误码` |
| `0x83` | 状态响应 | `TRAFFIC_MODE IS_DARK AO_H AO_L BUZZER` |

常用 HEX 示例：

```text
PING:      AA 55 01 00 01 02
LED OFF:   AA 55 02 01 02 00 05
LED RED:   AA 55 03 01 02 01 07
LED AUTO:  AA 55 06 01 02 04 0D
STATUS:    AA 55 07 00 03 0A
BUZZER ON: AA 55 09 01 04 01 0F
```

ACK 示例：

```text
PING 成功:
AA 55 01 02 80 01 00 84

LED 红灯成功:
AA 55 03 02 80 02 00 87
```

错误码：

| 错误码 | 含义 |
| --- | --- |
| `0x00` | OK |
| `0x01` | 长度错误 |
| `0x02` | 参数错误 |
| `0x03` | 未知命令 |
| `0x04` | 接收忙 |

更完整的协议说明见：

- `protocol_practice_protocol.md`
- `练习/protocol_document.md`

## 串口接收流程

USART1 中断里每次只处理一个字节：

```c
Res = USART_ReceiveData(USART1);
if (App_ProtocolPractice_ReceiveByte(Res) == 0)
{
    /* 不是二进制协议帧时，再进入文本行接收逻辑 */
}
```

设计思路：

1. 如果字节属于 `AA 55 ...` 二进制协议帧，交给协议状态机。
2. 如果不是协议帧，继续按普通文本行收进 `USART_RX_BUF`。
3. 文本命令不在中断里执行，主循环中的 `App_UARTPractice_Task()` 处理完整一行。
4. 二进制协议命令也不在中断里执行，中断只把完整帧放到 pending 区，主循环中的 `App_ProtocolPractice_Task()` 再执行命令。

这样做可以避免在中断里执行复杂业务逻辑，也方便同时练习文本串口和二进制协议。

## 上位机脚本

`Tools/uart_protocol_host.py` 是二进制协议的上位机测试脚本，负责生成帧、分配 SEQ、等待 ACK、按 SEQ 匹配响应和超时重试。

安装依赖：

```powershell
pip install pyserial
```

示例：

```powershell
python Tools/uart_protocol_host.py COM3 ping
python Tools/uart_protocol_host.py COM3 led --value 1
python Tools/uart_protocol_host.py COM3 led --value 4
python Tools/uart_protocol_host.py COM3 status
python Tools/uart_protocol_host.py COM3 buzzer --value 1
```

参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--baud` | `115200` | 串口波特率 |
| `--timeout` | `0.2` | 等待响应超时时间，单位秒 |
| `--retries` | `2` | 超时后的最大重试次数 |
| `--value` | `0` | LED 或 buzzer 命令参数 |

## 可靠通信练习

本项目已经接入四个可靠通信点：

1. MCU 接收超时：半包超过 `PROTO_RX_TIMEOUT_MS = 50ms` 后丢弃。
2. SEQ 序列号：上位机生成，STM32 响应带回。
3. 上位机超时重试：重试时复用同一个 SEQ。
4. 响应按 SEQ 匹配：SEQ 不一致的响应会被忽略。

这些内容对应真实串口通信中常见的问题：丢字节、半包、粘包、旧响应晚到、重复发送和主从同步。

## PC 端协议测试

`练习/protocol_test_host.c` 可以在电脑上编译运行，不需要 STM32 板子。它通过假的 `Uart_SendByte()` 和 `Led_SetState()` 捕获输出，验证协议练习代码的行为。

编译运行：

```powershell
gcc 练习\protocol_test_host.c -o 练习\output\protocol_test_host.exe
练习\output\protocol_test_host.exe
```

测试覆盖：

- 正确帧解析和响应
- 校验错误丢弃
- `AA AA 55` 重同步
- LED 参数错误
- PING 响应
- 未知命令错误响应
- 半包超时复位
- 迟到字节不拼成旧帧
- 上位机按 SEQ 匹配响应
- 超时后使用同一 SEQ 重试

期望输出结尾：

```text
ALL TESTS PASSED
```

## 其他外设功能

虽然项目主线是串口通信，但外设提供了真实控制对象和状态数据。

| 模块 | 代码位置 | 作用 |
| --- | --- | --- |
| OLED | `User/oled.c`、`User/app_ui.c` | 显示菜单、状态、阈值和交通灯模式 |
| 5D 摇杆 | `User/joystick.c` | 操作 OLED 菜单 |
| 光敏传感器 | `User/light_sensor.c`、`User/app_light.c` | 读取 AO，判断亮/暗，自动控制交通灯 |
| DS18B20 | `User/ds18b20.c`、`User/app_temp.c` | 非阻塞温度采样 |
| 交通灯 LED | `User/led.c`、`User/app_light.c` | 被 UI、文本串口和二进制协议共同控制 |

硬件接线：

| 模块 | 信号 | STM32F103 引脚 |
| --- | --- | --- |
| OLED | SCL | PB6 / I2C1_SCL |
| OLED | SDA | PB7 / I2C1_SDA |
| 光敏传感器 | AO | PA0 |
| 光敏传感器 | DO | PA1 |
| DS18B20 | DQ | PB0 |
| 5D 摇杆 | UP | PA2 |
| 5D 摇杆 | DOWN | PA3 |
| 5D 摇杆 | LEFT | PA4 |
| 5D 摇杆 | RIGHT | PB5 |
| 5D 摇杆 | PRESS / MID | PB1 |
| 交通灯 LED | 红灯 | PA5 |
| 交通灯 LED | 黄灯 | PA6 |
| 交通灯 LED | 绿灯 | PA7 |

## 主循环结构

主循环位于 `User/main.c`：

```c
while (1)
{
    App_Light_Task();
    App_Temp_Task();
    Joystick_Task();
    App_UI_Task();
    App_UARTPractice_Task();
    App_ProtocolPractice_Task();
}
```

这是一个非阻塞任务轮询结构。串口中断只负责收字节，真正的命令执行和界面刷新都放在主循环任务中。

## 软件结构

```text
SYSTEM/usart/
  usart.c/.h              USART1 初始化、printf 重定向、接收中断

Tools/
  uart_protocol_host.py   二进制协议上位机脚本

User/
  main.c                  初始化和主循环
  app_uart_practice.c/.h  文本串口命令练习
  app_protocol_practice.c/.h 二进制协议练习
  app_light.c/.h          光照状态、阈值、交通灯模式
  app_temp.c/.h           DS18B20 温度采样状态机
  app_ui.c/.h             OLED 菜单和刷新逻辑
  app_light_control.c/.h  旧接口兼容转发
  led.c/.h                交通灯 LED 底层驱动
  oled.c/.h               OLED 底层显示接口
  joystick.c/.h           5D 摇杆扫描和去抖
  light_sensor.c/.h       光敏传感器
  ds18b20.c/.h            DS18B20 单总线驱动

练习/
  practice                简化版协议练习代码
  protocol_test_host.c    PC 端协议测试程序
  protocol_document.md    协议学习文档
```

## Keil 工程

Keil 工程文件：

```text
Project/led.uvprojx
```

确认这些文件已经加入编译：

- `SYSTEM/usart/usart.c`
- `User/app_uart_practice.c`
- `User/app_protocol_practice.c`
- `User/app_light.c`
- `User/app_temp.c`
- `User/app_ui.c`
- `User/app_light_control.c`
- `User/led.c`
- `User/oled.c`
- `User/joystick.c`
- `User/light_sensor.c`
- `User/ds18b20.c`

## 常见问题

### 串口没有输出

- 检查 USB-TTL 的 TXD/RXD 是否交叉连接。
- 检查 GND 是否共地。
- 检查串口助手是否选择正确 COM 口。
- 检查波特率是否为 115200。
- 检查 USB-TTL 是否为 3.3V TTL 电平。

### 文本命令没有响应

- 发送文本命令时需要带换行。
- 可以先发送 `HELP` 或 `PING`。
- 如果串口助手处于 HEX 模式，文本命令会被当作 ASCII 字节发送，显示上可能不直观。

### 二进制协议没有响应

- 串口助手必须使用 HEX 发送。
- 检查帧头是否为 `AA 55`。
- 检查 `LEN` 是否等于 DATA 长度。
- 检查 `CHECKSUM` 是否正确。
- 如果发送半包后停顿超过 50ms，MCU 会主动丢弃这帧。

### 交通灯被 UI 或串口改乱

UI、文本串口和二进制协议最终都通过 `app_light` 修改交通灯模式。发送：

```text
LED AUTO
```

或二进制帧：

```text
AA 55 06 01 02 04 0D
```

即可恢复光照自动控制。

## 后续练习方向

- 把二进制协议校验从 8 位 checksum 升级为 CRC16。
- 给协议加入设备地址，练习一主多从通信。
- 增加 `TEMP?`、`LIGHT?` 等文本命令。
- 将光照阈值保存到 Flash。
- 增加周期性串口上报，例如每秒输出一次状态帧。
- 给上位机脚本增加持续监控模式和日志保存。
- 把协议测试继续扩展到更多异常帧和边界长度。
