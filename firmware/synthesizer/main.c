#include <avr/io.h>
#include <util/delay.h>
#include "voices.h"

// --- Pin Configuration ---
#define SPEAKER_PIN PB1  // PWM output (OC1A)
#define CV_INPUT_CH 2    // PB4 = ADC2
#define DECAY_CH 1       // PB2 = ADC1
#define TONE_CH 3        // PB3 = ADC3

// --- CV Threshold (with hysteresis) ---
#define CV_THRESHOLD_ON  10  // ~0.2V to trigger
#define CV_THRESHOLD_OFF 3   // ~0.06V to reset

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

    // 5. Voice selection button
    setup_voice_button();

    sei();
}

// --- Main Loop ---
int main(void)
{
    setup();

    uint8_t prev_state = 0;

    uint8_t loop_div = 0;  // Divide pot reading frequency

    while (1)
    {
        // Read CV first (high priority)
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

        // Rising edge (LOW -> HIGH) = trigger current voice with accent
        if (curr_state && !prev_state) {
            cli();  // Disable interrupts during 16-bit updates
            // Accent: CV voltage scales volume (min 25%, max 100%)
            // CV 10-255 maps to 16384-65535
            uint16_t accent_vol = 16384 + ((uint16_t)(cv - CV_THRESHOLD_ON) * 200);
            switch (current_voice) {
                case 0:  // Kick
                    k_vol = accent_vol;
                    k_active = 1;
                    k_step = param_tone;
                    // k_phase not reset - avoids click on retrigger
                    break;
                case 1:  // Snare
                    s_vol = (uint16_t)(((uint32_t)accent_vol * 35000) >> 16);
                    s_tone_vol = (uint16_t)(((uint32_t)accent_vol * 50000) >> 16);
                    s_phase = 0x6000;
                    s_active = 1;
                    break;
                case 2:  // Hi-hat
                    h_vol = (uint16_t)(((uint32_t)accent_vol * 30000) >> 16);
                    h_active = 1;
                    break;
                case 3:  // Clap
                    c_vol = (uint16_t)(((uint32_t)accent_vol * 50000) >> 16);
                    if (!c_active) {
                        // Fresh trigger: do stutter
                        c_stutter = 3;
                        c_stutter_timer = 0;
                    }
                    // Retrigger while active: skip stutter, just boost volume
                    c_active = 1;
                    break;
                case 4:  // Tom
                    t_vol = (uint16_t)(((uint32_t)accent_vol * 55000) >> 16);
                    t_active = 1;
                    t_step = param_tone + 200;  // Linked to TONE
                    t_phase = 0x6000;
                    break;
                case 5:  // Cowbell
                    cb_vol = (uint16_t)(((uint32_t)accent_vol * 45000) >> 16);
                    cb_active = 1;
                    cb_phase1 = 0x6000;
                    cb_phase2 = 0x6000;
                    break;
            }
            sei();  // Re-enable interrupts
        }

        prev_state = curr_state;

        // Check voice button frequently (every 16 loops)
        if ((loop_div & 0x0F) == 0) {
            update_voice_button();
        }

        // Read pots less frequently (every 256 loops)
        if (++loop_div == 0)
        {
            // Read DECAY/TONE potentiometers
            uint8_t decay_raw = read_adc(DECAY_CH);
            uint8_t tone_raw = read_adc(TONE_CH);

            // Map decay to valid values (3, 7, 15)
            if (decay_raw < 85) param_decay = 3;
            else if (decay_raw < 170) param_decay = 7;
            else param_decay = 15;

            // Map param_decay to per-voice decay
            cb_decay = (param_decay >> 1) | 1;
            h_decay = (param_decay >> 1) | 1;
            c_decay = (param_decay >> 1) | 1;
            t_decay = param_decay >> 1;
            s_decay = param_decay >> 1;
            k_decay = param_decay;

            // Map tone to frequency range
            param_tone = 470 + ((uint16_t)tone_raw * 6);
        }
    }
}
