/**
 ******************************************************************************
 * @file           : hal_lite.c
 * @brief          : Register-level implementation of the HAL subset used by
 *                   nau7802.c and main.c. Assumes the default 16 MHz HSI clock
 *                   (SYSCLK = HCLK = PCLK1 = PCLK2 = 16 MHz).
 *
 * Pins (Nucleo-64 / NUCLEO-F411RE):
 *   USART2: PA2 = TX            (ST-LINK virtual COM port)
 *   I2C1  : PB8 = SCL (D15), PB9 = SDA (D14)
 *   I2C2  : PB10 = SCL (D6),  PB3 = SDA (D3)
 *   LD2   : PA5
 ******************************************************************************
 */
#include "main.h"

#define REG(addr)       (*(volatile uint32_t *)(addr))

#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     REG(RCC_BASE + 0x30)
#define RCC_APB1ENR     REG(RCC_BASE + 0x40)

#define SYSTICK_CTRL    REG(0xE000E010UL)
#define SYSTICK_LOAD    REG(0xE000E014UL)
#define SYSTICK_VAL     REG(0xE000E018UL)

#define CPU_HZ          16000000UL
#define PCLK1_MHZ       16U

/* I2C register bits */
#define I2C_CR1_PE      (1UL << 0)
#define I2C_CR1_START   (1UL << 8)
#define I2C_CR1_STOP    (1UL << 9)
#define I2C_CR1_ACK     (1UL << 10)
#define I2C_CR1_POS     (1UL << 11)
#define I2C_CR1_SWRST   (1UL << 15)
#define I2C_SR1_SB      (1UL << 0)
#define I2C_SR1_ADDR    (1UL << 1)
#define I2C_SR1_BTF     (1UL << 2)
#define I2C_SR1_RXNE    (1UL << 6)
#define I2C_SR1_TXE     (1UL << 7)
#define I2C_SR1_BERR    (1UL << 8)
#define I2C_SR1_ARLO    (1UL << 9)
#define I2C_SR1_AF      (1UL << 10)
#define I2C_SR2_BUSY    (1UL << 1)

I2C_HandleTypeDef  hi2c1  = { I2C1 };
I2C_HandleTypeDef  hi2c2  = { I2C2 };
UART_HandleTypeDef huart2 = { USART2 };

static volatile uint32_t uwTick;

/* ---------------------------------------------------------------------------
 * Tick
 * ------------------------------------------------------------------------- */
void SysTick_Handler(void)
{
    uwTick++;
}

void HAL_Init(void)
{
    SYSTICK_LOAD = CPU_HZ / 1000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = (1UL << 2) | (1UL << 1) | 1UL;  /* processor clock, interrupt, enable */
}

uint32_t HAL_GetTick(void)
{
    return uwTick;
}

void HAL_Delay(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    if (ms < HAL_MAX_DELAY) ms++;  /* guarantee at least the requested time, like HAL */
    while (HAL_GetTick() - t0 < ms) {}
}

/* ---------------------------------------------------------------------------
 * GPIO
 * ------------------------------------------------------------------------- */
static void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af, int open_drain)
{
    port->MODER = (port->MODER & ~(3UL << (pin * 2))) | (2UL << (pin * 2));
    port->AFR[pin / 8] = (port->AFR[pin / 8] & ~(0xFUL << ((pin % 8) * 4)))
                       | (af << ((pin % 8) * 4));
    if (open_drain) {
        port->OTYPER |= (1UL << pin);
        port->PUPDR = (port->PUPDR & ~(3UL << (pin * 2))) | (1UL << (pin * 2));  /* pull-up */
    }
    port->OSPEEDR |= (3UL << (pin * 2));
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    port->ODR ^= pin;
}

void MX_GPIO_Init(void)
{
    RCC_AHB1ENR |= (1UL << 0) | (1UL << 1);  /* GPIOA, GPIOB clocks */

    /* PA5: user LED LD2, output */
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << (5 * 2))) | (1UL << (5 * 2));
}

/* ---------------------------------------------------------------------------
 * USART2
 * ------------------------------------------------------------------------- */
