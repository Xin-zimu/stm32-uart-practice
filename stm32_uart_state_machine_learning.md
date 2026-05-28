# STM32 串口命令解析、状态机与 DS18B20 学习资料

> 适用对象：你当前的 STM32F103 项目。  
> 目标：不是让你一次性掌握所有“工程化写法”，而是把你现在看不懂的代码拆成能逐步理解的知识块。

---

## 0. 先给你一个定位

你现在不是“串口不会了”，而是遇到了这几类进阶内容叠在一起：

1. **模块架构**
   - `app_light` 统一管理光敏状态、阈值、交通灯模式。
   - `app_temp` 用状态机管理 DS18B20。
   - `app_uart_practice` 只负责解析串口命令，不直接操作硬件。
   - `app_ui` 通过 `version` 判断要不要刷新 OLED。

2. **非阻塞状态机**
   - DS18B20 转换温度要等待约 750ms。
   - 不能 `delay_ms(750)` 卡死主循环。
   - 所以用 `START / WAIT / READ / ERROR` 状态机。

3. **C 语言抽象**
   - `typedef`
   - `struct`
   - 函数指针
   - `const char *`
   - `const char **`
   - 表驱动
   - `sizeof(array) / sizeof(array[0])`

4. **命令解析器**
   - 原来是 `if-else`。
   - 优化后变成 `commands[]` 命令表 + `CommandMatch()` + `DispatchCommand()`。
   - 对初学者来说，这已经明显偏工程化。

所以这份资料会按“先低配理解，再看工程版”的方式整理。

---

# 第一部分：项目模块之间到底怎么分工

## 1.1 主循环不是多线程

你的主循环大概是：

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

它不是同时执行，而是一个个执行：

```text
光敏任务
  ↓
温度任务
  ↓
摇杆任务
  ↓
UI 任务
  ↓
串口任务
  ↓
下一轮循环
```

所以裸机程序的核心是：

```text
每个 Task 都不能长时间卡住
每个 Task 做一点事就返回
```

这就是为什么你要避免：

```c
delay_ms(750);
```

---

## 1.2 模块分工

| 模块 | 负责什么 | 不应该负责什么 |
|---|---|---|
| `app_light.c` | 光敏 AO、亮暗判断、阈值、交通灯模式 | 不解析串口命令 |
| `app_temp.c` | DS18B20 采样状态机、温度有效性、温度版本号 | 不直接刷新 OLED |
| `app_uart_practice.c` | 串口命令解析 | 不直接点亮 GPIO 灯 |
| `app_ui.c` | OLED 显示和摇杆交互 | 不直接控制底层传感器 |
| `ds18b20.c` | 1-Wire 底层通信 | 不决定多久采一次温度 |
| `led.c` | 真实 GPIO 控制 | 不判断自动/手动模式 |

你现在要形成一个概念：

```text
底层驱动负责“怎么做”
应用模块负责“什么时候做、为什么做”
UI / UART 负责“用户怎么控制”
```

---

# 第二部分：`app_light` 为什么能避免冲突

## 2.1 冲突问题是什么

你的系统有多个入口都可能影响交通灯：

```text
光敏自动判断
串口命令 LED RED / LED GREEN
OLED + 摇杆选择交通灯模式
```

如果每个模块都直接调用：

```c
Traffic_RedOn();
Traffic_GreenOn();
Traffic_AllOff();
```

就会冲突。

例如：

```text
串口刚设置红灯
下一轮光敏又判断为亮，改成绿灯
UI 又选择 OFF
```

这样系统会乱。

---

## 2.2 真正避免冲突的是 `s_traffic_mode`

`app_light.c` 里有一个核心变量：

```c
static AppLightTrafficMode s_traffic_mode = APP_LIGHT_TRAFFIC_AUTO;
```

它的作用是：

```text
决定交通灯现在听谁的
```

逻辑是：

