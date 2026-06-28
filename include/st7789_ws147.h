/**
 * @file st7789_ws147.h
 * @brief Minimal ST7789 driver for Waveshare ESP32-C6-LCD-1.47 (172x320)
 */

#ifndef ST7789_WS147_H
#define ST7789_WS147_H

#include <Arduino.h>

#define WS147_LCD_W 172
#define WS147_LCD_H 320

#define WS147_COLOR_BLACK     0x0000
#define WS147_COLOR_WHITE     0xFFFF
#define WS147_COLOR_CYAN      0x07FF
#define WS147_COLOR_GREEN     0x07E0
#define WS147_COLOR_RED       0xF800
#define WS147_COLOR_YELLOW    0xFFE0
#define WS147_COLOR_ORANGE    0xFD20
#define WS147_COLOR_MAGENTA   0xF81F
#define WS147_COLOR_DARKGREY  0x4208

void ws147LcdInit();
void ws147LcdFillScreen(uint16_t color);
void ws147LcdFillRect(int x, int y, int w, int h, uint16_t color);
void ws147LcdDrawString(int x, int y, const char *text, uint16_t color, uint16_t bg, uint8_t scale);

#endif /* ST7789_WS147_H */
