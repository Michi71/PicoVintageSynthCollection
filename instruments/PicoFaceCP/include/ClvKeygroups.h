// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN
   Instrument: Clv   Keygroups: 36 (12 Regionen x 3 Velocity-Layer)
   Engine: k += 3;  Velocity-Schwellen [48, 80]
   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)
   -> kgrp[] muss >= 36 sein (mda hat 34). */
#ifndef CLV_KEYGROUPS_H
#define CLV_KEYGROUPS_H
#include <stdint.h>

/* spiegelt struct KGRP aus mdaEPiano.h */
static const struct { int32_t root; int32_t high; int32_t pos;
                         int32_t end; int32_t loop; } ClvKgrp[36] = {
  /*  0 */ {  42,  46,       0,    9072,    691 },
  /*  1 */ {   0,   0,    9073,   19306,   2765 },
  /*  2 */ {   0,   0,   19307,   27256,    690 },
  /*  3 */ {  47,  51,   27257,   35663,    519 },
  /*  4 */ {   0,   0,   35664,   44347,    518 },
  /*  5 */ {   0,   0,   44348,   56340,    514 },
  /*  6 */ {  52,  56,   56341,   68158,   1164 },
  /*  7 */ {   0,   0,   68159,   76783,    389 },
  /*  8 */ {   0,   0,   76784,   85454,    388 },
  /*  9 */ {  57,  61,   85455,   93738,   1460 },
  /* 10 */ {   0,   0,   93739,  101672,    584 },
  /* 11 */ {   0,   0,  101673,  110284,    582 },
  /* 12 */ {  62,  68,  110285,  119057,    436 },
  /* 13 */ {   0,   0,  119058,  127085,   1089 },
  /* 14 */ {   0,   0,  127086,  135747,    218 },
  /* 15 */ {  69,  73,  135748,  145142,    439 },
  /* 16 */ {   0,   0,  145143,  153506,    584 },
  /* 17 */ {   0,   0,  153507,  165361,    145 },
  /* 18 */ {  74,  78,  165362,  174925,    110 },
  /* 19 */ {   0,   0,  174926,  184264,    219 },
  /* 20 */ {   0,   0,  184265,  194082,    874 },
  /* 21 */ {  79,  83,  194083,  204184,    246 },
  /* 22 */ {   0,   0,  204185,  214222,    246 },
  /* 23 */ {   0,   0,  214223,  222208,     82 },
  /* 24 */ {  84,  88,  222209,  232797,    368 },
  /* 25 */ {   0,   0,  232798,  243255,    368 },
  /* 26 */ {   0,   0,  243256,  253365,    183 },
  /* 27 */ {  89,  94,  253366,  263971,    368 },
  /* 28 */ {   0,   0,  263972,  273825,     47 },
  /* 29 */ {   0,   0,  273826,  283729,    274 },
  /* 30 */ {  95,  99,  283730,  294120,    163 },
  /* 31 */ {   0,   0,  294121,  304187,    163 },
  /* 32 */ {   0,   0,  304188,  314458,    323 },
  /* 33 */ { 100, 999,  314459,  325006,    147 },
  /* 34 */ {   0,   0,  325007,  335437,    244 },
  /* 35 */ {   0,   0,  335438,  345833,     98 },
};
#define CLV_NKGRP 36

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)ClvData;
   kgrp muss >= 36 sein (z.B. KGRP kgrp[36];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 42; kgrp[ 0].high= 46; kgrp[ 0].pos=      0; kgrp[ 0].end=   9072; kgrp[ 0].loop=   691;
  kgrp[ 1].pos=   9073; kgrp[ 1].end=  19306; kgrp[ 1].loop=  2765;
  kgrp[ 2].pos=  19307; kgrp[ 2].end=  27256; kgrp[ 2].loop=   690;
  kgrp[ 3].root= 47; kgrp[ 3].high= 51; kgrp[ 3].pos=  27257; kgrp[ 3].end=  35663; kgrp[ 3].loop=   519;
  kgrp[ 4].pos=  35664; kgrp[ 4].end=  44347; kgrp[ 4].loop=   518;
  kgrp[ 5].pos=  44348; kgrp[ 5].end=  56340; kgrp[ 5].loop=   514;
  kgrp[ 6].root= 52; kgrp[ 6].high= 56; kgrp[ 6].pos=  56341; kgrp[ 6].end=  68158; kgrp[ 6].loop=  1164;
  kgrp[ 7].pos=  68159; kgrp[ 7].end=  76783; kgrp[ 7].loop=   389;
  kgrp[ 8].pos=  76784; kgrp[ 8].end=  85454; kgrp[ 8].loop=   388;
  kgrp[ 9].root= 57; kgrp[ 9].high= 61; kgrp[ 9].pos=  85455; kgrp[ 9].end=  93738; kgrp[ 9].loop=  1460;
  kgrp[10].pos=  93739; kgrp[10].end= 101672; kgrp[10].loop=   584;
  kgrp[11].pos= 101673; kgrp[11].end= 110284; kgrp[11].loop=   582;
  kgrp[12].root= 62; kgrp[12].high= 68; kgrp[12].pos= 110285; kgrp[12].end= 119057; kgrp[12].loop=   436;
  kgrp[13].pos= 119058; kgrp[13].end= 127085; kgrp[13].loop=  1089;
  kgrp[14].pos= 127086; kgrp[14].end= 135747; kgrp[14].loop=   218;
  kgrp[15].root= 69; kgrp[15].high= 73; kgrp[15].pos= 135748; kgrp[15].end= 145142; kgrp[15].loop=   439;
  kgrp[16].pos= 145143; kgrp[16].end= 153506; kgrp[16].loop=   584;
  kgrp[17].pos= 153507; kgrp[17].end= 165361; kgrp[17].loop=   145;
  kgrp[18].root= 74; kgrp[18].high= 78; kgrp[18].pos= 165362; kgrp[18].end= 174925; kgrp[18].loop=   110;
  kgrp[19].pos= 174926; kgrp[19].end= 184264; kgrp[19].loop=   219;
  kgrp[20].pos= 184265; kgrp[20].end= 194082; kgrp[20].loop=   874;
  kgrp[21].root= 79; kgrp[21].high= 83; kgrp[21].pos= 194083; kgrp[21].end= 204184; kgrp[21].loop=   246;
  kgrp[22].pos= 204185; kgrp[22].end= 214222; kgrp[22].loop=   246;
  kgrp[23].pos= 214223; kgrp[23].end= 222208; kgrp[23].loop=    82;
  kgrp[24].root= 84; kgrp[24].high= 88; kgrp[24].pos= 222209; kgrp[24].end= 232797; kgrp[24].loop=   368;
  kgrp[25].pos= 232798; kgrp[25].end= 243255; kgrp[25].loop=   368;
  kgrp[26].pos= 243256; kgrp[26].end= 253365; kgrp[26].loop=   183;
  kgrp[27].root= 89; kgrp[27].high= 94; kgrp[27].pos= 253366; kgrp[27].end= 263971; kgrp[27].loop=   368;
  kgrp[28].pos= 263972; kgrp[28].end= 273825; kgrp[28].loop=    47;
  kgrp[29].pos= 273826; kgrp[29].end= 283729; kgrp[29].loop=   274;
  kgrp[30].root= 95; kgrp[30].high= 99; kgrp[30].pos= 283730; kgrp[30].end= 294120; kgrp[30].loop=   163;
  kgrp[31].pos= 294121; kgrp[31].end= 304187; kgrp[31].loop=   163;
  kgrp[32].pos= 304188; kgrp[32].end= 314458; kgrp[32].loop=   323;
  kgrp[33].root=100; kgrp[33].high=999; kgrp[33].pos= 314459; kgrp[33].end= 325006; kgrp[33].loop=   147;
  kgrp[34].pos= 325007; kgrp[34].end= 335437; kgrp[34].loop=   244;
  kgrp[35].pos= 335438; kgrp[35].end= 345833; kgrp[35].loop=    98;
*/
#endif /* CLV_KEYGROUPS_H */
