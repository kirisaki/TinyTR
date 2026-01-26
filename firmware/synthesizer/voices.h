#ifndef VOICES_H
#define VOICES_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

// --- Sine Wave Table (PROGMEM) ---
const uint8_t sinewave[] PROGMEM = {
    128, 134, 140, 147, 153, 159, 165, 171, 177, 182, 188, 193, 198, 203, 208, 212,
    216, 220, 224, 227, 231, 234, 236, 239, 241, 243, 245, 247, 248, 249, 250, 250,
    250, 250, 249, 248, 247, 245, 243, 241, 239, 236, 234, 231, 227, 224, 220, 216,
    212, 208, 203, 198, 193, 188, 182, 177, 171, 165, 159, 153, 147, 140, 134, 128,
    122, 116, 109, 103, 97, 91, 85, 79, 74, 68, 63, 58, 53, 48, 44, 40,
    36, 32, 29, 25, 22, 19, 17, 15, 13, 11, 9, 8, 7, 6, 6, 6,
    6, 7, 8, 9, 11, 13, 15, 17, 19, 22, 25, 29, 32, 36, 40, 44,
    48, 53, 58, 63, 68, 74, 79, 85, 91, 97, 103, 109, 116, 122};

// --- Global Variables (for Mixer) ---
volatile uint16_t tick_counter = 0;

// --- Configurable Parameters (set via ADC) ---
volatile uint8_t param_decay = 7;      // Decay speed (must be 2^n-1: 1,3,7,15)
volatile uint16_t param_tone = 1000;   // Initial pitch for kick

// --- Voice Selection Button ---
#define VOICE_BTN_PIN PB0
#define NUM_VOICES 6
volatile uint8_t current_voice = 0;    // 0=kick, 1=snare, 2=hihat, 3=clap, 4=tom, 5=cowbell
volatile uint8_t btn_prev_state = 1;   // Previous button state (1=released)

// Kick
volatile uint16_t k_phase = 0;
volatile uint16_t k_step = 0;
volatile uint16_t k_vol = 0;
volatile uint8_t k_active = 0;

// Snare
volatile uint16_t s_vol = 0;        // Noise volume
volatile uint16_t s_tone_vol = 0;   // Tonal body volume
volatile uint16_t s_phase = 0;
volatile uint8_t s_active = 0;

// Hi-Hat
volatile uint16_t h_vol = 0;
volatile uint8_t h_active = 0;
volatile uint8_t h_decay_speed = 1; // 1=Short, 3=Long

// Clap
volatile uint16_t c_vol = 0;
volatile uint8_t c_active = 0;

// Tom
volatile uint16_t t_phase = 0;
volatile uint16_t t_step = 0;
volatile uint16_t t_vol = 0;
volatile uint8_t t_active = 0;

// Cowbell (two oscillators)
volatile uint16_t cb_phase1 = 0;
volatile uint16_t cb_phase2 = 0;
volatile uint16_t cb_vol = 0;
volatile uint8_t cb_active = 0;

// Noise Generator (shared by Snare/Hat/Clap)
volatile uint16_t lfsr = 0xACE1;

// --- Sound Synthesis Engine (Inline Functions) ---

// 1. Kick calculation: Sine wave + Pitch sweep + Exponential decay
static inline int16_t calc_kick()
{
    if (!k_active)
        return 0;

    // Pitch sweep downward (end point linked to param_tone)
    uint16_t tone_end = param_tone / 20;
    if (k_step > tone_end)
        k_step--;

    // Volume decay
    static uint8_t div = 0;
    if ((++div & param_decay) == 0)
    { // Decay speed controlled by param_decay
        uint16_t decay = k_vol >> 8;
        if (decay == 0 && k_vol > 0)
            decay = 1;
        if (k_vol > decay)
            k_vol -= decay;
        else
        {
            k_vol = 0;
            k_active = 0;
        }
    }

    // Waveform generation
    k_phase += k_step;
    // Read from PROGMEM
    uint8_t raw = pgm_read_byte(&sinewave[(k_phase >> 8) & 0x7F]);

    // Apply volume (unipolar output)
    return ((raw * (k_vol >> 8)) >> 8);
}

