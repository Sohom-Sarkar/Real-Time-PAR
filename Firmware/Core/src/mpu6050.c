/*
 * mpu6050.c  —  MPU-6050 I2C driver implementation
 */
#include "mpu6050.h"

/* ── helpers ──────────────────────────────────────────────────────────────── */

static HAL_StatusTypeDef write_reg(I2C_HandleTypeDef *hi2c,
                                   uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(hi2c, MPU6050_I2C_ADDR,
                                   buf, 2, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef read_regs(I2C_HandleTypeDef *hi2c,
                                   uint8_t reg, uint8_t *dst, uint16_t len)
{
    HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(hi2c, MPU6050_I2C_ADDR,
                                  &reg, 1, HAL_MAX_DELAY);
    if (ret != HAL_OK) return ret;
    return HAL_I2C_Master_Receive(hi2c, MPU6050_I2C_ADDR,
                                  dst, len, HAL_MAX_DELAY);
}

/* ── public API ───────────────────────────────────────────────────────────── */

HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t who;
    HAL_StatusTypeDef ret;

    /* Verify device presence */
    ret = read_regs(hi2c, MPU6050_REG_WHO_AM_I, &who, 1);
    if (ret != HAL_OK || who != MPU6050_WHO_AM_I_VAL) return HAL_ERROR;

    /* Wake up, use PLL with X-axis gyro reference (bit 0 = 0x01) */
    ret = write_reg(hi2c, MPU6050_REG_PWR_MGMT_1, 0x01U);
    if (ret != HAL_OK) return ret;

    HAL_Delay(100);   /* let PLL lock */

    /*
     * DLPF_CFG = 3  →  Accel BW 44 Hz, Gyro BW 42 Hz, Gyro Fs = 1 kHz
     * This enables the 1 kHz internal sample rate needed so that
     * SMPLRT_DIV = 19 gives exactly 50 Hz output:
     *   Fs_out = 1000 / (1 + 19) = 50 Hz
     */
    ret = write_reg(hi2c, MPU6050_REG_CONFIG, 0x03U);
    if (ret != HAL_OK) return ret;

    /* Sample rate divider: 50 Hz */
    ret = write_reg(hi2c, MPU6050_REG_SMPLRT_DIV, 19U);
    if (ret != HAL_OK) return ret;

    /* Gyroscope full scale: ±250 °/s  (FS_SEL = 0) */
    ret = write_reg(hi2c, MPU6050_REG_GYRO_CFG, 0x00U);
    if (ret != HAL_OK) return ret;

    /* Accelerometer full scale: ±2 g  (AFS_SEL = 0) */
    ret = write_reg(hi2c, MPU6050_REG_ACCEL_CFG, 0x00U);
    if (ret != HAL_OK) return ret;

    return HAL_OK;
}

HAL_StatusTypeDef MPU6050_ReadRaw(I2C_HandleTypeDef *hi2c, MPU6050_Raw *out)
{
    uint8_t buf[14];
    HAL_StatusTypeDef ret = read_regs(hi2c, MPU6050_REG_ACCEL_XOUT, buf, 14);
    if (ret != HAL_OK) return ret;

    /* Registers 0x3B–0x40: ACCEL_XOUT, ACCEL_YOUT, ACCEL_ZOUT
     * Registers 0x41–0x42: TEMP_OUT (skipped)
     * Registers 0x43–0x48: GYRO_XOUT, GYRO_YOUT, GYRO_ZOUT
     * Each value is big-endian signed 16-bit. */
    out->ax = (int16_t)((buf[0]  << 8) | buf[1]);
    out->ay = (int16_t)((buf[2]  << 8) | buf[3]);
    out->az = (int16_t)((buf[4]  << 8) | buf[5]);
    /* buf[6..7] = temperature, ignored */
    out->gx = (int16_t)((buf[8]  << 8) | buf[9]);
    out->gy = (int16_t)((buf[10] << 8) | buf[11]);
    out->gz = (int16_t)((buf[12] << 8) | buf[13]);

    return HAL_OK;
}