```text
如果 s_traffic_mode == AUTO
    根据光敏状态控制：
        DARK   → 红灯
        BRIGHT → 绿灯

如果 s_traffic_mode == RED
    强制红灯

如果 s_traffic_mode == YELLOW
    强制黄灯

如果 s_traffic_mode == GREEN
    强制绿灯

如果 s_traffic_mode == OFF
    全灭
```

所以冲突被这个模式变量挡住了。

---

## 2.3 `s_light_state` 和 `s_traffic_mode` 不一样

这两个变量很容易混。

| 变量 | 含义 |
|---|---|
| `s_light_state` | 当前环境是亮还是暗 |
| `s_traffic_mode` | 交通灯现在听自动控制还是手动控制 |

例如：

```text
s_light_state = DARK
s_traffic_mode = RED
```

含义是：

```text
环境现在是暗的
但交通灯处于手动红灯模式
```

再比如：

```text
s_light_state = BRIGHT
s_traffic_mode = AUTO
```

含义是：

```text
环境现在是亮的
交通灯听光敏控制
所以应该显示绿灯
```

---

## 2.4 为什么串口不应该直接 `Traffic_RedOn()`

旧写法可能是：

```c
if (StrEqual(arg, "RED"))
{
    Traffic_RedOn();
}
```

这个能用，但有问题：

```text
串口直接控制硬件
app_light 不知道交通灯模式变了
UI 不知道交通灯模式变了
光敏任务也可能再次覆盖它
```

更好的写法是：

```c
App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_RED);
```

这句的意义是：

```text
不是“直接点红灯”
而是“把系统状态改成红灯模式”
```

然后由 `app_light` 统一决定真正怎么点灯。

---

# 第三部分：`version` 机制到底是什么

## 3.1 `version` 不是数据本身

例如：

```c
static uint16_t s_temp_version = 0;
```

它不是温度。

它的作用是：

```text
告诉 UI：温度模块的数据有没有发生变化
```

你可以理解成：

```text
每当温度模块发生值得刷新的变化，就 version++
UI 发现 version 变了，就刷新 OLED
```

---

## 3.2 为什么不每次都刷新 OLED

OLED 刷新比较慢，而且频繁刷新会闪烁。

所以更好的做法是：

```text
数据没变 → 不刷新
数据变了 → 刷新
```

`version` 就是用来判断“有没有变”的。

---

## 3.3 为什么不直接每次都 `s_temp_version++`

假设 DS18B20 一直没接好，连续读取失败：

```text
第 1 次失败：valid = 0
第 2 次失败：valid = 0
第 3 次失败：valid = 0
```

如果每次都 `version++`，OLED 会不停刷新，但显示内容其实一直是：

```text
----
```

没有意义。

所以只在状态真的变化时刷新：

```text
有效 → 无效：刷新
无效 → 有效：刷新
无效 → 无效：不刷新
有效 → 有效：不刷新
```

这就是 `App_Temp_SetValid()` 的意义。

---

## 3.4 温度读取成功后为什么还要判断

你看到过这段：

```c
if ((s_temp_valid == 0) || (temp10 != s_temp10))
{
    s_temp10 = temp10;
    s_temp_valid = 1;
    App_Temp_BumpVersion();
}
```

翻译成中文：

```text
如果之前温度无效
或者这次温度和上次不同
才保存新温度，并通知 UI 刷新
```

为什么？

因为这几种情况不同：

| 之前状态 | 这次读取 | 是否刷新 |
|---|---|---|
| 无效 | 25.0℃ | 刷新，因为从无效变有效 |
| 25.0℃ | 25.0℃ | 不刷新，因为显示内容没变 |
| 25.0℃ | 25.1℃ | 刷新，因为温度变了 |
| 有效 | 读取失败 | 刷新，因为要显示无效 |

---

# 第四部分：DS18B20 状态机与非阻塞

## 4.1 DS18B20 为什么要等 750ms

