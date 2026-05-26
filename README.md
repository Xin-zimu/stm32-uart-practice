# STM32F103 + OLED + 5D 摇杆交互界面

本工程实现了一个基于 STM32F103 的小型 OLED 交互界面，使用 5D 摇杆进行菜单控制，并显示光敏、温度和交通灯状态。

当前功能：

- 128x64 OLED 显示界面
- 5D 摇杆菜单控制
- 光敏传感器 AO/DO 读取
- DS18B20 温度读取
- 红黄绿交通灯控制
- 非阻塞任务轮询
- OLED 分区/按行刷新

## 引脚接线表

当前代码推荐接线如下：

| 模块 | 信号 | 接 STM32F103 引脚 | 对应代码 |
| --- | --- | --- | --- |
| OLED | SCL | PB6 / I2C1_SCL | `User/oled.c` |
| OLED | SDA | PB7 / I2C1_SDA | `User/oled.c` |
| OLED | VCC | 3.3V | - |
| OLED | GND | GND | - |
| 光敏传感器 | AO | PA0 | `User/light_sensor.c` |
| 光敏传感器 | DO | PA1 | `User/light_sensor.c` |
| 光敏传感器 | VCC | 3.3V | - |
| 光敏传感器 | GND | GND | - |
| 5D 摇杆 | UP/上 | PA2 | `User/joystick.h` |
| 5D 摇杆 | DOWN/下 | PA3 | `User/joystick.h` |
| 5D 摇杆 | LEFT/左 | PA4 | `User/joystick.h` |
| 5D 摇杆 | RIGHT/右 | PB5 | `User/joystick.h` |
| 5D 摇杆 | PRESS/SW/按压 | PB1 | `User/joystick.h` |
| 5D 摇杆 | COM | GND | 公共端 |
| 5D 摇杆 | SET | 不接 | 当前未使用 |
| 5D 摇杆 | RST | 不接 | 当前未使用 |
| DS18B20 | DQ | PB0 | `User/ds18b20.c` |
| DS18B20 | VCC | 3.3V | - |
| DS18B20 | GND | GND | - |
| 交通灯 LED | 红灯 | PA5 | `User/led.c` |
| 交通灯 LED | 黄灯 | PA6 | `User/led.c` |
| 交通灯 LED | 绿灯 | PA7 | `User/led.c` |

注意：

- `PA0` 已经给光敏 AO 使用。
- `PA1` 已经给光敏 DO 使用。
- `PB0` 已经给 DS18B20 使用。
- 所以 5D 摇杆默认没有使用 `PA0`、`PA1`、`PB0`，避免冲突。

## 5D 摇杆说明

你的摇杆引脚为 `COM`、`UP`、`DOWN`、`LFT`、`RHT`、`MID`、`SET`、`RST`。当前代码只使用方向键和中键，接线如下：

| 摇杆引脚 | 接 STM32F103 |
| --- | --- |
| COM | GND |
| UP | PA2 |
| DOWN | PA3 |
| LFT | PA4 |
| RHT | PB5 |
| MID | PB1 |
| SET | 不接 |
| RST | 不接 |

当前摇杆输入使用上拉输入模式：

- 未按下：高电平
- 按下：低电平

如果你的摇杆模块输出逻辑相反，需要修改 `User/joystick.c` 里的按键读取判断。

默认按键映射：

| 操作 | 引脚 |
| --- | --- |
| 上 | PA2 |
| 下 | PA3 |
| 左 | PA4 |
| 右 | PB5 |
| 按压 | PB1 |

## OLED 界面

### 主菜单

菜单项：

- `STATUS`
- `SET TH`
- `TRAFFIC`

操作：

- 上/下：移动光标
- 按压：进入选中的页面

### 状态页

显示内容：

- 光照状态：`BRIGHT` 或 `DARK`
- 光敏 AO 数值
- DS18B20 温度

操作：

- 按压或左：返回主菜单

### 阈值设置页

显示内容：

- 当前 AO 数值
- 当前光敏阈值

操作：

- 左：阈值减少 50
- 右：阈值增加 50
- 按压：保存并返回主菜单

阈值限制在 `User/app_light.c` 中：

- 最小值：300
- 最大值：3800

### 交通灯控制页

模式：

- `AUTO`：自动模式，随光敏状态控制红/绿灯
- `RED`：红灯
- `YELLOW`：黄灯
- `GREEN`：绿灯
- `OFF`：全灭

操作：

- 上/下：移动光标
- 按压：应用当前模式
- 左：返回主菜单

## 主循环任务

主循环位于 `User/main.c`：

```c
while (1)
{
    App_Light_Task();
    App_Temp_Task();
    Joystick_Task();
    App_UI_Task();
}
```

任务说明：

- `App_Light_Task()`：读取光敏 AO，并判断亮/暗状态。
- `App_Temp_Task()`：非阻塞读取 DS18B20 温度。
- `Joystick_Task()`：扫描 5D 摇杆，并做去抖处理。
- `App_UI_Task()`：处理菜单逻辑和 OLED 刷新。

## OLED 刷新策略

OLED 当前使用硬件 I2C1：

