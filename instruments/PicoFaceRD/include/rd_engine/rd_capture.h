// host-only register-write capture; the original firmware itself acts as the
// params_rom parser -- we record what it programs into the chip.

#ifndef RD_CAPTURE_H
#define RD_CAPTURE_H

#include <stdint.h>
#include <vector>

struct RdCaptureEvent {
    uint64_t sample;
    uint8_t  voice;
    uint8_t  part;
    uint8_t  field;
    uint8_t  value;
};

extern std::vector<RdCaptureEvent>* g_rd_capture;       // nullptr = capture off
extern uint64_t                     g_rd_capture_clock; // advanced once per generated sample by the capture driver

#endif // RD_CAPTURE_H