DS18B20 温度转换不是 STM32 CPU 在算，而是 DS18B20 芯片内部硬件在测温和数字转换。

流程是：

```text
STM32 发 Convert T 命令
        ↓
DS18B20 内部测温、转换、保存结果
        ↓
STM32 过一段时间再读取结果
```

所以 750ms 是：

```text
DS18B20 自己内部转换温度的时间
```

不是 STM32 计算温度的时间。

---

## 4.2 为什么不能这样写

阻塞式写法：

```c
DS18B20_StartConvert();
delay_ms(750);
DS18B20_ReadTemp10(&temp10);
```

问题是：

```text
这 750ms 里主循环停住
OLED 不刷新
串口不处理
摇杆不扫描
光敏不更新
```

所以这不适合你的项目。

---

## 4.3 正确做法：启动后先离开

非阻塞思路：

```text
START：发送开始转换命令
WAIT：不死等，只检查时间够不够
READ：时间到了再读取结果
ERROR：失败后等一段时间再重试
```

状态机：

```text
START
  ↓
DS18B20_StartConvert()
  ↓
WAIT
  ↓ 750ms 到
READ
  ↓
DS18B20_ReadTemp10()
  ↓
再次 StartConvert
```

---

## 4.4 `START` 还有没有用

有，主要用于错误恢复。

例如：

```c
else
{
    if (now - s_temp_start_time >= TEMP_ERROR_RETRY_MS)
    {
        s_temp_state = APP_TEMP_STATE_START;
        s_temp_start_time = now;
    }
}
```

意思是：

```text
如果进入 ERROR 状态
    等 1000ms
    再回到 START
```

下一次 `App_Temp_Task()` 看到：

```c
if (s_temp_state == APP_TEMP_STATE_START)
{
    App_Temp_StartConvert(now);
}
```

就会重新尝试启动 DS18B20 转换。

---

## 4.5 DS18B20 读写 bit 会阻塞吗

会。

因为 1-Wire 协议需要微秒级时序：

```text
拉低 DATA
等待几微秒
释放 DATA
采样电平
等待时隙结束
```

这段时间 CPU 会等待，所以是阻塞的。

但是它很短：

```text
读/写 1 bit：几十微秒
读/写 1 byte：几百微秒
读取温度低字节和高字节：通常很短
```

真正要避免的是：

```text
750ms 的转换等待阻塞
```

所以原则是：

```text
毫秒级、几百毫秒级等待 → 用状态机
微秒级通信时序 → 可以短暂阻塞
```

---

## 4.6 `ds18b20.c` 大概怎么分层

底层驱动可以分成 4 层：

```text
第 1 层：GPIO 层
    DATA 配成输出
    DATA 配成输入
    DATA 拉低
    DATA 读取电平

第 2 层：bit 层
    写 1 bit
    读 1 bit

第 3 层：byte 层
    写 1 byte
    读 1 byte

第 4 层：命令层
    StartConvert
    ReadTemp10
```

`ds18b20.c` 的核心就是：

```text
用一个 GPIO 模拟 1-Wire 时序，
通过拉低/释放 DATA 线的时间长短传输 0 和 1，
先发 Convert T 命令让 DS18B20 自己转换温度，
等转换完成后再发 Read Scratchpad 命令读取温度寄存器。
```

---

# 第五部分：串口应用为什么优化后很难

## 5.1 旧版是直观写法

旧版大概是：

```c
if (StrEqual(cmd, "HELP"))
{
    PrintHelp();
}
else if (StrEqual(cmd, "PING"))
{
    printf("PONG\r\n");
}
else if (StrStartsWith(cmd, "LED "))
{
    HandleLedCommand(cmd + 4);
}
else if (StrStartsWith(cmd, "TH"))
{
    HandleThresholdCommand(cmd + 2);
}
```

优点：

```text
一眼能看懂
适合初学
```

缺点：

```text
命令一多，if-else 很长
容易误匹配
不好扩展
```

