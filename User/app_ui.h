#ifndef __APP_UI_H
#define __APP_UI_H

#include "stm32f10x.h"

/* 初始化 OLED UI 状态，默认进入主菜单。 */
void App_UI_Init(void);

/*
 * UI 主任务。
 * 处理摇杆事件、数据变化刷新、光标闪烁和页面重绘。
 */
void App_UI_Task(void);

#endif
