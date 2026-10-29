/*
 * nau7802.h - NAU7802 24-bit load cell ADC driver for STM32 HAL
 *
 * Works with both:
 *   - Adafruit NAU7802 STEMMA QT (product 4538)
 *   - SparkFun Qwiic Scale NAU7802 (SEN-15242)
 *
 * Both boards have a fixed I2C address of 0x2A, so if you run both at the
 * same time each one must be on its own I2C peripheral (e.g. I2C1 + I2C2).
 */
#ifndef NAU7802_H
#define NAU7802_H

#include "main.h"      /* CubeMX main.h pulls in the HAL for your STM32 family */
#include <stdint.h>
#include <stdbool.h>

#define NAU7802_I2C_ADDR   (0x2A << 1)   /* HAL wants the 8-bit (shifted) address */

typedef enum {
    NAU7802_GAIN_1   = 0,
    NAU7802_GAIN_2   = 1,
    NAU7802_GAIN_4   = 2,
    NAU7802_GAIN_8   = 3,
    NAU7802_GAIN_16  = 4,
    NAU7802_GAIN_32  = 5,
    NAU7802_GAIN_64  = 6,
    NAU7802_GAIN_128 = 7
} NAU7802_Gain;

typedef enum {
    NAU7802_RATE_10SPS  = 0,   /* lowest noise, good 50/60 Hz rejection */
    NAU7802_RATE_20SPS  = 1,
    NAU7802_RATE_40SPS  = 2,
    NAU7802_RATE_80SPS  = 3,
    NAU7802_RATE_320SPS = 7
} NAU7802_Rate;

typedef enum {
    NAU7802_LDO_4V5 = 0,
    NAU7802_LDO_4V2 = 1,
    NAU7802_LDO_3V9 = 2,
    NAU7802_LDO_3V6 = 3,
    NAU7802_LDO_3V3 = 4,
    NAU7802_LDO_3V0 = 5,
    NAU7802_LDO_2V7 = 6,
    NAU7802_LDO_2V4 = 7
} NAU7802_LDO;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    int32_t zero_offset;   /* raw reading with nothing on the scale */
    float   cal_factor;    /* raw counts per unit (e.g. per gram) */
} NAU7802_t;

/* Setup */
bool NAU7802_IsConnected(NAU7802_t *dev);
bool NAU7802_Init(NAU7802_t *dev, I2C_HandleTypeDef *hi2c);
bool NAU7802_SetGain(NAU7802_t *dev, NAU7802_Gain gain);
bool NAU7802_SetRate(NAU7802_t *dev, NAU7802_Rate rate);
bool NAU7802_SetLDO(NAU7802_t *dev, NAU7802_LDO ldo);
bool NAU7802_CalibrateAFE(NAU7802_t *dev);   /* internal offset cal; rerun after gain/rate change */

/* Reading */
bool NAU7802_DataReady(NAU7802_t *dev);
bool NAU7802_ReadRaw(NAU7802_t *dev, int32_t *out, uint32_t timeout_ms);
bool NAU7802_ReadAverage(NAU7802_t *dev, uint8_t samples, int32_t *out);

/* Scale helpers */
bool NAU7802_Tare(NAU7802_t *dev, uint8_t samples);
bool NAU7802_CalibrateScale(NAU7802_t *dev, float known_mass, uint8_t samples);
void NAU7802_SetCalibration(NAU7802_t *dev, int32_t zero_offset, float cal_factor);
bool NAU7802_GetUnits(NAU7802_t *dev, uint8_t samples, float *out);

#endif /* NAU7802_H */
