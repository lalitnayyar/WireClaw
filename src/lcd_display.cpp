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
#include <math.h>
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
static unsigned long g_lcd_last_poll = 0;
static int g_uptime_y = 0;

#define LCD_POLL_MS          500
#define LCD_ALERT_MS         8000
#define LCD_ALERT_REFRESH_MS 1000
#define LCD_HEAP_DELTA       8192u
#define LCD_RSSI_DELTA       5
#define LCD_TEMP_DELTA       1.0f
#define LCD_LINE_CHARS       26
#define LCD_DIV_W            120
#define LCD_STATUS_LH        10
#define LCD_CLOCK_Y          58
#define LCD_CLOCK_H          16

struct LcdSnapshot {
    wl_status_t wifi_status;
    uint32_t    ip;
    int8_t      rssi;
    uint32_t    heap_free;
    uint32_t    heap_total;
    uint32_t    heap_min;
    uint16_t    cpu_mhz;
    uint32_t    flash_kb;
    float       temp;
    bool        have_temp;
    int         history;
    unsigned long llm_calls;
    unsigned long last_llm_ms;
    int         last_prompt_tok;
    int         last_comp_tok;
    int         rules;
    bool        tg_enabled;
    char        clock[16];
    char        date[20];
    char        uptime[24];
    char        model[LCD_LINE_CHARS + 1];
};

static LcdSnapshot g_last_drawn;
static bool g_have_last_drawn = false;

static char g_alert_text[128];
static bool g_alert_incoming = false;
static unsigned long g_alert_until = 0;
static bool g_was_alert = false;
static unsigned long g_last_alert_footer = 999;

static int lcdRuleCount() {
    int count = 0;
    const Rule *rules = ruleGetAll();
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].used) count++;
    }
    return count;
}

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

static void lcdCollectSnapshot(LcdSnapshot *s) {
    memset(s, 0, sizeof(*s));
    s->wifi_status = WiFi.status();
    if (s->wifi_status == WL_CONNECTED) {
        s->ip = (uint32_t)WiFi.localIP();
        s->rssi = (int8_t)WiFi.RSSI();
    }
    s->heap_free = ESP.getFreeHeap();
    s->heap_total = ESP.getHeapSize();
    s->heap_min = ESP.getMinFreeHeap();
    s->cpu_mhz = (uint16_t)ESP.getCpuFreqMHz();
    s->flash_kb = (uint32_t)(ESP.getFlashChipSize() / 1024);
#if !defined(CONFIG_IDF_TARGET_ESP32)
    if (g_temp_sensor) {
        s->have_temp = temperature_sensor_get_celsius(g_temp_sensor, &s->temp) == ESP_OK;
    }
#endif
    s->history = historyCount;
    s->llm_calls = g_llm_call_count;
    s->last_llm_ms = g_last_llm_ms;
    s->last_prompt_tok = g_last_prompt_tokens;
    s->last_comp_tok = g_last_completion_tokens;
    s->rules = lcdRuleCount();
    s->tg_enabled = g_telegram_enabled;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        strftime(s->clock, sizeof(s->clock), "%H:%M:%S", &timeinfo);
        strftime(s->date, sizeof(s->date), "%a %d %b %Y", &timeinfo);
    } else {
        strncpy(s->clock, "--:--:--", sizeof(s->clock) - 1);
        strncpy(s->date, "SYNCING TIME", sizeof(s->date) - 1);
    }

    snprintf(s->uptime, sizeof(s->uptime), "%lus", millis() / 1000);

    if (cfg_model[0]) {
        lcdSanitize(cfg_model, s->model, sizeof(s->model));
        if (strlen(s->model) > LCD_LINE_CHARS) {
            s->model[LCD_LINE_CHARS - 1] = '\0';
        }
    }
}

