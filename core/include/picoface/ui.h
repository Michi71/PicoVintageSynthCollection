#ifndef PICOFACE_UI_H
#define PICOFACE_UI_H

#include <cstdint>
#include <cstddef>

// Forward declaration of the u8g2 context struct in the GLOBAL namespace,
// matching the C library's declaration (typedef struct u8g2_struct u8g2_t;).
// This ensures we refer to the same type as the u8g2 header, so instruments
// do not need to include it (only display.cpp does).
struct u8g2_struct;

namespace picoface {
namespace ui {

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

// Rotary encoders: SEL for navigation, ParamA / ParamB for parameter editing.
enum class Encoder : uint8_t {
    Sel,
    ParamA,
    ParamB,
    Count
};

// Push buttons integrated into the three encoders.
enum class Button : uint8_t {
    Sel,
    ParamA,
    ParamB,
    Count
};

constexpr size_t kEncoderCount = static_cast<size_t>(Encoder::Count);
constexpr size_t kButtonCount  = static_cast<size_t>(Button::Count);

// Snapshot of all user inputs for one UI tick.
// Filled by the input driver, consumed read-only by the instruments.
struct InputState {
    int8_t   encoderDelta[kEncoderCount];    // signed steps since last tick
    bool     buttonDown[kButtonCount];       // current button level
    bool     buttonPressed[kButtonCount];    // rising edge this tick
    bool     buttonLongPress[kButtonCount];  // long-press event this tick
    uint16_t pot;                            // 0..4095 (12-bit ADC)
    uint32_t nowMs;                          // tick timestamp in milliseconds

    // Bounds-checked accessors; an invalid index yields 0 / false.
    constexpr int8_t delta(Encoder e) const {
        const size_t i = static_cast<size_t>(e);
        return (i < kEncoderCount) ? encoderDelta[i] : 0;
    }
    constexpr bool down(Button b) const {
        const size_t i = static_cast<size_t>(b);
        return (i < kButtonCount) ? buttonDown[i] : false;
    }
    constexpr bool pressed(Button b) const {
        const size_t i = static_cast<size_t>(b);
        return (i < kButtonCount) ? buttonPressed[i] : false;
    }
    constexpr bool longPress(Button b) const {
        const size_t i = static_cast<size_t>(b);
        return (i < kButtonCount) ? buttonLongPress[i] : false;
    }
};

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// Alias for the opaque u8g2 context pointer, explicitly qualified to refer
// to the global ::u8g2_struct declared by the C library.
using DisplayHandle = ::u8g2_struct*;

// Thin facade over u8g2 for the 128x64 I2C OLED.
// Rationale: instruments draw exclusively through this interface and stay
// independent of the concrete display driver. The driver can be swapped
// (different controller, different library, or a host-side mock for tests)
// without touching instrument code.
//
// Declarations only; the implementation lives in core/src/ui/display.cpp.
class Display {
public:
    static constexpr int16_t kWidth  = 128;
    static constexpr int16_t kHeight = 64;

    explicit Display(DisplayHandle u8g2);

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    DisplayHandle raw() const;  // escape hatch for special cases

    void clear();                        // clear the back buffer
    void flush();                        // send the back buffer to the display
    void setFont(const uint8_t* font);   // u8g2 font, e.g. u8g2_font_6x10_tf

    void drawText(int16_t x, int16_t y, const char* text);
    void drawTextCentered(int16_t y, const char* text);
    int16_t textWidth(const char* text) const;

    void drawFrame(int16_t x, int16_t y, int16_t w, int16_t h);
    void drawBox(int16_t x, int16_t y, int16_t w, int16_t h);
    void drawHLine(int16_t x, int16_t y, int16_t w);
    void drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* bits);

private:
    DisplayHandle u8g2_;
};

} // namespace ui
} // namespace picoface

#endif // PICOFACE_UI_H
