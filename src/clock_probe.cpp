#include <Arduino.h>

extern "C" {
volatile uint32_t clock_probe_ms = 0;
volatile uint32_t clock_probe_hse_ready = 0;

void SystemClock_Config(void) {
    RCC_OscInitTypeDef oscillator = {};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        return; // Keep the reset HSI clock and report failure through SWD.
    }
    RCC_ClkInitTypeDef clock = {};
    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    // Direct HSE, no PLL: the frequency check cannot overclock the CPU.
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_2) == HAL_OK) {
        clock_probe_hse_ready = 1;
    }
}
}

void setup() {}
void loop() { clock_probe_ms = millis(); }
