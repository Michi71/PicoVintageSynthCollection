/*
 * File: push_button.cpp
 * Description: 
 * A Simple small library to read push buttons
 * Description: See URL for full details.
 * URL: https://github.com/gavinlyonsrepo/PushButtonLib_PICO
 */

#include "pico/stdlib.h"
#include "push_button.h"

// Constructor
// Param1 :: The GPIO button connected to.
// Param2 :: The debounce delay in milliseconds. 
// Notes ::  creates a new push button object.
PushButton::PushButton(uint8_t pin, uint16_t DebounceDelay= DEBOUNCE_DELAY)
{
	_PushButtonPin = pin;
	_DeBounceDelay = DebounceDelay;
}

// Method :: Init
// Notes ::Initiate the GPIO button connected to. Sets iin as a input with
// Pull up resistor on. Button must be connected to GND on other side
// HIGH  = OFF :: LOW  = On
// Call at start of program loop Once.
void PushButton::Init()
{
	gpio_init(_PushButtonPin);
	gpio_set_dir(_PushButtonPin, GPIO_IN);
	gpio_pull_up(_PushButtonPin);
}

// Method :: ReadButton
// Return :: Bool :: _PushButtonReadButton 
// Notes Returns the current debounced state of the button, 
// i.e. PRESSED or RELEASED
// Pressed Button is logic level: low, 
// Released button is logic level: high.
bool PushButton::ReadButton()
{
	// ignore pin changes until after this delay time
	if (_IgnoreUntil > to_ms_since_boot(get_absolute_time()))
	{
		// ignore any changes during this period
	} else if (gpio_get(_PushButtonPin) != _PushButtonReadButton)
	{ // pin has changed
		_IgnoreUntil = to_ms_since_boot(get_absolute_time()) + _DeBounceDelay;
		_PushButtonReadButton = !_PushButtonReadButton;
		_HasChanged = true;
	}
	
	return _PushButtonReadButton;
}

// Method Toggled
// Returns :: bool :: Haschanged 
// Returns true whenever the button is pressed or released, i.e. toggled. 
// To find out what the position actually is, use the read() function.
bool PushButton::Toggled()
{
	ReadButton();
	return HasChanged();
}

// Method :: HasChanged
// Returns :: bool :: True changed , false not changed
// Returns whether the state of the button has changed 
// after calling the previous read() function.
bool PushButton::HasChanged()
{
	if (_HasChanged  == true)
	{
		_HasChanged  = false;
		return true;
	}
	return false;
}

// Method :: IsPressed
// Returns :: bool :: Returns true when and only when the button is pressed. 
// Button has gone from off ->on
bool PushButton::IsPressed()
{
	if (ReadButton() == PRESSED && HasChanged() == true)
		return true;
	else
		return false;
}

// Method :: IsReleased
// Returns :: bool :: Returns true when and only when the button is released.
// has the button gone from on -> off
bool PushButton::IsReleased()
{
	if (ReadButton() == RELEASED && HasChanged() == true)
		return true;
	else
		return false;
}