void MX_USART2_UART_Init(void)
{
    RCC_APB1ENR |= (1UL << 17);  /* USART2 clock */
    gpio_af(GPIOA, 2, 7, 0);     /* PA2: USART2_TX, AF7 */

    /* 115200 baud @ 16 MHz: 16e6 / (16 * 115200) = 8.68 -> mantissa 8, fraction 11 */
    USART2->BRR = (8UL << 4) | 11UL;
    USART2->CR1 = (1UL << 13) | (1UL << 3);  /* UE, TE */
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *data,
                                    uint16_t size, uint32_t timeout)
{
    uint32_t t0 = HAL_GetTick();
    for (uint16_t i = 0; i < size; i++) {
        while (!(huart->Instance->SR & (1UL << 7))) {  /* TXE */
            if (HAL_GetTick() - t0 >= timeout) return HAL_TIMEOUT;
        }
        huart->Instance->DR = data[i];
    }
    while (!(huart->Instance->SR & (1UL << 6))) {      /* TC */
        if (HAL_GetTick() - t0 >= timeout) return HAL_TIMEOUT;
    }
    return HAL_OK;
}

/* ---------------------------------------------------------------------------
 * I2C (master, polling, 100 kHz standard mode)
 * ------------------------------------------------------------------------- */
static void i2c_setup(I2C_TypeDef *i2c)
{
    i2c->CR1 = I2C_CR1_SWRST;    /* clear any stuck state */
    i2c->CR1 = 0;
    i2c->CR2   = PCLK1_MHZ;                    /* FREQ = 16 MHz */
    i2c->CCR   = (PCLK1_MHZ * 1000000UL) / (2 * 100000UL);  /* 100 kHz -> 80 */
    i2c->TRISE = PCLK1_MHZ + 1;                /* 1000 ns max rise time */
    i2c->CR1   = I2C_CR1_PE;
}

void MX_I2C1_Init(void)
{
    RCC_APB1ENR |= (1UL << 21);  /* I2C1 clock */
    gpio_af(GPIOB, 8, 4, 1);     /* PB8: I2C1_SCL, AF4 */
    gpio_af(GPIOB, 9, 4, 1);     /* PB9: I2C1_SDA, AF4 */
    i2c_setup(I2C1);
}

void MX_I2C2_Init(void)
{
    RCC_APB1ENR |= (1UL << 22);  /* I2C2 clock */
    gpio_af(GPIOB, 10, 4, 1);    /* PB10: I2C2_SCL, AF4 */
    gpio_af(GPIOB, 3, 9, 1);     /* PB3:  I2C2_SDA, AF9 */
    i2c_setup(I2C2);
}