// 2. Snare calculation: Tonal body + Noise
static inline int16_t calc_snare()
{
    if (!s_active)
        return 0;

    // Noise generation (LFSR)
    uint8_t lsb = lfsr & 1;
    lfsr >>= 1;
    if (lsb)
        lfsr ^= 0xB400;

    // Decay for both components
    static uint8_t div = 0;
    if ((++div & 3) == 0)
    {
        // Noise decay (slower)
        uint16_t decay = s_vol >> 8;
        if (decay == 0 && s_vol > 0) decay = 1;
        if (s_vol > decay) s_vol -= decay;
        else s_vol = 0;

        // Tone decay (faster)
        uint16_t tone_decay = s_tone_vol >> 6;
        if (tone_decay == 0 && s_tone_vol > 0) tone_decay = 1;
        if (s_tone_vol > tone_decay) s_tone_vol -= tone_decay;
        else s_tone_vol = 0;

        // Deactivate when both are done
        if (s_vol == 0 && s_tone_vol == 0)
            s_active = 0;
    }

    // Tonal body (~180Hz for snare punch)
    s_phase += 590;
    uint8_t tone_raw = pgm_read_byte(&sinewave[(s_phase >> 8) & 0x7F]);
    int16_t tone_out = ((tone_raw * (s_tone_vol >> 8)) >> 8);

    // Noise output
    int16_t noise_out = (lfsr & 1) ? (s_vol >> 8) : 0;

    return tone_out + noise_out;
}

// 3. Hi-Hat calculation (Shared for Open/Closed)
static inline int16_t calc_hihat()
{
    if (!h_active)
        return 0;

    uint8_t lsb = lfsr & 1;
    lfsr >>= 1;
    if (lsb)
        lfsr ^= 0xB400;

    static uint8_t div = 0;
    // decay_speed: 1=every 2 calls (closed), 3=every 4 calls (open), 7=every 8 calls
    if ((++div & h_decay_speed) == 0)
    {
        uint16_t decay = h_vol >> 7;
        if (decay == 0 && h_vol > 0)
            decay = 1;
        if (h_vol > decay)
            h_vol -= decay;
        else
        {
            h_vol = 0;
            h_active = 0;
        }
    }
    return (lfsr & 1) ? (h_vol >> 8) : 0;
}

// 4. Clap calculation
static inline int16_t calc_clap()
{
    if (!c_active)
        return 0;

    // Noise generation (shared with Snare/Hat)
    uint8_t lsb = lfsr & 1;
    lfsr >>= 1;
    if (lsb)
        lfsr ^= 0xB400;

    // Clap envelope
    static uint8_t div = 0;
    if ((++div & 7) == 0)
    { // Every 8 calls (slightly longer decay)
        uint16_t decay = c_vol >> 8;
        if (decay == 0 && c_vol > 0)
            decay = 1;
        if (c_vol > decay)
            c_vol -= decay;
        else
        {
            c_vol = 0;
            c_active = 0;
        }
    }

    // Adjusted shift from >>7 to >>8 for level matching
    return ((lfsr & 3) == 0) ? (c_vol >> 8) : 0;
}

// 5. Tom calculation: Similar to kick but higher pitch, faster decay
static inline int16_t calc_tom()
{
    if (!t_active)
        return 0;

    // Pitch sweep downward (faster than kick)
    if (t_step > 80)
        t_step--;

    // Volume decay (faster than kick)
    static uint8_t div = 0;
    if ((++div & 3) == 0)
    {
        uint16_t decay = t_vol >> 7;
        if (decay == 0 && t_vol > 0)
            decay = 1;
        if (t_vol > decay)
            t_vol -= decay;
        else
        {
            t_vol = 0;
            t_active = 0;
        }
    }

    // Waveform generation
    t_phase += t_step;
    uint8_t raw = pgm_read_byte(&sinewave[(t_phase >> 8) & 0x7F]);
    return ((raw * (t_vol >> 8)) >> 8);
}

