#include "hardware.h"
#include "voices.h"

// --- Setup ---
static void setup(void)
{
    setup_hardware();
    setup_voice_button();
}

// --- Read Potentiometers and Update Parameters ---
void update_params()
{
    uint8_t decay_raw = read_adc(DECAY_CH);
    uint8_t tone_raw = read_adc(TONE_CH);

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
