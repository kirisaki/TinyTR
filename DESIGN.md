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
PB1 (Pin 6): LED output (rhythm/mode indicator)
PB2 (Pin 7): SCL (I2C communication with other sequencers)
PB3 (Pin 2): Button input (ADC3, resistor divider)
PB4 (Pin 3): CV output to synthesizer (Timer1 OC1B PWM)
```

**User Interface:**
- 3 buttons via resistor divider (PB3):
  - Button A: Step ON / Value decrease
  - Button B: Step OFF / Value increase (long press: pattern clear in Play mode)
  - Mode: Toggle/Cycle modes (short) / Switch layer (long)
- 1 LED (PB1):
  - Mode-specific feedback (see Sequencer Modes)

**Mode Layer System:**
```
┌─────────────────────────────────────────────────────────┐
│ Main Layer (M short: toggle)                            │
│   Play ←→ Bank                                          │
├─────────────────────────────────────────────────────────┤
│ Settings Layer (M short: cycle)                         │
│   Tempo → LFO Rate → LFO Depth → Etc → Tempo...         │
└─────────────────────────────────────────────────────────┘
        ↑                              ↓
        └──── M long press (500ms) ────┘
```

**Sequencer Modes:**
```
Mode        A           B              A Long      B Long      LED Pattern
────────────────────────────────────────────────────────────────────────────────
Play        Step ON     Step OFF       -           Clear       Bar head bright, beats dim, off otherwise
Bank        Bank ↓      Bank ↑         -           -           Bar head bright, beats off, dim otherwise
Tempo       BPM ↓       BPM ↑          -           -           Flash on downbeat
LFO Rate    Rate ↓      Rate ↑         -           -           Blink at LFO freq
LFO Depth   Depth ↓     Depth ↑        -           -           PWM brightness
Etc         -           -              I2C toggle  All clear   Double blink
```

**LED Pattern Detail (Play vs Bank):**
```
Step        Play        Bank
────────────────────────────────
0 (Bar1)    Bright      Bright (same)
16,18       Bright      Bright (same)
4,8,12...   Dim         Off
Others      Off         Dim
```

**Pattern Save Behavior:**
- Auto-save to EEPROM at bar end (step 31→0) when pattern changed
- Bank switch: Auto-save current pattern before loading new bank

**Mode Button:**
- Short press: Toggle Play↔Bank (main layer) / Cycle settings (settings layer)
- Long press (500ms): Switch between main layer and settings layer

**Etc Mode (Miscellaneous Settings):**
- A long press: Toggle I2C master mode
- B long press: Clear all 8 banks (full reset)

**I2C Communication:**
- Tempo synchronization between multiple sequencer units
- Enables multi-sequencer jam sessions
- Uses broadcast (no individual addressing required)
- Access via Settings Layer → Etc mode → A long press

**I2C Sync States:**
- **Standalone**: Default mode, runs on internal tempo
- **Primary**: Sends tempo clock via I2C broadcast
- **Secondary**: Receives clock and syncs to primary

**State Transitions:**
```
[Power ON] → [Standalone]

[Standalone] + A long press in Etc mode → [Primary]
  → Broadcasts tempo clock on each step
  → LED: 2 blinks on mode change

[Standalone] + Receive I2C clock → [Secondary]
  → Syncs to received tempo (auto-detect)
  → LED: 3 blinks on mode change

[Primary] + A long press in Etc mode → [Standalone]
  → Stops broadcasting

[Secondary] + A long press in Etc mode → [Standalone]
  → Ignores I2C clock
```

**Protocol:**
- Primary broadcasts beat pulse on each step
- Secondary advances step on received pulse
- No address management needed (broadcast only)

### Synthesizer (ATtiny85)

**Pin Assignment:**
```
PB0 (Pin 5): Voice select button (digital input)
PB1 (Pin 6): PWM audio output (Timer1 OC1A)
PB2 (Pin 7): DECAY potentiometer (ADC1)
PB3 (Pin 2): TONE potentiometer (ADC3)
PB4 (Pin 3): CV input from sequencer (ADC2, 0-5V)
PB5 (Pin 1): RESET (kept functional for ISP programming)
```

**Hardware:**
- PAM8302 audio amplifier (always ON, no shutdown control)
- Single voice per chip (kick, snare, hi-hat, etc.)
- Multiple chips can be driven from one sequencer in parallel
- Analog post-processing circuits (NJM2746M dual op-amp):
  - Distortion circuit
  - Audio chain input mixer (for cascading multiple units)
  - Signal chain: Local PWM → Distortion → Mix with chain input → Amp

## Communication Protocol

### CV (Control Voltage) Output

The sequencer sends trigger and accent information to synthesizers via analog voltage.

**Implementation: PWM + RC Filter**
- Sequencer outputs PWM on PB4 (Timer1 OC1B)
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

**Synthesizer CV Input Mapping (Current Implementation):**
```
ADC Value   Voltage    Action
---------   -------    ------
0-2         ~0.0V      Idle (no trigger)
3-9         ~0.06-0.2V Hysteresis zone (hold previous state)
10-255      ~0.2-5.0V  Trigger + Accent

Accent Volume Mapping:
  ADC 10  → 16384 (25% volume)
  ADC 255 → 65384 (100% volume)
  Formula: volume = 16384 + (ADC - 10) * 200
```

The synthesizer detects trigger by voltage threshold (~0.2V) and scales output volume based on CV amplitude.

## Button Input Design

### 3-Button Resistor Divider

Buttons are read via a single ADC pin using resistor divider network.

**Circuit:**
```
VCC (5V)
    │
   [10kΩ] Pull-up
    │
    ├────────────→ PB3 (ADC3)
    │
    ├──[A]──┤0Ω├──→ GND      (0V)
    │
    ├──[B]──┤2.2kΩ├──→ GND   (~0.9V)
    │
    └──[M]──┤5kΩ├──→ GND     (~1.67V)
