#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ClearPage(uint8_t page);
void OLED_Fill(uint8_t data);
void OLED_ShowChar(uint8_t page, uint8_t column, char ch);
void OLED_ShowString(uint8_t page, uint8_t column, const char *str);
void OLED_ShowNum(uint8_t page, uint8_t column, uint16_t num, uint8_t len);

#endif
