/**
 * @file st7789_ws147.cpp
 * @brief Minimal ST7789 SPI driver — Waveshare ESP32-C6-LCD-1.47
 *
 * Pins: MOSI=6, SCLK=7, CS=14, DC=15, RST=21, BL=22
 * Panel: 172x320 visible area (240x320 ST7789 with column offset 35)
 */

#ifdef WIRECLAW_LCD_WS147

#include "st7789_ws147.h"
#include <SPI.h>

#define PIN_MOSI 6
#define PIN_SCLK 7
#define PIN_CS   14
#define PIN_DC   15
#define PIN_RST  21
#define PIN_BL   22

#define COL_OFFSET 35
#define ROW_OFFSET 0

static SPIClass lcdSpi(FSPI);

static void writeCmd(uint8_t cmd) {
    digitalWrite(PIN_DC, LOW);
    digitalWrite(PIN_CS, LOW);
    lcdSpi.transfer(cmd);
    digitalWrite(PIN_CS, HIGH);
}

static void writeData(uint8_t data) {
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_CS, LOW);
    lcdSpi.transfer(data);
    digitalWrite(PIN_CS, HIGH);
}

static void writeData16(uint16_t data) {
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_CS, LOW);
    lcdSpi.transfer16(data);
    digitalWrite(PIN_CS, HIGH);
}

static void setWindow(int x, int y, int w, int h) {
    int x0 = x + COL_OFFSET;
    int x1 = x + w - 1 + COL_OFFSET;
    int y0 = y + ROW_OFFSET;
    int y1 = y + h - 1 + ROW_OFFSET;

    writeCmd(0x2A);
    writeData16((uint16_t)x0);
    writeData16((uint16_t)x1);
    writeCmd(0x2B);
    writeData16((uint16_t)y0);
    writeData16((uint16_t)y1);
    writeCmd(0x2C);
}

static void hwReset() {
    digitalWrite(PIN_RST, LOW);
    delay(10);
    digitalWrite(PIN_RST, HIGH);
    delay(120);
}

void ws147LcdInit() {
    pinMode(PIN_CS, OUTPUT);
    pinMode(PIN_DC, OUTPUT);
    pinMode(PIN_RST, OUTPUT);
    pinMode(PIN_BL, OUTPUT);

    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_BL, HIGH);

    lcdSpi.begin(PIN_SCLK, -1, PIN_MOSI, PIN_CS);
    lcdSpi.setFrequency(80000000);
    lcdSpi.setDataMode(SPI_MODE0);

    hwReset();

    writeCmd(0x01); /* SWRESET */
    delay(150);

    writeCmd(0x11); /* SLPOUT */
    delay(120);

    writeCmd(0x3A);
    writeData(0x55); /* 16-bit */

    writeCmd(0x36);
    writeData(0x00); /* portrait RGB */

    writeCmd(0x21); /* INVON — matches Waveshare TFT_INVERSION_ON */

    writeCmd(0x13); /* NORON */
    delay(10);

    writeCmd(0x29); /* DISPON */
    delay(120);

    ws147LcdFillScreen(WS147_COLOR_BLACK);
}

void ws147LcdFillScreen(uint16_t color) {
    ws147LcdFillRect(0, 0, WS147_LCD_W, WS147_LCD_H, color);
}

void ws147LcdFillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WS147_LCD_W) w = WS147_LCD_W - x;
    if (y + h > WS147_LCD_H) h = WS147_LCD_H - y;
    if (w <= 0 || h <= 0) return;

    setWindow(x, y, w, h);

    digitalWrite(PIN_DC, HIGH);
    digitalWrite(PIN_CS, LOW);
    int pixels = w * h;
    while (pixels-- > 0) {
        lcdSpi.transfer16(color);
    }
    digitalWrite(PIN_CS, HIGH);
}

