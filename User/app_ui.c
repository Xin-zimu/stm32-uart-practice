#include "app_ui.h"
#include "oled.h"
#include "timing.h"
#include "joystick.h"
#include "app_light.h"
#include "app_temp.h"

#define UI_CURSOR_BLINK_MS      500
#define UI_THRESHOLD_STEP       50

typedef enum
{
    UI_SCREEN_MENU = 0,
    UI_SCREEN_STATUS,
    UI_SCREEN_THRESHOLD,
    UI_SCREEN_TRAFFIC
} UiScreen;

typedef enum
{
    UI_TRAFFIC_AUTO = 0,
    UI_TRAFFIC_RED,
    UI_TRAFFIC_YELLOW,
    UI_TRAFFIC_GREEN,
    UI_TRAFFIC_OFF
} UiTrafficIndex;

static UiScreen s_screen = UI_SCREEN_MENU;
static uint8_t s_menu_index = 0;
static UiTrafficIndex s_traffic_index = UI_TRAFFIC_AUTO;
static uint8_t s_cursor_visible = 1;
static uint8_t s_screen_dirty = 1;
static uint8_t s_status_dirty = 1;
static uint8_t s_cursor_dirty = 1;
static uint32_t s_last_cursor_time = 0;
static uint16_t s_last_light_version = 0;
static uint16_t s_last_temp_version = 0;

static UiTrafficIndex UI_ModeToTrafficIndex(AppLightTrafficMode mode)
{
    if (mode == APP_LIGHT_TRAFFIC_RED)
    {
        return UI_TRAFFIC_RED;
    }

    if (mode == APP_LIGHT_TRAFFIC_YELLOW)
    {
        return UI_TRAFFIC_YELLOW;
    }

    if (mode == APP_LIGHT_TRAFFIC_GREEN)
    {
        return UI_TRAFFIC_GREEN;
    }

    if (mode == APP_LIGHT_TRAFFIC_OFF)
    {
        return UI_TRAFFIC_OFF;
    }

    return UI_TRAFFIC_AUTO;
}

static AppLightTrafficMode UI_TrafficIndexToMode(UiTrafficIndex index)
{
    if (index == UI_TRAFFIC_RED)
    {
        return APP_LIGHT_TRAFFIC_RED;
    }

    if (index == UI_TRAFFIC_YELLOW)
    {
        return APP_LIGHT_TRAFFIC_YELLOW;
    }

    if (index == UI_TRAFFIC_GREEN)
    {
        return APP_LIGHT_TRAFFIC_GREEN;
    }

    if (index == UI_TRAFFIC_OFF)
    {
        return APP_LIGHT_TRAFFIC_OFF;
    }

    return APP_LIGHT_TRAFFIC_AUTO;
}

static void UI_UpdateDataVersions(void)
{
    uint16_t light_version;
    uint16_t temp_version;
    UiTrafficIndex traffic_index;

    light_version = App_Light_GetVersion();

    if (light_version != s_last_light_version)
    {
        s_last_light_version = light_version;
        s_status_dirty = 1;

        traffic_index = UI_ModeToTrafficIndex(App_Light_GetTrafficMode());

        if (traffic_index != s_traffic_index)
        {
            s_traffic_index = traffic_index;
            s_cursor_dirty = 1;
        }
    }

    temp_version = App_Temp_GetVersion();

    if (temp_version != s_last_temp_version)
    {
        s_last_temp_version = temp_version;
        s_status_dirty = 1;
    }
}

static void UI_ClearLine(uint8_t page)
{
    OLED_ClearPage(page);
}

static void UI_ShowValue4(uint8_t page, uint8_t column, uint16_t value)
{
    OLED_ShowString(page, column, "    ");
    OLED_ShowNum(page, column, value, 4);
}

static void UI_ShowTemp10(uint8_t page, uint8_t column)
{
    int16_t temp10;
    uint16_t value;
    uint16_t integer;
    uint8_t decimal;
    uint8_t pos;

    OLED_ShowString(page, column, "       ");

    if (App_Temp_IsValid() == 0)
    {
        OLED_ShowString(page, column, "----");
        return;
    }

    temp10 = App_Temp_GetTemp10();
    pos = column;

    if (temp10 < 0)
    {
        OLED_ShowChar(page, pos, '-');
        pos += 6;
        value = (uint16_t)(0 - temp10);
    }
    else
    {
        value = (uint16_t)temp10;
    }

    integer = value / 10;
    decimal = (uint8_t)(value % 10);

    if (integer >= 100)
    {
        OLED_ShowNum(page, pos, integer, 3);
        pos += 18;
    }
    else if (integer >= 10)
    {
        OLED_ShowNum(page, pos, integer, 2);
        pos += 12;
    }
    else
    {
        OLED_ShowNum(page, pos, integer, 1);
        pos += 6;
    }

    OLED_ShowChar(page, pos, '.');
    pos += 6;
    OLED_ShowNum(page, pos, decimal, 1);
    pos += 6;
    OLED_ShowChar(page, pos, 'C');
}

