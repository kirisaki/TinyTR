#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

// --- Pin Configuration ---
#define LED_PIN PB0     // LED for visual feedback
#define AMP_SD_PIN PB3  // Amplifier shutdown control (High=ON)
#define SPEAKER_PIN PB4 // PWM output

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
// Kick
volatile uint16_t k_phase = 0;
volatile uint16_t k_step = 0;
volatile uint16_t k_vol = 0;
volatile uint8_t k_active = 0;

// Snare
volatile uint16_t s_vol = 0;
volatile uint8_t s_active = 0;

// Hi-Hat
volatile uint16_t h_vol = 0;
volatile uint8_t h_active = 0;

// Noise Generator (shared by Snare/Hat)
volatile uint16_t lfsr = 0xACE1;

// --- Sound Synthesis Engine (Inline Functions) ---

// 1. Kick calculation: Sine wave + Pitch sweep + Exponential decay
static inline int16_t calc_kick()
{
    if (!k_active)
        return 0;

    // Pitch sweep downward
    if (k_step > 50)
        k_step--;

    // Volume decay
    static uint8_t div = 0;
    if ((++div & 5) == 0)
    { // Decay every 8 calls (long decay)
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

// 2. Snare calculation: Noise + Fast decay
static inline int16_t calc_snare()
{
    if (!s_active)
        return 0;

    // Shared noise generation (LFSR)
    uint8_t lsb = lfsr & 1;
    lfsr >>= 1;
    if (lsb)
        lfsr ^= 0xB400;

    // Volume decay
    static uint8_t div = 0;
    if ((++div & 3) == 0)
    { // Every 4 calls (medium decay)
        uint16_t decay = s_vol >> 8;
        if (decay == 0 && s_vol > 0)
            decay = 1;
        if (s_vol > decay)
            s_vol -= decay;
        else
        {
            s_vol = 0;
            s_active = 0;
        }
    }

    // Output noise
    return (lfsr & 1) ? (s_vol >> 8) : 0;
}

// --- 3. Hi-Hat calculation (Shared for Open/Closed) ---
volatile uint8_t h_decay_speed = 1; // 1=Short, 3=Long

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

// --- 4. Clap calculation ---
volatile uint16_t c_vol = 0;
volatile uint8_t c_active = 0;

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

void wait_exact_ms(uint16_t ms)
{
    // 20kHz, so 1ms = 20 counts
    uint16_t target_ticks = ms * 20;
    tick_counter = 0;
    while (tick_counter < target_ticks)
    {
        // Wait
    }
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

    // Overflow protection after mixing
    // Divide by 2 to create headroom since sum can exceed 255
    output = output >> 1;

    // Clipping if still exceeds 255
    if (output > 255)
        output = 255;

    // PWM output
    OCR1B = (uint8_t)output;
}

// --- Trigger Functions ---
void trigger_kick()
{
    k_active = 1;
    k_vol = 65535;          // Max volume
    k_step = 1000;          // Initial pitch
    k_phase = 0xC000;       // Phase reset
    PINB |= (1 << LED_PIN); // LED blink
}

void trigger_snare()
{
    s_active = 1;
    s_vol = 40000; // Volume adjustment
    PINB |= (1 << LED_PIN);
}

void trigger_hihat()
{
    h_active = 1;
    h_vol = 30000; // Lower volume
    PINB |= (1 << LED_PIN);
}

// Closed Hi-Hat
void trigger_hihat_closed()
{
    h_active = 1;
    h_vol = 30000;
    h_decay_speed = 1; // Decay every 2 calls (fast)
    PINB |= (1 << LED_PIN);
}

// Open Hi-Hat
void trigger_hihat_open()
{
    h_active = 1;
    h_vol = 30000;
    h_decay_speed = 7; // Decay every 8 calls (slow)
    PINB |= (1 << LED_PIN);
}

// Clap
void trigger_clap()
{
    c_active = 1;
    c_vol = 50000; // Slightly louder
    PINB |= (1 << LED_PIN);
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

// --- Main Loop (Sequencer) ---
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