#include "hardware.h"
#include "voices.h"

// --- CV Threshold (with hysteresis) ---
#define CV_THRESHOLD_ON  10  // ~0.2V to trigger
#define CV_THRESHOLD_OFF 3   // ~0.06V to reset

// --- Setup ---
static void setup(void)
{
    setup_hardware();
    setup_voice_button();
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
            // Accent: CV voltage scales volume (min 25%, max 100%)
            // CV 10-255 maps to 16384-65535
            uint16_t accent_vol = 16384 + ((uint16_t)(cv - CV_THRESHOLD_ON) * 200);
            cli();
            trigger_current_voice_with_accent(accent_vol);
            sei();
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