static void UI_RenderStatusArea(void)
{
    OLED_ShowString(0, 0, "LIGHT:");
    OLED_ShowString(0, 42, "      ");

    if (App_Light_IsDark())
    {
        OLED_ShowString(0, 42, "DARK");
    }
    else
    {
        OLED_ShowString(0, 42, "BRIGHT");
    }

    OLED_ShowString(1, 0, "AO:");
    UI_ShowValue4(1, 18, App_Light_GetAO());

    OLED_ShowString(2, 0, "TEMP:");
    UI_ShowTemp10(2, 30);
}

static void UI_RenderMenuItems(void)
{
    const char *items[3];
    uint8_t i;
    uint8_t page;

    items[0] = "STATUS";
    items[1] = "SET TH";
    items[2] = "TRAFFIC";

    for (i = 0; i < 3; i++)
    {
        page = 4 + i;
        UI_ClearLine(page);

        if ((i == s_menu_index) && s_cursor_visible)
        {
            OLED_ShowChar(page, 0, '>');
        }
        else
        {
            OLED_ShowChar(page, 0, ' ');
        }

        OLED_ShowString(page, 12, items[i]);
    }
}

static void UI_RenderMenuScreen(void)
{
    OLED_Clear();
    UI_RenderStatusArea();
    UI_RenderMenuItems();
}

static void UI_RenderStatusScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "STATUS");
    OLED_ShowString(2, 0, "LIGHT:");
    OLED_ShowString(3, 0, "AO:");
    OLED_ShowString(4, 0, "TEMP:");
    OLED_ShowString(7, 0, "PRESS BACK");
}

static void UI_RenderStatusPageData(void)
{
    OLED_ShowString(2, 42, "      ");

    if (App_Light_IsDark())
    {
        OLED_ShowString(2, 42, "DARK");
    }
    else
    {
        OLED_ShowString(2, 42, "BRIGHT");
    }

    UI_ShowValue4(3, 18, App_Light_GetAO());
    UI_ShowTemp10(4, 30);
}

static void UI_RenderThresholdScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "SET THRESH");
    OLED_ShowString(2, 0, "AO:");
    OLED_ShowString(3, 0, "TH:");
    OLED_ShowString(5, 0, "L:- R:+");
    OLED_ShowString(7, 0, "PRESS SAVE");
}

static void UI_RenderThresholdData(void)
{
    UI_ShowValue4(2, 18, App_Light_GetAO());
    UI_ShowValue4(3, 18, App_Light_GetThreshold());
}

static void UI_RenderTrafficItems(void)
{
    const char *items[5];
    uint8_t i;
    uint8_t page;

    items[UI_TRAFFIC_AUTO] = "AUTO";
    items[UI_TRAFFIC_RED] = "RED";
    items[UI_TRAFFIC_YELLOW] = "YELLOW";
    items[UI_TRAFFIC_GREEN] = "GREEN";
    items[UI_TRAFFIC_OFF] = "OFF";

    for (i = 0; i < 5; i++)
    {
        page = 2 + i;
        UI_ClearLine(page);

        if ((i == s_traffic_index) && s_cursor_visible)
        {
            OLED_ShowChar(page, 0, '>');
        }
        else
        {
            OLED_ShowChar(page, 0, ' ');
        }

        OLED_ShowString(page, 12, items[i]);
    }
}

static void UI_RenderTrafficScreen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "TRAFFIC");
    UI_RenderTrafficItems();
    OLED_ShowString(7, 0, "LEFT BACK");
}

static void UI_EnterScreen(UiScreen screen)
{
    s_screen = screen;
    s_cursor_visible = 1;
    s_screen_dirty = 1;
    s_status_dirty = 1;
    s_cursor_dirty = 1;

    if (screen == UI_SCREEN_TRAFFIC)
    {
        s_traffic_index = UI_ModeToTrafficIndex(App_Light_GetTrafficMode());
    }
}

static void UI_ApplyTrafficSelection(void)
{
    App_Light_SetTrafficMode(UI_TrafficIndexToMode(s_traffic_index));
}

