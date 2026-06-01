#include "joystick.h"
#include "timing.h"

#define JOY_SCAN_MS        10
#define JOY_DEBOUNCE_MS    30

/*
 * Debounced joystick driver.
 *
 * s_stable_state stores currently pressed directions. s_events stores only new
 * press edges, so the UI handles one action per physical press.
 */
static uint8_t s_last_sample = 0;
static uint8_t s_stable_state = 0;
static uint8_t s_events = 0;
static uint32_t s_last_scan_time = 0;
static uint32_t s_change_time = 0;

static uint8_t Joystick_ReadRaw(void)
{
    uint8_t state = 0;

    if (GPIO_ReadInputDataBit(JOY_UP_PORT, JOY_UP_PIN) == Bit_RESET)
    {
        state |= JOY_EVENT_UP;
    }

    if (GPIO_ReadInputDataBit(JOY_DOWN_PORT, JOY_DOWN_PIN) == Bit_RESET)
    {
        state |= JOY_EVENT_DOWN;
    }

    if (GPIO_ReadInputDataBit(JOY_LEFT_PORT, JOY_LEFT_PIN) == Bit_RESET)
    {
        state |= JOY_EVENT_LEFT;
    }

    if (GPIO_ReadInputDataBit(JOY_RIGHT_PORT, JOY_RIGHT_PIN) == Bit_RESET)
    {
        state |= JOY_EVENT_RIGHT;
    }

    if (GPIO_ReadInputDataBit(JOY_PRESS_PORT, JOY_PRESS_PIN) == Bit_RESET)
    {
        state |= JOY_EVENT_PRESS;
    }

    return state;
}

/* All joystick pins use pull-up input, so a pressed key reads Bit_RESET. */
static void Joystick_InitPin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &GPIO_InitStructure);
}

void Joystick_Init(void)
{
    RCC_APB2PeriphClockCmd(JOY_UP_CLK | JOY_DOWN_CLK | JOY_LEFT_CLK |
                           JOY_RIGHT_CLK | JOY_PRESS_CLK, ENABLE);

    Joystick_InitPin(JOY_UP_PORT, JOY_UP_PIN);
    Joystick_InitPin(JOY_DOWN_PORT, JOY_DOWN_PIN);
    Joystick_InitPin(JOY_LEFT_PORT, JOY_LEFT_PIN);
    Joystick_InitPin(JOY_RIGHT_PORT, JOY_RIGHT_PIN);
    Joystick_InitPin(JOY_PRESS_PORT, JOY_PRESS_PIN);

    s_last_sample = Joystick_ReadRaw();
    s_stable_state = s_last_sample;
    s_change_time = Timing_GetTick();
}

void Joystick_Task(void)
{
    uint32_t now;
    uint8_t raw;
    uint8_t changed;

    now = Timing_GetTick();

    if (now - s_last_scan_time < JOY_SCAN_MS)
    {
        return;
    }

    s_last_scan_time = now;
    raw = Joystick_ReadRaw();

    if (raw != s_last_sample)
    {
        /* Input changed; wait until it remains stable for debounce time. */
        s_last_sample = raw;
        s_change_time = now;
        return;
    }

    if ((now - s_change_time >= JOY_DEBOUNCE_MS) && (raw != s_stable_state))
    {
        changed = raw ^ s_stable_state;
        s_stable_state = raw;
        /* Only rising "pressed" edges become events. Releases are ignored. */
        s_events |= changed & raw;
    }
}

/* Read and clear accumulated press events for one main-loop cycle. */
uint8_t Joystick_GetEvents(void)
{
    uint8_t events;

    events = s_events;
    s_events = 0;

    return events;
}
