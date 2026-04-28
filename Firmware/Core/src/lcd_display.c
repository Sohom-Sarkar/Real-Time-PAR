/*
 * lcd_display.c  —  PAR LCD display driver
 *
 * Uses the STM32F429I-DISC1 BSP LCD API.
 * The BSP headers are in:
 *   Drivers/BSP/STM32F429I-Discovery/stm32f429i_discovery_lcd.h
 */
#include "lcd_display.h"
#include "par_pipeline.h"

/* BSP LCD headers — CubeIDE adds these when the BSP is enabled */
#include "stm32f429i_discovery_lcd.h"

#include <stdio.h>
#include <string.h>

/* ── Layout constants (pixels) ─────────────────────────────────────────── */
#define LCD_W   240
#define LCD_H   320

#define Y_TITLE    8
#define Y_ACTIVITY 80    /* centre of large activity text */
#define Y_CONF_LBL 130
#define Y_CONF_BAR 148
#define Y_SENSOR   185
#define Y_COUNTERS 230
#define Y_STATUS   260

#define BAR_X   10
#define BAR_W  220
#define BAR_H   14

/* Confidence bar colours: green (high) → orange → red (low) */
#define COL_HIGH    LCD_COLOR_GREEN
#define COL_MED     0xFFFFA500U   /* orange */
#define COL_LOW     LCD_COLOR_RED
#define COL_BG      0xFF1C1C1CU   /* dark grey background */
#define COL_TEXT    LCD_COLOR_WHITE
#define COL_TITLE   0xFF00BFFFU   /* cyan */
#define COL_DKGRAY  0xFF3C3C3CU

/* Per-class accent colours */
static const uint32_t CLASS_COLOURS[PAR_NUM_CLASSES] = {
    0xFF00E5FFU,   /* STATIONARY — cyan   */
    0xFF69FF47U,   /* WALKING    — green  */
    0xFFFFD700U,   /* CLIMBING   — gold   */
    0xFF7B68EEU,   /* LAYING     — purple */
};

/* ── Internal state ─────────────────────────────────────────────────────── */
static int   current_class      = -2;   /* -2 = force first draw */
static float current_confidence = -1.0f;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint32_t colour)
{
    BSP_LCD_SetTextColor(colour);
    BSP_LCD_FillRect(x, y, w, h);
}

static void draw_string_at(uint16_t x, uint16_t y,
                            const char *s, uint32_t fg, uint32_t bg,
                            sFONT *font)
{
    BSP_LCD_SetFont(font);
    BSP_LCD_SetBackColor(bg);
    BSP_LCD_SetTextColor(fg);
    BSP_LCD_DisplayStringAt(x, y, (uint8_t *)s, LEFT_MODE);
}

/* ── public API ─────────────────────────────────────────────────────────── */

void LCD_PAR_Init(void)
{
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LCD_FOREGROUND_LAYER, LCD_FRAME_BUFFER_LAYER1);
    BSP_LCD_SelectLayer(LCD_FOREGROUND_LAYER);
    BSP_LCD_SetLayerVisible(LCD_FOREGROUND_LAYER, ENABLE);
    BSP_LCD_SetTransparency(LCD_FOREGROUND_LAYER, 255);

    /* Fill background */
    BSP_LCD_Clear(COL_BG);

    /* Title bar */
    fill_rect(0, 0, LCD_W, 26, COL_DKGRAY);
    draw_string_at(10, Y_TITLE, "PAR  System  |  STM32F429",
                   COL_TITLE, COL_DKGRAY, &Font12);

    /* Dividers */
    fill_rect(0, 27,  LCD_W, 2, 0xFF404040U);
    fill_rect(0, 160, LCD_W, 2, 0xFF404040U);
    fill_rect(0, 218, LCD_W, 2, 0xFF404040U);
    fill_rect(0, 250, LCD_W, 2, 0xFF404040U);

    /* Static labels */
    draw_string_at(BAR_X, Y_CONF_LBL, "Confidence:",
                   0xFFAAAAAAAAU, COL_BG, &Font12);
    draw_string_at(BAR_X, Y_SENSOR,     "Acc (g):",
                   0xFFAAAAAAAAU, COL_BG, &Font12);
    draw_string_at(BAR_X, Y_SENSOR + 16,"Gyr (d/s):",
                   0xFFAAAAAAAAU, COL_BG, &Font12);

    /* Initial activity label */
    LCD_PAR_UpdateActivity(-1, 0.0f);
}

void LCD_PAR_UpdateActivity(int class_idx, float confidence)
{
    /* Activity name — only redraw if changed to avoid flicker */
    if (class_idx != current_class) {
        current_class = class_idx;

        /* Clear activity area */
        fill_rect(0, 32, LCD_W, 94, COL_BG);

        const char *name = (class_idx < 0) ? "Initialising..." :
                            PAR_GetActivityName(class_idx);
        uint32_t col = (class_idx < 0) ? 0xFF888888U : CLASS_COLOURS[class_idx];

        BSP_LCD_SetFont(&Font24);
        BSP_LCD_SetBackColor(COL_BG);
        BSP_LCD_SetTextColor(col);
        BSP_LCD_DisplayStringAt(0, Y_ACTIVITY - 12, (uint8_t *)name, CENTER_MODE);
    }

    /* Confidence bar — update every call */
    if (confidence != current_confidence || class_idx != current_class) {
        current_confidence = confidence;

        /* Clear bar row */
        fill_rect(BAR_X, Y_CONF_BAR, BAR_W, BAR_H, 0xFF282828U);

        if (confidence > 0.0f && class_idx >= 0) {
            int    filled_w = (int)(confidence * (float)BAR_W);
            uint32_t bar_col = (confidence >= 0.7f) ? COL_HIGH :
                               (confidence >= 0.4f) ? COL_MED   : COL_LOW;
            fill_rect(BAR_X, Y_CONF_BAR, (uint16_t)filled_w, BAR_H, bar_col);
        }

        /* Percentage text */
        char pct[8];
        if (class_idx < 0)
            snprintf(pct, sizeof(pct), "  --  ");
        else
            snprintf(pct, sizeof(pct), " %3d%% ", (int)(confidence * 100.0f));

        draw_string_at(BAR_X + BAR_W + 4, Y_CONF_BAR,
                       pct, COL_TEXT, COL_BG, &Font12);
    }
}

void LCD_PAR_UpdateSensorData(float ax_g, float ay_g, float az_g,
                              float gx_d, float gy_d, float gz_d)
{
    char line[48];

    snprintf(line, sizeof(line), "%+6.2f %+6.2f %+6.2f  ",
             (double)ax_g, (double)ay_g, (double)az_g);
    draw_string_at(72, Y_SENSOR, line, COL_TEXT, COL_BG, &Font12);

    snprintf(line, sizeof(line), "%+7.1f %+7.1f %+7.1f  ",
             (double)gx_d, (double)gy_d, (double)gz_d);
    draw_string_at(72, Y_SENSOR + 16, line, COL_TEXT, COL_BG, &Font12);
}

void LCD_PAR_UpdateCounters(uint32_t samples, uint32_t windows)
{
    char line[48];
    snprintf(line, sizeof(line), "Samples: %6lu   Windows: %4lu  ",
             (unsigned long)samples, (unsigned long)windows);
    draw_string_at(BAR_X, Y_COUNTERS, line, 0xFF888888U, COL_BG, &Font12);
}
