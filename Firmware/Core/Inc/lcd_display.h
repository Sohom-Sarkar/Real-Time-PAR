/*
 * lcd_display.h  —  PAR result display on STM32F429I-DISC1 LCD
 *
 * The board has a 2.4" QVGA (240 × 320) ILI9341 display driven via
 * the BSP_LCD_* API from the STM32F429I-DISC1 BSP package.
 *
 * Layout (240 wide × 320 tall, landscape origin top-left):
 *
 *   y=  0 ┌─────────────────────────────┐
 *         │   PAR System (title, 16px)  │
 *   y= 30 ├─────────────────────────────┤
 *         │                             │
 *         │   WALKING   (large, 24px)   │  ← activity name
 *         │                             │
 *   y=120 ├─────────────────────────────┤
 *         │  Confidence:  ▓▓▓▓▓░░░  85% │  ← coloured bar
 *   y=160 ├─────────────────────────────┤
 *         │  Acc: [  1.02  0.02  9.80 ] │  ← raw accel (g)
 *         │  Gyr: [  0.10 -0.20  0.01 ] │  ← raw gyro (°/s)
 *   y=220 ├─────────────────────────────┤
 *         │  Samples: 12345  Win: 96    │
 *   y=250 ├─────────────────────────────┤
 *         │  (status line)              │
 *   y=320 └─────────────────────────────┘
 */
#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>

/**
 * @brief  Initialise the LCD and draw the static layout.
 *         Call once in main(), after BSP drivers are up.
 */
void LCD_PAR_Init(void);

/**
 * @brief  Update the activity name and confidence bar.
 * @param  class_idx   0–5 (see PAR_GetActivityName), or -1 for "Initialising"
 * @param  confidence  0.0 – 1.0
 */
void LCD_PAR_UpdateActivity(int class_idx, float confidence);

/**
 * @brief  Update the raw sensor readings shown at the bottom of the screen.
 * @param  ax_g   Accelerometer X in g
 * @param  ay_g   Accelerometer Y in g
 * @param  az_g   Accelerometer Z in g
 * @param  gx_d   Gyroscope X in deg/s
 * @param  gy_d   Gyroscope Y in deg/s
 * @param  gz_d   Gyroscope Z in deg/s
 */
void LCD_PAR_UpdateSensorData(float ax_g, float ay_g, float az_g,
                              float gx_d, float gy_d, float gz_d);

/**
 * @brief  Update the sample/window counter line.
 * @param  samples      Total samples received since boot.
 * @param  windows      Total inference windows completed.
 */
void LCD_PAR_UpdateCounters(uint32_t samples, uint32_t windows);

#endif /* LCD_DISPLAY_H */
