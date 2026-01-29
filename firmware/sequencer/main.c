#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include "../common/adc.h"

// === Pin Configuration ===
// PB0: I2C SDA (future)
// PB1: LED output
// PB2: I2C SCL (future)
// PB3: Button input (ADC3)
// PB4: CV output (OC1B PWM)
#define LED_PIN PB1
#define BTN_PIN PB3
#define BTN_CH 3
#define CV_PIN PB4

// === Button Thresholds (ADC 0-255) ===
#define BTN_A_MAX 25
#define BTN_B_MAX 60
#define BTN_M_MAX 120
#define BTN_NONE_MIN 200

// === Button States ===
#define BTN_A 1
#define BTN_B 2
#define BTN_M 3
#define BTN_NONE 0

// === Timing ===
#define BPM 120
#define STEPS_PER_BEAT 4
#define MS_PER_STEP (60000 / BPM / STEPS_PER_BEAT) // 125ms

// === CV Output ===
#define CV_ACCENT 255 // Full accent for now (LFO later)

// === LED Brightness ===
#define LED_BAR_HEAD 255  // Bar start (step 0, 16): bright
#define LED_BEAT 15       // Other 8th notes: dim

// === Pattern ===
volatile uint32_t pattern = 0x00000000; // 32 steps, 1 bit each
volatile uint8_t current_step = 0;
volatile uint16_t tick_count = 0;
volatile uint8_t step_triggered = 0; // Flag: step just changed
volatile uint8_t current_btn = BTN_NONE; // Current button state (for ISR)
volatile uint8_t pattern_dirty = 0; // Flag: pattern changed, needs save

// === EEPROM ===
#define EEPROM_MAGIC_ADDR ((uint8_t*)0x00)
#define EEPROM_MAGIC_VALUE 0xA5
#define EEPROM_PATTERN_ADDR ((uint32_t*)0x01)

// === Timer0 ISR: 1ms tick ===
ISR(TIMER0_COMPA_vect)
{
    tick_count++;

    if (tick_count >= MS_PER_STEP)
    {
        tick_count = 0;
        step_triggered = 1;

        // LED: blink on quarter notes, except bar 2 beat 1 (8th notes)
        if (current_step >= 16 && current_step < 20) {
            // Bar 2 beat 1: 8th note blink (steps 16,18 on / 17,19 off)
            if (current_step & 0x01) {
                OCR1A = 0;
            } else {
                OCR1A = LED_BAR_HEAD;
            }
        } else if ((current_step & 0x03) != 0) {
            OCR1A = 0;  // Non-beat steps: off
        } else if (current_step == 0) {
            OCR1A = LED_BAR_HEAD;  // Bar 1 start: bright
        } else {
            OCR1A = LED_BEAT;  // Other beats: dim
        }

        // CV output (button overrides pattern)
        uint8_t should_play;
        if (current_btn == BTN_A) {
            should_play = 1;  // A held: always play
            pattern |= (1UL << current_step);  // Also add to pattern
            pattern_dirty = 1;
        } else if (current_btn == BTN_B) {
            should_play = 0;  // B held: always mute
            pattern &= ~(1UL << current_step);  // Also remove from pattern
            pattern_dirty = 1;
        } else {
            should_play = (pattern & (1UL << current_step)) ? 1 : 0;
        }

        if (should_play) {
            OCR1B = CV_ACCENT;
        } else {
            OCR1B = 0;
        }

        // Advance step
        current_step = (current_step + 1) & 0x1F;
    }

    // Auto-off CV after 10ms (LED stays on for full beat)
    if (tick_count == 10)
    {
        OCR1B = 0;
    }
}

// === Get Button State ===
uint8_t get_button(void)
{
    uint8_t val = read_adc(BTN_CH);
    if (val <= BTN_A_MAX)
        return BTN_A;
    if (val <= BTN_B_MAX)
        return BTN_B;
    if (val <= BTN_M_MAX)
        return BTN_M;
    return BTN_NONE;
}

// === Setup ===
void setup(void)
{
    // Timer1: PWM for CV on PB4 (OC1B) and LED on PB1 (OC1A)
    TCCR1 = (1 << PWM1A) | (1 << COM1A1) | (1 << CS10); // PWM on OC1A
    GTCCR = (1 << PWM1B) | (1 << COM1B1);               // PWM on OC1B
    OCR1A = 0;                                          // LED starts off
    OCR1B = 0;                                          // CV starts at 0
    OCR1C = 255;                                        // TOP

    // Timer0: 1ms interrupt
    TCCR0A = (1 << WGM01);
    TCCR0B = (1 << CS01) | (1 << CS00); // Prescaler 64
    OCR0A = 124;                        // 8MHz / 64 / 125 = 1kHz
    TIMSK |= (1 << OCIE0A);

    // ADC
    adc_init();

    // GPIO
    DDRB |= (1 << CV_PIN) | (1 << LED_PIN);

    sei();
}

// === Main ===
int main(void)
{
    // Load pattern from EEPROM (check magic byte for valid data)
    if (eeprom_read_byte(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
        pattern = eeprom_read_dword(EEPROM_PATTERN_ADDR);
    } else {
        pattern = 0x00000000;  // First boot: empty pattern
    }

    setup();

    uint8_t prev_step = 0;
    uint16_t b_hold_time = 0;  // B button hold duration in ms
    uint16_t last_tick = 0;

    while (1)
    {
        // Update button state continuously (for ISR to use)
        current_btn = get_button();

        // Calculate elapsed time since last loop
        uint16_t now = tick_count;
        uint16_t elapsed = (now >= last_tick) ? (now - last_tick) : (now + MS_PER_STEP - last_tick);
        last_tick = now;

        // B long press (2 sec) = clear pattern
        if (current_btn == BTN_B) {
            b_hold_time += elapsed;
            if (b_hold_time >= 2000) {
                pattern = 0x00000000;
                pattern_dirty = 1;
                b_hold_time = 0;  // Reset to prevent repeated clear
            }
        } else {
            b_hold_time = 0;
        }

        // Auto-save at pattern end (step 31→0) if pattern changed
        uint8_t step = current_step;  // Read once (volatile)
        if (step == 0 && prev_step == 31 && pattern_dirty) {
            eeprom_update_byte(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
            eeprom_update_dword(EEPROM_PATTERN_ADDR, pattern);
            pattern_dirty = 0;
        }
        prev_step = step;

        // M: reserved for mode switch (later)
    }
}
