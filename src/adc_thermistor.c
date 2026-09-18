#include "adc_thermistor.h"
#include "stm32f4xx_hal.h"

/* Thermistor input: PB1 (board connector C8 "A0", net ARD_A0) -> ADC1_IN9 */
#define THERMISTOR_GPIO_PORT    GPIOB
#define THERMISTOR_GPIO_PIN     GPIO_PIN_1
#define THERMISTOR_ADC_CHANNEL  ADC_CHANNEL_9

/* The F469's ADC is fed from the SAI PLL (PLLSAI_Q). That same PLL also clocks
 * the LTDC — BSP_LCD_InitEx() programs N=384 / R=7 for it, so we pre-configure
 * it here with an added Q divider for the ADC: VCO = 384 MHz, Q = 12 -> 32 MHz.
 * Must run BEFORE BSP_LCD_InitEx(): its HAL_RCCEx_PeriphCLKConfig() call rewrites
 * N/R but preserves P/Q from the register. */
#define THERMO_PLLSAIN   384U
#define THERMO_PLLSAIQ   12U
#define THERMO_PLLSAIR    7U

/* Thermistor divider — ASSUMED values, confirm against the actual part:
 * VCC --[R_FIXED]-- A0/PB1 --[NTC]-- GND. The ~1.67 V room-temp reading is
 * consistent with R_NTC(25 °C) ≈ R_FIXED (midpoint of the divider). */
#define THERMISTOR_R_FIXED   10000   /* pull-up to VCC, ohms            */
#define THERMISTOR_R25       10000   /* NTC resistance at 25 °C, ohms   */
#define THERMISTOR_B         3950    /* B constant, K                   */

static ADC_HandleTypeDef s_hadc;

bool adc_thermistor_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t tickstart;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* P = /2 (unused output), Q = 12 -> ADC, R = 7 -> LTDC */
    __HAL_RCC_PLLSAI_CONFIG(THERMO_PLLSAIN, 2U, THERMO_PLLSAIQ, THERMO_PLLSAIR);
    __HAL_RCC_PLLSAI_ENABLE();

    tickstart = HAL_GetTick();
    while (__HAL_RCC_PLLSAI_GET_FLAG() == RESET) {
        if (HAL_GetTick() - tickstart > 100) {
            __HAL_RCC_PLLSAI_DISABLE();   /* leave the board as we found it */
            return false;
        }
    }

    gpio.Pin   = THERMISTOR_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_ANALOG;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(THERMISTOR_GPIO_PORT, &gpio);

    s_hadc.Instance                   = ADC1;
    s_hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_hadc.Init.Resolution            = ADC_RESOLUTION_12B;
    s_hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_hadc.Init.ScanConvMode          = DISABLE;
    s_hadc.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    s_hadc.Init.ContinuousConvMode    = DISABLE;
    s_hadc.Init.NbrOfConversion       = 1;
    s_hadc.Init.DiscontinuousConvMode = DISABLE;
    s_hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;

    if (HAL_ADC_Init(&s_hadc) != HAL_OK) return false;

    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = THERMISTOR_ADC_CHANNEL;
    ch.Rank         = 1U;
    ch.SamplingTime = ADC_SAMPLETIME_56CYCLES;
    if (HAL_ADC_ConfigChannel(&s_hadc, &ch) != HAL_OK) return false;

    return true;
}

uint16_t adc_thermistor_read_raw(void)
{
    if (HAL_ADC_Start(&s_hadc) != HAL_OK) return 0xFFFF;

    if (HAL_ADC_PollForConversion(&s_hadc, 5) != HAL_OK) {
        HAL_ADC_Stop(&s_hadc);
        return 0xFFFF;
    }

    uint16_t raw = (uint16_t)HAL_ADC_GetValue(&s_hadc);
    HAL_ADC_Stop(&s_hadc);
    return raw;
}

/* ---------------------------------------------------------------------------
 * NTC B-parameter conversion — pure integer math (no FPU in this build).
 *   R(T)  = R25 · exp(B·(1/T − 1/298.15))          →  T from measured R
 *   ln(x) via argument reduction x = m·2^e, m ∈ [1,2), then the atanh series
 *   ln(m) = 2(z + z³/3 + z⁵/5), z = (m−1)/(m+1) — error < 0.0002 nats here.
 * ------------------------------------------------------------------------- */

/* Natural log of num/den in milli-nats (×1000). num, den > 0. */
static int32_t fixed_ln_ratio(int32_t num, int32_t den)
{
    bool neg = false;
    uint32_t q;

    if (num >= den) {
        q = (uint32_t)(((int64_t)num << 16) / den);   /* ratio in Q16 */
    } else {
        q = (uint32_t)(((int64_t)den << 16) / num);
        neg = true;
    }

    int e = 0;
    while (q >= (2U << 16)) { q >>= 1; e++; }         /* mantissa m ∈ [1,2) */

    int32_t z = (int32_t)(((int64_t)(q - (1U << 16)) << 16) / (q + (1U << 16)));
    int64_t z2 = ((int64_t)z * z) >> 16;              /* all Q16 */
    int64_t z3 = (z2 * z) >> 16;
    int64_t z5 = (z3 * z2) >> 16;
    int32_t ln_m_q16 = (int32_t)(2 * (z + z3 / 3 + z5 / 5));

    /* ln(ratio) = e·ln2 + ln(m); ln2 ≈ 0.693147 → 45426 in Q16 */
    int32_t ln_q16 = e * 45426 + ln_m_q16;
    if (neg) ln_q16 = -ln_q16;

    return (int32_t)(((int64_t)ln_q16 * 1000) >> 16);
}

bool adc_thermistor_raw_to_temp_f_tenths(uint16_t raw, int32_t *out_tenths)
{
    if (raw == 0xFFFF || raw >= 4095 || raw == 0) return false;

    /* Divider: VCC --[R_FIXED]-- A0 --[NTC]-- GND → R = Rf·V/(VCC−V) */
    int32_t r_therm = (int32_t)(((int64_t)THERMISTOR_R_FIXED * raw) / (4095 - raw));
    if (r_therm <= 0) return false;

    /* 1/T_K = 1/298.15 + ln(R/R25)/B, computed in micro-K⁻¹ */
    int32_t ln_milli = fixed_ln_ratio(r_therm, THERMISTOR_R25);
    int64_t inv_t_u6 = 3354 + ((int64_t)ln_milli * 1000) / THERMISTOR_B;
    if (inv_t_u6 <= 0) return false;

    /* K → °C in hundredths, then °F: F = C·9/5 + 32 */
    int32_t t_k_x100 = (int32_t)(100000000LL / inv_t_u6);
    int32_t t_c_x100 = t_k_x100 - 27315;
    int32_t f_x100   = (t_c_x100 * 9) / 5 + 3200;

    *out_tenths = (f_x100 + 5) / 10;                  /* hundredths → tenths */
    return true;
}
