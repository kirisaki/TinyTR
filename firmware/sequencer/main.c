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
// Theoretical: A=0, B≈46, M≈85, None=255
// Midpoints: A-B=23, B-M=65, M-None=160
#define BTN_A_MAX 23
#define BTN_B_MAX 65
#define BTN_M_MAX 160
#define BTN_NONE_MIN 200

// === Button States ===
#define BTN_A 1
#define BTN_B 2
#define BTN_M 3
#define BTN_NONE 0

// === Modes ===
// Main layer (toggle with M short press)
#define MODE_PLAY 0
#define MODE_BANK 1
// Settings layer (enter/exit with M long press, cycle with M short press)
#define MODE_TEMPO 2
#define MODE_LFO_RATE 3
#define MODE_LFO_DEPTH 4
#define MODE_I2C 5

#define SETTINGS_MODE_FIRST MODE_TEMPO
#define SETTINGS_MODE_LAST MODE_I2C

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

// === Mode ===
volatile uint8_t current_mode = MODE_PLAY;

// === Bank ===
#define BANK_COUNT 8
#define BANK_NO_PENDING 0xFF
volatile uint8_t current_bank = 0;
volatile uint8_t pending_bank = BANK_NO_PENDING;  // Bank to switch at next bar

// === EEPROM Layout ===
// 0x00: Magic byte
// 0x01: Current bank number
// 0x02-0x21: 8 banks × 4 bytes = 32 bytes of patterns
#define EEPROM_MAGIC_ADDR ((uint8_t*)0x00)
#define EEPROM_MAGIC_VALUE 0xA5
#define EEPROM_BANK_ADDR ((uint8_t*)0x01)
#define EEPROM_PATTERNS_BASE ((uint32_t*)0x02)
#define EEPROM_PATTERN_ADDR(bank) ((uint32_t*)(0x02 + (bank) * 4))

// === CV Auto-off Timing ===
#define CV_GATE_MS 10

// === Bank Switch ===
// Schedule bank switch at next pattern start (step 0)
static void schedule_bank_switch(uint8_t new_bank)
{
    if (new_bank >= BANK_COUNT) return;
    if (new_bank == current_bank) {
        pending_bank = BANK_NO_PENDING;  // Cancel pending switch
        return;
    }
    pending_bank = new_bank;
}

// Apply pending bank switch (call at step 0)
static void apply_pending_bank(void)
{
    if (pending_bank == BANK_NO_PENDING) return;
    if (pending_bank >= BANK_COUNT) {
        pending_bank = BANK_NO_PENDING;
        return;
    }

    // Save current pattern to current bank
    eeprom_update_dword(EEPROM_PATTERN_ADDR(current_bank), pattern);

    // Load new bank's pattern
    current_bank = pending_bank;
    pattern = eeprom_read_dword(EEPROM_PATTERN_ADDR(current_bank));

    // Save current bank number
    eeprom_update_byte(EEPROM_BANK_ADDR, current_bank);

    pending_bank = BANK_NO_PENDING;
    pattern_dirty = 0;  // Just loaded, not dirty
}

// === LED Update ===
static inline void update_led(uint8_t step)
{
    switch (current_mode) {
    case MODE_PLAY:
        // Bar 2 beat 1: 8th note blink (steps 16,18 on / 17,19 off)
        if (step >= 16 && step < 20) {
            OCR1A = (step & 0x01) ? 0 : LED_BAR_HEAD;
        } else if ((step & 0x03) != 0) {
            OCR1A = 0;  // Non-beat steps: off
        } else if (step == 0) {
            OCR1A = LED_BAR_HEAD;  // Bar 1 start: bright
        } else {
            OCR1A = LED_BEAT;  // Other beats: dim
        }
        break;

    case MODE_TEMPO:
        // Flash on downbeat only (step 0 and 16)
        OCR1A = ((step & 0x0F) == 0) ? LED_BAR_HEAD : 0;
        break;

    case MODE_BANK:
        // Inverted pattern: bar head same, others inverted
        // Bar 2 beat 1: 8th note blink (same as Play)
        if (step >= 16 && step < 20) {
            OCR1A = (step & 0x01) ? 0 : LED_BAR_HEAD;
        } else if ((step & 0x03) != 0) {
            OCR1A = LED_BEAT;  // Non-beat steps: dim (inverted from Play)
        } else if (step == 0) {
            OCR1A = LED_BAR_HEAD;  // Bar 1 start: bright (same as Play)
        } else {
            OCR1A = 0;  // Other beats: off (inverted from Play)
        }
        break;

    case MODE_LFO_RATE:
    case MODE_LFO_DEPTH:
        // TODO: LFO-based LED patterns
        OCR1A = LED_BEAT;
        break;

    case MODE_I2C:
        // Double blink pattern
        OCR1A = ((step & 0x03) < 2) ? LED_BAR_HEAD : 0;
        break;
    }
}

// === CV Output + Pattern Update ===
static inline void update_cv(uint8_t step)
{
    uint8_t should_play;

    // Pattern editing only in Play mode (blocked during pending bank switch)
    uint8_t can_edit = (current_mode == MODE_PLAY && pending_bank == BANK_NO_PENDING);
    if (can_edit && current_btn == BTN_A) {
        should_play = 1;
        pattern |= (1UL << step);
        pattern_dirty = 1;
    } else if (can_edit && current_btn == BTN_B) {
        should_play = 0;
        pattern &= ~(1UL << step);
        pattern_dirty = 1;
    } else {
        should_play = (pattern & (1UL << step)) ? 1 : 0;
    }

    OCR1B = should_play ? CV_ACCENT : 0;
}

