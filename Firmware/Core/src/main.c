/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : PAR on STM32F429I-DISC1 with MPU-6050
 *
 * Hardware connections
 * ────────────────────
 *  MPU-6050  →  STM32F429I-DISC1
 *  VCC       →  3.3 V
 *  GND       →  GND
 *  SCL       →  PB8  (I2C1_SCL, AF4)
 *  SDA       →  PB9  (I2C1_SDA, AF4)
 *  AD0       →  GND  (I2C address 0x68)
 *  INT       →  (not used — polled by TIM6 ISR)
 *
 * CubeMX configuration summary
 * ──────────────────────────────
 *  System clock : 180 MHz (HSE + PLL, M=8 N=360 P=2 Q=7)
 *  I2C1         : PB8/PB9, Fast-mode 400 kHz
 *  TIM6         : 50 Hz interrupt
 *                 PSC = 899, ARR = 1999
 *                 APB1 timer clock = 90 MHz
 *                 90 000 000 / (900 × 2000) = 50 Hz
 *  USART1       : PA9 (TX) PA10 (RX), 115200 8N1
 *  BSP LCD      : enabled (ILI9341 via LTDC)
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ---------------------------------------------------------------- */
#include "main.h"
#include <stdio.h>
#include <string.h>

/* USER CODE BEGIN Includes */
#include "mpu6050.h"
#include "par_pipeline.h"
#include "lcd_display.h"
/* USER CODE END Includes */

/* Private variables ------------------------------------------------------- */
I2C_HandleTypeDef  hi2c1;
TIM_HandleTypeDef  htim6;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static volatile uint8_t  g_sample_flag = 0;   /* set by TIM6 ISR */
static uint32_t          g_sample_count = 0;
static uint32_t          g_window_count = 0;
static int               g_last_activity = -1;
/* USER CODE END PV */

/* Private function prototypes --------------------------------------------- */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN PFP */
static void PAR_UART_Report(int activity, float confidence);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
/**
 * @brief  Redirect printf to USART1 (add to syscalls.c or keep here).
 *         In CubeIDE: Project → Properties → C/C++ Build → Settings →
 *         MCU GCC Linker → Miscellaneous → add: -u _printf_float
 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
/* USER CODE END 0 */

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    HAL_Init();
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_TIM6_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */

    /* ── LCD initialisation ── */
    LCD_PAR_Init();

    /* ── MPU-6050 initialisation ── */
    if (MPU6050_Init(&hi2c1) != HAL_OK) {
        /* MPU-6050 not found — show error on LCD and halt */
        BSP_LCD_SetFont(&Font16);
        BSP_LCD_SetTextColor(LCD_COLOR_RED);
        BSP_LCD_DisplayStringAt(0, 140, (uint8_t *)"MPU-6050 not found!", CENTER_MODE);
        BSP_LCD_DisplayStringAt(0, 160, (uint8_t *)"Check wiring + AD0", CENTER_MODE);
        Error_Handler();
    }

    /* ── PAR pipeline initialisation ── */
    PAR_Init();

    /* ── Start 50 Hz TIM6 interrupt ── */
    HAL_TIM_Base_Start_IT(&htim6);

    printf("\r\n== PAR System Ready ==\r\n");
    printf("Collecting data at 50 Hz, window=128, hop=64\r\n");

    /* USER CODE END 2 */

    /* ── Main loop ────────────────────────────────────────────────────────── */
    while (1)
    {
        /* USER CODE BEGIN WHILE */

        if (g_sample_flag) {
            g_sample_flag = 0;

            /* Read MPU-6050 */
            MPU6050_Raw raw;
            if (MPU6050_ReadRaw(&hi2c1, &raw) == HAL_OK) {
                g_sample_count++;

                /* Push into pipeline */
                PAR_PushSample(raw.ax, raw.ay, raw.az,
                               raw.gx, raw.gy, raw.gz);

                /* Check for new result */
                int act = PAR_GetActivity();
                if (act != g_last_activity && act >= 0) {
                    g_last_activity = act;
                    g_window_count++;

                    float conf = PAR_GetConfidence();

                    /* Update LCD */
                    LCD_PAR_UpdateActivity(act, conf);
                    LCD_PAR_UpdateCounters(g_sample_count, g_window_count);

                    /* UART report */
                    PAR_UART_Report(act, conf);
                }

                /* Update sensor readings every 10 samples (~5 Hz LCD refresh) */
                if (g_sample_count % 10 == 0) {
                    float ax_g = (float)raw.ax / 16384.0f;
                    float ay_g = (float)raw.ay / 16384.0f;
                    float az_g = (float)raw.az / 16384.0f;
                    float gx_d = (float)raw.gx / 131.0f;
                    float gy_d = (float)raw.gy / 131.0f;
                    float gz_d = (float)raw.gz / 131.0f;
                    LCD_PAR_UpdateSensorData(ax_g, ay_g, az_g, gx_d, gy_d, gz_d);

                    /* Also update activity in case confidence changed */
                    if (g_last_activity >= 0) {
                        LCD_PAR_UpdateActivity(g_last_activity, PAR_GetConfidence());
                    }
                    LCD_PAR_UpdateCounters(g_sample_count, g_window_count);
                }
            }
        }

        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */
        /* USER CODE END 3 */
    }
}

/* USER CODE BEGIN 4 */

/**
 * @brief  TIM6 period-elapsed callback — fires at 50 Hz.
 *         Sets the flag; the main loop does the actual I2C read.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        g_sample_flag = 1;
    }
}

/**
 * @brief  Print activity + confidence on UART (useful for PC-side logging).
 */
static void PAR_UART_Report(int activity, float confidence)
{
    printf("[PAR] %-22s  conf=%.2f  samples=%lu  windows=%lu\r\n",
           PAR_GetActivityName(activity),
           (double)confidence,
           (unsigned long)g_sample_count,
           (unsigned long)g_window_count);
}

/* USER CODE END 4 */

/* ── SystemClock_Config ───────────────────────────────────────────────────
 *  180 MHz from 8 MHz HSE crystal (on-board on STM32F429I-DISC1)
 *  PLL: M=8  N=360  P=2  Q=7
 *  AHB/1, APB1/4 (45 MHz), APB2/2 (90 MHz)
 * ────────────────────────────────────────────────────────────────────────── */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 360;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
        Error_Handler();
}

/* ── Peripheral init ──────────────────────────────────────────────────────── */

static void MX_I2C1_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.ClockSpeed       = 400000;   /* Fast mode 400 kHz */
    hi2c1.Init.DutyCycle        = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_TIM6_Init(void)
{
    /*
     * APB1 timer clock = 90 MHz (APB1 = 45 MHz, prescaler > 1  → ×2)
     * Period = 90,000,000 / (PSC+1) / (ARR+1) = 90e6 / 900 / 2000 = 50 Hz
     */
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 899;
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = 1999;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
        Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();   /* user LED PG13/PG14 on -DISC1 */

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PG13 / PG14 — user LEDs (optional debug) */
    GPIO_InitStruct.Pin   = GPIO_PIN_13 | GPIO_PIN_14;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
}

/* ── Error handler ─────────────────────────────────────────────────────── */
void Error_Handler(void)
{
    __disable_irq();
    /* Blink red LED PG14 rapidly to signal fault */
    while (1) {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
        HAL_Delay(100);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
