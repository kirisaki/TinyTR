#ifndef HARDWARE_H
#define HARDWARE_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// --- Pin Configuration ---
#define SPEAKER_PIN PB1  // PWM output (OC1A)
#define CV_INPUT_CH 2    // PB4 = ADC2
#define DECAY_CH 1       // PB2 = ADC1
#define TONE_CH 3        // PB3 = ADC3

// --- ADC Functions ---
static inline uint8_t read_adc(uint8_t channel)
{
    ADMUX = (1 << ADLAR) | (channel & 0x03);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCH;
}

// --- Hardware Setup ---
static inline void setup_hardware(void)
{
    // 1. Timer1 for PWM (64MHz PLL -> 250kHz PWM)
    PLLCSR |= (1 << PLLE);
    _delay_ms(1);
    while (!(PLLCSR & (1 << PLOCK)));
    PLLCSR |= (1 << PCKE);

    TCCR1 = (1 << PWM1A) | (1 << COM1A1) | (1 << CS10);
    GTCCR = 0;

    // 2. Timer0 for sampling (20kHz interrupt)
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01);
    OCR0A = 49;
    TIMSK |= (1 << OCIE0A);

    // 3. ADC initialization
    ADMUX = (1 << ADLAR);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);

    // 4. Pin configuration
    DDRB |= (1 << SPEAKER_PIN);

    sei();
}

#endif // HARDWARE_H
