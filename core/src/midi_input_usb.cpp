#include "midi_input_usb.h"

void MIDIInputUSB::process()
{
    uint8_t p[4];

    while (tud_midi_available() && tud_midi_packet_read(p)) {
        if (MIDIActivityCallback) {
            MIDIActivityCallback();
        }

        uint8_t cin = p[0] & 0x0F;

        switch (cin) {
            case 0x8:
                if (MIDINoteOffCallback) {
                    MIDINoteOffCallback(p[2] & 0x7F, p[3] & 0x7F, p[1] & 0x0F);
                }
                break;

            case 0x9:
                if (MIDINoteOnCallback) {
                    MIDINoteOnCallback(p[2] & 0x7F, p[3] & 0x7F, p[1] & 0x0F);
                }
                break;

            case 0xB:
                if (MIDICCCallback) {
                    MIDICCCallback(p[2] & 0x7F, p[3] & 0x7F, p[1] & 0x0F);
                }
                break;

            case 0xC:
                if (MIDIProgramChangeCallback) {
                    MIDIProgramChangeCallback(p[2] & 0x7F, p[1] & 0x0F);
                }
                break;

            case 0xE: {
                uint16_t bend14 = (uint16_t)(p[2] & 0x7F) | ((uint16_t)(p[3] & 0x7F) << 7);
                if (MIDIPitchBendCallback) {
                    MIDIPitchBendCallback(bend14, p[1] & 0x0F);
                }
                break;
            }

            case 0xF:
                if (p[1] >= 0xF8 && MIDIRealtimeCallback) {
                    MIDIRealtimeCallback(p[1]);
                }
                break;

            case 0x4:
                if (p[1] == 0xF0) {
                    sysexStart();
                }
                sysexAppend(&p[1], 3);
                break;

            case 0x5:
                if (p[1] >= 0xF8) {
                    if (MIDIRealtimeCallback) {
                        MIDIRealtimeCallback(p[1]);
                    }
                } else {
                    if (p[1] == 0xF0) {
                        sysexStart();
                    }
                    sysexAppend(&p[1], 1);
                    sysexFinish();
                }
                break;

            case 0x6:
                if (p[1] == 0xF0) {
                    sysexStart();
                }
                sysexAppend(&p[1], 2);
                sysexFinish();
                break;

            case 0x7:
                if (p[1] == 0xF0) {
                    sysexStart();
                }
                sysexAppend(&p[1], 3);
                sysexFinish();
                break;

            default:
                break;
        }
    }
}

void MIDIInputUSB::sysexStart()
{
    _syxLen = 0;
    _syxActive = true;
    _syxOverflow = false;
}

void MIDIInputUSB::sysexAppend(const uint8_t* b, uint8_t n)
{
    if (!_syxActive) {
        return;
    }

    for (uint8_t i = 0; i < n; i++) {
        if (_syxLen < sizeof(_syx)) {
            _syx[_syxLen++] = b[i];
        } else {
            _syxOverflow = true;
        }
    }
}

void MIDIInputUSB::sysexFinish()
{
    if (_syxActive &&
        !_syxOverflow &&
        _syxLen >= 2 &&
        _syx[0] == 0xF0 &&
        _syx[_syxLen - 1] == 0xF7 &&
        MIDISysExCallback) {
        MIDISysExCallback(_syx, _syxLen);
    }

    _syxActive = false;
    _syxLen = 0;
    _syxOverflow = false;
}