---

## 5.2 新版是工程化写法

新版改成：

```text
命令表
    ↓
CommandMatch()
    ↓
DispatchCommand()
    ↓
commands[i].handler(...)
```

它本质上还是：

```text
收到 HELP   → 调用 HandleHelpCommand()
收到 PING   → 调用 HandlePingCommand()
收到 LED    → 调用 HandleLedCommand()
收到 TH     → 调用 HandleThresholdCommand()
```

只是写法更抽象。

---

# 第六部分：C 语言知识点补全

## 6.1 `typedef` 是什么

例如：

```c
typedef uint8_t u8;
```

意思是：

```text
以后可以用 u8 代表 uint8_t
```

再比如：

```c
typedef struct
{
    const char *name;
    uint8_t allow_arg;
    UartCommandHandler handler;
} UartCommand;
```

意思是：

```text
定义一种结构体类型，名字叫 UartCommand
```

以后就可以写：

```c
UartCommand cmd;
```

---

## 6.2 `struct` 是什么

结构体就是把多个相关数据打包成一个整体。

例如：

```c
typedef struct
{
    const char *name;
    uint8_t allow_arg;
    UartCommandHandler handler;
} UartCommand;
```

每一条串口命令需要三个信息：

```text
命令名
是否允许参数
处理函数
```

所以把它们打包成 `UartCommand`。

---

## 6.3 `const char *` 是什么

例如：

```c
const char *name;
```

可以先理解成：

```text
name 指向一个字符串
这个字符串内容不应该被修改
```

比如：

```c
name = "PING";
```

---

## 6.4 函数指针是什么

你看到最吓人的这一句：

```c
typedef void (*UartCommandHandler)(const char *raw_arg, const char *arg);
```

先翻译成人话：

```text
UartCommandHandler 是一种函数类型。
这种函数：
    返回值是 void
    参数是两个字符串：
        const char *raw_arg
        const char *arg
```

下面这些函数都符合这个格式：

```c
static void HandleHelpCommand(const char *raw_arg, const char *arg);
static void HandlePingCommand(const char *raw_arg, const char *arg);
static void HandleEchoCommand(const char *raw_arg, const char *arg);
static void HandleStatusCommand(const char *raw_arg, const char *arg);
```

所以它们都可以被保存到：

```c
UartCommandHandler handler;
```

---

## 6.5 `UartCommand` 结构体是什么

```c
typedef struct
{
    const char *name;
    uint8_t allow_arg;
    UartCommandHandler handler;
} UartCommand;
```

它表示命令表中的一行。

比如：

```c
{"PING", 0, HandlePingCommand}
```

翻译成：

```text
命令名：PING
是否允许参数：不允许
处理函数：HandlePingCommand
```

再比如：

```c
{"ECHO", 1, HandleEchoCommand}
```

翻译成：

```text
命令名：ECHO
是否允许参数：允许
处理函数：HandleEchoCommand
```

---

## 6.6 `LedCommand` 结构体是什么

```c
typedef struct
{
    const char *name;
    AppLightTrafficMode mode;
} LedCommand;
```

它表示：

```text
LED 子命令文本 → app_light 交通灯模式
```

例如：

```c
{"RED", APP_LIGHT_TRAFFIC_RED}
```

意思是：

```text
用户输入 RED
就对应 APP_LIGHT_TRAFFIC_RED 模式
```

---

## 6.7 `(void)raw_arg;` 是什么意思

例如：

```c
static void HandleHelpCommand(const char *raw_arg, const char *arg)
{
    (void)raw_arg;
    (void)arg;
    PrintHelp();
}
```

`HELP` 命令不需要参数，所以 `raw_arg` 和 `arg` 没用。

写：

```c
(void)raw_arg;
(void)arg;
```

意思是：

```text
这个参数我知道存在，但这个函数里不需要用它。
这样可以避免编译器提示“未使用参数”。
```