static bool lcdMetricsChanged(const LcdSnapshot *cur) {
    if (!g_have_last_drawn) return true;

    if (cur->wifi_status != g_last_drawn.wifi_status) return true;
    if (cur->ip != g_last_drawn.ip) return true;
    if (abs((int)cur->rssi - (int)g_last_drawn.rssi) >= LCD_RSSI_DELTA) return true;

    if (cur->heap_min != g_last_drawn.heap_min) return true;
    if (cur->heap_free > g_last_drawn.heap_free) {
        if (cur->heap_free - g_last_drawn.heap_free >= LCD_HEAP_DELTA) return true;
    } else if (g_last_drawn.heap_free - cur->heap_free >= LCD_HEAP_DELTA) {
        return true;
    }
    if (cur->heap_total != g_last_drawn.heap_total) return true;

    if (cur->cpu_mhz != g_last_drawn.cpu_mhz) return true;
    if (cur->flash_kb != g_last_drawn.flash_kb) return true;

    if (cur->have_temp != g_last_drawn.have_temp) return true;
    if (cur->have_temp &&
        fabsf(cur->temp - g_last_drawn.temp) >= LCD_TEMP_DELTA) {
        return true;
    }

    if (cur->history != g_last_drawn.history) return true;
    if (cur->llm_calls != g_last_drawn.llm_calls) return true;
    if (cur->last_llm_ms != g_last_drawn.last_llm_ms) return true;
    if (cur->last_prompt_tok != g_last_drawn.last_prompt_tok) return true;
    if (cur->last_comp_tok != g_last_drawn.last_comp_tok) return true;
    if (cur->rules != g_last_drawn.rules) return true;
    if (cur->tg_enabled != g_last_drawn.tg_enabled) return true;
    if (strcmp(cur->date, g_last_drawn.date) != 0) return true;
    if (strcmp(cur->model, g_last_drawn.model) != 0) return true;

    return false;
}

static bool lcdClockChanged(const LcdSnapshot *cur) {
    return !g_have_last_drawn || strcmp(cur->clock, g_last_drawn.clock) != 0;
}

static bool lcdUptimeChanged(const LcdSnapshot *cur) {
    return !g_have_last_drawn || strcmp(cur->uptime, g_last_drawn.uptime) != 0;
}

static void lcdUpdateClockPartial(const char *clock) {
    ws147LcdFillRect(0, LCD_CLOCK_Y, WS147_LCD_W, LCD_CLOCK_H, WS147_COLOR_BLACK);
    ws147LcdDrawStringCentered(LCD_CLOCK_Y, clock, WS147_COLOR_WHITE, WS147_COLOR_BLACK, 2);
}

static void lcdUpdateDatePartial(const char *date) {
    ws147LcdFillRect(0, 76, WS147_LCD_W, 10, WS147_COLOR_BLACK);
    ws147LcdDrawStringCentered(78, date, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
}

static void lcdUpdateUptimePartial(const char *uptime, uint16_t color) {
    if (g_uptime_y <= 0) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "UPTIME %s", uptime);
    ws147LcdFillRect(0, g_uptime_y, WS147_LCD_W, LCD_STATUS_LH, WS147_COLOR_BLACK);
    ws147LcdDrawStringCentered(g_uptime_y, buf, color, WS147_COLOR_BLACK, 1);
}