// 6. Cowbell calculation: Two detuned oscillators
static inline int16_t calc_cowbell()
{
    if (!cb_active)
        return 0;

    // Volume decay (very fast for metallic sound)
    static uint8_t div = 0;
    if ((++div & 1) == 0)
    {
        uint16_t decay = cb_vol >> 7;
        if (decay == 0 && cb_vol > 0)
            decay = 1;
        if (cb_vol > decay)
            cb_vol -= decay;
        else
        {
            cb_vol = 0;
            cb_active = 0;
        }
    }

    // Two oscillators: 587Hz and 845Hz (at 20kHz sample rate)
    // step = freq * 65536 / 20000
    cb_phase1 += 1925;  // ~587Hz
    cb_phase2 += 2770;  // ~845Hz

    uint8_t raw1 = pgm_read_byte(&sinewave[(cb_phase1 >> 8) & 0x7F]);
    uint8_t raw2 = pgm_read_byte(&sinewave[(cb_phase2 >> 8) & 0x7F]);

    // Mix both oscillators
    uint16_t mixed = ((uint16_t)raw1 + raw2) >> 1;
    return ((mixed * (cb_vol >> 8)) >> 8);
}

// --- Interrupt Mixer (20kHz) ---
ISR(TIMER0_COMPA_vect)
{
    tick_counter++;
    int16_t output = 0;

    // Mix all instrument sounds
    output += calc_kick();
    output += calc_snare();
    output += calc_hihat();
    output += calc_clap();
    output += calc_tom();
    output += calc_cowbell();

    // Overflow protection after mixing
    // Divide by 2 to create headroom since sum can exceed 255
    output = output >> 1;

    // Clipping if still exceeds 255
    if (output > 255)
        output = 255;

    // PWM output (OC1A = PB1)
    OCR1A = (uint8_t)output;
}

// --- Trigger Functions ---
static inline void trigger_kick()
{
    k_active = 1;
    k_vol = 65535;          // Max volume
    k_step = param_tone;    // Initial pitch (configurable)
    k_phase = 0x6000;       // Start at trough for smooth attack
}

static inline void trigger_snare()
{
    s_active = 1;
    s_vol = 35000;          // Noise volume
    s_tone_vol = 50000;     // Tonal body (louder, decays faster)
    s_phase = 0x6000;
}

static inline void trigger_hihat()
{
    h_active = 1;
    h_vol = 30000;          // Lower volume
}

static inline void trigger_hihat_closed()
{
    h_active = 1;
    h_vol = 30000;
    h_decay_speed = 1;      // Decay every 2 calls (fast)
}

static inline void trigger_hihat_open()
{
    h_active = 1;
    h_vol = 30000;
    h_decay_speed = 7;      // Decay every 8 calls (slow)
}

static inline void trigger_clap()
{
    c_active = 1;
    c_vol = 50000;          // Slightly louder
}

static inline void trigger_tom()
{
    t_active = 1;
    t_vol = 55000;          // Similar to kick
    t_step = 600;           // Higher starting pitch than kick
    t_phase = 0x6000;       // Start at trough
}

static inline void trigger_cowbell()
{
    cb_active = 1;
    cb_vol = 45000;
    cb_phase1 = 0x6000;
    cb_phase2 = 0x6000;
}

// --- Voice Selection Button Functions ---
static inline void setup_voice_button()
{
    DDRB &= ~(1 << VOICE_BTN_PIN);   // Input
    PORTB |= (1 << VOICE_BTN_PIN);   // Internal pull-up enabled
}

static inline void update_voice_button()
{
    uint8_t btn_state = (PINB >> VOICE_BTN_PIN) & 1;  // 0=pressed, 1=released

    // Detect falling edge (released -> pressed)
    if (btn_state == 0 && btn_prev_state == 1) {
        current_voice = (current_voice + 1) % NUM_VOICES;
    }
    btn_prev_state = btn_state;
}

static inline void trigger_current_voice()
{
    switch (current_voice) {
        case 0: trigger_kick(); break;
        case 1: trigger_snare(); break;
        case 2: trigger_hihat(); break;
        case 3: trigger_clap(); break;
        case 4: trigger_tom(); break;
        case 5: trigger_cowbell(); break;
    }
}

// --- Utility Functions ---
static inline void wait_exact_ms(uint16_t ms)
{
    // 20kHz, so 1ms = 20 counts
    uint16_t target_ticks = ms * 20;
    tick_counter = 0;
    while (tick_counter < target_ticks)
    {
        // Wait
    }
}

#endif // VOICES_H
