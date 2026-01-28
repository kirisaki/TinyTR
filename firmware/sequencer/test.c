#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// --- Pin Configuration ---
// PB1: LED output (GPIO)
// PB3: Button input (ADC3)
// PB4: CV output (OC1B PWM)
#define LED_PIN PB1
#define BTN_PIN PB3
#define BTN_CH 3
#define CV_PIN PB4

// --- Button thresholds (ADC 0-255) ---
#define BTN_A_MAX 25
#define BTN_B_MAX 60
#define BTN_M_MAX 120

// --- Timing ---
#define BPM 120
#define STEPS_PER_BEAT 4    // 16th notes
#define MS_PER_STEP (60000 / BPM / STEPS_PER_BEAT)  // ~125ms at 120BPM

// --- Patterns (16 steps, 0-255 accent) ---
// Pattern A: Funky hi-hat
const uint8_t pattern_a[16] = {
    180,  0, 255, 100,
    160,  0, 255,  80,
    180,  0, 255, 100,
    160, 90, 255, 120
};

// Pattern B: 4-on-the-floor
const uint8_t pattern_b[16] = {
    255,  0,  0,  0,
    200,  0,  0,  0,
    230,  0,  0,  0,
    180,  0,  0,  0
};

// Pattern M: Mute
const uint8_t pattern_mute[16] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};

volatile const uint8_t *current_pattern = pattern_a;

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
        uint8_t accent = current_pattern[current_step];

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

uint8_t read_adc(uint8_t channel) {
    ADMUX = (1 << ADLAR) | (channel & 0x03);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADCH;
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

    // --- ADC for button reading ---
    ADMUX = (1 << ADLAR);                     // Left-adjust, VCC ref
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);  // Enable, prescaler 64

    // --- Pin configuration ---
    DDRB |= (1 << CV_PIN);   // CV output (PWM)
    DDRB |= (1 << LED_PIN);  // LED output (GPIO)
    // PB3 is input by default (for ADC)

    sei();
}

int main(void) {
    setup();

    while (1) {
        uint8_t btn = read_adc(BTN_CH);

        if (btn <= BTN_A_MAX) {
            current_pattern = pattern_a;
        } else if (btn <= BTN_B_MAX) {
            current_pattern = pattern_b;
        } else if (btn <= BTN_M_MAX) {
            current_pattern = pattern_mute;
        }
        // None pressed: keep current pattern

        _delay_ms(50);  // Debounce
    }
}
