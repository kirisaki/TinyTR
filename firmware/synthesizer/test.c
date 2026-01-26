#include <avr/io.h>
#include <util/delay.h>
#include "voices.h"

// --- Pin Configuration ---
#define SPEAKER_PIN PB1 // PWM output (OC1A)
#define DECAY_POT_CH 1  // PB2 = ADC1
#define TONE_POT_CH 3   // PB3 = ADC3

// --- ADC Functions ---
uint8_t read_adc(uint8_t channel)
{
    ADMUX = (1 << ADLAR) | (channel & 0x03); // Left adjust, select channel
    ADCSRA |= (1 << ADSC);                   // Start conversion
    while (ADCSRA & (1 << ADSC));            // Wait for completion
    return ADCH;                              // Return 8-bit result
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

    TCCR1 = (1 << PWM1A) | (1 << COM1A1) | (1 << CS10); // OC1A PWM, no prescaler
    GTCCR = 0;

    // 2. Timer0 for sampling (20kHz interrupt)
    TCCR0A = (1 << WGM01); // CTC mode
    TCCR0B = (1 << CS01);  // Prescaler 8
    OCR0A = 49;            // 8MHz/8/(49+1) = 20kHz
    TIMSK |= (1 << OCIE0A);

    // 3. ADC initialization
    ADMUX = (1 << ADLAR);  // Left adjust, VCC reference
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); // Enable, prescaler 64

    // 4. Pin configuration
    // PB1: PWM output, PB2/PB3: ADC input (default)
    DDRB |= (1 << SPEAKER_PIN);

    // 5. Voice selection button
    setup_voice_button();

    sei(); // Enable interrupts
}

// --- Read Potentiometers and Update Parameters ---
void update_params()
{
    uint8_t decay_raw = read_adc(DECAY_POT_CH);
    uint8_t tone_raw = read_adc(TONE_POT_CH);

    // Convert to parameter ranges
    // Decay: must be 2^n-1, max 7 to avoid artifacts
    uint8_t decay_idx = decay_raw >> 6;  // 0-3
    if (decay_idx > 2) decay_idx = 2;    // cap at 2
    param_decay = (1 << (decay_idx + 1)) - 1;  // 1, 3, 7

    // Tone: 700-1700 (narrower range to avoid artifacts)
    param_tone = 700 + ((uint16_t)tone_raw << 2);
}

// --- Wait with continuous pot/button reading ---
void wait_and_update(uint16_t ms)
{
    uint16_t target_ticks = ms * 20;
    tick_counter = 0;
    while (tick_counter < target_ticks) {
        update_params();
        update_voice_button();
    }
}

// --- Main Loop (Test Sequencer) ---
int main(void)
{
    setup();
    uint16_t step_delay = 125; // 16th note at 120 BPM (ms)

    while (1)
    {
        // 16-beat pattern: trigger on 1, 5, 9, 13 (four-on-the-floor)
        for (uint8_t step = 0; step < 16; step++) {
            if ((step & 3) == 0) {
                trigger_current_voice();
            }
            wait_and_update(step_delay);
        }
    }
}
