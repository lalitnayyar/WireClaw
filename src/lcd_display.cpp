/**
 * @file lcd_display.cpp
 * @brief Status + benchmarks on Waveshare ESP32-C6-LCD-1.47 (ST7789 172x320)
 */

#ifdef WIRECLAW_LCD_WS147

#include "lcd_display.h"
#include "st7789_ws147.h"
#include <WiFi.h>
#include "version.h"
#if !defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/temperature_sensor.h"
#endif

extern char cfg_device_name[32];
extern char cfg_owner_name[48];
extern char cfg_model[64];
extern unsigned long g_last_llm_ms;
extern int           g_last_prompt_tokens;
extern int           g_last_completion_tokens;
extern unsigned long g_llm_call_count;
extern int           historyCount;
#if !defined(CONFIG_IDF_TARGET_ESP32)
extern temperature_sensor_handle_t g_temp_sensor;
#endif

static bool g_lcd_ok = false;
static unsigned long g_lcd_last_draw = 0;

#define LCD_UPDATE_MS    2000
#define LCD_ALERT_MS     8000
#define LCD_ALERT_REFRESH_MS 400
#define LCD_HEADER_H     36
#define LCD_LINE_CHARS   26

static char g_alert_text[128];
static bool g_alert_incoming = false;
static unsigned long g_alert_until = 0;

