#include "oled.h"

#define OLED_GPIO_PORT     GPIOB
#define OLED_GPIO_CLK      RCC_APB2Periph_GPIOB
#define OLED_I2C           I2C1
#define OLED_I2C_CLK       RCC_APB1Periph_I2C1
#define OLED_I2C_SPEED     400000
#define OLED_I2C_TIMEOUT   10000
#define OLED_SCL_PIN       GPIO_Pin_6
#define OLED_SDA_PIN       GPIO_Pin_7
#define OLED_I2C_ADDR      0x78

/*
 * Minimal SSD1306 OLED driver.
 *
 * The UI only needs ASCII text and numbers, so the font table is kept small
 * and each character is drawn as 5 columns plus one blank spacing column.
 */
static void OLED_DelayMs(uint32_t ms)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 8000; j++)
        {
        }
    }
}

static uint8_t OLED_I2C_WaitEvent(uint32_t event)
{
    uint32_t timeout;

    timeout = OLED_I2C_TIMEOUT;

    while (I2C_CheckEvent(OLED_I2C, event) != SUCCESS)
    {
        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

static uint8_t OLED_I2C_WaitFlag(FlagStatus status, uint32_t flag)
{
    uint32_t timeout;

    timeout = OLED_I2C_TIMEOUT;

    while (I2C_GetFlagStatus(OLED_I2C, flag) == status)
    {
        if (timeout == 0)
        {
            return 0;
        }

        timeout--;
    }

    return 1;
}

/* Timeout wrappers keep a missing OLED from locking the main program forever. */
static uint8_t OLED_I2C_BeginWrite(uint8_t control)
{
    if (OLED_I2C_WaitFlag(SET, I2C_FLAG_BUSY) == 0)
    {
        return 0;
    }

    I2C_GenerateSTART(OLED_I2C, ENABLE);

    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) == 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(OLED_I2C, OLED_I2C_ADDR, I2C_Direction_Transmitter);

    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        return 0;
    }

    I2C_SendData(OLED_I2C, control);

    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        return 0;
    }

    return 1;
}

static uint8_t OLED_I2C_WriteByte(uint8_t data)
{
    I2C_SendData(OLED_I2C, data);

    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) == 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        return 0;
    }

    return 1;
}

static void OLED_I2C_EndWrite(void)
{
    I2C_GenerateSTOP(OLED_I2C, ENABLE);
}

/* Initialize PB6/PB7 as I2C1 pins and enable the hardware I2C peripheral. */
static void OLED_HW_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef I2C_InitStructure;

    RCC_APB2PeriphClockCmd(OLED_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_I2C_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_GPIO_PORT, &GPIO_InitStructure);

    I2C_DeInit(OLED_I2C);

    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Disable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = OLED_I2C_SPEED;

    I2C_Init(OLED_I2C, &I2C_InitStructure);
    I2C_Cmd(OLED_I2C, ENABLE);
}

/* Send one SSD1306 command byte. */
static void OLED_WriteCommand(uint8_t command)
{
    if (OLED_I2C_BeginWrite(0x00))
    {
        OLED_I2C_WriteByte(command);
        OLED_I2C_EndWrite();
    }
}

static void OLED_WriteData(uint8_t data)
{
    if (OLED_I2C_BeginWrite(0x40))
    {
        OLED_I2C_WriteByte(data);
        OLED_I2C_EndWrite();
    }
}

static void OLED_SetPos(uint8_t page, uint8_t column)
{
    /* SSD1306 addresses the 128x64 screen as 8 pages of 8 vertical pixels. */
    OLED_WriteCommand(0xB0 + page);
    OLED_WriteCommand(0x00 + (column & 0x0F));
    OLED_WriteCommand(0x10 + ((column >> 4) & 0x0F));
}

void OLED_ClearPage(uint8_t page)
{
    uint8_t column;

    if (page >= 8)
    {
        return;
    }

    OLED_SetPos(page, 0);

    if (OLED_I2C_BeginWrite(0x40))
    {
        for (column = 0; column < 128; column++)
        {
            if (OLED_I2C_WriteByte(0x00) == 0)
            {
                return;
            }
        }

        OLED_I2C_EndWrite();
    }
}

void OLED_Clear(void)
{
    uint8_t page;

    for (page = 0; page < 8; page++)
    {
        OLED_ClearPage(page);
    }
}

void OLED_Fill(uint8_t data)
{
    uint8_t page;
    uint8_t column;

    for (page = 0; page < 8; page++)
    {
        OLED_SetPos(page, 0);

        if (OLED_I2C_BeginWrite(0x40))
        {
            for (column = 0; column < 128; column++)
            {
                if (OLED_I2C_WriteByte(data) == 0)
                {
                    return;
                }
            }

            OLED_I2C_EndWrite();
        }
    }
}

