/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Minimal HAL-compatible layer for the bare-metal project.
 *
 * nau7802.c and the main.c snippets were written against the STM32 HAL. This
 * project has no HAL, so this header provides just the types, macros and
 * functions they use, implemented on raw registers in hal_lite.c.
 ******************************************************************************
 */
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * HAL status / constants
 * ------------------------------------------------------------------------- */
typedef enum {
    HAL_OK      = 0x00,
    HAL_ERROR   = 0x01,
    HAL_BUSY    = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

#define HAL_MAX_DELAY          0xFFFFFFFFUL
#define I2C_MEMADD_SIZE_8BIT   1U

/* ---------------------------------------------------------------------------
 * Peripheral register layouts (RM0383)
 * ------------------------------------------------------------------------- */
typedef struct {
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CR1, CR2, OAR1, OAR2, DR, SR1, SR2, CCR, TRISE, FLTR;
} I2C_TypeDef;

typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_TypeDef;

#define GPIOA   ((GPIO_TypeDef  *)0x40020000UL)
#define GPIOB   ((GPIO_TypeDef  *)0x40020400UL)
#define I2C1    ((I2C_TypeDef   *)0x40005400UL)
#define I2C2    ((I2C_TypeDef   *)0x40005800UL)
#define USART2  ((USART_TypeDef *)0x40004400UL)

/* ---------------------------------------------------------------------------
 * Handles
 * ------------------------------------------------------------------------- */
typedef struct {
    I2C_TypeDef *Instance;
} I2C_HandleTypeDef;

typedef struct {
    USART_TypeDef *Instance;
} UART_HandleTypeDef;

extern I2C_HandleTypeDef  hi2c1;
extern I2C_HandleTypeDef  hi2c2;
extern UART_HandleTypeDef huart2;

/* Nucleo-64 user LED */
#define LD2_GPIO_Port   GPIOA
#define LD2_Pin         ((uint16_t)(1U << 5))

/* ---------------------------------------------------------------------------
 * Functions (hal_lite.c)
 * ------------------------------------------------------------------------- */
void     HAL_Init(void);              /* 1 ms SysTick */
uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t ms);

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *data,
                                    uint16_t size, uint32_t timeout);

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                        uint32_t trials, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t addr, uint16_t reg,
                                   uint16_t reg_size, uint8_t *data, uint16_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t addr, uint16_t reg,
                                    uint16_t reg_size, uint8_t *data, uint16_t size,
                                    uint32_t timeout);

/* Peripheral setup, same names CubeMX would generate */
void MX_GPIO_Init(void);
void MX_USART2_UART_Init(void);
void MX_I2C1_Init(void);
void MX_I2C2_Init(void);

#endif /* MAIN_H */
