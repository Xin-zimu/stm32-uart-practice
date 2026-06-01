# STM32F103 UART / OLED / 光照温度交互练习

这是一个基于 STM32F103 的综合练习工程，包含 OLED 菜单界面、5D 摇杆输入、光敏传感器、DS18B20 温度采集、红黄绿交通灯 LED，以及 USART1 文本命令控制。

工程重点不是单个外设点亮，而是把多个外设组织成清晰的应用层任务：

- `app_light` 负责光照状态判断、阈值管理和交通灯模式控制。
- `app_temp` 负责 DS18B20 非阻塞采样状态机。
- `app_ui` 负责 OLED 菜单、页面切换和局部刷新。
- `app_uart_practice` 负责串口命令解析。
- `app_protocol_practice` 负责二进制协议设计练习，包含帧格式、状态机、校验、ACK 和序列号。
- `led.c`、`oled.c`、`ds18b20.c`、`light_sensor.c` 等文件保持底层驱动职责。

## 当前功能

- 128x64 OLED 显示菜单和状态信息。
- 5D 摇杆控制菜单、阈值和交通灯模式。
- 光敏传感器 AO 采样，并带滞回判断亮/暗状态。
- DS18B20 温度非阻塞读取。
- 红黄绿交通灯支持自动、红、黄、绿、关闭模式。
- USART1 命令行交互，支持状态查询、LED 模式控制和阈值调整。
- USART1 二进制协议练习，支持 HEX 帧解析、ACK、错误码和状态响应。
- UI 基于数据版本号刷新，避免固定周期无效刷屏。
- 主循环采用非阻塞任务轮询。

## 硬件接线

| 模块 | 信号 | STM32F103 引脚 | 代码位置 |
| --- | --- | --- | --- |
| OLED | SCL | PB6 / I2C1_SCL | `User/oled.c` |
| OLED | SDA | PB7 / I2C1_SDA | `User/oled.c` |
| 光敏传感器 | AO | PA0 | `User/light_sensor.c` |
| 光敏传感器 | DO | PA1 | `User/light_sensor.c` |
| DS18B20 | DQ | PB0 | `User/ds18b20.c` |
| 5D 摇杆 | UP | PA2 | `User/joystick.h` |
| 5D 摇杆 | DOWN | PA3 | `User/joystick.h` |
| 5D 摇杆 | LEFT | PA4 | `User/joystick.h` |
| 5D 摇杆 | RIGHT | PB5 | `User/joystick.h` |
| 5D 摇杆 | PRESS / MID | PB1 | `User/joystick.h` |
| 交通灯 LED | 红灯 | PA5 | `User/led.c` |
| 交通灯 LED | 黄灯 | PA6 | `User/led.c` |
| 交通灯 LED | 绿灯 | PA7 | `User/led.c` |
| USART1 | TX | PA9 | `SYSTEM/usart/usart.c` |
| USART1 | RX | PA10 | `SYSTEM/usart/usart.c` |

电源和公共端：

- OLED、光敏模块、DS18B20、摇杆模块 VCC 接 3.3V。
- 所有模块 GND 共地。
- 5D 摇杆 `COM` 接 GND。
- 摇杆 `SET`、`RST` 当前未使用，可不接。
- USB-TTL 必须使用 3.3V TTL 电平，`TXD` 接 PA10，`RXD` 接 PA9，GND 共地。

## OLED 菜单

主菜单包含：

- `STATUS`
- `SET TH`
- `TRAFFIC`

操作方式：

| 页面 | 操作 |
| --- | --- |
| 主菜单 | 上/下移动光标，按压进入页面 |
| 状态页 | 显示亮暗、AO、温度；按压或左键返回 |
| 阈值页 | 左键阈值 -50，右键阈值 +50，按压返回 |
| 交通灯页 | 上/下选择模式，按压应用，左键返回 |

光照阈值限制在 `User/app_light.c` 中：

- 最小值：300
- 最大值：3800
- 步进值：50

## 交通灯模式

交通灯模式由 `app_light` 统一管理，UI 和串口都只调用应用层接口，不直接操作底层 LED 驱动。

可用模式：

| 模式 | 行为 |
| --- | --- |
| `AUTO` | 根据光照状态自动控制，暗时红灯，亮时绿灯 |
| `RED` | 常亮红灯 |
| `YELLOW` | 常亮黄灯 |
| `GREEN` | 常亮绿灯 |
| `OFF` | 全部关闭 |

核心接口：

```c
void App_Light_SetTrafficMode(AppLightTrafficMode mode);
AppLightTrafficMode App_Light_GetTrafficMode(void);
```

## 串口命令

串口参数：

```text
USART1
TX: PA9
RX: PA10
波特率: 115200
数据格式: 8N1
换行: CRLF、CR 或 LF 均可
```

上电后串口会输出：

```text
STM32F103 UART practice ready.
USART1: PA9=TX, PA10=RX, 115200 8N1.
Send HELP for commands.
```

支持命令：

