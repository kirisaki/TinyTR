# Configuration
MCU = attiny85
F_CPU = 8000000UL
# Default is 1MHz (8MHz prescaled). Change to 8000000UL after fuse rewrite.

PROGRAMMER = usbasp
# Write speed adjustment. Increase value if errors occur (e.g., 20, 50)
BITCLOCK = 10

# Toolchain
CC = avr-gcc
OBJCOPY = avr-objcopy
AVRDUDE = avrdude

# Compile options
CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall

# Targets
all: main.hex

main.elf: main.c
	$(CC) $(CFLAGS) -o $@ $<

main.hex: main.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

# Flash command
flash: main.hex
	$(AVRDUDE) -c $(PROGRAMMER) -p t85 -B $(BITCLOCK) -U flash:w:$<:i

# Fuse configuration check (connection test)
check:
	$(AVRDUDE) -c $(PROGRAMMER) -p t85 -B $(BITCLOCK)

clean:
	rm -f *.elf *.hex

mute.elf: mute.c
	$(CC) $(CFLAGS) -o $@ $<

mute: mute.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< mute.hex
	$(AVRDUDE) -c $(PROGRAMMER) -p t85 -B $(BITCLOCK) -U flash:w:mute.hex:i