它不影响程序逻辑。

---

## 6.8 `arg = SkipSpaces(arg);` 是什么意思

例如用户输入：

```text
LED RED
```

匹配完 `LED` 后，`arg` 可能指向：

```text
" RED"
```

前面有一个空格。

执行：

```c
arg = SkipSpaces(arg);
```

之后，`arg` 指向：

```text
"RED"
```

这样后面才能和 `"RED"` 正确比较。

---

## 6.9 `sizeof(array) / sizeof(array[0])` 是什么

例如：

```c
for (i = 0; i < (sizeof(commands) / sizeof(commands[0])); i++)
```

它是在计算数组有多少项。

如果：

```c
static const UartCommand commands[] =
{
    {"HELP", 0, HandleHelpCommand},
    {"PING", 0, HandlePingCommand},
    {"ECHO", 1, HandleEchoCommand},
    {"STATUS", 0, HandleStatusCommand},
    {"LED", 1, HandleLedCommand},
    {"TH", 1, HandleThresholdCommand}
};
```

那么：

```text
sizeof(commands) / sizeof(commands[0]) = 6
```

它比直接写：

```c
for (i = 0; i < 6; i++)
```

更好，因为以后新增命令不需要改数字。

---

## 6.10 `const char **arg` 是什么

在：

```c
static uint8_t CommandMatch(const char *cmd,
                            const char *name,
                            const char **arg)
```

里面，`arg` 是一个输出参数。

这个函数需要返回两个信息：

```text
1. 是否匹配成功
2. 参数从哪里开始
```

返回值 `uint8_t` 用来表示：

```text
1 = 匹配
0 = 不匹配
```

那“参数从哪里开始”就通过：

```c
*arg = cmd + len;
```

带出去。

所以：

```text
return 返回是否匹配
*arg 返回参数地址
```

---

# 第七部分：重点函数逐个拆开

## 7.1 `HandleLedCommand()`

核心代码：

```c
(void)raw_arg;
arg = SkipSpaces(arg);

for (i = 0; i < (sizeof(led_commands) / sizeof(led_commands[0])); i++)
{
    if (StrEqual(arg, led_commands[i].name))
    {
        App_Light_SetTrafficMode(led_commands[i].mode);
        printf("OK: LED %s\r\n", led_commands[i].name);
        return;
    }
}

printf("ERR: Use LED RED/YELLOW/GREEN/OFF/AUTO\r\n");
```

翻译：

```text
忽略 raw_arg，因为这里不用原始大小写。

去掉 arg 前面的空格。

遍历 led_commands[] 表：
    如果用户输入的参数等于表中某一项的 name：
        设置交通灯模式
        打印 OK
        return 结束函数

如果整张表都查完还没找到：
    打印错误提示
```

例如：

```text
LED GREEN
```

流程：

```text
arg = " GREEN"
SkipSpaces 后变成 "GREEN"
查表找到 {"GREEN", APP_LIGHT_TRAFFIC_GREEN}
调用 App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_GREEN)
打印 OK: LED GREEN
```

如果：

```text
LED BLUE
```

查完整张表都没有 `BLUE`，所以打印错误：

```text
ERR: Use LED RED/YELLOW/GREEN/OFF/AUTO
```

---

## 7.2 `HandleThresholdCommand()` 为什么不用命令表

`TH` 的参数形式比较复杂：

```text
TH?       查询
TH +      加 50
TH -      减 50
TH 2000   设置具体数字
```

它不是简单的：

```text
固定文本 → 固定模式
```

而是：

```text
固定符号 + 任意数字
```

所以用 `if-else` 更合适：

```c
if (StrEqual(arg, "?"))
{
    printf("TH=%u\r\n", threshold);
}
else if (StrEqual(arg, "+"))
{
    App_Light_SetThreshold(threshold + 50);
}
else if (StrEqual(arg, "-"))
{
    App_Light_SetThreshold(threshold - 50);
}
else if (ParseNumber(arg, &threshold))
{
    App_Light_SetThreshold(threshold);
}
else
{
    printf("ERR: Use TH?, TH +, TH - or TH 2000\r\n");
}
```

