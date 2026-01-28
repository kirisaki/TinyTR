#include <avr/io.h>

int main(void) {
    // CV output pin low
    DDRB |= (1 << PB4);
    PORTB &= ~(1 << PB4);

    while (1) {
        // Do nothing
    }
}
