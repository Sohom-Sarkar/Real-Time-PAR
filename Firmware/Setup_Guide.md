# STM32 PAR Firmware — CubeIDE Setup Guide

## What you need
- STM32CubeIDE (already installed)
- STM32F429I-DISC1 board
- MPU-6050 breakout module

## Step 1 — Create a new CubeIDE project

1. **File → New → STM32 Project**
2. In the board selector type `429I-DISC1` and select **STM32F429I-DISC1**
3. Project name: `PAR_MPU6050`  — Language: **C++** (required for par_wrapper.cpp)
4. Click Finish — let CubeIDE generate the default project

## Step 2 — Configure peripherals in CubeMX (Device Configuration Tool)

Double-click the `.ioc` file in the project root.

### 2a. System Core → RCC
- HSE: **Crystal/Ceramic Resonator**
- Clock Configuration tab:
  - Input freq: 8 MHz
  - HCLK: **180 MHz**  (PLL: M=8, N=360, P=2, Q=7)
  - APB1 prescaler: /4  (45 MHz)
  - APB2 prescaler: /2  (90 MHz)

### 2b. Connectivity → I2C1
- Mode: **I2C**
- PB8 = I2C1_SCL, PB9 = I2C1_SDA (these are the defaults for I2C1 on this board)
- Parameter Settings → Clock Speed: **400000** Hz (Fast Mode)

### 2c. Timers → TIM6
- Activated: **Yes**
- Prescaler: **899**
- Counter Period: **1999**
- NVIC tab → TIM6 global interrupt: **Enabled**
> 90 MHz / 900 / 2000 = 50 Hz exactly

### 2d. Connectivity → USART1
- Mode: **Asynchronous**
- PA9 = USART1_TX, PA10 = USART1_RX
- Baud Rate: **115200**

### 2e. Middleware (optional but recommended) — no CMSIS-DSP needed
The pipeline uses a self-contained FFT — no extra library required.

Click **Generate Code** (or press Alt+S).

## Step 3 — Add BSP and LCD support

The BSP for STM32F429I-DISC1 is included with STM32CubeIDE.

1. In Project Explorer, right-click the project → **Properties → C/C++ General →
   Paths and Symbols → Includes** — verify that
   `Drivers/BSP/STM32F429I-Discovery` is on the include path.
2. The BSP source files (`stm32f429i_discovery_lcd.c`, `stm32f429i_discovery.c`,
   and `Utilities/Fonts/`) should already be added by CubeMX when you select the
   discovery board. If not, copy them from:
   `STM32Cube_FW_F4_Vx.xx.x/Drivers/BSP/STM32F429I-Discovery/`

## Step 4 — Add firmware source files

Copy the following directories from `STM32_PAR_Firmware/` into your CubeIDE
project (drag-and-drop works in Project Explorer):

```
Core/Inc/mpu6050.h          → Core/Inc/
Core/Inc/par_wrapper.h      → Core/Inc/
Core/Inc/par_pipeline.h     → Core/Inc/
Core/Inc/lcd_display.h      → Core/Inc/
Core/Src/mpu6050.c          → Core/Src/
Core/Src/par_wrapper.cpp    → Core/Src/
Core/Src/par_pipeline.c     → Core/Src/
Core/Src/lcd_display.c      → Core/Src/
ML_Model/par_model.h        → Core/Inc/   (or ML_Model/ — add to include path)
ML_Model/par_features.h     → Core/Inc/
```

Replace the CubeMX-generated `Core/Src/main.c` with the provided `main.c`
**but keep the USER CODE sections only** — or just copy the content between
`/* USER CODE BEGIN */` and `/* USER CODE END */` markers into the generated
file to preserve CubeMX compatibility.

### Include paths

Right-click project → Properties → C/C++ General → Paths and Symbols:
- Add `Core/Inc` (usually already there)
- Add `ML_Model` if you put the headers there instead of Core/Inc

## Step 5 — Compile as C++

`par_wrapper.cpp` must be compiled as C++. CubeIDE does this automatically
for `.cpp` files. Verify:
- Project → Properties → C/C++ Build → Settings
- The file `par_wrapper.cpp` should appear under **MCU G++ Compiler**

## Step 6 — Linker flags for printf float

To print floating-point values via printf/UART:
- Project → Properties → C/C++ Build → Settings → MCU GCC Linker →
  Miscellaneous → Other flags: add  `-u _printf_float`

## Step 7 — Hardware wiring

```
MPU-6050 Module    STM32F429I-DISC1
─────────────────────────────────────
VCC    ──────────  3.3V  (CN3 pin 1)
GND    ──────────  GND   (CN3 pin 2)
SCL    ──────────  PB8   (CN3 or Arduino header D15)
SDA    ──────────  PB9   (CN3 or Arduino header D14)
AD0    ──────────  GND   (sets I2C address to 0x68)
INT    ──(N/C)──   not connected
```

## Step 8 — Build and flash

1. Build the project (Ctrl+B)
2. Connect the board via USB
3. Run → Debug As → STM32 Cortex-M C/C++ Application
4. The LCD should show "PAR System | STM32F429" in the title bar
5. Start moving the board — after the first 128 samples (~2.56 s) you will
   see activity labels update on the LCD and UART output at 115200 baud

## Expected resource usage

| Resource | Used     | Available |
|----------|----------|-----------|
| Flash    | ~550 KB  | 2 MB      |
| SRAM     | ~18 KB   | 256 KB    |
| CPU load | < 5 %    | 180 MHz   |

Most of the Flash is the Random Forest model (`par_model.h` ≈ 500 KB compiled).

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| LCD shows "MPU-6050 not found!" | Wrong wiring or I2C address | Check SDA/SCL swap; confirm AD0=GND |
| Activity never changes | Too few samples | Wait >3 s before expecting results |
| All predictions are STATIONARY | Possible gain/clip mismatch | Check BODY_ACC_GAIN (default 6.0) and BODY_ACC_CLIP (default 0.5) |
| Compile error in par_wrapper.cpp | C vs C++ mismatch | Ensure file extension is .cpp |
| Hard fault | Stack overflow | All large arrays in par_pipeline.c are static — verify no accidental large locals |