/* Wait for an SR1 flag. A NACK or bus error aborts with HAL_ERROR. */
static HAL_StatusTypeDef i2c_wait(I2C_TypeDef *i2c, uint32_t flag, uint32_t t0, uint32_t timeout)
{
    while (!(i2c->SR1 & flag)) {
        if (i2c->SR1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO)) {
            i2c->SR1 &= ~(I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO);
            return HAL_ERROR;
        }
        if (HAL_GetTick() - t0 >= timeout) return HAL_TIMEOUT;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef i2c_wait_idle(I2C_TypeDef *i2c, uint32_t t0, uint32_t timeout)
{
    while (i2c->SR2 & I2C_SR2_BUSY) {
        if (HAL_GetTick() - t0 >= timeout) return HAL_BUSY;
    }
    return HAL_OK;
}

static void i2c_clear_addr(I2C_TypeDef *i2c)
{
    (void)i2c->SR1;
    (void)i2c->SR2;
}

static HAL_StatusTypeDef i2c_fail(I2C_TypeDef *i2c, HAL_StatusTypeDef st)
{
    i2c->CR1 |= I2C_CR1_STOP;
    i2c->CR1 &= ~I2C_CR1_POS;
    return st;
}

/* START + address phase. addr is the 8-bit (shifted) address incl. R/W bit. */
static HAL_StatusTypeDef i2c_start(I2C_TypeDef *i2c, uint8_t addr, uint32_t t0, uint32_t timeout)
{
    HAL_StatusTypeDef st;
    i2c->CR1 |= I2C_CR1_START;
    if ((st = i2c_wait(i2c, I2C_SR1_SB, t0, timeout)) != HAL_OK) return st;
    i2c->DR = addr;
    return i2c_wait(i2c, I2C_SR1_ADDR, t0, timeout);
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                        uint32_t trials, uint32_t timeout)
{
    I2C_TypeDef *i2c = hi2c->Instance;

    for (uint32_t n = 0; n < trials; n++) {
        uint32_t t0 = HAL_GetTick();
        if (i2c_wait_idle(i2c, t0, timeout) != HAL_OK) return HAL_BUSY;

        HAL_StatusTypeDef st = i2c_start(i2c, (uint8_t)(addr & 0xFE), t0, timeout);
        if (st == HAL_OK) i2c_clear_addr(i2c);
        i2c->CR1 |= I2C_CR1_STOP;
        if (st == HAL_OK) return HAL_OK;
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t addr, uint16_t reg,
                                    uint16_t reg_size, uint8_t *data, uint16_t size,
                                    uint32_t timeout)
{
    I2C_TypeDef *i2c = hi2c->Instance;
    uint32_t t0 = HAL_GetTick();
    HAL_StatusTypeDef st;
    (void)reg_size;  /* only 8-bit register addresses are used */

    if (i2c_wait_idle(i2c, t0, timeout) != HAL_OK) return HAL_BUSY;

    if ((st = i2c_start(i2c, (uint8_t)(addr & 0xFE), t0, timeout)) != HAL_OK)
        return i2c_fail(i2c, st);
    i2c_clear_addr(i2c);

    if ((st = i2c_wait(i2c, I2C_SR1_TXE, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
    i2c->DR = (uint8_t)reg;

    for (uint16_t i = 0; i < size; i++) {
        if ((st = i2c_wait(i2c, I2C_SR1_TXE, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
        i2c->DR = data[i];
    }

    if ((st = i2c_wait(i2c, I2C_SR1_BTF, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
    i2c->CR1 |= I2C_CR1_STOP;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t addr, uint16_t reg,
                                   uint16_t reg_size, uint8_t *data, uint16_t size,
                                   uint32_t timeout)
{
    I2C_TypeDef *i2c = hi2c->Instance;
    uint32_t t0 = HAL_GetTick();
    HAL_StatusTypeDef st;
    (void)reg_size;  /* only 8-bit register addresses are used */

    if (size == 0) return HAL_ERROR;
    if (i2c_wait_idle(i2c, t0, timeout) != HAL_OK) return HAL_BUSY;

    /* Write phase: register address */
    if ((st = i2c_start(i2c, (uint8_t)(addr & 0xFE), t0, timeout)) != HAL_OK)
        return i2c_fail(i2c, st);
    i2c_clear_addr(i2c);
    if ((st = i2c_wait(i2c, I2C_SR1_TXE, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
    i2c->DR = (uint8_t)reg;
    if ((st = i2c_wait(i2c, I2C_SR1_BTF, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);

    /* Read phase: repeated START. The ACK/STOP sequencing for the last bytes
     * follows RM0383 section 18.3.3 (method for 1, 2 and >2 bytes). */
    if ((st = i2c_start(i2c, (uint8_t)(addr | 0x01), t0, timeout)) != HAL_OK)
        return i2c_fail(i2c, st);

    if (size == 1) {
        i2c->CR1 &= ~I2C_CR1_ACK;
        i2c_clear_addr(i2c);
        i2c->CR1 |= I2C_CR1_STOP;
        if ((st = i2c_wait(i2c, I2C_SR1_RXNE, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
        data[0] = (uint8_t)i2c->DR;
    } else if (size == 2) {
        i2c->CR1 |= I2C_CR1_POS;
        i2c->CR1 &= ~I2C_CR1_ACK;
        i2c_clear_addr(i2c);
        if ((st = i2c_wait(i2c, I2C_SR1_BTF, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
        i2c->CR1 |= I2C_CR1_STOP;
        data[0] = (uint8_t)i2c->DR;
        data[1] = (uint8_t)i2c->DR;
        i2c->CR1 &= ~I2C_CR1_POS;
    } else {
        uint16_t i = 0;
        i2c->CR1 |= I2C_CR1_ACK;
        i2c_clear_addr(i2c);
        while (size - i > 3) {
            if ((st = i2c_wait(i2c, I2C_SR1_RXNE, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
            data[i++] = (uint8_t)i2c->DR;
        }
        /* Last three bytes: N-2 in DR, N-1 in shift register */
        if ((st = i2c_wait(i2c, I2C_SR1_BTF, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
        i2c->CR1 &= ~I2C_CR1_ACK;
        data[i++] = (uint8_t)i2c->DR;
        if ((st = i2c_wait(i2c, I2C_SR1_BTF, t0, timeout)) != HAL_OK) return i2c_fail(i2c, st);
        i2c->CR1 |= I2C_CR1_STOP;
        data[i++] = (uint8_t)i2c->DR;
        data[i++] = (uint8_t)i2c->DR;
    }

    return HAL_OK;
}
