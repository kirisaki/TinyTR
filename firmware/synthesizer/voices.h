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

// === TUNING CONSTANTS ===
// Initial volumes (0-65535)
#define K_VOL_INIT      65535   // Kick: max
#define S_VOL_INIT      25000   // Snare noise (was 35000, reduce crash)
#define S_TONE_VOL_INIT 50000   // Snare tone body
#define H_VOL_INIT      20000   // Hihat (was 30000, more subtle)
#define C_VOL_INIT      50000   // Clap
#define T_VOL_INIT      55000   // Tom
#define CB_VOL_INIT     45000   // Cowbell

// Decay rate shifts (higher = slower decay)
#define K_DECAY_SHIFT   7       // Kick (was 8, faster now)
#define S_NOISE_SHIFT   8       // Snare noise
#define S_TONE_SHIFT    7       // Snare tone (was 6, slower = less crash)
#define H_DECAY_SHIFT   7       // Hihat
#define C_DECAY_SHIFT   8       // Clap
#define T_DECAY_SHIFT   7       // Tom
#define CB_DECAY_SHIFT  7       // Cowbell

// --- Voice Selection Button ---
#define VOICE_BTN_PIN PB0
#define NUM_VOICES 6
volatile uint8_t current_voice = 0;    // 0=kick, 1=snare, 2=hihat, 3=clap, 4=tom, 5=cowbell
volatile uint8_t btn_prev_state = 1;   // Previous button state (1=released)

// Per-voice decay masks (set from param_decay in main loop)
volatile uint8_t k_decay = 7;      // Kick: longer
volatile uint8_t s_decay = 7;      // Snare
volatile uint8_t h_decay = 3;      // Hi-Hat: shorter
volatile uint8_t c_decay = 3;      // Clap: shorter
volatile uint8_t t_decay = 7;      // Tom
volatile uint8_t cb_decay = 3;     // Cowbell: shorter

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
volatile uint16_t h_phase1 = 0;     // Metallic tone oscillator 1
volatile uint16_t h_phase2 = 0;     // Metallic tone oscillator 2

// Clap
volatile uint16_t c_vol = 0;
volatile uint8_t c_active = 0;
volatile uint8_t c_stutter = 0;     // Stutter counter for clap bursts
volatile uint16_t c_stutter_timer = 0;

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

    // Pitch sweep downward (proportional - fast at high pitch, slow at low)
    uint16_t tone_end = param_tone / 20;
    if (k_step > tone_end) {
        uint16_t sweep = k_step >> 7;  // ~1% per sample
        if (sweep == 0) sweep = 1;
        k_step -= sweep;
    }

    // Volume decay
    static uint8_t k_div = 0;
    if ((++k_div & k_decay) == 0)
    {
        uint16_t decay = k_vol >> K_DECAY_SHIFT;
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
    uint8_t raw = pgm_read_byte(&sinewave[(k_phase >> 8) & 0x7F]);
    int16_t current = ((raw * (k_vol >> 8)) >> 8);

    // Low-pass filter (light: 50% current, 50% previous)
    static int16_t k_lpf = 0;
    int16_t k_filtered = (current + k_lpf) >> 1;
    k_lpf = current;

    return k_filtered;
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
    static uint8_t s_div = 0;
    if ((++s_div & s_decay) == 0)
    {
        // Noise decay
        uint16_t decay = s_vol >> S_NOISE_SHIFT;
        if (decay == 0 && s_vol > 0) decay = 1;
        if (s_vol > decay) s_vol -= decay;
        else s_vol = 0;

        // Tone decay
        uint16_t tone_decay = s_tone_vol >> S_TONE_SHIFT;
        if (tone_decay == 0 && s_tone_vol > 0) tone_decay = 1;
        if (s_tone_vol > tone_decay) s_tone_vol -= tone_decay;
        else s_tone_vol = 0;

        // Deactivate when both are done
        if (s_vol == 0 && s_tone_vol == 0)
            s_active = 0;
    }

    // Tonal body (pitch controlled by param_tone, scaled for snare range)
    s_phase += (param_tone >> 1);  // ~150-400Hz range
    uint8_t tone_raw = pgm_read_byte(&sinewave[(s_phase >> 8) & 0x7F]);
    int16_t tone_out = ((tone_raw * (s_tone_vol >> 8)) >> 8);

    // Noise output (use 8 bits from LFSR for finer grain)
    int16_t noise_out = ((lfsr & 0xFF) * (s_vol >> 8)) >> 8;

    return tone_out + noise_out;
}

