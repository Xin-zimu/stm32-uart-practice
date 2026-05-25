#include "timer3.h"
#include "led.h"

/**
 * @brief  初始化TIM3定时中断（72MHz系统时钟）
 * @param  arr: 自动重装值
 * @param  psc: 预分频系数
 * @retval 无
 */
void TIM3_Init(u16 arr, u16 psc)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能TIM3时钟（APB1总线时钟）
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    // 2. 配置TIM3基本参数
    TIM_TimeBaseStructure.TIM_Period = arr;         // 自动重装值
    TIM_TimeBaseStructure.TIM_Prescaler = psc;      // 预分频系数
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;    // 时钟分频因子
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数模式
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    
    // 3. 使能TIM3更新中断（溢出中断）
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    
    // 4. 配置NVIC中断优先级
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn; // TIM3中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级0
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;        // 子优先级3
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           // 使能该中断通道
    NVIC_Init(&NVIC_InitStructure);
    
    // 5. 启动TIM3定时器
    TIM_Cmd(TIM3, ENABLE);
}

/**
 * @brief  TIM3中断服务函数（自动触发，无需手动调用）
 * @param  无
 * @retval 无
 */
void TIM3_IRQHandler(void)
{
    static u16 cnt = 0;  // 静态变量，记录中断次数（断电才清零）
    
    // 检查TIM3更新中断标志位是否置1（防止误触发）
    if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        cnt++;  // 每次中断，计数+1
        if(cnt >= 1000)  // 累计1000次中断 = 1秒（因为TIM3配置为1ms中断）
        {
            cnt = 0;          // 计数清零
            LED_Toggle();     // 翻转LED电平（亮/灭切换）
        }
        
        // 清除中断标志位（核心！否则会无限触发中断）
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

