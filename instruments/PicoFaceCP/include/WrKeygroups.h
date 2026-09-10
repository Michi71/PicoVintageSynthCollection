// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN
   Instrument: Wr   Keygroups: 33 (11 Regionen x 3 Velocity-Layer)
   Engine: k += 3;  Velocity-Schwellen [48, 80]
   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)
   -> kgrp[] muss >= 33 sein (mda hat 34). */
#ifndef WR_KEYGROUPS_H
#define WR_KEYGROUPS_H
#include <stdint.h>

/* spiegelt struct KGRP aus mdaEPiano.h */
static const struct { int32_t root; int32_t high; int32_t pos;
                         int32_t end; int32_t loop; } WrKgrp[33] = {
  /*  0 */ {  36,  42,       0,    8785,    978 },
  /*  1 */ {   0,   0,    8786,   17314,    489 },
  /*  2 */ {   0,   0,   17315,   26066,    977 },
  /*  3 */ {  43,  47,   26067,   35020,   1305 },
  /*  4 */ {   0,   0,   35021,   43349,   1630 },
  /*  5 */ {   0,   0,   43350,   51404,    327 },
  /*  6 */ {  48,  54,   51405,   59368,   1468 },
  /*  7 */ {   0,   0,   59369,   67343,    979 },
  /*  8 */ {   0,   0,   67344,   76638,    490 },
  /*  9 */ {  55,  59,   76639,   84605,    981 },
  /* 10 */ {   0,   0,   84606,   92602,    164 },
  /* 11 */ {   0,   0,   92603,  101450,    491 },
  /* 12 */ {  60,  66,  101451,  110134,    735 },
  /* 13 */ {   0,   0,  110135,  119418,    246 },
  /* 14 */ {   0,   0,  119419,  129237,    246 },
  /* 15 */ {  67,  71,  129238,  140047,    410 },
  /* 16 */ {   0,   0,  140048,  150989,    410 },
  /* 17 */ {   0,   0,  150990,  162919,    247 },
  /* 18 */ {  72,  78,  162920,  172736,    551 },
  /* 19 */ {   0,   0,  172737,  182973,    307 },
  /* 20 */ {   0,   0,  182974,  193625,    307 },
  /* 21 */ {  79,  83,  193626,  203537,    246 },
  /* 22 */ {   0,   0,  203538,  213612,    287 },
  /* 23 */ {   0,   0,  213613,  223845,    287 },
  /* 24 */ {  84,  90,  223846,  233751,    123 },
  /* 25 */ {   0,   0,  233752,  243975,    123 },
  /* 26 */ {   0,   0,  243976,  254683,    276 },
  /* 27 */ {  91,  95,  254684,  265553,    103 },
  /* 28 */ {   0,   0,  265554,  277065,    103 },
  /* 29 */ {   0,   0,  277066,  288164,    103 },
  /* 30 */ {  96, 999,  288165,  300002,     62 },
  /* 31 */ {   0,   0,  300003,  311836,     77 },
  /* 32 */ {   0,   0,  311837,  323033,     77 },
};
#define WR_NKGRP 33

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)WrData;
   kgrp muss >= 33 sein (z.B. KGRP kgrp[34];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 36; kgrp[ 0].high= 42; kgrp[ 0].pos=      0; kgrp[ 0].end=   8785; kgrp[ 0].loop=   978;
  kgrp[ 1].pos=   8786; kgrp[ 1].end=  17314; kgrp[ 1].loop=   489;
  kgrp[ 2].pos=  17315; kgrp[ 2].end=  26066; kgrp[ 2].loop=   977;
  kgrp[ 3].root= 43; kgrp[ 3].high= 47; kgrp[ 3].pos=  26067; kgrp[ 3].end=  35020; kgrp[ 3].loop=  1305;
  kgrp[ 4].pos=  35021; kgrp[ 4].end=  43349; kgrp[ 4].loop=  1630;
  kgrp[ 5].pos=  43350; kgrp[ 5].end=  51404; kgrp[ 5].loop=   327;
  kgrp[ 6].root= 48; kgrp[ 6].high= 54; kgrp[ 6].pos=  51405; kgrp[ 6].end=  59368; kgrp[ 6].loop=  1468;
  kgrp[ 7].pos=  59369; kgrp[ 7].end=  67343; kgrp[ 7].loop=   979;
  kgrp[ 8].pos=  67344; kgrp[ 8].end=  76638; kgrp[ 8].loop=   490;
  kgrp[ 9].root= 55; kgrp[ 9].high= 59; kgrp[ 9].pos=  76639; kgrp[ 9].end=  84605; kgrp[ 9].loop=   981;
  kgrp[10].pos=  84606; kgrp[10].end=  92602; kgrp[10].loop=   164;
  kgrp[11].pos=  92603; kgrp[11].end= 101450; kgrp[11].loop=   491;
  kgrp[12].root= 60; kgrp[12].high= 66; kgrp[12].pos= 101451; kgrp[12].end= 110134; kgrp[12].loop=   735;
  kgrp[13].pos= 110135; kgrp[13].end= 119418; kgrp[13].loop=   246;
  kgrp[14].pos= 119419; kgrp[14].end= 129237; kgrp[14].loop=   246;
  kgrp[15].root= 67; kgrp[15].high= 71; kgrp[15].pos= 129238; kgrp[15].end= 140047; kgrp[15].loop=   410;
  kgrp[16].pos= 140048; kgrp[16].end= 150989; kgrp[16].loop=   410;
  kgrp[17].pos= 150990; kgrp[17].end= 162919; kgrp[17].loop=   247;
  kgrp[18].root= 72; kgrp[18].high= 78; kgrp[18].pos= 162920; kgrp[18].end= 172736; kgrp[18].loop=   551;
  kgrp[19].pos= 172737; kgrp[19].end= 182973; kgrp[19].loop=   307;
  kgrp[20].pos= 182974; kgrp[20].end= 193625; kgrp[20].loop=   307;
  kgrp[21].root= 79; kgrp[21].high= 83; kgrp[21].pos= 193626; kgrp[21].end= 203537; kgrp[21].loop=   246;
  kgrp[22].pos= 203538; kgrp[22].end= 213612; kgrp[22].loop=   287;
  kgrp[23].pos= 213613; kgrp[23].end= 223845; kgrp[23].loop=   287;
  kgrp[24].root= 84; kgrp[24].high= 90; kgrp[24].pos= 223846; kgrp[24].end= 233751; kgrp[24].loop=   123;
  kgrp[25].pos= 233752; kgrp[25].end= 243975; kgrp[25].loop=   123;
  kgrp[26].pos= 243976; kgrp[26].end= 254683; kgrp[26].loop=   276;
  kgrp[27].root= 91; kgrp[27].high= 95; kgrp[27].pos= 254684; kgrp[27].end= 265553; kgrp[27].loop=   103;
  kgrp[28].pos= 265554; kgrp[28].end= 277065; kgrp[28].loop=   103;
  kgrp[29].pos= 277066; kgrp[29].end= 288164; kgrp[29].loop=   103;
  kgrp[30].root= 96; kgrp[30].high=999; kgrp[30].pos= 288165; kgrp[30].end= 300002; kgrp[30].loop=    62;
  kgrp[31].pos= 300003; kgrp[31].end= 311836; kgrp[31].loop=    77;
  kgrp[32].pos= 311837; kgrp[32].end= 323033; kgrp[32].loop=    77;
*/
#endif /* WR_KEYGROUPS_H */
