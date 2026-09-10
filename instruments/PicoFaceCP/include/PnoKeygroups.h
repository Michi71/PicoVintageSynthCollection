// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN
   Instrument: Pno   Keygroups: 42 (14 Regionen x 3 Velocity-Layer)
   Engine: k += 3;  Velocity-Schwellen [48, 80]
   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)
   -> kgrp[] muss >= 42 sein (mda hat 34). */
#ifndef PNO_KEYGROUPS_H
#define PNO_KEYGROUPS_H
#include <stdint.h>

/* spiegelt struct KGRP aus mdaEPiano.h */
static const struct { int32_t root; int32_t high; int32_t pos;
                         int32_t end; int32_t loop; } PnoKgrp[42] = {
  /*  0 */ {  30,  35,       0,   11044,   2085 },
  /*  1 */ {   0,   0,       0,   11044,   2085 },
  /*  2 */ {   0,   0,       0,   11044,   2085 },
  /*  3 */ {  36,  42,   11045,   27493,    488 },
  /*  4 */ {   0,   0,   11045,   27493,    488 },
  /*  5 */ {   0,   0,   11045,   27493,    488 },
  /*  6 */ {  43,  47,   27494,   36563,    326 },
  /*  7 */ {   0,   0,   27494,   36563,    326 },
  /*  8 */ {   0,   0,   27494,   36563,    326 },
  /*  9 */ {  48,  54,   36564,   44548,    244 },
  /* 10 */ {   0,   0,   36564,   44548,    244 },
  /* 11 */ {   0,   0,   36564,   44548,    244 },
  /* 12 */ {  55,  59,   44549,   53613,    163 },
  /* 13 */ {   0,   0,   44549,   53613,    163 },
  /* 14 */ {   0,   0,   44549,   53613,    163 },
  /* 15 */ {  60,  66,   53614,   64223,    122 },
  /* 16 */ {   0,   0,   53614,   64223,    122 },
  /* 17 */ {   0,   0,   53614,   64223,    122 },
  /* 18 */ {  67,  71,   64224,   72277,     82 },
  /* 19 */ {   0,   0,   64224,   72277,     82 },
  /* 20 */ {   0,   0,   64224,   72277,     82 },
  /* 21 */ {  72,  78,   72278,   80544,    488 },
  /* 22 */ {   0,   0,   72278,   80544,    488 },
  /* 23 */ {   0,   0,   72278,   80544,    488 },
  /* 24 */ {  79,  83,   80545,   92432,    286 },
  /* 25 */ {   0,   0,   80545,   92432,    286 },
  /* 26 */ {   0,   0,   80545,   92432,    286 },
  /* 27 */ {  84,  90,   92433,  102494,    244 },
  /* 28 */ {   0,   0,   92433,  102494,    244 },
  /* 29 */ {   0,   0,   92433,  102494,    244 },
  /* 30 */ {  91,  95,  102495,  110486,    285 },
  /* 31 */ {   0,   0,  102495,  110486,    285 },
  /* 32 */ {   0,   0,  102495,  110486,    285 },
  /* 33 */ {  96, 103,  110487,  122320,     91 },
  /* 34 */ {   0,   0,  110487,  122320,     91 },
  /* 35 */ {   0,   0,  110487,  122320,     91 },
  /* 36 */ { 104, 107,  122321,  134157,     58 },
  /* 37 */ {   0,   0,  122321,  134157,     58 },
  /* 38 */ {   0,   0,  122321,  134157,     58 },
  /* 39 */ { 108, 999,  134158,  143116,     31 },
  /* 40 */ {   0,   0,  134158,  143116,     31 },
  /* 41 */ {   0,   0,  134158,  143116,     31 },
};
#define PNO_NKGRP 42

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)PnoData;
   kgrp muss >= 42 sein (z.B. KGRP kgrp[42];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 30; kgrp[ 0].high= 35; kgrp[ 0].pos=      0; kgrp[ 0].end=  11044; kgrp[ 0].loop=  2085;
  kgrp[ 1].pos=      0; kgrp[ 1].end=  11044; kgrp[ 1].loop=  2085;
  kgrp[ 2].pos=      0; kgrp[ 2].end=  11044; kgrp[ 2].loop=  2085;
  kgrp[ 3].root= 36; kgrp[ 3].high= 42; kgrp[ 3].pos=  11045; kgrp[ 3].end=  27493; kgrp[ 3].loop=   488;
  kgrp[ 4].pos=  11045; kgrp[ 4].end=  27493; kgrp[ 4].loop=   488;
  kgrp[ 5].pos=  11045; kgrp[ 5].end=  27493; kgrp[ 5].loop=   488;
  kgrp[ 6].root= 43; kgrp[ 6].high= 47; kgrp[ 6].pos=  27494; kgrp[ 6].end=  36563; kgrp[ 6].loop=   326;
  kgrp[ 7].pos=  27494; kgrp[ 7].end=  36563; kgrp[ 7].loop=   326;
  kgrp[ 8].pos=  27494; kgrp[ 8].end=  36563; kgrp[ 8].loop=   326;
  kgrp[ 9].root= 48; kgrp[ 9].high= 54; kgrp[ 9].pos=  36564; kgrp[ 9].end=  44548; kgrp[ 9].loop=   244;
  kgrp[10].pos=  36564; kgrp[10].end=  44548; kgrp[10].loop=   244;
  kgrp[11].pos=  36564; kgrp[11].end=  44548; kgrp[11].loop=   244;
  kgrp[12].root= 55; kgrp[12].high= 59; kgrp[12].pos=  44549; kgrp[12].end=  53613; kgrp[12].loop=   163;
  kgrp[13].pos=  44549; kgrp[13].end=  53613; kgrp[13].loop=   163;
  kgrp[14].pos=  44549; kgrp[14].end=  53613; kgrp[14].loop=   163;
  kgrp[15].root= 60; kgrp[15].high= 66; kgrp[15].pos=  53614; kgrp[15].end=  64223; kgrp[15].loop=   122;
  kgrp[16].pos=  53614; kgrp[16].end=  64223; kgrp[16].loop=   122;
  kgrp[17].pos=  53614; kgrp[17].end=  64223; kgrp[17].loop=   122;
  kgrp[18].root= 67; kgrp[18].high= 71; kgrp[18].pos=  64224; kgrp[18].end=  72277; kgrp[18].loop=    82;
  kgrp[19].pos=  64224; kgrp[19].end=  72277; kgrp[19].loop=    82;
  kgrp[20].pos=  64224; kgrp[20].end=  72277; kgrp[20].loop=    82;
  kgrp[21].root= 72; kgrp[21].high= 78; kgrp[21].pos=  72278; kgrp[21].end=  80544; kgrp[21].loop=   488;
  kgrp[22].pos=  72278; kgrp[22].end=  80544; kgrp[22].loop=   488;
  kgrp[23].pos=  72278; kgrp[23].end=  80544; kgrp[23].loop=   488;
  kgrp[24].root= 79; kgrp[24].high= 83; kgrp[24].pos=  80545; kgrp[24].end=  92432; kgrp[24].loop=   286;
  kgrp[25].pos=  80545; kgrp[25].end=  92432; kgrp[25].loop=   286;
  kgrp[26].pos=  80545; kgrp[26].end=  92432; kgrp[26].loop=   286;
  kgrp[27].root= 84; kgrp[27].high= 90; kgrp[27].pos=  92433; kgrp[27].end= 102494; kgrp[27].loop=   244;
  kgrp[28].pos=  92433; kgrp[28].end= 102494; kgrp[28].loop=   244;
  kgrp[29].pos=  92433; kgrp[29].end= 102494; kgrp[29].loop=   244;
  kgrp[30].root= 91; kgrp[30].high= 95; kgrp[30].pos= 102495; kgrp[30].end= 110486; kgrp[30].loop=   285;
  kgrp[31].pos= 102495; kgrp[31].end= 110486; kgrp[31].loop=   285;
  kgrp[32].pos= 102495; kgrp[32].end= 110486; kgrp[32].loop=   285;
  kgrp[33].root= 96; kgrp[33].high=103; kgrp[33].pos= 110487; kgrp[33].end= 122320; kgrp[33].loop=    91;
  kgrp[34].pos= 110487; kgrp[34].end= 122320; kgrp[34].loop=    91;
  kgrp[35].pos= 110487; kgrp[35].end= 122320; kgrp[35].loop=    91;
  kgrp[36].root=104; kgrp[36].high=107; kgrp[36].pos= 122321; kgrp[36].end= 134157; kgrp[36].loop=    58;
  kgrp[37].pos= 122321; kgrp[37].end= 134157; kgrp[37].loop=    58;
  kgrp[38].pos= 122321; kgrp[38].end= 134157; kgrp[38].loop=    58;
  kgrp[39].root=108; kgrp[39].high=999; kgrp[39].pos= 134158; kgrp[39].end= 143116; kgrp[39].loop=    31;
  kgrp[40].pos= 134158; kgrp[40].end= 143116; kgrp[40].loop=    31;
  kgrp[41].pos= 134158; kgrp[41].end= 143116; kgrp[41].loop=    31;
*/
#endif /* PNO_KEYGROUPS_H */
