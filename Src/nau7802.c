/*
 * nau7802.c - NAU7802 24-bit load cell ADC driver for STM32 HAL
 * Init sequence follows the Nuvoton datasheet and the Adafruit/SparkFun libraries.
 */
#include "nau7802.h"

/* ---- Registers ---- */
#define REG_PU_CTRL    0x00
#define REG_CTRL1      0x01
#define REG_CTRL2      0x02
#define REG_ADCO_B2    0x12   /* 0x12..0x14 = 24-bit result, MSB first */
#define REG_ADC        0x15
#define REG_PGA_PWR    0x1C
#define REG_REVISION   0x1F

/* PU_CTRL bits */
#define PU_RR     (1u << 0)   /* register reset */
#define PU_PUD    (1u << 1)   /* power up digital */
#define PU_PUA    (1u << 2)   /* power up analog */
#define PU_PUR    (1u << 3)   /* power up ready (read only) */
#define PU_CS     (1u << 4)   /* cycle start */
#define PU_CR     (1u << 5)   /* conversion ready (read only) */
#define PU_AVDDS  (1u << 7)   /* use internal LDO for AVDD */

/* CTRL1 fields */
#define CTRL1_GAIN_MASK   0x07u
#define CTRL1_VLDO_SHIFT  3
#define CTRL1_VLDO_MASK   (0x07u << CTRL1_VLDO_SHIFT)

/* CTRL2 fields */
#define CTRL2_CALMOD_MASK 0x03u
#define CTRL2_CALS        (1u << 2)
#define CTRL2_CAL_ERR     (1u << 3)
#define CTRL2_CRS_SHIFT   4
#define CTRL2_CRS_MASK    (0x07u << CTRL2_CRS_SHIFT)

#define ADC_REG_CHPS_MASK 0x30u   /* 11 = clock chopper off */
#define PGA_PWR_CAP_EN    0x80u   /* PGA output stabiliser cap */

#define I2C_TIMEOUT_MS    50