```

**ADC reading thresholds** (8-bit ADC, 0-255):
| Button | Voltage | Theoretical | Threshold |
|--------|---------|-------------|-----------|
| A      | 0V      | 0           | 0-23      |
| B      | 0.9V    | ~46         | 24-65     |
| Mode   | 1.67V   | ~85         | 66-160    |
| None   | 5V      | 255         | 200-255   |

**Debounce:**
- Software debounce: 3 consecutive identical readings required
- No hardware capacitor on sequencer buttons

**Timing:**
- Short press: < 500ms
- Long press: ≥ 500ms

## Sequencer Functionality

### Pattern Editing (Play Mode)
- **32-step pattern** (2 bars of 16th notes)
- Real-time editing during playback:
  - A held: Turn step ON + play immediately
  - B held: Turn step OFF + mute immediately
  - Auto-save to EEPROM at pattern end (no manual save needed)
- Pattern data structure:
  - 32 steps × ON/OFF (1 bit per step = 4 bytes)
  - Accent controlled by LFO, not per-step
- LED feedback:
  - Bar 1: Quarter note blink (steps 0,4,8,12 = on)
  - Bar 2 beat 1: 8th note blink (steps 16,18 = on) - distinguishes bar 2
  - Bar 2 beats 2-4: Quarter note blink (steps 20,24,28 = on)
  - Bar head (step 0,16,18): bright / Other beats: dim

### Tempo Control (Tempo Mode)
- A/B buttons adjust tempo
- Range: 60-240 BPM (typical)
- Synchronized via I2C for multi-sequencer setups

### Pattern Banks (Bank Mode)
- A/B buttons switch between pattern banks (A=down, B=up)
- 8 banks (0-7, wraps around)
- Bank switch is scheduled and applied at bar start (step 0)
- Auto-save current pattern before loading new bank

**EEPROM Layout (Current Implementation):**
```
Simple layout without wear leveling:
- 0x00: Magic byte (0xA5 = valid data)
- 0x01: Current bank number (0-7)
- 0x02-0x21: 8 banks × 4 bytes = 32 bytes of patterns
```

**EEPROM Layout (Future - Wear Leveling):**
```
Wear leveling with rotating slots:
- Slot structure: 1 byte seq + 4 bytes pattern = 5 bytes
- 512 bytes / 5 = 102 slots per bank (single bank mode)
- Or 8 banks × ~12 slots each
- Boot: scan for highest sequence number
- Save: write to next slot with seq+1
- Lifespan: 102× improvement (~5500 hours continuous editing)
```

### LFO Control
- **LFO Rate Mode**: A/B adjust LFO frequency
- **LFO Depth Mode**: A/B adjust LFO intensity
- LFO modulates CV output voltage (accent)

## Open Design Questions

### Resolved:
1. ✓ CV output method: **PWM + RC filter** (0-5V range)
2. ✓ Pattern memory structure: **32 steps × ON/OFF, accent via LFO**
3. ✓ CV voltage encoding: **0V = idle, 0.2-5V = trigger + accent**
4. ✓ Synthesizer CV input: **PB4 (ADC2)**
5. ✓ Button count: **3 buttons (A, B, Mode)**
6. ✓ Mode system: **2-layer system (Main: Play/Bank, Settings: Tempo/LFO Rate/LFO Depth/I2C)**
7. ✓ Power supply: **FP6291 boost + diode OR for chain sharing**
8. ✓ Pattern banks: **8 banks (4 bytes × 8 = 32 bytes EEPROM)**

### Remaining Decisions:
1. I2C protocol details (message format, timing)
2. LFO waveform (triangle, sine, random?)

### Future Considerations:
- Swing/shuffle timing
- External clock input

## Development Phases

### Phase 1: Voice Synthesis & Communication (Current)
- [x] Voice synthesis engine (kick, snare, hi-hat, clap, tom, cowbell)
- [x] PWM audio output at 250kHz
- [x] 20kHz sampling mixer
- [x] CV output on sequencer (PWM)
- [x] CV input on synthesizer (ADC)
- [x] Test single voice trigger via CV

### Phase 2: Sequencer Core
- [x] 32-step pattern playback
- [x] 3-button input (resistor divider ADC + software debounce)
- [x] Play mode with real-time editing (A=add, B=remove)
- [x] LED beat indicator (beat 1 bright, beat 3 dim)
- [x] Auto-save to EEPROM at bar end
- [x] 2-layer mode system with LED feedback
- [x] Pattern banks (8 banks, EEPROM storage)
- [x] Bank mode with scheduled switching (at bar start)
- [x] Long press actions (M=layer switch, B=pattern clear)
- [ ] Tempo control (60-240 BPM)
- [ ] LFO for accent modulation

### Phase 3: Extended Features
- [ ] I2C synchronization (Primary/Secondary)

### Phase 4: Hardware & Polish
- [ ] Distortion circuit (NJM2746M)
- [ ] Audio chain input mixing
- [ ] Power supply (FP6291 + diode OR)
- [ ] Final PCB design

## Notes

This is a deliberately minimal design using spare ATtiny85 chips. Once chips are depleted, migration to ESP32 or similar platform is planned for expanded features.

The design philosophy is "creative constraints" - working within the severe limitations of ATtiny85 (512B RAM, 8KB Flash, 5 I/O pins) to create a unique instrument.
