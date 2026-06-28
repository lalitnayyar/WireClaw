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
#else
static inline void lcdDisplayInit() {}
static inline void lcdDisplayUpdate() {}
static inline bool lcdDisplayReady() { return false; }
#endif

#endif /* LCD_DISPLAY_H */
