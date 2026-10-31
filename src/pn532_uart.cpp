#include <Arduino.h>
#include "pn532_uart.h"
#include "reader.h"

namespace {
class Usart3 final : public PN532Uart {
public:
    volatile uint16_t head = 0, tail = 0;
    volatile uint32_t faults = 0;
    uint8_t rx[512];
    uint32_t errors() const override { return faults; }
    void begin() override {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_AFIO_CLK_ENABLE();
        __HAL_RCC_USART3_CLK_ENABLE();
        HAL_NVIC_DisableIRQ(USART3_IRQn);
        USART3->CR1 = 0;
        __HAL_AFIO_REMAP_USART3_DISABLE();
        GPIO_InitTypeDef pin = {};
        pin.Pin = GPIO_PIN_10; pin.Mode = GPIO_MODE_AF_PP;
        pin.Pull = GPIO_NOPULL; pin.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &pin);
        pin.Pin = GPIO_PIN_11; pin.Mode = GPIO_MODE_INPUT;
        HAL_GPIO_Init(GPIOB, &pin);
        head = tail = 0;
        USART3->CR2 = 0; USART3->CR3 = 0;
        USART3->BRR = (HAL_RCC_GetPCLK1Freq() + 57600U) / 115200U;
        (void)USART3->SR; (void)USART3->DR;
        USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
        HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
        HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
    int read() override {
        if (head == tail) return -1;
        const uint8_t value = rx[tail];
        tail = (tail + 1U) & 511U;
        return value;
    }
    bool write(const uint8_t *data, size_t length) override {
        const uint32_t start = millis();
        for (size_t i = 0; i < length; ++i) {
            while (!(USART3->SR & USART_SR_TXE)) {
                if (millis() - start >= 40) { ++faults; return false; }
            }
            USART3->DR = data[i];
        }
        while (!(USART3->SR & USART_SR_TC)) {
            if (millis() - start >= 40) { ++faults; return false; }
        }
        return true;
    }
} uart;
}
extern "C" void USART3_IRQHandler() {
    const uint32_t status = USART3->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        const uint8_t value = USART3->DR;
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) { ++uart.faults; }
        if (status & USART_SR_RXNE) {
            const uint16_t next = (uart.head + 1U) & 511U;
            if (next == uart.tail) { ++uart.faults; }
            else { uart.rx[uart.head] = value; uart.head = next; }
        }
    }
}
PN532Uart &reader_pn532_uart() { return uart; }