// 3. Hi-Hat calculation (Shared for Open/Closed)
static inline int16_t calc_hihat()
{
    if (!h_active)
        return 0;

    // Noise generation
    uint8_t h_lsb = lfsr & 1;
    lfsr >>= 1;
    if (h_lsb)
        lfsr ^= 0xB400;

    // Volume decay
    static uint8_t h_div = 0;
    uint8_t h_decay_mask = h_decay_speed | h_decay;
    if ((++h_div & h_decay_mask) == 0)
    {
        uint16_t h_decay_amt = h_vol >> H_DECAY_SHIFT;
        if (h_decay_amt == 0 && h_vol > 0)
            h_decay_amt = 1;
        if (h_vol > h_decay_amt)
            h_vol -= h_decay_amt;
        else
        {
            h_vol = 0;
            h_active = 0;
        }
    }

    // Metallic tones (very high freq for sizzle)
    h_phase1 += 9000;   // ~2740 Hz
    h_phase2 += 11700;  // ~3560 Hz
    uint8_t h_tone1 = (h_phase1 >> 8) & 0x80 ? 128 : 0;
    uint8_t h_tone2 = (h_phase2 >> 8) & 0x80 ? 128 : 0;

    // Mix: tones XOR + noise
    uint8_t h_metal = (h_tone1 ^ h_tone2) >> 1;  // More metal (was >> 2)
    uint8_t h_noise = (lfsr & 0x7F);  // 7-bit noise

    // Blend: balanced
    int16_t h_out = ((h_metal + h_noise) * (h_vol >> 8)) >> 8;

    return h_out;
}

// 4. Clap calculation: Multiple bursts then decay
static inline int16_t calc_clap()
{
    if (!c_active)
        return 0;

    // Noise generation
    uint8_t lsb = lfsr & 1;
    lfsr >>= 1;
    if (lsb)
        lfsr ^= 0xB400;

    c_stutter_timer++;

    // Stutter phase: 3 short bursts with gaps
    if (c_stutter > 0) {
        // Each burst is ~60 samples on, ~140 samples off (~10ms total per burst)
        if (c_stutter_timer < 60) {
            return (lfsr & 1) ? (c_vol >> 8) : 0;
        } else if (c_stutter_timer > 200) {
            c_stutter--;
            c_stutter_timer = 0;
        }
        return 0;  // Gap between bursts
    }

    // Sustain phase: normal decay
    static uint8_t c_div = 0;
    if ((++c_div & c_decay) == 0)
    {
        uint16_t decay = c_vol >> C_DECAY_SHIFT;
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

    return (lfsr & 1) ? (c_vol >> 8) : 0;
}

// 5. Tom calculation: Similar to kick but higher pitch, faster decay
static inline int16_t calc_tom()
{
    if (!t_active)
        return 0;

    // Pitch sweep downward (end point linked to param_tone)
    uint16_t tone_end = param_tone / 10;  // Higher end than kick
    if (t_step > tone_end)
        t_step--;

    // Volume decay
    static uint8_t t_div = 0;
    if ((++t_div & t_decay) == 0)
    {
        uint16_t decay = t_vol >> T_DECAY_SHIFT;
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

    // Volume decay
    static uint8_t cb_div = 0;
    if ((++cb_div & cb_decay) == 0)
    {
        uint16_t decay = cb_vol >> CB_DECAY_SHIFT;
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

    // Two oscillators with pitch controlled by param_tone
    // Base: 587Hz and 845Hz, shifted by param_tone
    uint16_t base_step = 1500 + (param_tone >> 1);  // Pitch shift
    cb_phase1 += base_step;
    cb_phase2 += base_step + (base_step >> 1);  // 1.5x ratio for detune

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
    k_vol = K_VOL_INIT;
    k_step = param_tone;
    k_phase = 0x0000;   // Start at zero-crossing (value 128)
}

static inline void trigger_snare()
{
    s_active = 1;
    s_vol = S_VOL_INIT;
    s_tone_vol = S_TONE_VOL_INIT;
    s_phase = 0x6000;
}

static inline void trigger_hihat()
{
    h_active = 1;
    h_vol = H_VOL_INIT;
    h_phase1 = 0;
    h_phase2 = 0;  // No offset for softer attack
}

static inline void trigger_hihat_closed()
{
    h_active = 1;
    h_vol = H_VOL_INIT;
    h_decay_speed = 1;
    h_phase1 = 0;
    h_phase2 = 0;
}

static inline void trigger_hihat_open()
{
    h_active = 1;
    h_vol = H_VOL_INIT;
    h_decay_speed = 7;
    h_phase1 = 0;
    h_phase2 = 0;
}

static inline void trigger_clap()
{
    c_active = 1;
    c_vol = C_VOL_INIT;
    c_stutter = 3;
    c_stutter_timer = 0;
}

static inline void trigger_tom()
{
    t_active = 1;
    t_vol = T_VOL_INIT;
    t_step = param_tone + 200;
    t_phase = 0x6000;
}

static inline void trigger_cowbell()
{
    cb_active = 1;
    cb_vol = CB_VOL_INIT;
    cb_phase1 = 0x6000;
    cb_phase2 = 0x2000;
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
