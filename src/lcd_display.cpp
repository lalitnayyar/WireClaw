/**
 * @file lcd_display.cpp
 * @brief Professional status UI on Waveshare ESP32-C6-LCD-1.47 (ST7789 172x320)
 */

#ifdef WIRECLAW_LCD_WS147

#include "lcd_display.h"
#include "st7789_ws147.h"
#include "rules.h"
#include <WiFi.h>
#include <time.h>
#include "version.h"
#if !defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/temperature_sensor.h"
#endif

extern char cfg_device_name[32];
extern char cfg_owner_name[48];
extern char cfg_owner_phone[20];
extern char cfg_model[64];
extern unsigned long g_last_llm_ms;
extern int           g_last_prompt_tokens;
extern int           g_last_completion_tokens;
extern unsigned long g_llm_call_count;
extern int           historyCount;
extern bool          g_telegram_enabled;
#if !defined(CONFIG_IDF_TARGET_ESP32)
extern temperature_sensor_handle_t g_temp_sensor;
#endif

static bool g_lcd_ok = false;
static unsigned long g_lcd_last_draw = 0;

#define LCD_UPDATE_MS        1000
#define LCD_ALERT_MS         8000
#define LCD_ALERT_REFRESH_MS 400
#define LCD_LINE_CHARS       26
#define LCD_DIV_W            120
#define LCD_STATUS_LH        10

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

static void lcdDrawDivider(int y) {
    int x = (WS147_LCD_W - LCD_DIV_W) / 2;
    ws147LcdDrawHLine(x, y, LCD_DIV_W, WS147_COLOR_ACCENT);
}

static void lcdDrawCard(int y, int h) {
    ws147LcdFillRect(4, y, WS147_LCD_W - 8, h, WS147_COLOR_PANEL);
    ws147LcdDrawHLine(4, y, WS147_LCD_W - 8, WS147_COLOR_ACCENT);
    ws147LcdDrawHLine(4, y + h - 1, WS147_LCD_W - 8, WS147_COLOR_ACCENT);
}

static void lcdFormatUptime(char *buf, int len) {
    unsigned long s = millis() / 1000;
    unsigned long d = s / 86400;
    unsigned long h = (s % 86400) / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    if (d > 0) {
        snprintf(buf, len, "%lud %02luh %02lum %02lus", d, h, m, sec);
    } else {
        snprintf(buf, len, "%02luh %02lum %02lus", h, m, sec);
    }
}

static void lcdDrawIdentityHeader() {
    lcdDrawDivider(4);
    ws147LcdDrawStringCentered(10, "WIRECLAW", WS147_COLOR_CYAN, WS147_COLOR_BLACK, 1);

    char ver[16];
    snprintf(ver, sizeof(ver), "V%s", WIRECLAW_VERSION);
    ws147LcdDrawStringCentered(20, ver, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);

    if (cfg_owner_name[0]) {
        ws147LcdDrawStringRainbowCentered(32, cfg_owner_name, 2);
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        char clock[16];
        strftime(clock, sizeof(clock), "%H:%M:%S", &timeinfo);
        ws147LcdDrawStringCentered(58, clock, WS147_COLOR_WHITE, WS147_COLOR_BLACK, 2);

        char date[20];
        strftime(date, sizeof(date), "%a %d %b %Y", &timeinfo);
        ws147LcdDrawStringCentered(78, date, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
    } else {
        ws147LcdDrawStringCentered(58, "--:--:--", WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 2);
        ws147LcdDrawStringCentered(78, "SYNCING TIME", WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
    }

    if (cfg_owner_phone[0]) {
        ws147LcdDrawStringCentered(92, cfg_owner_phone, WS147_COLOR_CYAN, WS147_COLOR_BLACK, 1);
    }

    lcdDrawDivider(104);
}

static void lcdDrawAlert() {
    ws147LcdFillScreen(WS147_COLOR_BLACK);
    lcdDrawIdentityHeader();

    int bannerY = 112;
    ws147LcdFillRect(8, bannerY, WS147_LCD_W - 16, 20,
                     g_alert_incoming ? WS147_COLOR_GREEN : WS147_COLOR_ORANGE);
    ws147LcdDrawStringCentered(bannerY + 6,
                               g_alert_incoming ? "TELEGRAM IN" : "TELEGRAM OUT",
                               WS147_COLOR_BLACK,
                               g_alert_incoming ? WS147_COLOR_GREEN : WS147_COLOR_ORANGE, 1);

    int y = bannerY + 28;
    char line[LCD_LINE_CHARS + 1];
    const char *p = g_alert_text;
    for (int row = 0; row < 7 && *p && y + 10 < WS147_LCD_H - 16; row++) {
        int w = 0;
        while (*p == ' ') p++;
        while (*p && w < LCD_LINE_CHARS) {
            line[w++] = *p++;
        }
        line[w] = '\0';
        if (w == 0) break;
        ws147LcdDrawStringCentered(y, line, WS147_COLOR_WHITE, WS147_COLOR_BLACK, 1);
        y += 11;
    }

    unsigned long left = (g_alert_until > millis()) ? (g_alert_until - millis()) / 1000 : 0;
    char footer[28];
    snprintf(footer, sizeof(footer), "RETURN IN %lus", left);
    ws147LcdDrawStringCentered(WS147_LCD_H - 14, footer, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
}

static int lcdRuleCount() {
    int count = 0;
    const Rule *rules = ruleGetAll();
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].used) count++;
    }
    return count;
}

