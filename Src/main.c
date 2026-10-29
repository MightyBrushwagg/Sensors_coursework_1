/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Wind speed from drag force. A load cell on a NAU7802 24-bit
 *                   ADC (I2C1) measures the drag on the sensor, and the board
 *                   prints one wind speed in m/s per line over the ST-LINK
 *                   virtual COM port (USART2, 115200 8N1) at OUTPUT_HZ.
 *
 * Bare-metal, no ST HAL: the HAL calls used here and in nau7802.c are
 * provided by hal_lite.c. Runs from the default 16 MHz HSI clock.
 *
 * Wiring (Nucleo-64): PB8 = SCL (D15), PB9 = SDA (D14), 3V3, GND.
 * Optional second board on I2C2: PB10 = SCL (D6), PB3 = SDA (D3).
 *
 * Physics:
 *   Drag F = Cd * A * q, where q = 0.5 * rho * v^2 is the dynamic pressure.
 *   Cd * A is unknown, so we calibrate it out: at a known tunnel pressure
 *   KNOWN_PSI the load cell reads ref_counts above zero. Force is linear in
 *   counts, so at any other reading
 *       q = q_ref * counts / ref_counts
 *       v = sqrt(2 * q / rho) = v_ref * sqrt(counts / ref_counts)
 *
 * Wind calibration flow (WIND_CAL_AT_BOOT = 1):
 *   1. Power up with the tunnel OFF -> it tares (zero wind).
 *   2. When prompted, set the tunnel to KNOWN_PSI within WIND_CAL_DELAY_S.
 *   3. It measures the reference force, then streams speeds. Change the tunnel
 *      speed as you like from there.
 *   It also prints REF_COUNTS; put that in the #define and set
 *   WIND_CAL_AT_BOOT to 0 to skip step 2 next time (it still tares at boot).
 *
 * Weight calibration (RUN_CALIBRATION = 1) is kept for checking the load cell
 * with a known mass. Speed does not depend on it.
 ******************************************************************************
 */

#include <stdint.h>
#include <stdio.h>
#include "main.h"
#include "nau7802.h"

/* ---- Output ---- */
#define OUTPUT_HZ           200      /* speed lines per second (brief: 200 Hz) */
#define SPEED_DECIMALS      2
#define FILTER_ALPHA        0.3f     /* low-pass on the force, 1.0 = off, smaller = smoother */

/* ---- Wind calibration ---- */
#define WIND_CAL_AT_BOOT    1        /* 1 = measure the reference force at KNOWN_PSI on every boot */
#define KNOWN_PSI           0.010f   /* tunnel dynamic pressure used for calibration, psi */
#define REF_COUNTS          10000.0f /* counts above zero at KNOWN_PSI, used when WIND_CAL_AT_BOOT = 0 */
#define AIR_DENSITY         1.225f   /* kg/m^3 */
#define PA_PER_PSI          6894.757f
#define WIND_CAL_DELAY_S    10       /* time to bring the tunnel up to KNOWN_PSI */
#define WIND_CAL_SAMPLES    250      /* ~0.8 s at 320 SPS */

/* ---- Weight calibration ---- */
#define RUN_CALIBRATION     0        /* 1 = run the guided weight calibration at boot */
#define KNOWN_MASS_GRAMS    50.0f   /* the reference weight you'll use */
#define TARE_AT_BOOT        1

/* Fill these in from the calibration printout */
#define ZERO_OFFSET  -154503
#define CAL_FACTOR   1026.06f     /* raw counts per gram */

/* actually worked calibration numbers
  #define ZERO_OFFSET  -154503
  #define CAL_FACTOR   1026.06f
*/

#define MIN_CAL_COUNTS      200      /* reject calibration if the load moves the reading less than this */

/* ---- ADC ---- */
#define ADC_RATE            NAU7802_RATE_320SPS   /* must be faster than OUTPUT_HZ */
#define TARE_SAMPLES        200      /* ~0.6 s at 320 SPS */
#define USE_SECOND_SCALE    0        /* 1 = second NAU7802 on I2C2 */

#define SCB_CPACR           (*(volatile uint32_t *)0xE000ED88UL)

NAU7802_t scale1;
#if USE_SECOND_SCALE
NAU7802_t scale2;
#endif

static float ref_counts = REF_COUNTS;   /* counts above zero at KNOWN_PSI */
static float v_ref;                     /* wind speed at KNOWN_PSI, m/s */

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
    /* Init leaves the ADC at 10 SPS; speed it up and redo the AFE offset cal */
    if (!NAU7802_SetRate(s, ADC_RATE) || !NAU7802_CalibrateAFE(s))
        halt_with_error("could not set ADC rate");
    printf("OK\r\n");

    NAU7802_SetCalibration(s, ZERO_OFFSET, CAL_FACTOR);

