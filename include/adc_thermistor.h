#ifndef ADC_THERMISTOR_H
#define ADC_THERMISTOR_H

#include <stdbool.h>
#include <stdint.h>

/* Thermistor on PB1 (board connector C8 "A0", net ARD_A0) -> ADC1_IN9. */

bool     adc_thermistor_init(void);      /* false if the SAI PLL would not lock */
uint16_t adc_thermistor_read_raw(void);  /* 12-bit count, 0xFFFF on timeout   */

/* Convert a raw ADC count to tenths of °F via the NTC B-parameter equation.
 * Returns false for out-of-range counts (timeout / rail saturation). */
bool adc_thermistor_raw_to_temp_f_tenths(uint16_t raw, int32_t *out_tenths);

#endif /* ADC_THERMISTOR_H */