static void lcdRememberSnapshot(const LcdSnapshot *s) {
    g_last_drawn = *s;
    g_have_last_drawn = true;
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
    g_last_alert_footer = left;
    char footer[28];
    snprintf(footer, sizeof(footer), "RETURN IN %lus", left);
    ws147LcdDrawStringCentered(WS147_LCD_H - 14, footer, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
}

static void lcdUpdateAlertFooter() {
    unsigned long left = (g_alert_until > millis()) ? (g_alert_until - millis()) / 1000 : 0;
    if (left == g_last_alert_footer) return;
    g_last_alert_footer = left;
    char footer[28];
    snprintf(footer, sizeof(footer), "RETURN IN %lus", left);
    ws147LcdFillRect(0, WS147_LCD_H - 16, WS147_LCD_W, 12, WS147_COLOR_BLACK);
    ws147LcdDrawStringCentered(WS147_LCD_H - 14, footer, WS147_COLOR_DARKGREY, WS147_COLOR_BLACK, 1);
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
    g_uptime_y = y;
    snprintf(buf, sizeof(buf), "UPTIME %lus", upSec);
    lcdStatusLine(&y, buf, WS147_COLOR_YELLOW);

    if (cfg_model[0] && y + LCD_STATUS_LH <= WS147_LCD_H) {
        lcdSanitize(cfg_model, buf, sizeof(buf));
        if (strlen(buf) > LCD_LINE_CHARS) {
            buf[LCD_LINE_CHARS - 1] = '\0';
        }
        lcdStatusLine(&y, buf, WS147_COLOR_DARKGREY);
    }

    LcdSnapshot snap;
    lcdCollectSnapshot(&snap);
    lcdRememberSnapshot(&snap);
}

void lcdTelegramAlert(bool incoming, const char *text) {
    if (!g_lcd_ok || !text) return;
    lcdSanitize(text, g_alert_text, sizeof(g_alert_text));
    if (g_alert_text[0] == '\0') {
        strncpy(g_alert_text, "(EMPTY)", sizeof(g_alert_text) - 1);
    }
    g_alert_incoming = incoming;
    g_alert_until = millis() + LCD_ALERT_MS;
    g_was_alert = false;
    g_last_alert_footer = 999;
    g_lcd_last_poll = 0;
    lcdDrawAlert();
}

void lcdDisplayInit() {
    ws147LcdInit();
    g_lcd_ok = true;
    g_lcd_last_poll = 0;
    g_have_last_drawn = false;
    g_was_alert = false;
    lcdDisplayUpdate();
}

bool lcdDisplayReady() {
    return g_lcd_ok;
}

void lcdDisplayUpdate() {
    if (!g_lcd_ok) return;

    unsigned long now = millis();
    if (g_lcd_last_poll != 0 && (now - g_lcd_last_poll) < LCD_POLL_MS) return;
    g_lcd_last_poll = now;

    bool alertActive = (g_alert_until > now);
    if (alertActive) {
        if (!g_was_alert) {
            lcdDrawAlert();
            g_was_alert = true;
        } else {
            lcdUpdateAlertFooter();
        }
        return;
    }

    if (g_was_alert) {
        g_was_alert = false;
        g_have_last_drawn = false;
        g_alert_text[0] = '\0';
    }

    LcdSnapshot cur;
    lcdCollectSnapshot(&cur);

    if (lcdMetricsChanged(&cur)) {
        lcdDrawStatus();
        return;
    }

    bool partial = false;
    if (lcdClockChanged(&cur)) {
        lcdUpdateClockPartial(cur.clock);
        partial = true;
    }
    if (strcmp(cur.date, g_last_drawn.date) != 0) {
        lcdUpdateDatePartial(cur.date);
        partial = true;
    }
    if (lcdUptimeChanged(&cur)) {
        lcdUpdateUptimePartial(cur.uptime, WS147_COLOR_YELLOW);
        partial = true;
    }

    if (partial) {
        strncpy(g_last_drawn.clock, cur.clock, sizeof(g_last_drawn.clock) - 1);
        g_last_drawn.clock[sizeof(g_last_drawn.clock) - 1] = '\0';
        strncpy(g_last_drawn.date, cur.date, sizeof(g_last_drawn.date) - 1);
        g_last_drawn.date[sizeof(g_last_drawn.date) - 1] = '\0';
        strncpy(g_last_drawn.uptime, cur.uptime, sizeof(g_last_drawn.uptime) - 1);
        g_last_drawn.uptime[sizeof(g_last_drawn.uptime) - 1] = '\0';
    }
}

#endif /* WIRECLAW_LCD_WS147 */
