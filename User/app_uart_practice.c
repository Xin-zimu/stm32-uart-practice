#include "app_uart_practice.h"
#include "app_light.h"
#include "app_temp.h"
#include "led.h"
#include "usart.h"
#include <stdio.h>

#define UART_LINE_DONE      0x8000
#define UART_LINE_LEN_MASK  0x3FFF

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

static const char *SkipSpaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t'))
    {
        text++;
    }

    return text;
}

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

static void PrintTemp10(int16_t temp10)
{
    if (temp10 < 0)
    {
        printf("-");
        temp10 = (int16_t)(0 - temp10);
    }

    printf("%d.%d", temp10 / 10, temp10 % 10);
}

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

static void HandleLine(char *line)
{
    char cmd[USART_REC_LEN + 1];
    uint16_t i;

    for (i = 0; i < USART_REC_LEN; i++)
    {
        cmd[i] = line[i];

        if (line[i] == '\0')
        {
            break;
        }
    }

    cmd[USART_REC_LEN] = '\0';
    ToUpperString(cmd);

    printf("RX: %s\r\n", line);

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

void App_UARTPractice_Init(void)
{
    printf("\r\nSTM32F103 UART practice ready.\r\n");
    printf("USART1: PA9=TX, PA10=RX, 115200 8N1.\r\n");
    printf("Send HELP for commands.\r\n\r\n");
}

void App_UARTPractice_Task(void)
{
    char line[USART_REC_LEN + 1];
    uint16_t len;
    uint16_t i;

    if ((USART_RX_STA & UART_LINE_DONE) == 0)
    {
        return;
    }

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
    USART_RX_STA = 0;
    __enable_irq();

    if (len > 0)
    {
        HandleLine(line);
    }
}
