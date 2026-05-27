#include "app_uart_practice.h"
#include "app_light.h"
#include "app_temp.h"
#include "usart.h"
#include <stdio.h>

/* USART_RX_STA 标志位定义 */
#define UART_LINE_DONE      0x8000  /* bit15：收到完整一行 */
#define UART_LINE_LEN_MASK  0x3FFF  /* bit0~13：这一行的字节数 */

/**
 * StrEqual - 判断两个字符串是否完全相等
 * a, b：要比较的字符串
 * 返回 1 相等，0 不等
 */
static int StrEqual(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0'))
    {
        if (*a != *b)
        {
            return 0;
        }

        a++;
        b++;
    }

    return (*a == '\0') && (*b == '\0');
}

/**
 * StrStartsWith - 判断 text 是否以 prefix 开头
 * 用于识别带参数的命令，如 "LED RED" 以 "LED " 开头
 */
static int StrStartsWith(const char *text, const char *prefix)
{
    while (*prefix != '\0')
    {
        if (*text != *prefix)
        {
            return 0;
        }

        text++;
        prefix++;
    }

    return 1;
}

static uint16_t StrLen(const char *text)
{
    uint16_t len;

    len = 0;

    while (*text != '\0')
    {
        len++;
        text++;
    }

    return len;
}

/**
 * ToUpperString - 把字符串全部转成大写
 * 这样用户输入 help/Help/HELP 都能被识别
 */
static void ToUpperString(char *text)
{
    while (*text != '\0')
    {
        if ((*text >= 'a') && (*text <= 'z'))
        {
            *text = (char)(*text - 'a' + 'A');
        }

        text++;
    }
}

/**
 * SkipSpaces - 跳过字符串开头的空格和 Tab
 * 返回第一个非空白字符的指针
 */
static const char *SkipSpaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t'))
    {
        text++;
    }

    return text;
}

/**
 * ParseNumber - 将字符串解析为 uint16_t 数字
 * text：要解析的字符串，如 "2000"
 * value：输出参数，解析成功的数值
 * 返回 1 成功，0 失败（空、超范围、含非法字符）
 */
static uint8_t ParseNumber(const char *text, uint16_t *value)
{
    uint32_t result;
    uint8_t has_digit;

    result = 0;
    has_digit = 0;
    text = SkipSpaces(text);

    while ((*text >= '0') && (*text <= '9'))
    {
        has_digit = 1;
        result = result * 10u + (uint32_t)(*text - '0');

        if (result > 65535u)
        {
            return 0;
        }

        text++;
    }

    text = SkipSpaces(text);

    if ((*text != '\0') || (has_digit == 0))
    {
        return 0;
    }

    *value = (uint16_t)result;
    return 1;
}

/**
 * PrintTemp10 - 打印温度值（实际值 = temp10 / 10）
 * 如 temp10 = 256 时打印 "25.6"
 */
static void PrintTemp10(int16_t temp10)
{
    if (temp10 < 0)
    {
        printf("-");
        temp10 = (int16_t)(0 - temp10);
    }

    printf("%d.%d", temp10 / 10, temp10 % 10);
}

static const char *TrafficModeName(AppLightTrafficMode mode)
{
    if (mode == APP_LIGHT_TRAFFIC_RED)
    {
        return "RED";
    }

    if (mode == APP_LIGHT_TRAFFIC_YELLOW)
    {
        return "YELLOW";
    }

    if (mode == APP_LIGHT_TRAFFIC_GREEN)
    {
        return "GREEN";
    }

    if (mode == APP_LIGHT_TRAFFIC_OFF)
    {
        return "OFF";
    }

    return "AUTO";
}

/**
 * PrintHelp - 打印所有可用命令的帮助信息
 */