static void UI_ProcessMenuEvents(uint8_t events)
{
    if ((events & JOY_EVENT_UP) && (s_menu_index > 0))
    {
        s_menu_index--;
        s_cursor_dirty = 1;
    }

    if ((events & JOY_EVENT_DOWN) && (s_menu_index < 2))
    {
        s_menu_index++;
        s_cursor_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        if (s_menu_index == 0)
        {
            UI_EnterScreen(UI_SCREEN_STATUS);
        }
        else if (s_menu_index == 1)
        {
            UI_EnterScreen(UI_SCREEN_THRESHOLD);
        }
        else
        {
            UI_EnterScreen(UI_SCREEN_TRAFFIC);
        }
    }
}

static void UI_ProcessThresholdEvents(uint8_t events)
{
    uint16_t threshold;

    threshold = App_Light_GetThreshold();

    if (events & JOY_EVENT_LEFT)
    {
        if (threshold > UI_THRESHOLD_STEP)
        {
            threshold -= UI_THRESHOLD_STEP;
        }

        App_Light_SetThreshold(threshold);
        s_status_dirty = 1;
    }

    if (events & JOY_EVENT_RIGHT)
    {
        threshold += UI_THRESHOLD_STEP;
        App_Light_SetThreshold(threshold);
        s_status_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        UI_EnterScreen(UI_SCREEN_MENU);
    }
}

static void UI_ProcessTrafficEvents(uint8_t events)
{
    if ((events & JOY_EVENT_UP) && (s_traffic_index > 0))
    {
        s_traffic_index--;
        s_cursor_dirty = 1;
    }

    if ((events & JOY_EVENT_DOWN) && (s_traffic_index < UI_TRAFFIC_OFF))
    {
        s_traffic_index++;
        s_cursor_dirty = 1;
    }

    if (events & JOY_EVENT_PRESS)
    {
        UI_ApplyTrafficSelection();
    }

    if (events & JOY_EVENT_LEFT)
    {
        UI_EnterScreen(UI_SCREEN_MENU);
    }
}

static void UI_ProcessEvents(uint8_t events)
{
    if (events == 0)
    {
        return;
    }

    if (s_screen == UI_SCREEN_MENU)
    {
        UI_ProcessMenuEvents(events);
    }
    else if (s_screen == UI_SCREEN_STATUS)
    {
        if ((events & JOY_EVENT_PRESS) || (events & JOY_EVENT_LEFT))
        {
            UI_EnterScreen(UI_SCREEN_MENU);
        }
    }
    else if (s_screen == UI_SCREEN_THRESHOLD)
    {
        UI_ProcessThresholdEvents(events);
    }
    else
    {
        UI_ProcessTrafficEvents(events);
    }
}

void App_UI_Init(void)
{
    s_last_cursor_time = Timing_GetTick();
    s_last_light_version = App_Light_GetVersion();
    s_last_temp_version = App_Temp_GetVersion();
    s_traffic_index = UI_ModeToTrafficIndex(App_Light_GetTrafficMode());
    UI_EnterScreen(UI_SCREEN_MENU);
}

void App_UI_Task(void)
{
    uint32_t now;
    uint8_t events;

    now = Timing_GetTick();
    events = Joystick_GetEvents();
    UI_ProcessEvents(events);
    UI_UpdateDataVersions();

    if (now - s_last_cursor_time >= UI_CURSOR_BLINK_MS)
    {
        s_last_cursor_time = now;
        s_cursor_visible = !s_cursor_visible;
        s_cursor_dirty = 1;
    }

    if (s_screen_dirty)
    {
        s_screen_dirty = 0;
        s_status_dirty = 1;
        s_cursor_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderMenuScreen();
        }
        else if (s_screen == UI_SCREEN_STATUS)
        {
            UI_RenderStatusScreen();
        }
        else if (s_screen == UI_SCREEN_THRESHOLD)
        {
            UI_RenderThresholdScreen();
        }
        else
        {
            UI_RenderTrafficScreen();
        }
    }

    if (s_status_dirty)
    {
        s_status_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderStatusArea();
        }
        else if (s_screen == UI_SCREEN_STATUS)
        {
            UI_RenderStatusPageData();
        }
        else if (s_screen == UI_SCREEN_THRESHOLD)
        {
            UI_RenderThresholdData();
        }
    }

    if (s_cursor_dirty)
    {
        s_cursor_dirty = 0;

        if (s_screen == UI_SCREEN_MENU)
        {
            UI_RenderMenuItems();
        }
        else if (s_screen == UI_SCREEN_TRAFFIC)
        {
            UI_RenderTrafficItems();
        }
    }
}
