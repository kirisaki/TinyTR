# TinyTR Design Document

## Overview

TinyTR is a modular ATtiny85-based drum machine with a unique architecture:
- **Synthesizer chip**: Generates a single drum sound based on CV input
- **Sequencer chip**: Controls 16-step pattern playback and sends CV to synthesizers

Each synthesizer chip produces one voice. Multiple synthesizer chips can be connected in parallel to create a complete drum kit. This modular approach allows flexible voice configuration.

This document describes the hardware and communication design.

## Hardware Architecture

### Sequencer (ATtiny85)

**Pin Assignment:**
```
PB0 (Pin 5): SDA (I2C communication with other sequencers)
PB1 (Pin 6): ADC input - 4 buttons (resistor divider)
PB2 (Pin 7): SCL (I2C communication with other sequencers)
PB3 (Pin 2): CV output to synthesizer (PWM)
PB4 (Pin 3): LED output (rhythm/mode indicator)
```

**User Interface:**
- 4 buttons via resistor divider (PB1):
  - Button A: Value decrease / Step accent input
  - Button B: Value increase / Step accent input
  - Button C: Confirm / Step accent input
  - Mode switch: Switches between editing modes
    - Pattern editing (set accent per step)
    - Tempo adjustment
    - LFO rate and intensity adjustment
- 1 LED (PB4):
  - Current step indicator during playback
  - Mode indicator (blink pattern)

**I2C Communication:**
- Tempo synchronization between multiple sequencer units
- Enables multi-sequencer jam sessions
- Protocol: Master sends tempo clock pulses to slaves

### Synthesizer (ATtiny85)

**Pin Assignment (Planned):**
```
PB0 (Pin 5): Voice select button (digital input)
PB1 (Pin 6): PWM audio output (Timer1 OC1A)
PB2 (Pin 7): DECAY potentiometer (ADC1)
PB3 (Pin 2): TONE potentiometer (ADC3)
PB4 (Pin 3): CV input from sequencer (ADC2, 0-5V)
PB5 (Pin 1): RESET (kept functional for ISP programming)
```

**Pin Assignment (Current Implementation):**
```
PB0 (Pin 5): LED output (optional) / MOSI (programming)
PB1 (Pin 6): MISO (programming)
PB2 (Pin 7): SCK (programming)
PB3 (Pin 2): CV input from sequencer (ADC3, 0-5V) - TODO
PB4 (Pin 3): PWM audio output (Timer1 OC1B) - Current
```

**Hardware:**
- PAM8302 audio amplifier (always ON, no shutdown control)
- Single voice per chip (kick, snare, hi-hat, etc.)
- Multiple chips can be driven from one sequencer in parallel
- Analog post-processing circuits:
  - Resonance filter circuit (NJM2746M dual op-amp)
  - Distortion circuit (NJM2746M dual op-amp)
  - Applied to PWM output before amplification

## Communication Protocol

### CV (Control Voltage) Output

The sequencer sends trigger and accent information to synthesizers via analog voltage.

**Implementation: PWM + RC Filter**
- Sequencer outputs PWM on PB3
- External RC filter converts to DC voltage (0-5V range)
- Power supply: 5V
- Full voltage range utilized: 0-5V

### Voltage Encoding

Since each synthesizer produces only one voice, CV voltage represents:
- **0V**: No trigger (idle/silence)
- **0.1V - 5.0V**: Trigger ON + accent level
  - Low voltage (e.g., 1V): Soft accent
  - High voltage (e.g., 5V): Full accent

**PWM Duty Cycle Mapping:**
```
  0% duty → 0.0V → Idle (no trigger)
 20% duty → 1.0V → Soft trigger
 60% duty → 3.0V → Medium accent
100% duty → 5.0V → Maximum accent
```

The synthesizer detects trigger by voltage threshold (e.g., >0.5V) and scales output volume based on CV amplitude.

## Button Input Design

### 4-Button Resistor Divider

Buttons are read via a single ADC pin using resistor divider network.

**Example resistor values:**
```
Button A: 0Ω     → ~0V
Button B: 1kΩ    → ~1V
Button C: 2.2kΩ  → ~2V
Mode:     4.7kΩ  → ~3V
```

**ADC reading thresholds** (8-bit ADC, 0-255):
- Button A: 0-40
- Button B: 41-100
- Button C: 101-160
- Mode:     161-220
- None:     221-255

**Decision:** Final resistor values TBD based on noise margin testing.

## Sequencer Functionality

### Pattern Editing
- **16-step pattern** (fixed)
- Buttons A/B/C: Trigger/accent input for current step
- Mode button: Advances to next step or changes edit mode
- Pattern data structure:
  - 16 steps × accent level (0-255 per step)
  - Stored in RAM during playback
  - Optional: EEPROM storage for pattern save

### Tempo Control
- Mode switch enters tempo editing mode
- Buttons adjust tempo (coarse/fine adjustment)
- Range: 60-240 BPM (typical)
- Synchronized via I2C for multi-sequencer setups

### LFO/Accent Control
- Mode switch enters LFO editing mode
- Buttons adjust:
  - LFO frequency (rate)
  - LFO intensity (depth)
  - Global accent amount
- Per-step accent set in pattern editing mode

## Open Design Questions

### Resolved:
1. ✓ CV output method: **PWM + RC filter** (0-5V range)
2. ✓ Pattern memory structure: **16 steps, 0-255 accent level per step**
3. ✓ CV voltage encoding: **0V = idle, 0.1-5V = trigger + accent**
4. ✓ Synthesizer CV input: **PB3 (ADC)**

### Remaining Decisions:
1. Exact resistor values for button divider (requires noise margin testing)
2. I2C protocol details for multi-sequencer sync (message format, timing)

### Future Considerations:
- Pattern save/load mechanism
- EEPROM usage for pattern storage
- Multiple pattern banks
- Swing/shuffle timing
- External clock input

## Development Phases

### Phase 1: Voice Synthesis & Communication (Current)
- [x] Voice synthesis engine (kick, snare, hi-hat, clap)
- [x] PWM audio output at 250kHz
- [x] 20kHz sampling mixer
- [ ] CV output on sequencer (PWM)
- [ ] CV input on synthesizer (ADC)
- [ ] Test single voice trigger via CV

### Phase 2: Pattern Playback
- [ ] 16-step sequencer implementation
- [ ] Button input reading (resistor divider ADC)
- [ ] Tempo control (60-240 BPM)

### Phase 3: Full Features
- [ ] Mode switching
- [ ] LFO/accent control
- [ ] I2C synchronization

### Phase 4: Polish
- [ ] Pattern storage
- [ ] Optimization
- [ ] Hardware finalization

## Notes

This is a deliberately minimal design using spare ATtiny85 chips. Once chips are depleted, migration to ESP32 or similar platform is planned for expanded features.

The design philosophy is "creative constraints" - working within the severe limitations of ATtiny85 (512B RAM, 8KB Flash, 5 I/O pins) to create a unique instrument.
