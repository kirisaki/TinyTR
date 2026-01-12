# TinyTR

A compact drum machine project based on ATtiny85 microcontrollers.

## Overview

TinyTR is an open-source hardware drum machine that uses two ATtiny85 chips:
- **Synthesizer**: Generates drum sounds (kick, snare, hi-hat, clap) using digital synthesis
- **Sequencer**: Controls pattern playback and timing

## Project Structure

```
TinyTR/
├── firmware/       # Embedded software for ATtiny85
│   ├── synthesizer/  # Sound generation chip
│   └── sequencer/    # Pattern control chip
├── hardware/       # Circuit schematics (KiCad)
└── mechanical/     # Enclosure designs (build123d)
```

## Building

### Firmware
```bash
cd firmware/synthesizer
make flash
```

### Hardware
Open the KiCad project files in the `hardware/` directory.

### Enclosure
The enclosure is designed using build123d. Run the Python scripts in the `mechanical/` directory to generate STL files for 3D printing.

## License

This project uses multiple licenses depending on the component:

- **firmware/**: Dual-licensed under MIT OR Apache-2.0
  - See [LICENSE-MIT](LICENSE-MIT) and [LICENSE-APACHE](LICENSE-APACHE)
- **hardware/**: Licensed under CERN-OHL-P v2
  - See [LICENSE-CERN-OHL-P](LICENSE-CERN-OHL-P)
- **mechanical/**: Licensed under CC-BY-SA 4.0
  - See [LICENSE-CC-BY-SA](LICENSE-CC-BY-SA)

## Author

Akihito Kirisaki <kirisaki@klara.works>
