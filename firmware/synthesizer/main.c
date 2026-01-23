#include <avr/io.h>
#include <util/delay.h>
#include "voices.h"

// --- Pin Configuration ---
#define SPEAKER_PIN PB1  // PWM output (OC1A)
#define CV_INPUT_CH 2    // PB4 = ADC2

// --- CV Threshold (with hysteresis) ---
#define CV_THRESHOLD_ON  20  // ~0.4V to trigger
#define CV_THRESHOLD_OFF 5   // ~0.1V to reset

// --- ADC Functions ---
uint8_t read_adc(uint8_t channel)
{
    ADMUX = (1 << ADLAR) | (channel & 0x03);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCH;
}

// --- Setup ---
void setup()
{
    // 1. Timer1 for PWM (64MHz PLL -> 250kHz PWM)
    PLLCSR |= (1 << PLLE);
    _delay_ms(1);
    while (!(PLLCSR & (1 << PLOCK)))
        ;
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

// --- Main Loop ---
int main(void)
{
    setup();

    uint8_t prev_state = 0;

    while (1)
    {
        uint8_t cv = read_adc(CV_INPUT_CH);

        // State with hysteresis
        uint8_t curr_state;
        if (cv > CV_THRESHOLD_ON) {
            curr_state = 1;
        } else if (cv < CV_THRESHOLD_OFF) {
            curr_state = 0;
        } else {
            curr_state = prev_state;
        }

        // Rising edge (LOW -> HIGH) = trigger
        if (curr_state && !prev_state) {
            cli();  // Disable interrupts during 16-bit updates
            k_vol = 65535;
            k_active = 1;
            k_step = param_tone;
            k_phase = 0xC000;
            sei();  // Re-enable interrupts
        }

        prev_state = curr_state;
    }
}
