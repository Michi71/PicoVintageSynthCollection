#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

#include "rotary_encoder.h"
#include "rotary_encoder.pio.h"
#include "button_debounce.pio.h"

int RotaryEncoder::rotation;
int RotaryEncoder::old_rotation;
int RotaryEncoder::delta;

// class to read the rotation of the rotary encoder
    // constructor
    // rotary_encoder_A is the pin for the A of the rotary encoder.
    // The B of the rotary encoder has to be connected to the next GPIO.
RotaryEncoder::RotaryEncoder(PIO pio, uint sm, uint rotary_encoder_A)
{
	_pio = pio;
    _sm = sm;

	_smbt = sm + 1;
	// we don't really need to keep the offset, as this program must be loaded
    // at offset 0
    pio_add_program(_pio, &rotary_encoder_program);
	rotary_encoder_program_init(_pio, _sm, rotary_encoder_A, 0);
	
	_offset = pio_add_program(pio0, &button_debounce_program);
	_smbt = 1; 
	button_debounce_program_init(pio0, _smbt, 13, _offset);
}

// get the current rotation
int RotaryEncoder::get_rotation(void)
{
	rotation = rotary_encoder_get_count(_pio, _sm) / 8;
    delta = rotation - old_rotation;
    old_rotation = rotation;
	return rotation;
}

// get the current delta
int RotaryEncoder::get_delta(void)
{
	rotation = rotary_encoder_get_count(_pio, _sm) / 8;
    delta = rotation - old_rotation;
    old_rotation = rotation;
	return delta;
}

int RotaryEncoder::get_button(void)
{
	return button_debounce_read(pio0, _smbt, _offset);
}