- SCL：PB6 / I2C1_SCL
- SDA：PB7 / I2C1_SDA
- I2C 地址：`0x78`，对应常见 OLED 7 位地址 `0x3C`
- I2C 速度：`400kHz`

刷新策略：

- 状态数据：500 ms 刷新一次
- 光标闪烁：500 ms 翻转一次
- 光标移动：只刷新菜单行
- 页面切换：清屏后重绘

`OLED_ClearPage()` 用于按页清除，减少整屏刷新次数。

如果 OLED 不显示，优先检查：

- OLED 的 `SCL` 是否接到 `PB6`
- OLED 的 `SDA` 是否接到 `PB7`
- OLED 模块是否支持 `0x3C` 地址
- I2C 是否有上拉电阻，常见为 `4.7k` 上拉到 `3.3V`
- 如果 400kHz 不稳定，可把 `User/oled.c` 里的 `OLED_I2C_SPEED` 改成 `100000`

## 修改引脚的位置

如果你的实际接线不同，修改这些文件：

| 外设 | 修改文件 |
| --- | --- |
| OLED | `User/oled.c` |
| 光敏传感器 | `User/light_sensor.c` |
| DS18B20 | `User/ds18b20.c` |
| 交通灯 LED | `User/led.c` |
| 5D 摇杆 | `User/joystick.h` |

改引脚后要检查是否和已有外设冲突。

## Keil 工程

Keil 工程文件：

```text
Project/led.uvprojx
```

新增 UI 文件已经加入工程：

- `User/app_ui.c`
- `User/app_ui.h`
- `User/joystick.c`
- `User/joystick.h`

## 串口通信学习计划

建议按 5 个阶段练习，每个阶段都用串口助手验证一次。

### 第 1 阶段：认识 USB 转 TTL 和 USART

目标：

- 分清 USB、TTL、USART/UART 的关系。
- 会交叉接线：USB-TTL 的 `TXD` 接 STM32 `PA10/RX`，USB-TTL 的 `RXD` 接 STM32 `PA9/TX`，`GND` 必须共地。
- 使用 `3.3V TTL` 电平，不要把 5V TTL 信号直接接到 STM32 IO。
- 理解串口参数：波特率、数据位、停止位、校验位。

本工程默认参数：

```text
USART1
TX: PA9
RX: PA10
波特率: 115200
数据格式: 8N1，即 8 数据位、无校验、1 停止位
```

### 第 2 阶段：最小收发

目标：

- 单片机上电后主动发送启动信息。
- 串口助手发送 `PING`，单片机回复 `PONG`。
- 串口助手发送 `ECHO hello`，单片机回复 `hello`。

重点理解：

- `printf` 重定向到串口发送。
- 接收中断负责收字节。
- 主循环负责处理完整一行命令。

### 第 3 阶段：命令解析

目标：

- 会用一行文本命令控制硬件。
- 会判断正确命令和错误命令。

可练习命令：

```text
HELP
STATUS
LED RED
LED YELLOW
LED GREEN
LED OFF
LED AUTO
TH?
TH +
TH -
TH 2000
```

### 第 4 阶段：和传感器联动

目标：

- 用 `STATUS` 查看光敏 AO、亮暗状态、阈值、温度、LED 模式。
- 用 `TH +`、`TH -`、`TH 2000` 修改光敏阈值。
- 用 `LED AUTO` 恢复光敏自动控制交通灯。

重点理解：

- 串口不仅能打印调试信息，也能作为人机交互入口。
- 串口命令可以修改程序运行参数。

### 第 5 阶段：扩展练习

可以继续加这些命令：

- `TEMP?`：只返回温度。
- `LIGHT?`：只返回光敏 AO 和亮暗状态。
- `BAUD?`：返回当前波特率。
- `MODE DEBUG ON/OFF`：控制是否周期性打印状态。
- `SAVE`：把阈值保存到 Flash。

## 本工程串口练习程序

新增文件：

- `User/app_uart_practice.c`
- `User/app_uart_practice.h`

已修改文件：

- `User/main.c`：初始化 `USART1`，并在主循环里处理串口命令。
- `SYSTEM/usart/usart.c`：串口接收支持 `\r` 或 `\n` 作为一行结束，适配常见串口助手。
- `SYSTEM/usart/usart.h`：接收缓存变量声明为 `volatile`，适合中断和主循环共同访问。
- `Project/led.uvprojx`：新文件已加入 Keil 工程。

串口助手建议设置：

```text
波特率: 115200
数据位: 8
校验位: None
停止位: 1
发送格式: ASCII / 文本
换行: CRLF、CR 或 LF 均可
```

上电后串口助手应看到：

```text
STM32F103 UART practice ready.
USART1: PA9=TX, PA10=RX, 115200 8N1.
Send HELP for commands.
```

如果没有收到信息，按顺序检查：

- USB-TTL 的 `TXD/RXD` 是否交叉连接。
- `GND` 是否共地。
- 串口助手是否选对 COM 口。
- 波特率是否是 `115200`。
- USB-TTL 是否为 `3.3V TTL` 电平。