| 命令 | 说明 |
| --- | --- |
| `HELP` | 显示命令帮助 |
| `PING` | 回复 `PONG` |
| `ECHO text` | 回显文本 |
| `STATUS` | 输出 AO、亮暗状态、阈值、LED 模式和温度 |
| `LED AUTO` | 交通灯恢复自动模式 |
| `LED RED` | 红灯模式 |
| `LED YELLOW` | 黄灯模式 |
| `LED GREEN` | 绿灯模式 |
| `LED OFF` | 关闭交通灯 |
| `TH?` | 查询光照阈值 |
| `TH +` | 阈值增加 50 |
| `TH -` | 阈值减少 50 |
| `TH 2000` | 设置阈值为 2000 |

## 主循环

主循环位于 `User/main.c`：

```c
while (1)
{
    App_Light_Task();
    App_Temp_Task();
    Joystick_Task();
    App_UI_Task();
    App_UARTPractice_Task();
}
```

任务说明：

| 任务 | 职责 |
| --- | --- |
| `App_Light_Task()` | 读取光敏 AO，判断亮/暗状态，自动模式下更新交通灯 |
| `App_Temp_Task()` | DS18B20 显式状态机采样 |
| `Joystick_Task()` | 扫描摇杆并生成按键事件 |
| `App_UI_Task()` | 处理菜单事件并按数据变化刷新 OLED |
| `App_UARTPractice_Task()` | 处理串口完整行命令 |

## 软件结构

```text
User/
  main.c                 启动初始化和主循环
  app_light.c/.h         光照状态、阈值、交通灯模式
  app_temp.c/.h          DS18B20 温度采样状态机
  app_ui.c/.h            OLED 菜单和刷新逻辑
  app_uart_practice.c/.h USART1 命令行
  app_protocol_practice.c/.h 二进制协议练习
  app_light_control.c/.h 旧接口兼容转发
  led.c/.h               交通灯 LED 底层驱动
  oled.c/.h              OLED 底层显示接口
  joystick.c/.h          5D 摇杆扫描和去抖
  light_sensor.c/.h      光敏传感器
  ds18b20.c/.h           DS18B20 单总线驱动
```

### 应用层状态

- `app_light` 使用 `AppLightState` 管理亮/暗/未知状态。
- `app_light` 使用 `AppLightTrafficMode` 管理交通灯模式。
- `app_temp` 使用 `AppTempState` 管理 `START / WAIT / READ / ERROR` 状态。
- `app_ui` 使用 `UiScreen` 和 `UiTrafficIndex` 管理页面和交通灯菜单项。

### UI 刷新策略

`app_light` 和 `app_temp` 提供 version 版本号：

```c
uint16_t App_Light_GetVersion(void);
uint16_t App_Temp_GetVersion(void);
```

`app_ui` 记录上一次看到的版本号，仅在光照、阈值、交通灯模式、温度有效性或温度值变化时刷新对应区域。光标闪烁仍按 500ms 翻转。

## Keil 工程

Keil 工程文件：

```text
Project/led.uvprojx
```

打开工程后确认这些应用层文件已经加入编译：

- `User/app_light.c`
- `User/app_temp.c`
- `User/app_ui.c`
- `User/app_uart_practice.c`
- `User/app_protocol_practice.c`
- `User/app_light_control.c`

如果修改了引脚，优先检查这些文件：

| 外设 | 修改文件 |
| --- | --- |
| OLED | `User/oled.c` |
| 光敏传感器 | `User/light_sensor.c` |
| DS18B20 | `User/ds18b20.c` |
| 交通灯 LED | `User/led.c` |
| 5D 摇杆 | `User/joystick.h` |
| USART1 | `SYSTEM/usart/usart.c` |

## 常见问题

### OLED 不显示

- 检查 SCL 是否接 PB6，SDA 是否接 PB7。
- 确认 OLED 地址是否为常见 7 位地址 `0x3C`，代码中 8 位写地址为 `0x78`。
- 检查 I2C 上拉电阻，常见为 4.7k 上拉到 3.3V。
- 如果 400kHz 不稳定，可把 `User/oled.c` 里的 `OLED_I2C_SPEED` 改成 `100000`。

### 串口没有输出

- USB-TTL 的 TXD/RXD 是否交叉连接。
- GND 是否共地。
- 串口助手是否选择正确 COM 口。
- 波特率是否为 115200。
- USB-TTL 是否为 3.3V TTL 电平。

### 交通灯被串口或 UI 改乱

当前设计中 UI 和 UART 都通过 `app_light` 修改交通灯模式。发送：

```text
LED AUTO
```

即可恢复自动光照控制。

## 后续练习方向

- 增加 `TEMP?`、`LIGHT?` 等单项查询命令。
- 将光照阈值保存到 Flash。
- 增加 DS18B20 掉线提示和错误重试次数。
- 增加周期性调试输出开关，例如 `DEBUG ON/OFF`。
- 将串口命令扩展为多级命令表。
- 按 `protocol_practice_protocol.md` 继续练习二进制协议：CRC16、重传、设备地址和更完整的响应帧。
