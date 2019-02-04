/*
 * Copyright (c) 2019 Gijs Withagen.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
*/

/*
Note: Fuses on ATtiny85 must be changed to use the crystal. This is done
in the project specific boards file, attiny85_32khz.json. Settings are
calculated using http://www.engbedded.com/fusecalc.
Settings are:
 -Ext. Low-Freq crystal, Startup-time pwrdwn/reset: 32K CK/14 CK + 64 ms; [CKSEL=0110, SUT=10]
Fuses: Low 0xE6, High 0xDF, Extended 0xFF

In PlatformIO This can be programmed in the ATtiny85 by using pio run -t fuses

Drive a VID29 stepper motor directly. This can be handled since current is
only 20 mA

The VID29 is a 6 state stepper motor. Pin 2 and 3 of the stepper motor
are high and low simultaneously. So they share one IO pin.

24 hours, for 360 degrees, and one step per 1/3 degree.

This leads to 360*3 steps in 24*60*60 seconds, or 1 step every 80 seconds
A complete cycle of the VID29 is 6 steps.

ATtiny25/45/85 Pin map
                       +-\/-+
Reset/Ain0 (D 5) PB5  1|o   |8  Vcc
XTAL1/Ain3 (D 3) PB3  2|    |7  PB2 (D 2) Ain1 <-- connect VID29 4
XTAL2/Ain2 (D 4) PB4  3|    |6  PB1 (D 1) pwm1 <-- connect VID29 2 and 3
                 GND  4|    |5  PB0 (D 0) pwm0 <-- connect VID29 1
                       +----+
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#define PULSE_LENGTH 3000 // Pulse length to drive clock in microsecs.

#define MICROSECONDS_PER_PULSE (6*80L*1000L*1000L) // One pulse every 80 s is normal
#define MICROSECONDS_PER_INTERRUPT ((1024L*256L/F_CPU)*1000000L) // (1024 prescaler, 8 bits counter)

static const uint8_t pin_mask = _BV(PB0) | _BV(PB1) | _BV(PB2); //Use PB0, PB1 and PB2 to control clock
static uint8_t send_pulse;

// Stepper stae cycle is mapped to the followig array
//
// VID29  4 2&3 1
// State  2  1  0   Value
// 0      1  0  1   0x5
// 1      0  0  1   0x1
// 2      0  1  1   0x3
// 3      0  1  0   0x2
// 4      1  1  0   0x6
// 5      1  0  0   0x4

static uint8_t stateMap[] = {0x5, 0x1, 0x3, 0x2, 0x6, 0x4};

int main(void) {

	// Set prescaler to 1024 end enable interrupt for overflow on Timer 0
	TCCR0B |= _BV(CS00) | _BV(CS02); // prescaler of 1024
	TIMSK |= _BV(TOIE0);

	// Switch unused peripherals off to reduce power usage
	ADCSRA = 0;; //ADC disable
	PRR |= _BV(PRTIM1) | _BV(PRUSI) | _BV(PRADC);
	DDRB = _BV(PB0) | _BV(PB1) | _BV(PB2) | _BV(PB5); // Set PB0, PB1, PB2 and PB5 as an output

  sei(); // Turn on interrupts
	set_sleep_mode(SLEEP_MODE_IDLE); // Set sleep mode as idle, counters will work

  // Startup cycle to make assembly check easier
  for (int j=0; j<180; j++){
    // Send pulses to motor
    for (int i=0 ; i<6; i++ ){
      PORTB = stateMap[i];
      _delay_us(PULSE_LENGTH);
    }
    PORTB = 0; //and powerOff between pulses
  }


	while(1) {
		if (send_pulse) {
			send_pulse = 0; // Reset signal

			// Send pulses to motor
      for (int i=0 ; i<6; i++ ){
  			PORTB = stateMap[i];
  			_delay_us(PULSE_LENGTH);
      }
      PORTB = 0; //and powerOff between pulses
		}
    sleep_mode(); // System sleeps here, will be called after wakeup from interrupt
  }
}

// Timer 0 overflow interrupt
ISR(TIMER0_OVF_vect) {

  static uint64_t unaccounted_microseconds;

	// Send pulses at the correct average frequency.
	unaccounted_microseconds += MICROSECONDS_PER_INTERRUPT;
	if (unaccounted_microseconds < MICROSECONDS_PER_PULSE) return;
	unaccounted_microseconds -= MICROSECONDS_PER_PULSE;

	send_pulse = 1;
}
