#include "ds18b20.h"
#include "delay.h"

/*
   DS18B20 接线：
   VCC  -> 3.3V
   GND  -> GND
   DATA -> PB0
*/

#define DS18B20_GPIO_PORT      GPIOB
#define DS18B20_GPIO_CLK       RCC_APB2Periph_GPIOB
#define DS18B20_DQ_PIN         GPIO_Pin_0

#define DS18B20_DQ_LOW()       GPIO_ResetBits(DS18B20_GPIO_PORT, DS18B20_DQ_PIN)
#define DS18B20_DQ_HIGH()      GPIO_SetBits(DS18B20_GPIO_PORT, DS18B20_DQ_PIN)
#define DS18B20_DQ_READ()      GPIO_ReadInputDataBit(DS18B20_GPIO_PORT, DS18B20_DQ_PIN)

static void DS18B20_IO_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = DS18B20_DQ_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(DS18B20_GPIO_PORT, &GPIO_InitStructure);
}

static void DS18B20_IO_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = DS18B20_DQ_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(DS18B20_GPIO_PORT, &GPIO_InitStructure);
}

void DS18B20_Init(void)
{
    RCC_APB2PeriphClockCmd(DS18B20_GPIO_CLK, ENABLE);

    DS18B20_IO_OUT();
    DS18B20_DQ_HIGH();
}

uint8_t DS18B20_Check(void)
{
    uint8_t retry = 0;

    DS18B20_IO_OUT();

    DS18B20_DQ_LOW();
    delay_us(480);

    DS18B20_DQ_HIGH();
    delay_us(60);

    DS18B20_IO_IN();

    while (DS18B20_DQ_READ() && retry < 200)
    {
        retry++;
        delay_us(1);
    }

    if (retry >= 200)
    {
        return 0;
    }

    retry = 0;

    while (!DS18B20_DQ_READ() && retry < 240)
    {
        retry++;
        delay_us(1);
    }

    if (retry >= 240)
    {
        return 0;
    }

    return 1;
}

static void DS18B20_WriteBit(uint8_t bit)
{
    DS18B20_IO_OUT();

    DS18B20_DQ_LOW();

    if (bit)
    {
        delay_us(2);
        DS18B20_DQ_HIGH();
        delay_us(60);
    }
    else
    {
        delay_us(60);
        DS18B20_DQ_HIGH();
        delay_us(2);
    }
}

static uint8_t DS18B20_ReadBit(void)
{
    uint8_t bit = 0;

    DS18B20_IO_OUT();

    DS18B20_DQ_LOW();
    delay_us(2);

    DS18B20_DQ_HIGH();

    DS18B20_IO_IN();

    delay_us(12);

    if (DS18B20_DQ_READ())
    {
        bit = 1;
    }
    else
    {
        bit = 0;
    }

    delay_us(50);

    return bit;
}

static void DS18B20_WriteByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        DS18B20_WriteBit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t DS18B20_ReadByte(void)
{
    uint8_t i;
    uint8_t data = 0;

    for (i = 0; i < 8; i++)
    {
        data >>= 1;

        if (DS18B20_ReadBit())
        {
            data |= 0x80;
        }
    }

    return data;
}

uint8_t DS18B20_StartConvert(void)
{
    if (!DS18B20_Check())
    {
        return 0;
    }

    DS18B20_WriteByte(0xCC);    // Skip ROM
    DS18B20_WriteByte(0x44);    // Convert T

    return 1;
}

uint8_t DS18B20_ReadTemp10(int16_t *temp10)
{
    uint8_t temp_l;
    uint8_t temp_h;
    int16_t raw;

    if (!DS18B20_Check())
    {
        return 0;
    }

    DS18B20_WriteByte(0xCC);    // Skip ROM
    DS18B20_WriteByte(0xBE);    // Read Scratchpad

    temp_l = DS18B20_ReadByte();
    temp_h = DS18B20_ReadByte();

    raw = (int16_t)((temp_h << 8) | temp_l);

    /*
       DS18B20 默认 12 位分辨率：
       温度 = raw / 16.0

       这里不用 float：
       temp10 = 温度 * 10 = raw * 10 / 16
    */
    *temp10 = (int16_t)(((int32_t)raw * 10) / 16);

    return 1;
}