static void lcdSanitize(const char *src, char *dst, int dst_len) {
    int w = 0;
    for (int i = 0; src[i] && w < dst_len - 1; i++) {
        char c = src[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if ((uint8_t)c < 0x20) continue;
        dst[w++] = c;
    }
    dst[w] = '\0';
}

static void lcdDrawHeader() {
    ws147LcdFillRect(0, 0, WS147_LCD_W, LCD_HEADER_H, WS147_COLOR_BLACK);
    ws147LcdDrawString(4, 2, "WireClaw", WS147_COLOR_CYAN, WS147_COLOR_BLACK, 1);
    ws147LcdDrawString(118, 2, WIRECLAW_VERSION, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
    if (cfg_owner_name[0]) {
        ws147LcdDrawStringRainbow(4, 14, cfg_owner_name, 2);
    }
}

static void lcdRow(int y, const char *label, const char *value, uint16_t labelColor) {
    ws147LcdDrawString(4, y, label, labelColor, WS147_COLOR_BLACK, 1);
    ws147LcdDrawString(4, y + 9, value, WS147_COLOR_WHITE, WS147_COLOR_BLACK, 1);
}

static void lcdDrawAlert() {
    ws147LcdFillRect(0, LCD_HEADER_H, WS147_LCD_W, WS147_LCD_H - LCD_HEADER_H,
                     WS147_COLOR_BLACK);
    ws147LcdFillRect(0, LCD_HEADER_H, WS147_LCD_W, 22,
                     g_alert_incoming ? WS147_COLOR_GREEN : WS147_COLOR_ORANGE);
    ws147LcdDrawString(4, LCD_HEADER_H + 4,
                       g_alert_incoming ? "TELEGRAM IN" : "TELEGRAM OUT",
                       WS147_COLOR_BLACK, g_alert_incoming ? WS147_COLOR_GREEN : WS147_COLOR_ORANGE, 1);

    int y = LCD_HEADER_H + 28;
    char line[LCD_LINE_CHARS + 1];
    const char *p = g_alert_text;
    for (int row = 0; row < 8 && *p && y + 10 < WS147_LCD_H; row++) {
        int w = 0;
        while (*p == ' ') p++;
        while (*p && w < LCD_LINE_CHARS) {
            line[w++] = *p++;
        }
        line[w] = '\0';
        if (w == 0) break;
        ws147LcdDrawString(4, y, line, WS147_COLOR_WHITE, WS147_COLOR_BLACK, 1);
        y += 11;
    }

    unsigned long left = (g_alert_until > millis()) ? (g_alert_until - millis()) / 1000 : 0;
    char footer[24];
    snprintf(footer, sizeof(footer), "back in %lus", left);
    ws147LcdDrawString(4, WS147_LCD_H - 12, footer, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
}

static void lcdDrawStatus() {
    ws147LcdFillRect(0, LCD_HEADER_H, WS147_LCD_W, WS147_LCD_H - LCD_HEADER_H, WS147_COLOR_BLACK);

    char buf[40];
    int y = LCD_HEADER_H + 2;

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(buf, sizeof(buf), "connecting...");
    }
    lcdRow(y, "IP", buf, WS147_COLOR_CYAN);
    y += 20;

    snprintf(buf, sizeof(buf), "%u / %u KB",
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getHeapSize() / 1024));
    lcdRow(y, "Heap", buf, WS147_COLOR_YELLOW);
    y += 20;

    snprintf(buf, sizeof(buf), "%u KB min", (unsigned)(ESP.getMinFreeHeap() / 1024));
    lcdRow(y, "Min heap", buf, WS147_COLOR_YELLOW);
    y += 20;

#if !defined(CONFIG_IDF_TARGET_ESP32)
    if (g_temp_sensor) {
        float temp = 0.0f;
        if (temperature_sensor_get_celsius(g_temp_sensor, &temp) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%.1f C", temp);
            uint16_t tc = WS147_COLOR_GREEN;
            if (temp >= 55.0f) tc = WS147_COLOR_RED;
            else if (temp >= 45.0f) tc = WS147_COLOR_ORANGE;
            lcdRow(y, "Chip temp", buf, tc);
            y += 20;
        }
    }
#endif

    snprintf(buf, sizeof(buf), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
    lcdRow(y, "CPU", buf, WS147_COLOR_WHITE);
    y += 20;

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s (%ddBm)", cfg_device_name, WiFi.RSSI());
    } else {
        snprintf(buf, sizeof(buf), "%s", cfg_device_name);
    }
    lcdRow(y, "Device", buf, WS147_COLOR_GREEN);
    y += 20;

    unsigned long uptime = millis() / 1000;
    snprintf(buf, sizeof(buf), "%luh %lum %lus",
             (uptime % 86400) / 3600, (uptime % 3600) / 60, uptime % 60);
    lcdRow(y, "Uptime", buf, WS147_COLOR_DARKGREY);
    y += 20;

    snprintf(buf, sizeof(buf), "%lu (%d+%d tok)",
             g_last_llm_ms, g_last_prompt_tokens, g_last_completion_tokens);
    lcdRow(y, "Last LLM", buf, WS147_COLOR_MAGENTA);
    y += 20;

    snprintf(buf, sizeof(buf), "%lu calls, %d hist",
             g_llm_call_count, historyCount);
    lcdRow(y, "LLM", buf, WS147_COLOR_MAGENTA);
    y += 20;

    if (y + 18 < WS147_LCD_H) {
        lcdRow(y, "Model", cfg_model, WS147_COLOR_DARKGREY);
    }
}

void lcdTelegramAlert(bool incoming, const char *text) {
    if (!g_lcd_ok || !text) return;
    lcdSanitize(text, g_alert_text, sizeof(g_alert_text));
    if (g_alert_text[0] == '\0') {
        strncpy(g_alert_text, "(empty)", sizeof(g_alert_text) - 1);
    }
    g_alert_incoming = incoming;
    g_alert_until = millis() + LCD_ALERT_MS;
    g_lcd_last_draw = 0;
    lcdDrawHeader();
    lcdDrawAlert();
}

void lcdDisplayInit() {
    ws147LcdInit();
    lcdDrawHeader();
    g_lcd_ok = true;
    g_lcd_last_draw = 0;
    lcdDisplayUpdate();
}

bool lcdDisplayReady() {
    return g_lcd_ok;
}

void lcdDisplayUpdate() {
    if (!g_lcd_ok) return;

    unsigned long now = millis();
    bool alertActive = (g_alert_until > now);

    unsigned long interval = alertActive ? LCD_ALERT_REFRESH_MS : LCD_UPDATE_MS;
    if (g_lcd_last_draw != 0 && (now - g_lcd_last_draw) < interval) return;
    g_lcd_last_draw = now;

    lcdDrawHeader();
    if (alertActive) {
        lcdDrawAlert();
    } else {
        g_alert_text[0] = '\0';
        lcdDrawStatus();
    }
}

#endif /* WIRECLAW_LCD_WS147 */
