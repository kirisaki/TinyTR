#include <avr/io.h>
#include <avr/sleep.h>

int main(void)
{
  // 1. Set all pins to input (Hi-Z) and Low to cut power to LED and speaker
  DDRB = 0x00;
  PORTB = 0x00;

  // 2. Sleep mode configuration (Power Down: lowest power consumption mode)
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  // 3. Sleep (won't wake up unless external interrupt occurs)
  sleep_cpu();

  while (1)
    ;
}