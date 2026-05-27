#include "app_uart_practice.h"
#include "app_light.h"
#include "app_temp.h"
#include "led.h"
#include "usart.h"
#include <stdio.h>

/* USART_RX_STA 标志位定义 */
#define UART_LINE_DONE      0x8000  // bit15：收到完整一行
#define UART_LINE_LEN_MASK  0x3FFF  // bit0~13：这一行的字节数

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
           App_Light_IsAutoTraffic() ? "AUTO" : "MANUAL");

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

/**
 * HandleLedCommand - 处理 "LED xxx" 子命令
 * RED / YELLOW / GREEN / OFF / AUTO
 */
static void HandleLedCommand(const char *arg)
{
    arg = SkipSpaces(arg);

    if (StrEqual(arg, "RED"))
    {
        App_Light_SetAutoTraffic(0);
        Traffic_RedOn();
        printf("OK: LED RED\r\n");
    }
    else if (StrEqual(arg, "YELLOW"))
    {
        App_Light_SetAutoTraffic(0);
        Traffic_YellowOn();
        printf("OK: LED YELLOW\r\n");
    }
    else if (StrEqual(arg, "GREEN"))
    {
        App_Light_SetAutoTraffic(0);
        Traffic_GreenOn();
        printf("OK: LED GREEN\r\n");
    }
    else if (StrEqual(arg, "OFF"))
    {
        App_Light_SetAutoTraffic(0);
        Traffic_AllOff();
        printf("OK: LED OFF\r\n");
    }
    else if (StrEqual(arg, "AUTO"))
    {
        App_Light_SetAutoTraffic(1);
        printf("OK: LED AUTO\r\n");
    }
    else
    {
        printf("ERR: Use LED RED/YELLOW/GREEN/OFF/AUTO\r\n");
    }
}

/**
 * HandleThresholdCommand - 处理 "TH" 子命令
 * TH? 查询 / TH + 加 50 / TH - 减 50 / TH 数字 设指定值
 */
static void HandleThresholdCommand(const char *arg)
{
    uint16_t threshold;

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

    /* 命令匹配 */
    if (StrEqual(cmd, "HELP"))
    {
        PrintHelp();
    }
    else if (StrEqual(cmd, "PING"))
    {
        printf("PONG\r\n");
    }
    else if (StrStartsWith(cmd, "ECHO "))
    {
        printf("%s\r\n", line + 5);
    }
    else if (StrEqual(cmd, "STATUS"))
    {
        PrintStatus();
    }
    else if (StrStartsWith(cmd, "LED "))
    {
        HandleLedCommand(cmd + 4);
    }
    else if (StrStartsWith(cmd, "TH"))
    {
        HandleThresholdCommand(cmd + 2);
    }
    else
    {
        printf("ERR: Unknown command. Send HELP\r\n");
    }
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
