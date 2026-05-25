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
