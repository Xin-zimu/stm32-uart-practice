#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

/* SSD1306 128x64 OLED driver over hardware I2C1, using 8 page rows. */
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ClearPage(uint8_t page);
void OLED_Fill(uint8_t data);

/* Text drawing uses a compact 5x7 ASCII font; column is a pixel column. */
void OLED_ShowChar(uint8_t page, uint8_t column, char ch);
void OLED_ShowString(uint8_t page, uint8_t column, const char *str);
void OLED_ShowNum(uint8_t page, uint8_t column, uint16_t num, uint8_t len);

#endif