static void PrintHelp(void)
{
    printf("\r\nCommands:\r\n");
    printf("  HELP        Show commands\r\n");
    printf("  PING        Reply PONG\r\n");
    printf("  ECHO text   Reply text\r\n");
    printf("  STATUS      Show light, temp and LED mode\r\n");
    printf("  LED RED     Turn red LED on\r\n");
    printf("  LED YELLOW  Turn yellow LED on\r\n");
    printf("  LED GREEN   Turn green LED on\r\n");
    printf("  LED OFF     Turn all LEDs off\r\n");
    printf("  LED AUTO    Enable light controlled LED\r\n");
    printf("  TH?         Show light threshold\r\n");
    printf("  TH +        Threshold +50\r\n");
    printf("  TH -        Threshold -50\r\n");
    printf("  TH 2000     Set threshold\r\n\r\n");
}

/**
 * PrintStatus - 打印当前光照、温度、LED 模式等状态
 */
static void PrintStatus(void)
{
    printf("\r\nAO=%u, LIGHT=%s, TH=%u, LED_MODE=%s, TEMP=",
           App_Light_GetAO(),
           App_Light_IsDark() ? "DARK" : "BRIGHT",
           App_Light_GetThreshold(),
           TrafficModeName(App_Light_GetTrafficMode()));

    if (App_Temp_IsValid())
    {
        PrintTemp10(App_Temp_GetTemp10());
        printf("C");
    }
    else
    {
        printf("NA");
    }

    printf("\r\n");
}

typedef void (*UartCommandHandler)(const char *raw_arg, const char *arg);

typedef struct
{
    const char *name;
    uint8_t allow_arg;
    UartCommandHandler handler;
} UartCommand;

typedef struct
{
    const char *name;
    AppLightTrafficMode mode;
} LedCommand;

static void HandleHelpCommand(const char *raw_arg, const char *arg)
{
    (void)raw_arg;
    (void)arg;
    PrintHelp();
}

static void HandlePingCommand(const char *raw_arg, const char *arg)
{
    (void)raw_arg;
    (void)arg;
    printf("PONG\r\n");
}

static void HandleEchoCommand(const char *raw_arg, const char *arg)
{
    (void)arg;
    printf("%s\r\n", SkipSpaces(raw_arg));
}

static void HandleStatusCommand(const char *raw_arg, const char *arg)
{
    (void)raw_arg;
    (void)arg;
    PrintStatus();
}

/**
 * HandleLedCommand - 处理 "LED xxx" 子命令
 * RED / YELLOW / GREEN / OFF / AUTO
 */
static void HandleLedCommand(const char *raw_arg, const char *arg)
{
    static const LedCommand led_commands[] =
    {
        {"AUTO", APP_LIGHT_TRAFFIC_AUTO},
        {"RED", APP_LIGHT_TRAFFIC_RED},
        {"YELLOW", APP_LIGHT_TRAFFIC_YELLOW},
        {"GREEN", APP_LIGHT_TRAFFIC_GREEN},
        {"OFF", APP_LIGHT_TRAFFIC_OFF}
    };
    uint8_t i;

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
}

/**
 * HandleThresholdCommand - 处理 "TH" 子命令
 * TH? 查询 / TH + 加 50 / TH - 减 50 / TH 数字 设指定值
 */
static void HandleThresholdCommand(const char *raw_arg, const char *arg)
{
    uint16_t threshold;

    (void)raw_arg;
    arg = SkipSpaces(arg);
    threshold = App_Light_GetThreshold();

    if (StrEqual(arg, "?"))
    {
        printf("TH=%u\r\n", threshold);
    }
    else if (StrEqual(arg, "+"))
    {
        App_Light_SetThreshold((uint16_t)(threshold + 50u));
        printf("OK: TH=%u\r\n", App_Light_GetThreshold());
    }
    else if (StrEqual(arg, "-"))
    {
        if (threshold > 50u)
        {
            threshold = (uint16_t)(threshold - 50u);
        }

        App_Light_SetThreshold(threshold);
        printf("OK: TH=%u\r\n", App_Light_GetThreshold());
    }
    else if (ParseNumber(arg, &threshold))
    {
        App_Light_SetThreshold(threshold);
        printf("OK: TH=%u\r\n", App_Light_GetThreshold());
    }
    else
    {
        printf("ERR: Use TH?, TH +, TH - or TH 2000\r\n");
    }
}