设计原则：

```text
固定命令列表 → 适合表
复杂条件判断 → 适合 if-else
需要解析数字 → 用 ParseNumber()
```

---

## 7.3 `CommandMatch()` 在干什么

函数原型：

```c
static uint8_t CommandMatch(const char *cmd,
                            const char *name,
                            const char **arg)
```

作用：

```text
判断 cmd 是否匹配 name。
如果匹配，把参数开始的位置保存到 *arg。
匹配成功返回 1，不匹配返回 0。
```

例如：

```text
cmd = "LED RED"
name = "LED"
```

结果：

```text
匹配成功
*arg 指向 " RED"
```

例如：

```text
cmd = "PING"
name = "PING"
```

结果：

```text
匹配成功
*arg 指向字符串结尾
```

例如：

```text
cmd = "TH?"
name = "TH"
```

结果：

```text
匹配成功
*arg 指向 "?"
```

例如：

```text
cmd = "THABC"
name = "TH"
```

结果：

```text
不匹配
```

它的中文流程：

```text
计算 name 的长度。

先判断 cmd 是不是以 name 开头。
如果不是，返回 0。

看 name 后面的那个字符 next 是什么。

如果 next 是字符串结束符：
    说明用户只输入了这个命令，比如 PING。
    匹配成功，参数为空。

如果 next 是空格或 Tab：
    说明用户输入了带参数的命令，比如 LED RED。
    匹配成功，参数从空格那里开始。

如果 name 是 TH，并且 next 是 ?：
    允许 TH? 这种特殊写法。
    匹配成功，参数是 ?。

其他情况：
    不匹配。
```

---

## 7.4 `DispatchCommand()` 在干什么

函数原型：

```c
static void DispatchCommand(const char *raw_line, const char *cmd)
```

作用：

```text
在 commands[] 命令表里查找用户输入的命令。
找到后调用对应的处理函数。
找不到就打印未知命令。
```

它的核心循环：

```c
for (i = 0; i < (sizeof(commands) / sizeof(commands[0])); i++)
{
    if (CommandMatch(cmd, commands[i].name, &arg))
    {
        trimmed_arg = SkipSpaces(arg);

        if ((commands[i].allow_arg == 0) && (*trimmed_arg != '\0'))
        {
            printf("ERR: %s takes no arguments\r\n", commands[i].name);
            return;
        }

        commands[i].handler(raw_arg, trimmed_arg);
        return;
    }
}
```

翻译：

```text
从 commands[] 表的第 0 项开始查：
    看用户输入的命令是否匹配这一项的 name。

如果匹配：
    找到参数
    去掉参数前面的空格
    检查这个命令允不允许带参数
    调用这一项对应的处理函数 handler
    return 结束函数

如果整张表都查完还没匹配：
    打印 Unknown command
```

---

## 7.5 `commands[i].handler(...)` 是什么

这是函数指针调用。

命令表类似：

```c
static const UartCommand commands[] =
{
    {"HELP", 0, HandleHelpCommand},
    {"PING", 0, HandlePingCommand},
    {"ECHO", 1, HandleEchoCommand},
    {"STATUS", 0, HandleStatusCommand},
    {"LED", 1, HandleLedCommand},
    {"TH", 1, HandleThresholdCommand}
};
```

当 `i = 1` 时：

```text
commands[i].name = "PING"
commands[i].handler = HandlePingCommand
```

所以：

```c
commands[i].handler(raw_arg, trimmed_arg);
```

等价于：

```c
HandlePingCommand(raw_arg, trimmed_arg);
```

当 `i = 4` 时：

```text
commands[i].name = "LED"
commands[i].handler = HandleLedCommand
```

所以：