void OLED_Init(void)
{
    OLED_HW_I2C_Init();
    OLED_DelayMs(100);

    /* Standard SSD1306 128x64 initialization sequence. */
    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0x20);
    OLED_WriteCommand(0x10);
    OLED_WriteCommand(0xB0);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x10);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0x7F);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);

    OLED_Clear();
}

static const uint8_t *OLED_GetMiniFont(char ch)
{
    static const uint8_t font_space[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t font_colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t font_dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t font_minus[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t font_plus[5] = {0x08,0x08,0x3E,0x08,0x08};
    static const uint8_t font_lt[5] = {0x00,0x08,0x14,0x22,0x41};
    static const uint8_t font_gt[5] = {0x00,0x41,0x22,0x14,0x08};

    static const uint8_t font_0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t font_1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t font_2[5] = {0x42,0x61,0x51,0x49,0x46};
    static const uint8_t font_3[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t font_4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t font_5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t font_6[5] = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t font_7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t font_8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t font_9[5] = {0x06,0x49,0x49,0x29,0x1E};

    static const uint8_t font_A[5] = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t font_B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t font_C[5] = {0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t font_D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t font_E[5] = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t font_F[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t font_G[5] = {0x3E,0x41,0x49,0x49,0x7A};
    static const uint8_t font_H[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t font_I[5] = {0x00,0x41,0x7F,0x41,0x00};
    static const uint8_t font_J[5] = {0x20,0x40,0x41,0x3F,0x01};
    static const uint8_t font_K[5] = {0x7F,0x08,0x14,0x22,0x41};
    static const uint8_t font_L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t font_M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t font_N[5] = {0x7F,0x04,0x08,0x10,0x7F};
    static const uint8_t font_O[5] = {0x3E,0x41,0x41,0x41,0x3E};
    static const uint8_t font_P[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t font_Q[5] = {0x3E,0x41,0x51,0x21,0x5E};
    static const uint8_t font_R[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t font_S[5] = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t font_T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t font_U[5] = {0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t font_V[5] = {0x1F,0x20,0x40,0x20,0x1F};
    static const uint8_t font_W[5] = {0x7F,0x20,0x18,0x20,0x7F};
    static const uint8_t font_X[5] = {0x63,0x14,0x08,0x14,0x63};
    static const uint8_t font_Y[5] = {0x07,0x08,0x70,0x08,0x07};
    static const uint8_t font_Z[5] = {0x61,0x51,0x49,0x45,0x43};

    switch (ch)
    {
        case ' ': return font_space;
        case ':': return font_colon;
        case '.': return font_dot;
        case '-': return font_minus;
        case '+': return font_plus;
        case '<': return font_lt;
        case '>': return font_gt;
        case '0': return font_0;
        case '1': return font_1;
        case '2': return font_2;
        case '3': return font_3;
        case '4': return font_4;
        case '5': return font_5;
        case '6': return font_6;
        case '7': return font_7;
        case '8': return font_8;
        case '9': return font_9;
        case 'A': return font_A;
        case 'B': return font_B;
        case 'C': return font_C;
        case 'D': return font_D;
        case 'E': return font_E;
        case 'F': return font_F;
        case 'G': return font_G;
        case 'H': return font_H;
        case 'I': return font_I;
        case 'J': return font_J;
        case 'K': return font_K;
        case 'L': return font_L;
        case 'M': return font_M;
        case 'N': return font_N;
        case 'O': return font_O;
        case 'P': return font_P;
        case 'Q': return font_Q;
        case 'R': return font_R;
        case 'S': return font_S;
        case 'T': return font_T;
        case 'U': return font_U;
        case 'V': return font_V;
        case 'W': return font_W;
        case 'X': return font_X;
        case 'Y': return font_Y;
        case 'Z': return font_Z;
        default: return font_space;
    }
}

/* Draw one 5x7 ASCII character at a page/column position. */
void OLED_ShowChar(uint8_t page, uint8_t column, char ch)
{
    uint8_t i;
    const uint8_t *font;

    if ((page >= 8) || (column > 122))
    {
        return;
    }

    font = OLED_GetMiniFont(ch);
    OLED_SetPos(page, column);

    for (i = 0; i < 5; i++)
    {
        OLED_WriteData(font[i]);
    }

    OLED_WriteData(0x00);
}

void OLED_ShowString(uint8_t page, uint8_t column, const char *str)
{
    while (*str != '\0')
    {
        OLED_ShowChar(page, column, *str);
        column += 6;
        str++;

        if (column > 122)
        {
            break;
        }
    }
}

/* Draw a fixed-width decimal number with leading zeroes when len is larger. */
void OLED_ShowNum(uint8_t page, uint8_t column, uint16_t num, uint8_t len)
{
    uint8_t i;
    uint16_t div = 1;
    uint8_t digit;

    for (i = 1; i < len; i++)
    {
        div *= 10;
    }

    for (i = 0; i < len; i++)
    {
        digit = (uint8_t)(num / div);
        OLED_ShowChar(page, column, (char)(digit + '0'));
        num %= div;
        div /= 10;
        column += 6;
    }
}
