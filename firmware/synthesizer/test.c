#include <avr/io.h>
#include <util/delay.h>
#include "voices.h"

// --- Pin Configuration ---
#define LED_PIN PB0     // LED for visual feedback
#define AMP_SD_PIN PB3  // Amplifier shutdown control (High=ON)
#define SPEAKER_PIN PB4 // PWM output

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

// --- Main Loop (Test Sequencer) ---
int main(void)
{
    setup();
    uint16_t beat_delay = 100; // Beat length (ms)

    while (1)
    {
        // Typical 8-beat + Clap

        // Beat 1
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);
        wait_exact_ms(beat_delay);

        // Beat 2
        trigger_kick();
        wait_exact_ms(beat_delay);
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);

        // Beat 3
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);
        trigger_kick();
        wait_exact_ms(beat_delay);

        // Beat 4
        wait_exact_ms(beat_delay);
        trigger_kick(); // Off-beat
        wait_exact_ms(beat_delay);

        // --- Bar 2 ---

        // Beat 1
        trigger_clap();
        wait_exact_ms(beat_delay);
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);

        // Beat 2
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);
        trigger_snare();
        wait_exact_ms(beat_delay);

        // Beat 3
        trigger_clap();
        wait_exact_ms(beat_delay);
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);

        // Beat 4
        trigger_hihat_closed();
        wait_exact_ms(beat_delay);
        wait_exact_ms(beat_delay);
        trigger_kick();
        wait_exact_ms(beat_delay * 4);
    }
}