/* 5x7 ASCII font, 96 chars from space (32) */
static const uint8_t font5x7[] PROGMEM = {
    0x00,0x00,0x00,0x00,0x00, /* space */
    0x00,0x00,0x5F,0x00,0x00, /* ! */
    0x00,0x07,0x00,0x07,0x00, /* " */
    0x14,0x7F,0x14,0x7F,0x14, /* # */
    0x24,0x2A,0x7F,0x2A,0x12, /* $ */
    0x23,0x13,0x08,0x64,0x62, /* % */
    0x36,0x49,0x55,0x22,0x50, /* & */
    0x00,0x05,0x03,0x00,0x00, /* ' */
    0x00,0x1C,0x22,0x41,0x00, /* ( */
    0x00,0x41,0x22,0x1C,0x00, /* ) */
    0x14,0x08,0x3E,0x08,0x14, /* * */
    0x08,0x08,0x3E,0x08,0x08, /* + */
    0x00,0x50,0x30,0x00,0x00, /* , */
    0x08,0x08,0x08,0x08,0x08, /* - */
    0x00,0x60,0x60,0x00,0x00, /* . */
    0x20,0x10,0x08,0x04,0x02, /* / */
    0x3E,0x51,0x49,0x45,0x3E, /* 0 */
    0x00,0x42,0x7F,0x40,0x00, /* 1 */
    0x42,0x61,0x51,0x49,0x46, /* 2 */
    0x21,0x41,0x45,0x4B,0x31, /* 3 */
    0x18,0x14,0x12,0x7F,0x10, /* 4 */
    0x27,0x45,0x45,0x45,0x39, /* 5 */
    0x3C,0x4A,0x49,0x49,0x30, /* 6 */
    0x01,0x71,0x09,0x05,0x03, /* 7 */
    0x36,0x49,0x49,0x49,0x36, /* 8 */
    0x06,0x49,0x49,0x29,0x1E, /* 9 */
    0x00,0x36,0x36,0x00,0x00, /* : */
    0x00,0x56,0x36,0x00,0x00, /* ; */
    0x08,0x14,0x22,0x41,0x00, /* < */
    0x14,0x14,0x14,0x14,0x14, /* = */
    0x00,0x41,0x22,0x14,0x08, /* > */
    0x02,0x01,0x51,0x09,0x06, /* ? */
    0x32,0x49,0x79,0x41,0x3E, /* @ */
    0x7E,0x11,0x11,0x11,0x7E, /* A */
    0x7F,0x49,0x49,0x49,0x36, /* B */
    0x3E,0x41,0x41,0x41,0x22, /* C */
    0x7F,0x41,0x41,0x22,0x1C, /* D */
    0x7F,0x49,0x49,0x49,0x41, /* E */
    0x7F,0x09,0x09,0x09,0x01, /* F */
    0x3E,0x41,0x49,0x49,0x7A, /* G */
    0x7F,0x08,0x08,0x08,0x7F, /* H */
    0x00,0x41,0x7F,0x41,0x00, /* I */
    0x20,0x40,0x41,0x3F,0x01, /* J */
    0x7F,0x08,0x14,0x22,0x41, /* K */
    0x7F,0x40,0x40,0x40,0x40, /* L */
    0x7F,0x02,0x0C,0x02,0x7F, /* M */
    0x7F,0x04,0x08,0x10,0x7F, /* N */
    0x3E,0x41,0x41,0x41,0x3E, /* O */
    0x7F,0x09,0x09,0x09,0x06, /* P */
    0x3E,0x41,0x51,0x21,0x5E, /* Q */
    0x7F,0x09,0x19,0x29,0x46, /* R */
    0x46,0x49,0x49,0x49,0x31, /* S */
    0x01,0x01,0x7F,0x01,0x01, /* T */
    0x3F,0x40,0x40,0x40,0x3F, /* U */
    0x1F,0x20,0x40,0x20,0x1F, /* V */
    0x3F,0x40,0x38,0x40,0x3F, /* W */
    0x63,0x14,0x08,0x14,0x63, /* X */
    0x07,0x08,0x70,0x08,0x07, /* Y */
    0x61,0x51,0x49,0x45,0x43, /* Z */
};

static void drawChar5x7(int x, int y, char c, uint16_t color, uint16_t bg, uint8_t scale) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 32 || c > 90) c = '?';
    const uint8_t *glyph = &font5x7[(c - 32) * 5];

    for (int col = 0; col < 5; col++) {
        uint8_t line = pgm_read_byte(&glyph[col]);
        for (int row = 0; row < 7; row++) {
            uint16_t px = (line & (1 << row)) ? color : bg;
            if (scale == 1) {
                ws147LcdFillRect(x + col, y + row, 1, 1, px);
            } else {
                ws147LcdFillRect(x + col * scale, y + row * scale, scale, scale, px);
            }
        }
    }
}

void ws147LcdDrawString(int x, int y, const char *text, uint16_t color, uint16_t bg, uint8_t scale) {
    if (!text) return;
    int cx = x;
    int adv = (5 + 1) * scale;
    while (*text) {
        if (*text == '\n') {
            y += 8 * scale;
            cx = x;
            text++;
            continue;
        }
        drawChar5x7(cx, y, *text, color, bg, scale);
        cx += adv;
        if (cx > WS147_LCD_W - adv) break;
        text++;
    }
}

void ws147LcdDrawStringRainbow(int x, int y, const char *text, uint8_t scale) {
    if (!text) return;
    static const uint16_t colors[] = {
        WS147_COLOR_RED, WS147_COLOR_ORANGE, WS147_COLOR_YELLOW,
        WS147_COLOR_GREEN, WS147_COLOR_CYAN, WS147_COLOR_MAGENTA,
        WS147_COLOR_WHITE
    };
    const int ncolors = (int)(sizeof(colors) / sizeof(colors[0]));
    int cx = x;
    int adv = (5 + 1) * scale;
    int ci = 0;
    while (*text) {
        if (*text == ' ') {
            cx += adv;
            text++;
            continue;
        }
        drawChar5x7(cx, y, *text, colors[ci % ncolors], WS147_COLOR_BLACK, scale);
        ci++;
        cx += adv;
        if (cx > WS147_LCD_W - adv) break;
        text++;
    }
}

#endif /* WIRECLAW_LCD_WS147 */
