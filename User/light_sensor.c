#include "light_sensor.h"

/*
   光敏模块接线：
   VCC -> 3.3V
   GND -> GND
   DO  -> PA1
   AO  -> PA0
。
*/

#define LIGHT_GPIO_PORT      GPIOA
#define LIGHT_GPIO_CLK       RCC_APB2Periph_GPIOA
#define LIGHT_DO_PIN         GPIO_Pin_1
#define LIGHT_AO_PIN         GPIO_Pin_0

/*
   不同模块 DO 逻辑可能相反。

   如果你发现：
   光线暗时绿灯亮，光线亮时红灯亮，
   就把这里的 Bit_RESET 改成 Bit_SET。
*/
#define LIGHT_DARK_LEVEL     Bit_SET

void LightSensor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(LIGHT_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = LIGHT_DO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LIGHT_GPIO_PORT, &GPIO_InitStructure);
}

uint8_t LightSensor_ReadDO(void)
{
    return GPIO_ReadInputDataBit(LIGHT_GPIO_PORT, LIGHT_DO_PIN);
}

uint8_t LightSensor_IsDark(void)
{
    if (LightSensor_ReadDO() == LIGHT_DARK_LEVEL)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void LightSensor_ADC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitStructure.GPIO_Pin = LIGHT_AO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;

    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));

    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

uint16_t LightSensor_ReadAO(void)
{
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);

    return ADC_GetConversionValue(ADC1);
}