/* ---- Low-level helpers ---- */
static bool reg_read(NAU7802_t *dev, uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Mem_Read(dev->hi2c, NAU7802_I2C_ADDR, reg,
                            I2C_MEMADD_SIZE_8BIT, val, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool reg_write(NAU7802_t *dev, uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(dev->hi2c, NAU7802_I2C_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, &val, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool reg_modify(NAU7802_t *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t v;
    if (!reg_read(dev, reg, &v)) return false;
    v = (uint8_t)((v & ~mask) | (value & mask));
    return reg_write(dev, reg, v);
}

/* ---- Setup ---- */
bool NAU7802_IsConnected(NAU7802_t *dev)
{
    return HAL_I2C_IsDeviceReady(dev->hi2c, NAU7802_I2C_ADDR, 3, I2C_TIMEOUT_MS) == HAL_OK;
}

bool NAU7802_SetGain(NAU7802_t *dev, NAU7802_Gain gain)
{
    return reg_modify(dev, REG_CTRL1, CTRL1_GAIN_MASK, (uint8_t)gain);
}

bool NAU7802_SetRate(NAU7802_t *dev, NAU7802_Rate rate)
{
    return reg_modify(dev, REG_CTRL2, CTRL2_CRS_MASK, (uint8_t)(rate << CTRL2_CRS_SHIFT));
}

bool NAU7802_SetLDO(NAU7802_t *dev, NAU7802_LDO ldo)
{
    if (!reg_modify(dev, REG_CTRL1, CTRL1_VLDO_MASK, (uint8_t)(ldo << CTRL1_VLDO_SHIFT)))
        return false;
    return reg_modify(dev, REG_PU_CTRL, PU_AVDDS, PU_AVDDS);   /* enable internal LDO */
}

bool NAU7802_CalibrateAFE(NAU7802_t *dev)
{
    uint8_t v;

    /* CALMOD = 00 -> internal offset calibration, then set CALS to start */
    if (!reg_modify(dev, REG_CTRL2, CTRL2_CALMOD_MASK, 0x00)) return false;
    if (!reg_modify(dev, REG_CTRL2, CTRL2_CALS, CTRL2_CALS))  return false;

    uint32_t t0 = HAL_GetTick();
    do {
        if (!reg_read(dev, REG_CTRL2, &v)) return false;
        if (!(v & CTRL2_CALS)) break;              /* CALS clears when done */
        HAL_Delay(1);
    } while (HAL_GetTick() - t0 < 2000);

    if (v & (CTRL2_CALS | CTRL2_CAL_ERR)) return false;   /* timeout or error */

    /* Throw away a couple of conversions after calibration */
    int32_t dummy;
    NAU7802_ReadRaw(dev, &dummy, 500);
    NAU7802_ReadRaw(dev, &dummy, 500);
    return true;
}

bool NAU7802_Init(NAU7802_t *dev, I2C_HandleTypeDef *hi2c)
{
    uint8_t v = 0;

    dev->hi2c = hi2c;
    dev->zero_offset = 0;
    dev->cal_factor = 1.0f;

    if (!NAU7802_IsConnected(dev)) return false;

    /* Reset all registers */
    if (!reg_write(dev, REG_PU_CTRL, PU_RR)) return false;
    HAL_Delay(1);

    /* Leave reset, power up digital then analog */
    if (!reg_write(dev, REG_PU_CTRL, PU_PUD)) return false;
    if (!reg_modify(dev, REG_PU_CTRL, PU_PUA, PU_PUA)) return false;

    /* Wait for power-up ready */
    uint32_t t0 = HAL_GetTick();
    do {
        if (!reg_read(dev, REG_PU_CTRL, &v)) return false;
        if (v & PU_PUR) break;
        HAL_Delay(1);
    } while (HAL_GetTick() - t0 < 200);
    if (!(v & PU_PUR)) return false;

    /* Sanity check: low nibble of revision register is always 0xF */
    if (!reg_read(dev, REG_REVISION, &v) || (v & 0x0F) != 0x0F) return false;

    /*
     * Excitation voltage. 3.0 V leaves headroom when the board is fed 3.3 V
     * (the LDO needs ~0.3 V dropout). If you power the Adafruit board's VIN
     * from 5 V you can raise this to 3.3 V.
     */
    if (!NAU7802_SetLDO(dev, NAU7802_LDO_3V0)) return false;
    if (!NAU7802_SetGain(dev, NAU7802_GAIN_128)) return false;
    if (!NAU7802_SetRate(dev, NAU7802_RATE_10SPS)) return false;

    /* Turn off ADC clock chopper (recommended in datasheet) */
    if (!reg_modify(dev, REG_ADC, ADC_REG_CHPS_MASK, ADC_REG_CHPS_MASK)) return false;

    /* Enable PGA output stabiliser capacitor */
    if (!reg_modify(dev, REG_PGA_PWR, PGA_PWR_CAP_EN, PGA_PWR_CAP_EN)) return false;

    /* Start conversions */
    if (!reg_modify(dev, REG_PU_CTRL, PU_CS, PU_CS)) return false;

    HAL_Delay(300);   /* let the LDO and load cell excitation settle */

    return NAU7802_CalibrateAFE(dev);
}

/* ---- Reading ---- */
bool NAU7802_DataReady(NAU7802_t *dev)
{
    uint8_t v;
    if (!reg_read(dev, REG_PU_CTRL, &v)) return false;
    return (v & PU_CR) != 0;
}

bool NAU7802_ReadRaw(NAU7802_t *dev, int32_t *out, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (!NAU7802_DataReady(dev)) {
        if (HAL_GetTick() - t0 > timeout_ms) return false;
        HAL_Delay(1);
    }

    uint8_t b[3];
    if (HAL_I2C_Mem_Read(dev->hi2c, NAU7802_I2C_ADDR, REG_ADCO_B2,
                         I2C_MEMADD_SIZE_8BIT, b, 3, I2C_TIMEOUT_MS) != HAL_OK)
        return false;

    uint32_t raw = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    if (raw & 0x800000u) raw |= 0xFF000000u;   /* sign-extend 24 -> 32 bit */
    *out = (int32_t)raw;
    return true;
}

bool NAU7802_ReadAverage(NAU7802_t *dev, uint8_t samples, int32_t *out)
{
    if (samples == 0) samples = 1;
    int64_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        int32_t r;
        if (!NAU7802_ReadRaw(dev, &r, 500)) return false;
        sum += r;
    }
    *out = (int32_t)(sum / samples);
    return true;
}

/* ---- Scale helpers ---- */
bool NAU7802_Tare(NAU7802_t *dev, uint8_t samples)
{
    int32_t avg;
    if (!NAU7802_ReadAverage(dev, samples, &avg)) return false;
    dev->zero_offset = avg;
    return true;
}

bool NAU7802_CalibrateScale(NAU7802_t *dev, float known_mass, uint8_t samples)
{
    int32_t avg;
    if (known_mass == 0.0f) return false;
    if (!NAU7802_ReadAverage(dev, samples, &avg)) return false;

    int32_t diff = avg - dev->zero_offset;
    if (diff == 0) return false;               /* nothing changed: check wiring */
    dev->cal_factor = (float)diff / known_mass;
    return true;
}

void NAU7802_SetCalibration(NAU7802_t *dev, int32_t zero_offset, float cal_factor)
{
    dev->zero_offset = zero_offset;
    dev->cal_factor = cal_factor;
}

bool NAU7802_GetUnits(NAU7802_t *dev, uint8_t samples, float *out)
{
    int32_t avg;
    if (!NAU7802_ReadAverage(dev, samples, &avg)) return false;
    *out = (float)(avg - dev->zero_offset) / dev->cal_factor;
    return true;
}