```c
commands[i].handler(raw_arg, trimmed_arg);
```

等价于：

```c
HandleLedCommand(raw_arg, trimmed_arg);
```

你可以把它理解成：

```text
表里存了“该调用哪个函数”
程序查到哪一行，就调用哪一行登记的函数
```

---

# 第八部分：完整命令执行流程

## 8.1 `LED RED`

```text
用户输入：LED RED
        ↓
USART 中断接收一行
        ↓
App_UARTPractice_Task() 复制缓冲区
        ↓
HandleLine()
        ↓
把 cmd 转成大写
        ↓
DispatchCommand(raw_line, cmd)
        ↓
命令表查到 LED
        ↓
调用 HandleLedCommand()
        ↓
子命令表查到 RED
        ↓
App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_RED)
        ↓
app_light 更新交通灯模式
        ↓
Traffic_RedOn()
```

---

## 8.2 `TH +`

```text
用户输入：TH +
        ↓
DispatchCommand() 查到 TH
        ↓
调用 HandleThresholdCommand()
        ↓
识别参数是 "+"
        ↓
读取当前阈值
        ↓
阈值 +50
        ↓
App_Light_SetThreshold()
        ↓
app_light version++
        ↓
App_UI_Task() 发现 version 变化
        ↓
OLED 阈值页面刷新
```

---

## 8.3 `PING abc`

```text
用户输入：PING abc
        ↓
DispatchCommand() 查到 PING
        ↓
发现 PING 的 allow_arg = 0
        ↓
但参数不是空
        ↓
打印 ERR: PING takes no arguments
```

---

# 第九部分：你现在应该怎么学这份代码

## 9.1 不要直接从工程版开始

建议路线：

```text
第一步：写 if-else 版
第二步：把每个命令拆成独立 HandleXXXCommand()
第三步：把命令登记到 commands[] 表
第四步：理解 handler 函数指针
第五步：理解 CommandMatch() 的严谨匹配
```

现在你的代码直接到了第四、第五步，所以会吓人。

---

## 9.2 你现在必须掌握的低配版

你应该先能自己写出这个：

```c
static void HandleLine(char *line)
{
    if (StrEqual(line, "HELP"))
    {
        PrintHelp();
    }
    else if (StrEqual(line, "PING"))
    {
        printf("PONG\r\n");
    }
    else if (StrStartsWith(line, "LED "))
    {
        HandleLedCommand(line + 4);
    }
    else if (StrStartsWith(line, "TH"))
    {
        HandleThresholdCommand(line + 2);
    }
    else
    {
        printf("ERR\r\n");
    }
}
```

这是你当前阶段更合理的起点。

---

## 9.3 再升级成中配版

中配版是：

```text
仍然用 if-else
但每个命令有独立处理函数
```

例如：

```c
if (StrEqual(cmd, "PING"))
{
    HandlePingCommand(raw_arg, arg);
}
```

先不要管命令表和函数指针。

---

## 9.4 最后再理解工程版

工程版是：

```text
commands[] 表
for 循环查表
CommandMatch()
commands[i].handler()
```

这时候你再看，就会明白它只是把重复的 if-else 折叠起来。

---

# 第十部分：给你的练习任务

## 练习 1：自己写低配版 `PING`

目标：

```text
串口输入 PING
返回 PONG
```

代码思路：

```c
if (StrEqual(cmd, "PING"))
{
    printf("PONG\r\n");
}
```

---

## 练习 2：自己写低配版 `LED RED`

目标：

```text
串口输入 LED RED
红灯亮
```

代码思路：

```c
if (StrStartsWith(cmd, "LED "))
{
    if (StrEqual(cmd + 4, "RED"))
    {
        App_Light_SetTrafficMode(APP_LIGHT_TRAFFIC_RED);
    }
}
```

---

## 练习 3：自己写低配版 `TH +`

目标：

```text
串口输入 TH +
阈值增加 50
```

代码思路：

