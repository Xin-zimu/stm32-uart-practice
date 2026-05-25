#ifndef __TIMER3_H
#define __TIMER3_H

#include "stm32f10x.h"

// 函数声明
void TIM3_Init(u16 arr, u16 psc);  // TIM3定时中断初始化
// arr：自动重装值；psc：预分频系数；定时时间 = (arr+1)*(psc+1)/72MHz

#endif /* __TIMER3_H */

