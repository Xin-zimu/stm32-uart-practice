#ifndef __APP_PROTOCOL_PRACTICE_H
#define __APP_PROTOCOL_PRACTICE_H

#include "stm32f10x.h"

/*
 * Binary protocol practice module.
 *
 * The main-loop RX dispatcher should call App_ProtocolPractice_ReceiveByte()
 * once for every queued byte, then call App_ProtocolPractice_Task() regularly.
 */
#define APP_PROTOCOL_FEED_TEXT          0u      // Byte does not belong to a binary frame
#define APP_PROTOCOL_FEED_CONSUMED      1u      // Byte was consumed and the frame continues
#define APP_PROTOCOL_FEED_FRAME_END     2u      // A complete or rejected frame attempt ended

void App_ProtocolPractice_Init(void);
uint8_t App_ProtocolPractice_ReceiveByte(uint8_t byte);
void App_ProtocolPractice_Task(void);

uint8_t App_ProtocolPractice_GetQueueCount(void);
uint16_t App_ProtocolPractice_GetFrameDropCount(void);
uint16_t App_ProtocolPractice_GetRxOkCount(void);
uint16_t App_ProtocolPractice_GetRxErrorCount(void);
uint16_t App_ProtocolPractice_GetRxTimeoutCount(void);

/* The counters above are useful for OLED pages, debugging, or protocol tests. */

#endif
