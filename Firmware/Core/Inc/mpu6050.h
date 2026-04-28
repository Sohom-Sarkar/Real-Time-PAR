/*
 * mpu6050.h  —  MPU-6050 IMU driver for STM32 HAL
 *
 * Configuration:
 *   Accelerometer : ±2g   → sensitivity 16384 LSB/g
 *   Gyroscope     : ±250°/s → sensitivity 131 LSB/(°/s)
 *   Sample rate   : 50 Hz  (SMPLRT_DIV=19, DLPF enabled)
 *   Interface     : I2C, AD0=GND → address 0x68
 */
#ifndef MPU6050_H
#define MPU6050_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* I2C address (7-bit shifted left by 1 for HAL) */
#define MPU6050_I2C_ADDR        (0x68U << 1)

/* Register map */
#define MPU6050_REG_SMPLRT_DIV  0x19U
#define MPU6050_REG_CONFIG      0x1AU
#define MPU6050_REG_GYRO_CFG    0x1BU
#define MPU6050_REG_ACCEL_CFG   0x1CU
#define MPU6050_REG_ACCEL_XOUT  0x3BU   /* first of 14 data bytes */
#define MPU6050_REG_PWR_MGMT_1  0x6BU
#define MPU6050_REG_WHO_AM_I    0x75U
#define MPU6050_WHO_AM_I_VAL    0x68U

/* Raw sensor reading (int16, straight from the registers) */
typedef struct {
    int16_t ax, ay, az;   /* accelerometer, unit: LSB  (÷16384 → g)     */
    int16_t gx, gy, gz;   /* gyroscope,     unit: LSB  (÷131   → deg/s)  */
} MPU6050_Raw;

/**
 * @brief  Initialise the MPU-6050 at 50 Hz.
 * @param  hi2c  Pointer to I2C handle (I2C1, Fast-mode 400 kHz).
 * @retval HAL_OK on success, HAL_ERROR if device not found.
 */
HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Read one sample (6 axes, 14 bytes burst).
 * @param  hi2c  Pointer to I2C handle.
 * @param  out   Pointer to MPU6050_Raw struct to fill.
 * @retval HAL_OK on success.
 */
HAL_StatusTypeDef MPU6050_ReadRaw(I2C_HandleTypeDef *hi2c, MPU6050_Raw *out);

#endif /* MPU6050_H */
