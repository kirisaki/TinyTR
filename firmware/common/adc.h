#ifndef ADC_H
#define ADC_H

#include <avr/io.h>

// --- ADC Initialization ---
static inline void adc_init(void)
{
    ADMUX = (1 << ADLAR);  // Left adjust, VCC reference
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);  // Enable, prescaler 64
}

// --- ADC Read (8-bit) ---
static inline uint8_t read_adc(uint8_t channel)
{
    ADMUX = (1 << ADLAR) | (channel & 0x03);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCH;
}

#endif // ADC_H
