/*
 * File: push_button.hpp
 * Description: 
 * A small library to read push buttons basic functionality 
 * Description: See URL for full details.
 * URL: https://github.com/gavinlyonsrepo/PushButtonLib_PICO
 */

#ifndef _PushButtonLib_h
#define _PushButtonLib_h


#define DEBOUNCE_DELAY 50 // mS

class PushButton
{
	public:
		PushButton(uint8_t pin, uint16_t DebounceDelay);
		void Init();
		
		bool ReadButton();
		bool IsPressed();
		bool IsReleased();
		bool HasChanged();
		bool Toggled();
		
		const static bool PRESSED = false;
		const static bool RELEASED = true;
		
	private:
	
		uint8_t  _PushButtonPin;
		uint16_t _DeBounceDelay= DEBOUNCE_DELAY;
		uint32_t _IgnoreUntil = 0;
		bool     _HasChanged = false;
		bool     _PushButtonReadButton = true;
		
};

#endif