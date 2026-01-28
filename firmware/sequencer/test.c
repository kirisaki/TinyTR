#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// --- Pin Configuration ---
// PB3: LED output (GPIO)
// PB4: CV output (OC1B PWM)
#define LED_PIN PB3
#define CV_PIN PB4

// --- Timing ---
#define BPM 120
#define STEPS_PER_BEAT 4    // 16th notes
#define MS_PER_STEP (60000 / BPM / STEPS_PER_BEAT)  // ~125ms at 120BPM

// --- Pattern (16 steps, 0-255 accent) ---
// Funky hi-hat: accents on offbeats (&), ghost notes on "e" and "a"
// Beat:  1   e   &   a   2   e   &   a   3   e   &   a   4   e   &   a
uint8_t pattern[16] = {
    180,  0, 255, 100,   // 1 - & accented
    160,  0, 255,  80,   // 2
    180,  0, 255, 100,   // 3
    160, 90, 255, 120    // 4 - extra ghost before turnaround
};

volatile uint8_t current_step = 0;
volatile uint16_t tick_count = 0;

// --- Timer0 interrupt (~1ms tick) ---
ISR(TIMER0_COMPA_vect) {
    tick_count++;

    if (tick_count >= MS_PER_STEP) {
        tick_count = 0;

        // LED on quarter notes (every 4 steps)
        if ((current_step & 0x03) == 0) {
            PORTB |= (1 << LED_PIN);
        }

        // Output CV for current step
        uint8_t accent = pattern[current_step];

        if (accent > 0) {
            // Trigger: set CV voltage based on accent
            // Map 1-255 to ~10-255 PWM (avoid 0 = idle)
            OCR1B = 10 + ((uint16_t)accent * 245 / 255);
        } else {
            // No trigger: CV = 0
            OCR1B = 0;
        }

        // Advance step
        current_step = (current_step + 1) & 0x0F;  // Wrap at 16
    }

    // Auto-off after ~10ms
    if (tick_count == 10) {
        OCR1B = 0;
        PORTB &= ~(1 << LED_PIN);
    }
}

void setup(void) {
    // --- Timer1: PWM for CV on PB4 (OC1B) ---
    TCCR1 = (1 << CS10);                      // No prescaler, ~31kHz PWM
    GTCCR = (1 << PWM1B) | (1 << COM1B1);     // PWM on OC1B, clear on match
    OCR1B = 0;                                // Start with 0V
    OCR1C = 255;                              // TOP = 255

    // --- Timer0: ~1ms interrupt for timing ---
    // Requires 8MHz fuse (make fuse)
    TCCR0A = (1 << WGM01);                    // CTC mode
    TCCR0B = (1 << CS01) | (1 << CS00);       // Prescaler 64
    OCR0A = 124;                              // 8MHz / 64 / 125 = 1kHz (1ms)
    TIMSK |= (1 << OCIE0A);                   // Enable compare match interrupt

    // --- Pin configuration ---
    DDRB |= (1 << CV_PIN);   // CV output (PWM)
    DDRB |= (1 << LED_PIN);  // LED output (GPIO)

    sei();
}

int main(void) {
    setup();

    while (1) {
        // Main loop idle - timing handled by interrupt
    }
}
