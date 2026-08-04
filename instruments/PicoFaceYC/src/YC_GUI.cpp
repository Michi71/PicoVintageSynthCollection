

#include "YC_GUI.h"
#include <cstdio>

void ycDrawScreen(u8g2_t* u8g2, YC_Controller& controller) {
    static const char* waveNames[5] = {"H","V","F","A","Y"};
    static const char* rotarySpeeds[4] = {"OFF","STOP","SLOW","FAST"};

    u8g2_SetFont(u8g2, u8g2_font_8x13B_tf);
    u8g2_SetFontPosBaseline(u8g2);
    u8g2_DrawStr(u8g2, 4, 14, controller.pageName());
    u8g2_DrawHLine(u8g2, 0, 18, 128);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    char buf[24];
    const yc_engine_state_t& s = controller.state();

    switch (controller.currentPage()) {
        case YcPage::VOLUME: {
            snprintf(buf, sizeof(buf), "VOL: %d", s.volume);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            int barW = (int)((s.volume * 120) / 127);
            u8g2_DrawFrame(u8g2, 4, 44, 120, 10);
            u8g2_DrawBox(u8g2, 4, 44, barW, 10);
            break;
        }
        case YcPage::WAVE_OCTAVE:
            snprintf(buf, sizeof(buf), "WAVE: %s", waveNames[s.wave]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "OCT: %+d", (int)s.octave);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::FOOT_16_513:
            snprintf(buf, sizeof(buf), "FT16: %d", s.footage[0]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "FT513: %d", s.footage[1]);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::FOOT_8_4:
            snprintf(buf, sizeof(buf), "FT8: %d", s.footage[2]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "FT4: %d", s.footage[3]);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::FOOT_223_2:
            snprintf(buf, sizeof(buf), "FT223: %d", s.footage[4]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "FT2: %d", s.footage[5]);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::FOOT_135_113:
            snprintf(buf, sizeof(buf), "FT135: %d", s.footage[6]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "FT113: %d", s.footage[7]);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::FOOT_1:
            snprintf(buf, sizeof(buf), "FT1: %d", s.footage[8]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            break;
        case YcPage::PERCUSSION:
            snprintf(buf, sizeof(buf), "TYPE: %s", s.perc_type == 0 ? "A" : "B");
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "LEN: %d", s.perc_length);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            snprintf(buf, sizeof(buf), "%s", s.perc_on != 0 ? "ON" : "OFF");
            u8g2_DrawStr(u8g2, 4, 62, buf);
            break;
        case YcPage::VIBCHO:
            snprintf(buf, sizeof(buf), "MODE: %s", s.vibcho_select == 0 ? "VIBRATO" : "CHORUS");
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "DEPTH: %d", s.vibcho_depth);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::ROTARY:
            snprintf(buf, sizeof(buf), "SPEED: %s", rotarySpeeds[s.rotary_speed]);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            break;
        case YcPage::EFFECT:
            snprintf(buf, sizeof(buf), "DIST: %d", s.distortion);
            u8g2_DrawStr(u8g2, 4, 36, buf);
            snprintf(buf, sizeof(buf), "REV: %d", s.reverb);
            u8g2_DrawStr(u8g2, 4, 52, buf);
            break;
        case YcPage::COUNT:
        default:
            break;
    }
}