```c
if (StrEqual(cmd, "TH +"))
{
    threshold = App_Light_GetThreshold();
    App_Light_SetThreshold(threshold + 50);
}
```

---

## 练习 4：加一个 `TEMP?` 命令

目标：

```text
串口输入 TEMP?
返回当前温度
```

低配思路：

```c
if (StrEqual(cmd, "TEMP?"))
{
    if (App_Temp_IsValid())
    {
        PrintTemp10(App_Temp_GetTemp10());
        printf("C\r\n");
    }
    else
    {
        printf("TEMP=NA\r\n");
    }
}
```

---

## 练习 5：再把 `TEMP?` 放入命令表

当你能写低配版后，再尝试：

```c
static void HandleTempCommand(const char *raw_arg, const char *arg)
{
    (void)raw_arg;
    (void)arg;

    if (App_Temp_IsValid())
    {
        PrintTemp10(App_Temp_GetTemp10());
        printf("C\r\n");
    }
    else
    {
        printf("TEMP=NA\r\n");
    }
}
```

然后命令表里加：

```c
{"TEMP?", 0, HandleTempCommand}
```

不过这里要注意：如果你当前 `CommandMatch()` 不支持 `TEMP?` 这种形式，就需要改匹配规则，或者命令改成：

```text
TEMP
```

这样更简单。

---

# 第十一部分：你应该记住的核心句子

## 状态机

```text
状态机不是同时做很多事，而是每次进来只根据当前状态做一点事。
```

## 非阻塞

```text
不能在主循环里死等几百毫秒。
要记录开始时间，然后每轮检查时间到没到。
```

## DS18B20

```text
DS18B20 的 750ms 是传感器内部转换时间。
STM32 只发命令，之后可以去做别的事。
```

## version

```text
version 不是数据，而是“数据变化通知”。
```

## traffic_mode

```text
traffic_mode 决定交通灯听自动控制还是手动控制。
它是避免串口、UI、光敏互相抢控制权的关键。
```

## 命令表

```text
命令表就是把“命令名、能否带参数、处理函数”登记到一张表里。
```

## 函数指针

```text
函数指针就是把“要调用哪个函数”也当成数据保存起来。
```

## DispatchCommand

```text
DispatchCommand() 就是在命令表里查找用户输入，找到后调用对应处理函数。
```

---

# 第十二部分：当前阶段的合理目标

你现在不需要做到：

```text
独立写出完整命令表框架
独立设计函数指针分发系统
从零写 DS18B20 1-Wire 驱动
```

你现在应该做到：

```text
能读懂 if-else 版命令解析
能知道命令表版在替代 if-else
能知道 App_Light_SetTrafficMode() 为什么比直接 Traffic_RedOn() 好
能知道 DS18B20 为什么要状态机
能知道 version 为什么不是每次都加
能跟着一条命令走完整流程
```

如果你能做到这些，当前阶段已经够了。

---

# 第十三部分：复习路线

建议按这个顺序复习：

```text
1. main.c 主循环
2. app_light.c 的 s_traffic_mode
3. app_temp.c 的 START / WAIT / READ / ERROR
4. version 机制
5. 串口 if-else 低配版
6. HandleXXXCommand() 拆分
7. commands[] 命令表
8. CommandMatch()
9. DispatchCommand()
10. commands[i].handler()
```

不要反过来从函数指针开始看。

---

# 最后总结

这份优化后的串口代码难，是因为它已经从：

```text
能用的初学者代码
```

升级成了：

```text
可扩展的工程化命令解析器
```

它不是你现在必须立刻完全掌握的代码。

你现在最重要的是把它还原成主线：

```text
收到一行串口输入
        ↓
判断命令
        ↓
解析参数
        ↓
调用 app_light / app_temp
        ↓
更新系统状态
        ↓
UI 根据 version 刷新
```

只要这条线能跑通，后面的命令表、函数指针、表驱动都可以慢慢补。
