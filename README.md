# Real-Time Physical Activity Recognition on STM32F429I-DISC1

**EEP3020 — Digital Systems Lab, IIT Jodhpur**
Group: Sohom Sarkar (B23EE1099), Sambhav Jha (B23EE1092), Rudra Khokhani (B23EE1036), Tula Mrudhul (B23EE1076)
Supervisor: Prof. Binod Kumar

---

This project implements a complete, self-contained Physical Activity Recognition (PAR) system on the STM32F429I-DISC1 discovery board. The board reads a 6-DoF IMU at 50 Hz, processes the data through a signal conditioning pipeline, and runs a trained Random Forest classifier entirely on-chip — no laptop or cloud needed during deployment. Results are shown in real time on the board's built-in 2.4-inch LCD.

We collected labelled inertial data for four activities using the same MPU-6050 hardware and sensor placement, then trained the model offline. The firmware implements the identical processing chain (IIR filters, feature extraction, z-score normalisation) so that the on-device pipeline matches the training environment as closely as possible.

**Achieved accuracy: 93.4% across four activity classes over a structured 120-second test.**

---

## What it recognises

| Index | Class | Description |
|-------|-------|-------------|
| 0 | STATIONARY | Sitting or standing still |
| 1 | WALKING | Normal level-ground walking |
| 2 | CLIMBING | Ascending or descending stairs |
| 3 | LAYING | Lying flat |

---

## Hardware

- **MCU:** STM32F429ZIT6, ARM Cortex-M4F, 72 MHz, 2 MB Flash, 256 KB SRAM
- **IMU:** InvenSense MPU-6050 breakout (±2 g, ±250 °/s), connected via I²C3
  - SCL → PA8, SDA → PC9, AD0 → GND (I²C address 0x68)
- **Display:** ILI9341 2.4-inch TFT (240×320 px) — built into the DISC1 board
- **UART:** USART1 @ 115200 baud (PA9/PA10) for logging

---

## Repository layout

```
STM32_PAR_Firmware/
    Core/
        Inc/
            par_pipeline.h      PAR pipeline public API
            par_wrapper.h       C/C++ bridge header
            lcd_display.h       LCD display API
            mpu6050.h           IMU driver
        Src/
            par_pipeline.c      Signal conditioning + feature extraction + voting
            par_wrapper.cpp     C++ wrapper for the RF classifier
            lcd_display.c       ILI9341 display logic
            main.c              Initialisation, TIM7 ISR, FreeRTOS LCD task
            mpu6050.c           MPU-6050 I²C driver
    ML_Model/
        par_features.h          Feature selection indices + z-score stats
        par_filters.h           IIR filter coefficients
        par_model.h             Random Forest (generated C++ decision trees)
    SETUP_GUIDE.md              Step-by-step CubeIDE setup instructions

par_test_analysis.py            Python script to parse UART accuracy test logs
EEP3020_HAR_Report.pdf          Project report
```

---

## How the pipeline works

Every 20 ms a TIM7 interrupt fires and calls `PAR_PushSample()` with raw IMU readings. Inside that function:

1. Raw counts are converted to g and °/s
2. A **18 Hz noise low-pass filter** (2nd-order causal Butterworth biquad) is applied to all 6 IMU axes
3. A **0.3 Hz gravity low-pass filter** separates gravity from body acceleration
4. Body acceleration = noise-filtered acc − gravity-filtered acc, then scaled by **GAIN = 6.0** and clipped at **±0.5 g**
5. The 9 conditioned signals land in a **128-sample circular buffer** (2.56 s of history)
6. Every 64 new samples, **284 features** are extracted (time-domain: mean, std, energy, IQR, correlation, AR-4; frequency-domain via FFT: centroid, band energies, spectral entropy)
7. A pre-computed index mask picks the **100 most discriminative features**, which are z-score normalised using training-set statistics stored in flash
8. A **Random Forest** (compiled to C++ decision trees) produces a class prediction
9. The result goes into a **3-window majority vote buffer** — the final label is the mode, confidence is the agreement fraction

A FreeRTOS LCD task refreshes the display at ~5 Hz independently of the above.

---

## Getting started

See [`STM32_PAR_Firmware/SETUP_GUIDE.md`](STM32_PAR_Firmware/SETUP_GUIDE.md) for the full step-by-step CubeIDE setup. The short version:

1. Create a new STM32CubeIDE project for the **STM32F429I-DISC1**, language **C++**
2. Copy the `Core/` and `ML_Model/` source files into the project
3. Add `ML_Model/` to the include paths
4. Add `-u _printf_float` to linker flags (for UART float output)
5. Build (Ctrl+B) and flash via the onboard ST-LINK

Once flashed, the LCD shows the current activity label and a confidence bar that turns green (≥70%), orange (≥40%), or red (<40%). UART output at 115200 baud prints one line per window:

```
[PAR] WALKING                conf=1.00  samples=3200  windows=25
```

---

## Running the accuracy test

Enable test mode by setting `#define PAR_TEST 1` in `main.c` before building. The firmware will automatically cycle through all four activity classes (30 s each), printing CSV data to UART. Capture the output with PuTTY or any serial terminal, then analyse it:

```
python par_test_analysis.py putty.log
```

The script skips the first 3 windows of each phase (buffer settling), then prints per-class accuracy, a confusion matrix, and mean confidence.

---

## Key tunable parameters

| Define | Default | Effect |
|--------|---------|--------|
| `BODY_ACC_GAIN` | 6.0 | Scales body acceleration into the trained feature range |
| `BODY_ACC_CLIP` | 0.5 g | Per-sample saturation to suppress tap artefacts |
| `VOTE_LEN` | 3 | Majority vote buffer depth (longer = more stable, slower) |
| `PAR_TEST` | 0 | Set to 1 to enable the structured accuracy test mode |
| `TEST_HOLD_MS` | 30000 | Duration of each test phase in milliseconds |

---

## Results summary

| Class | Correct | Total | Accuracy |
|-------|---------|-------|----------|
| STATIONARY | 16 | 18 | 88.9% |
| WALKING | 18 | 19 | 94.7% |
| CLIMBING | 17 | 19 | 89.5% |
| LAYING | 20 | 20 | 100.0% |
| **OVERALL** | **71** | **76** | **93.4%** |

The main confusion is between CLIMBING and WALKING (3 windows), which share a similar periodic leg-swing signature. LAYING is perfectly separated due to its distinct gravity-vector orientation.