#if RUN_CALIBRATION
    printf("[%s] Remove everything from the scale. Taring in 3 s...\r\n", name);
    HAL_Delay(3000);
    if (!NAU7802_Tare(s, TARE_SAMPLES)) halt_with_error("tare failed");
    printf("[%s] Tared. Zero offset = %ld\r\n", name, (long)s->zero_offset);

    printf("[%s] Place ", name);
    print_fixed(KNOWN_MASS_GRAMS, 1);
    printf(" g on the scale now. Measuring in:\r\n");
    /* Show the live change from zero so you can see the weight register */
    for (int i = 10; i > 0; i--) {
        int32_t now;
        if (NAU7802_ReadAverage(s, 32, &now))
            printf("  %2d   raw=%ld  change=%ld counts\r\n", i, (long)now, (long)(now - s->zero_offset));
        HAL_Delay(800);
    }

    if (!NAU7802_CalibrateScale(s, KNOWN_MASS_GRAMS, TARE_SAMPLES))
        halt_with_error("calibration failed (reading didn't change - check A+/A- wiring)");

    int32_t change = (int32_t)(s->cal_factor * KNOWN_MASS_GRAMS);
    printf("[%s] Weight changed the reading by %ld counts\r\n", name, (long)change);
    if (change < MIN_CAL_COUNTS && change > -MIN_CAL_COUNTS)
        halt_with_error("weight barely changed the reading - check load cell wires and mounting, then reset");

    printf("[%s] Calibration done. Put these in your #defines:\r\n", name);
    printf("  #define ZERO_OFFSET  %ld\r\n", (long)s->zero_offset);
    printf("  #define CAL_FACTOR   %.6gf\r\n", (double)s->cal_factor);
    printf("  (If CAL_FACTOR is negative, swap A+ and A- or just keep the negative value.)\r\n\r\n");
    printf("[%s] Remove the weight. Re-taring in 5 s...\r\n", name);
    HAL_Delay(5000);
    if (!NAU7802_Tare(s, TARE_SAMPLES)) halt_with_error("tare failed");
#elif TARE_AT_BOOT
    printf("[%s] Taring - keep the tunnel OFF...\r\n", name);
    HAL_Delay(1000);
    if (!NAU7802_Tare(s, TARE_SAMPLES)) halt_with_error("tare failed");
#endif
}

/* Measure the force at the known tunnel pressure. Must run after the tare. */
static void calibrate_wind(NAU7802_t *s)
{
    printf("Set the tunnel to ");
    print_fixed(KNOWN_PSI, 4);
    printf(" psi. Measuring in:\r\n");
    for (int i = WIND_CAL_DELAY_S; i > 0; i--) {
        int32_t now;
        if (NAU7802_ReadAverage(s, 32, &now))
            printf("  %2d   change=%ld counts\r\n", i, (long)(now - s->zero_offset));
        HAL_Delay(900);
    }

    int32_t avg;
    if (!NAU7802_ReadAverage(s, WIND_CAL_SAMPLES, &avg)) halt_with_error("wind calibration read failed");
    ref_counts = (float)(avg - s->zero_offset);
    if (ref_counts < MIN_CAL_COUNTS && ref_counts > -MIN_CAL_COUNTS)
        halt_with_error("wind barely changed the reading - check the tunnel is running and the sensor is free");

    printf("Wind calibration done. To skip it next time:\r\n");
    printf("  #define REF_COUNTS   %ld.0f\r\n", (long)ref_counts);
}

/* Hardware square root on the M4 FPU. The toolchain links -lm before the
 * object files, so libm's sqrtf() never resolves. */
static inline float fsqrt(float x)
{
    float r;
    __asm__ ("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}

/* counts = filtered reading minus zero offset, so proportional to drag force */
static float counts_to_speed(float counts)
{
    float ratio = counts / ref_counts;   /* = q / q_ref */
    if (ratio <= 0.0f) return 0.0f;      /* noise around zero, or pushed backwards */
    return v_ref * fsqrt(ratio);
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
    printf("\r\nWind speed sensor\r\n");

    setup_scale(&scale1, &hi2c1, "Scale 1");
#if USE_SECOND_SCALE
    setup_scale(&scale2, &hi2c2, "Scale 2");
#endif

#if WIND_CAL_AT_BOOT
    calibrate_wind(&scale1);
#endif
    v_ref = fsqrt(2.0f * KNOWN_PSI * PA_PER_PSI / AIR_DENSITY);
    printf("Reference speed = ");
    print_fixed(v_ref, 2);
    printf(" m/s. Streaming at %d Hz\r\n", OUTPUT_HZ);

    /* Read every new ADC sample as it arrives (320 SPS) and print on a fixed
     * OUTPUT_HZ schedule. The schedule keeps the 1000 % OUTPUT_HZ remainder so
     * rates that don't divide 1000 ms still average out exactly. */
    float filtered = 0.0f;               /* counts above zero, after the tare */
    uint32_t due = HAL_GetTick();
    uint32_t rem = 0;

    for (;;)
    {
        int32_t raw;
        if (NAU7802_DataReady(&scale1) && NAU7802_ReadRaw(&scale1, &raw, 5))
            filtered += FILTER_ALPHA * ((float)(raw - scale1.zero_offset) - filtered);

        if ((int32_t)(HAL_GetTick() - due) >= 0) {
            print_fixed(counts_to_speed(filtered), SPEED_DECIMALS);
            printf("\r\n");

            due += 1000 / OUTPUT_HZ;
            rem += 1000 % OUTPUT_HZ;
            if (rem >= OUTPUT_HZ) { rem -= OUTPUT_HZ; due++; }

            HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);  /* heartbeat */
        }
    }
}