static uint8_t CommandMatch(const char *cmd,
                            const char *name,
                            const char **arg)
{
    uint16_t len;
    char next;

    len = StrLen(name);

    if (StrStartsWith(cmd, name) == 0)
    {
        return 0;
    }

    next = cmd[len];

    if (next == '\0')
    {
        *arg = cmd + len;
        return 1;
    }

    if ((next == ' ') || (next == '\t'))
    {
        *arg = cmd + len;
        return 1;
    }

    if ((StrEqual(name, "TH")) && (next == '?'))
    {
        *arg = cmd + len;
        return 1;
    }

    return 0;
}

static void DispatchCommand(const char *raw_line, const char *cmd)
{
    static const UartCommand commands[] =
    {
        {"HELP", 0, HandleHelpCommand},
        {"PING", 0, HandlePingCommand},
        {"ECHO", 1, HandleEchoCommand},
        {"STATUS", 0, HandleStatusCommand},
        {"LED", 1, HandleLedCommand},
        {"TH", 1, HandleThresholdCommand}
    };
    const char *arg;
    const char *raw_arg;
    const char *trimmed_arg;
    uint8_t i;

    for (i = 0; i < (sizeof(commands) / sizeof(commands[0])); i++)
    {
        if (CommandMatch(cmd, commands[i].name, &arg))
        {
            raw_arg = raw_line + StrLen(commands[i].name);
            trimmed_arg = SkipSpaces(arg);

            if ((commands[i].allow_arg == 0) && (*trimmed_arg != '\0'))
            {
                printf("ERR: %s takes no arguments\r\n", commands[i].name);
                return;
            }

            commands[i].handler(raw_arg, arg);
            return;
        }
    }

    printf("ERR: Unknown command. Send HELP\r\n");
}

/**
 * HandleLine - 串口命令分发
 * 把收到的行转大写后，匹配是哪个命令，交给对应的处理函数
 */
static void HandleLine(char *line)
{
    char cmd[USART_REC_LEN + 1];
    uint16_t i;

    /* 复制收到的行到 cmd 缓冲区 */
    for (i = 0; i < USART_REC_LEN; i++)
    {
        cmd[i] = line[i];

        if (line[i] == '\0')
        {
            break;
        }
    }

    cmd[USART_REC_LEN] = '\0';
    ToUpperString(cmd);           /* 转大写，不区分大小写 */

    printf("RX: %s\r\n", line);   /* 回显收到的原始内容 */

    DispatchCommand(line, cmd);
}

/**
 * App_UARTPractice_Init - 串口命令行初始化
 * 上电时调用一次，打印欢迎信息
 */
void App_UARTPractice_Init(void)
{
    printf("\r\nSTM32F103 UART practice ready.\r\n");
    printf("USART1: PA9=TX, PA10=RX, 115200 8N1.\r\n");
    printf("Send HELP for commands.\r\n\r\n");
}

/**
 * App_UARTPractice_Task - 串口命令行主任务
 * 在 while(1) 中反复调用，检查串口是否有新行到达
 * 如果收到完整一行，复制出来交给 HandleLine 处理
 */
void App_UARTPractice_Task(void)
{
    char line[USART_REC_LEN + 1];
    uint16_t len;
    uint16_t i;

    /* 检查是否收到完整一行（USART_RX_STA 的 bit15） */
    if ((USART_RX_STA & UART_LINE_DONE) == 0)
    {
        return;     /* 没有新行，直接返回 */
    }

    /* 关中断，安全地从共享缓冲区读取数据 */
    __disable_irq();
    len = (uint16_t)(USART_RX_STA & UART_LINE_LEN_MASK);

    if (len > USART_REC_LEN)
    {
        len = USART_REC_LEN;
    }

    for (i = 0; i < len; i++)
    {
        line[i] = (char)USART_RX_BUF[i];
    }

    line[len] = '\0';
    USART_RX_STA = 0;           /* 清空接收标志，允许中断继续收下一行 */
    __enable_irq();              /* 开中断 */

    if (len > 0)
    {
        HandleLine(line);       /* 解析并执行命令 */
    }
}
