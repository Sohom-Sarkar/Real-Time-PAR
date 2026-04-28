/*
 * par_pipeline.h  —  Real-time PAR pipeline for STM32F429I-DISC1
 *
 * Pipeline per sample (call from TIM6 ISR or main loop at 50 Hz):
 *
 *   MPU6050_ReadRaw()
 *       │ int16 raw counts
 *       ▼
 *   PAR_PushSample()      ← your application calls this
 *       │ convert to g / deg/s
 *       │ causal biquad IIR: noise LPF (18 Hz) on acc + gyro
 *       │ causal biquad IIR: gravity LPF (0.3 Hz) on noise-filtered acc
 *       │ body_acc = noise_acc − gravity_acc
 *       │ store in circular buffer (128 samples × 9 signals)
 *       │ every 64 new samples → extract window, compute 284 features
 *       │ select 100 features → z-score normalise → RF inference
 *       ▼
 *   PAR_GetActivity()     ← returns latest class index (0–5) or -1
 */
#ifndef PAR_PIPELINE_H
#define PAR_PIPELINE_H

#include <stdint.h>

/* Number of output classes */
#define PAR_NUM_CLASSES  4

/**
 * @brief  Initialise pipeline state (call once before the first sample).
 */
void PAR_Init(void);

/**
 * @brief  Push one MPU-6050 sample into the pipeline.
 *         Call this at exactly 50 Hz (e.g. from a TIM6 50 Hz callback).
 *
 * @param  ax_raw  Accelerometer X raw int16 (÷16384 → g)
 * @param  ay_raw  Accelerometer Y raw int16
 * @param  az_raw  Accelerometer Z raw int16
 * @param  gx_raw  Gyroscope X raw int16 (÷131 → deg/s)
 * @param  gy_raw  Gyroscope Y raw int16
 * @param  gz_raw  Gyroscope Z raw int16
 */
void PAR_PushSample(int16_t ax_raw, int16_t ay_raw, int16_t az_raw,
                    int16_t gx_raw, int16_t gy_raw, int16_t gz_raw);

/**
 * @brief  Get the most recent activity classification.
 * @retval 0–3 (class index), or -1 if fewer than 128 samples collected yet.
 *         0=STATIONARY, 1=WALKING, 2=CLIMBING, 3=LAYING
 */
int PAR_GetActivity(void);

/**
 * @brief  Get the human-readable name for a class index.
 * @param  class_idx  0–3
 * @retval Pointer to static string, e.g. "WALKING"
 */
const char *PAR_GetActivityName(int class_idx);

/**
 * @brief  Get confidence of the last prediction (0.0 – 1.0).
 *         Defined as fraction of the majority vote buffer that agrees.
 */
float PAR_GetConfidence(void);

#endif /* PAR_PIPELINE_H */
