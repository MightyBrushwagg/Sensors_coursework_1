/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Read a load cell through a NAU7802 24-bit ADC over I2C1
 *                   and print the weight over the ST-LINK virtual COM port
 *                   (USART2, 115200 8N1).
 *
 * Bare-metal, no ST HAL: the HAL calls used here and in nau7802.c are
 * provided by hal_lite.c. Runs from the default 16 MHz HSI clock.
 *
 * Wiring (Nucleo-64): PB8 = SCL (D15), PB9 = SDA (D14), 3V3, GND.
 * Optional second board on I2C2: PB10 = SCL (D6), PB3 = SDA (D3).
 *
 * Calibration flow (first run, RUN_CALIBRATION = 1):
 *   1. Power up with the scale EMPTY -> it tares.
 *   2. When prompted, put your known weight on within 10 s.
 *   3. It prints ZERO_OFFSET and CAL_FACTOR. Copy them into the #defines below,
 *      set RUN_CALIBRATION to 0, rebuild. It will then start reading straight away
 *      (it still re-tares at boot unless you set TARE_AT_BOOT to 0).
 ******************************************************************************
 */

#include <stdint.h>
#include <stdio.h>
#include "main.h"
#include "nau7802.h"

#define RUN_CALIBRATION     0        /* 1 = run the guided calibration at boot */
#define KNOWN_MASS_GRAMS    50.0f   /* the reference weight you'll use */
#define TARE_AT_BOOT        1

/* Fill these in from the calibration printout */
#define ZERO_OFFSET  -154503
#define CAL_FACTOR   1026.06f     /* raw counts per gram */

/* actually worked calibration numbers
  #define ZERO_OFFSET  -154503
  #define CAL_FACTOR   1026.06f
*/

#define MIN_CAL_COUNTS      200      /* reject calibration if the weight moves the reading less than this */

#define SAMPLES_PER_READ    4        /* at 10 SPS -> ~0.4 s per reading */
#define USE_SECOND_SCALE    0        /* 1 = second NAU7802 on I2C2 */

#define SCB_CPACR           (*(volatile uint32_t *)0xE000ED88UL)

NAU7802_t scale1;
#if USE_SECOND_SCALE
NAU7802_t scale2;
#endif

/* Called by the startup code before main(). Enables the FPU, which the
 * hard-float build uses as soon as any float maths runs. */
void SystemInit(void)
{
    SCB_CPACR |= (0xFUL << 20);  /* CP10 + CP11 full access */
}

/* printf() -> _write() (syscall.c) -> __io_putchar() -> USART2 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* Print a float without needing printf float support enabled */
static void print_fixed(float v, int decimals)
{
    int32_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;

    int32_t s = (int32_t)(v * scale + (v >= 0 ? 0.5f : -0.5f));
    int32_t whole = s / scale;
    int32_t frac  = s % scale;
    if (frac < 0) frac = -frac;
    if (s < 0 && whole == 0) printf("-");
    printf("%ld.%0*ld", (long)whole, decimals, (long)frac);
}

static void halt_with_error(const char *msg)
{
    printf("ERROR: %s\r\n", msg);
    while (1) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);   /* green LED on Nucleo-64 */
        HAL_Delay(100);
    }
}

static void setup_scale(NAU7802_t *s, I2C_HandleTypeDef *hi2c, const char *name)
{
    printf("Init %s... ", name);
    if (!NAU7802_Init(s, hi2c)) {
        printf("\r\n");
        halt_with_error("NAU7802 not found or init failed. Check wiring/I2C pins.");
    }
    printf("OK\r\n");

    NAU7802_SetCalibration(s, ZERO_OFFSET, CAL_FACTOR);

#if RUN_CALIBRATION
    printf("[%s] Remove everything from the scale. Taring in 3 s...\r\n", name);
    HAL_Delay(3000);
    if (!NAU7802_Tare(s, 20)) halt_with_error("tare failed");
    printf("[%s] Tared. Zero offset = %ld\r\n", name, (long)s->zero_offset);

    printf("[%s] Place ", name);
    print_fixed(KNOWN_MASS_GRAMS, 1);
    printf(" g on the scale now. Measuring in:\r\n");
    /* Show the live change from zero so you can see the weight register */
    for (int i = 10; i > 0; i--) {
        int32_t now;
        if (NAU7802_ReadAverage(s, 2, &now))
            printf("  %2d   raw=%ld  change=%ld counts\r\n", i, (long)now, (long)(now - s->zero_offset));
        HAL_Delay(800);
    }

    if (!NAU7802_CalibrateScale(s, KNOWN_MASS_GRAMS, 20))
        halt_with_error("calibration failed (reading didn't change - check A+/A- wiring)");

    int32_t change = (int32_t)(s->cal_factor * KNOWN_MASS_GRAMS);
    printf("[%s] Weight changed the reading by %ld counts\r\n", name, (long)change);
    if (change < MIN_CAL_COUNTS && change > -MIN_CAL_COUNTS)
        halt_with_error("weight barely changed the reading - check load cell wires and mounting, then reset");

    printf("[%s] Calibration done. Put these in your #defines:\r\n", name);
    printf("  #define ZERO_OFFSET  %ld\r\n", (long)s->zero_offset);
    printf("  #define CAL_FACTOR   %.6gf\r\n", (double)s->cal_factor);
    printf("  (If CAL_FACTOR is negative, swap A+ and A- or just keep the negative value.)\r\n\r\n");
#elif TARE_AT_BOOT
    printf("[%s] Taring - keep scale empty...\r\n", name);
    HAL_Delay(1000);
    if (!NAU7802_Tare(s, 20)) halt_with_error("tare failed");
#endif
}

int main(void)
{
    HAL_Init();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();
#if USE_SECOND_SCALE
    MX_I2C2_Init();
#endif

    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered printf */
    printf("\r\nNAU7802 load cell demo\r\n");

    setup_scale(&scale1, &hi2c1, "Scale 1");
#if USE_SECOND_SCALE
    setup_scale(&scale2, &hi2c2, "Scale 2");
#endif

    for (;;)
    {
        float grams;
        int32_t raw;

        if (NAU7802_ReadAverage(&scale1, SAMPLES_PER_READ, &raw)) {
            grams = (float)(raw - scale1.zero_offset) / scale1.cal_factor;
            printf("S1 raw=%ld  weight=", (long)raw);
            print_fixed(grams, 1);
            printf(" g\r\n");
        } else {
            printf("S1 read timeout\r\n");
        }

#if USE_SECOND_SCALE
        if (NAU7802_GetUnits(&scale2, SAMPLES_PER_READ, &grams)) {
            printf("S2 weight=");
            print_fixed(grams, 1);
            printf(" g\r\n");
        } else {
            printf("S2 read timeout\r\n");
        }
#endif

        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);  /* heartbeat */
    }
}