// === Timer0 ISR: 1ms tick ===
ISR(TIMER0_COMPA_vect)
{
    tick_count++;

    if (tick_count >= MS_PER_STEP)
    {
        tick_count = 0;
        step_triggered = 1;

        update_led(current_step);
        update_cv(current_step);

        current_step = (current_step + 1) & 0x1F;
    }

    if (tick_count == CV_GATE_MS)
    {
        OCR1B = 0;
    }
}

// === Get Button State (with debounce) ===
#define DEBOUNCE_COUNT 3
uint8_t get_button(void)
{
    static uint8_t stable_btn = BTN_NONE;
    static uint8_t last_raw = BTN_NONE;
    static uint8_t match_count = 0;

    // Read raw button
    uint8_t val = read_adc(BTN_CH);
    uint8_t raw;
    if (val <= BTN_A_MAX)
        raw = BTN_A;
    else if (val <= BTN_B_MAX)
        raw = BTN_B;
    else if (val <= BTN_M_MAX)
        raw = BTN_M;
    else
        raw = BTN_NONE;

    // Debounce: require consecutive matches
    if (raw == last_raw) {
        if (match_count < DEBOUNCE_COUNT)
            match_count++;
        if (match_count >= DEBOUNCE_COUNT)
            stable_btn = raw;
    } else {
        last_raw = raw;
        match_count = 0;
    }

    return stable_btn;
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
    // Load bank and pattern from EEPROM (check magic byte for valid data)
    if (eeprom_read_byte(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE) {
        current_bank = eeprom_read_byte(EEPROM_BANK_ADDR);
        if (current_bank >= BANK_COUNT) current_bank = 0;
        pattern = eeprom_read_dword(EEPROM_PATTERN_ADDR(current_bank));
    } else {
        // First boot: initialize EEPROM
        eeprom_update_byte(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
        eeprom_update_byte(EEPROM_BANK_ADDR, 0);
        for (uint8_t i = 0; i < BANK_COUNT; i++) {
            eeprom_update_dword(EEPROM_PATTERN_ADDR(i), 0x00000000);
        }
        current_bank = 0;
        pattern = 0x00000000;
    }

    setup();

    uint8_t prev_step = 0;
    uint8_t prev_btn = BTN_NONE;
    uint16_t b_hold_time = 0;  // B button hold duration in ms
    uint16_t m_hold_time = 0;  // M button hold duration in ms
    uint16_t last_tick = 0;

    while (1)
    {
        // Update button state continuously (for ISR to use)
        current_btn = get_button();

        // Calculate elapsed time since last loop
        uint16_t now = tick_count;
        uint16_t elapsed = (now >= last_tick) ? (now - last_tick) : (now + MS_PER_STEP - last_tick);
        last_tick = now;

        // B long press = clear pattern (only in Play mode, blocked during pending)
        if (current_mode == MODE_PLAY && pending_bank == BANK_NO_PENDING && current_btn == BTN_B) {
            b_hold_time += elapsed;
            if (b_hold_time >= 1200) {
                pattern = 0x00000000;
                pattern_dirty = 1;
                b_hold_time = 0;  // Reset to prevent repeated clear
            }
        } else {
            b_hold_time = 0;
        }

        // Mode button handling
        if (current_btn == BTN_M) {
            m_hold_time += elapsed;
        } else {
            if (prev_btn == BTN_M) {
                // M button released
                if (m_hold_time < 500) {
                    // Short press: toggle/cycle within layer
                    if (current_mode == MODE_PLAY) {
                        current_mode = MODE_BANK;
                    } else if (current_mode == MODE_BANK) {
                        current_mode = MODE_PLAY;
                    } else {
                        // Settings layer: cycle through settings modes
                        current_mode = (current_mode >= SETTINGS_MODE_LAST)
                            ? SETTINGS_MODE_FIRST
                            : current_mode + 1;
                    }
                } else {
                    // Long press: switch between main/settings layer
                    if (current_mode <= MODE_BANK) {
                        // Main → Settings (enter at Tempo)
                        current_mode = MODE_TEMPO;
                    } else {
                        // Settings → Main (return to Play)
                        current_mode = MODE_PLAY;
                    }
                }
            }
            m_hold_time = 0;  // Always reset when not pressing M
        }

        // Bank mode: A/B buttons change bank (on release)
        if (current_mode == MODE_BANK) {
            if (prev_btn == BTN_A && current_btn != BTN_A) {
                // A released: bank down (with wrap)
                uint8_t target = (pending_bank != BANK_NO_PENDING) ? pending_bank : current_bank;
                schedule_bank_switch((target + BANK_COUNT - 1) % BANK_COUNT);
            } else if (prev_btn == BTN_B && current_btn != BTN_B) {
                // B released: bank up (with wrap)
                uint8_t target = (pending_bank != BANK_NO_PENDING) ? pending_bank : current_bank;
                schedule_bank_switch((target + 1) % BANK_COUNT);
            }
        }
        prev_btn = current_btn;

        // Pattern start (step 31→0): apply pending bank switch and auto-save
        uint8_t step = current_step;  // Read once (volatile)
        if (step == 0 && prev_step == 31) {
            // Apply pending bank switch first
            apply_pending_bank();

            // Auto-save if pattern changed
            if (pattern_dirty) {
                eeprom_update_byte(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
                eeprom_update_dword(EEPROM_PATTERN_ADDR(current_bank), pattern);
                pattern_dirty = 0;
            }
        }
        prev_step = step;
    }
}