static void lcdStatusLine(int *y, const char *text, uint16_t fg, uint16_t bg = WS147_COLOR_BLACK) {
    ws147LcdDrawStringCentered(*y, text, fg, bg, 1);
    *y += LCD_STATUS_LH;
}

static void lcdDrawStatus() {
    ws147LcdFillScreen(WS147_COLOR_BLACK);
    lcdDrawIdentityHeader();

    char buf[56];
    int y = 110;

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "WIFI OK (%s)", WiFi.localIP().toString().c_str());
        lcdStatusLine(&y, buf, WS147_COLOR_GREEN);
        snprintf(buf, sizeof(buf), "%ddBM  %s", WiFi.RSSI(), cfg_device_name);
        lcdStatusLine(&y, buf, WS147_COLOR_CYAN);
    } else {
        lcdStatusLine(&y, "WIFI CONNECTING...", WS147_COLOR_ORANGE);
    }

    snprintf(buf, sizeof(buf), "HEAP %u / %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getHeapSize());
    lcdStatusLine(&y, buf, WS147_COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "MIN %u", (unsigned)ESP.getMinFreeHeap());
    lcdStatusLine(&y, buf, WS147_COLOR_YELLOW);

    snprintf(buf, sizeof(buf), "CPU %uMHZ  FL %uKB",
             (unsigned)ESP.getCpuFreqMHz(),
             (unsigned)(ESP.getFlashChipSize() / 1024));
    lcdStatusLine(&y, buf, WS147_COLOR_WHITE);

    float chipTemp = 0.0f;
    bool haveTemp = false;
#if !defined(CONFIG_IDF_TARGET_ESP32)
    if (g_temp_sensor) {
        haveTemp = temperature_sensor_get_celsius(g_temp_sensor, &chipTemp) == ESP_OK;
    }
#endif
    if (haveTemp) {
        uint16_t tc = WS147_COLOR_GREEN;
        if (chipTemp >= 55.0f) tc = WS147_COLOR_RED;
        else if (chipTemp >= 45.0f) tc = WS147_COLOR_ORANGE;
        snprintf(buf, sizeof(buf), "TEMP %.1f C", chipTemp);
        lcdStatusLine(&y, buf, tc);
    }

    snprintf(buf, sizeof(buf), "HIST %d  LLM %lu",
             historyCount, g_llm_call_count);
    lcdStatusLine(&y, buf, WS147_COLOR_MAGENTA);

    snprintf(buf, sizeof(buf), "LAST %lums (%d+%d tok)",
             g_last_llm_ms, g_last_prompt_tokens, g_last_completion_tokens);
    lcdStatusLine(&y, buf, WS147_COLOR_MAGENTA);

    snprintf(buf, sizeof(buf), "RULES %d ACTIVE", lcdRuleCount());
    lcdStatusLine(&y, buf, WS147_COLOR_CYAN);

    snprintf(buf, sizeof(buf), "TG %s", g_telegram_enabled ? "ENABLED" : "OFF");
    lcdStatusLine(&y, buf, g_telegram_enabled ? WS147_COLOR_GREEN : WS147_COLOR_DARKGREY);

    unsigned long upSec = millis() / 1000;
    snprintf(buf, sizeof(buf), "UPTIME %lus", upSec);
    lcdStatusLine(&y, buf, WS147_COLOR_YELLOW);

    if (cfg_model[0] && y + LCD_STATUS_LH <= WS147_LCD_H) {
        lcdSanitize(cfg_model, buf, sizeof(buf));
        if (strlen(buf) > LCD_LINE_CHARS) {
            buf[LCD_LINE_CHARS - 1] = '\0';
        }
        lcdStatusLine(&y, buf, WS147_COLOR_DARKGREY);
    }
}

void lcdTelegramAlert(bool incoming, const char *text) {
    if (!g_lcd_ok || !text) return;
    lcdSanitize(text, g_alert_text, sizeof(g_alert_text));
    if (g_alert_text[0] == '\0') {
        strncpy(g_alert_text, "(EMPTY)", sizeof(g_alert_text) - 1);
    }
    g_alert_incoming = incoming;
    g_alert_until = millis() + LCD_ALERT_MS;
    g_lcd_last_draw = 0;
    lcdDrawAlert();
}

void lcdDisplayInit() {
    ws147LcdInit();
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

    if (alertActive) {
        lcdDrawAlert();
    } else {
        g_alert_text[0] = '\0';
        lcdDrawStatus();
    }
}

#endif /* WIRECLAW_LCD_WS147 */
