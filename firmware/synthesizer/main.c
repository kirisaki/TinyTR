#include <avr/io.h>
#include <util/delay.h>
#include "voices.h"

// --- Pin Configuration ---
#define LED_PIN PB0     // LED for visual feedback
#define AMP_SD_PIN PB3  // Amplifier shutdown control (High=ON)
#define SPEAKER_PIN PB4 // PWM output
// TODO: Define sequencer input pins

// --- Setup ---
void setup()
{
    // 1. Timer1 for PWM (64MHz PLL -> 250kHz PWM)
    PLLCSR |= (1 << PLLE);
    _delay_ms(1);
    while (!(PLLCSR & (1 << PLOCK)))
        ;
    PLLCSR |= (1 << PCKE);

    TCCR1 = (1 << CS10); // No prescaler
    GTCCR = (1 << PWM1B) | (1 << COM1B1);

    // 2. Timer0 for sampling (20kHz interrupt)
    TCCR0A = (1 << WGM01); // CTC mode
    TCCR0B = (1 << CS01);  // Prescaler 8
    OCR0A = 49;            // 8MHz/8/(49+1) = 20kHz
    TIMSK |= (1 << OCIE0A);

    // 3. Pin configuration
    DDRB |= (1 << SPEAKER_PIN) | (1 << LED_PIN) | (1 << AMP_SD_PIN);

    // Start amplifier
    PORTB |= (1 << AMP_SD_PIN);

    sei(); // Enable interrupts
}

// --- Main Loop ---
int main(void)
{
    setup();

    // TODO: Implement sequencer signal reception
    // This version will receive trigger signals from the sequencer chip

    while (1)
    {
        // Wait for sequencer signals
    }
}