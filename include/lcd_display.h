/**
 * @file lcd_display.h
 * @brief Onboard ST7789 display (Waveshare ESP32-C6-LCD-1.47)
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#ifdef WIRECLAW_LCD_WS147
void lcdDisplayInit();
void lcdDisplayUpdate();
bool lcdDisplayReady();
/** Show Telegram alert on LCD for ~8s (incoming=true: received, false: sent). */
void lcdTelegramAlert(bool incoming, const char *text);
#else
static inline void lcdDisplayInit() {}
static inline void lcdDisplayUpdate() {}
static inline bool lcdDisplayReady() { return false; }
static inline void lcdTelegramAlert(bool incoming, const char *text) {
    (void)incoming;
    (void)text;
}
#endif

#endif /* LCD_DISPLAY_H */
