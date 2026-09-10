// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN
   Instrument: CP   Keygroups: 42 (14 Regionen x 3 Velocity-Layer)
   Engine: k += 3;  Velocity-Schwellen [48, 80]
   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)
   -> kgrp[] muss >= 42 sein (mda hat 34). */
#ifndef CP_KEYGROUPS_H
#define CP_KEYGROUPS_H
#include <stdint.h>

/* spiegelt struct KGRP aus mdaEPiano.h */
static const struct { int32_t root; int32_t high; int32_t pos;
                         int32_t end; int32_t loop; } CPKgrp[42] = {
  /*  0 */ {  42,  45,       0,    8319,   1038 },
  /*  1 */ {   0,   0,       0,    8319,   1038 },
  /*  2 */ {   0,   0,    8320,   18522,   1037 },
  /*  3 */ {  46,  49,   18523,   29388,    550 },
  /*  4 */ {   0,   0,   18523,   29388,    550 },
  /*  5 */ {   0,   0,   29389,   37968,   1374 },
  /*  6 */ {  50,  52,   37969,   46742,   4145 },
  /*  7 */ {   0,   0,   37969,   46742,   4145 },
  /*  8 */ {   0,   0,   46743,   54734,    217 },
  /*  9 */ {  53,  56,   54735,   63224,    735 },
  /* 10 */ {   0,   0,   54735,   63224,    735 },
  /* 11 */ {   0,   0,   63225,   73363,   1833 },
  /* 12 */ {  57,  62,   73364,   81522,   2187 },
  /* 13 */ {   0,   0,   73364,   81522,   2187 },
  /* 14 */ {   0,   0,   81523,   89821,    146 },
  /* 15 */ {  63,  64,   89822,   98262,    412 },
  /* 16 */ {   0,   0,   89822,   98262,    412 },
  /* 17 */ {   0,   0,   98263,  107004,   1854 },
  /* 18 */ {  65,  67,  107005,  114995,    551 },
  /* 19 */ {   0,   0,  107005,  114995,    551 },
  /* 20 */ {   0,   0,  114996,  122972,   1011 },
  /* 21 */ {  68,  71,  122973,  130962,    387 },
  /* 22 */ {   0,   0,  122973,  130962,    387 },
  /* 23 */ {   0,   0,  130963,  139746,    622 },
  /* 24 */ {  72,  79,  139747,  148366,    123 },
  /* 25 */ {   0,   0,  139747,  148366,    123 },
  /* 26 */ {   0,   0,  148367,  158604,    184 },
  /* 27 */ {  80,  81,  158605,  166763,    462 },
  /* 28 */ {   0,   0,  158605,  166763,    462 },
  /* 29 */ {   0,   0,  166764,  176826,    308 },
  /* 30 */ {  82,  87,  176827,  187860,    172 },
  /* 31 */ {   0,   0,  176827,  187860,    172 },
  /* 32 */ {   0,   0,  187861,  199219,    411 },
  /* 33 */ {  88,  90,  199220,  209458,    122 },
  /* 34 */ {   0,   0,  199220,  209458,    122 },
  /* 35 */ {   0,   0,  209459,  217773,    291 },
  /* 36 */ {  91,  96,  217774,  228804,    285 },
  /* 37 */ {   0,   0,  217774,  228804,    285 },
  /* 38 */ {   0,   0,  228805,  239993,    123 },
  /* 39 */ {  97, 999,  239994,  251830,    230 },
  /* 40 */ {   0,   0,  239994,  251830,    230 },
  /* 41 */ {   0,   0,  251831,  261424,    130 },
};
#define CP_NKGRP 42

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)CPData;
   kgrp muss >= 42 sein (z.B. KGRP kgrp[42];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 42; kgrp[ 0].high= 45; kgrp[ 0].pos=      0; kgrp[ 0].end=   8319; kgrp[ 0].loop=  1038;
  kgrp[ 1].pos=      0; kgrp[ 1].end=   8319; kgrp[ 1].loop=  1038;
  kgrp[ 2].pos=   8320; kgrp[ 2].end=  18522; kgrp[ 2].loop=  1037;
  kgrp[ 3].root= 46; kgrp[ 3].high= 49; kgrp[ 3].pos=  18523; kgrp[ 3].end=  29388; kgrp[ 3].loop=   550;
  kgrp[ 4].pos=  18523; kgrp[ 4].end=  29388; kgrp[ 4].loop=   550;
  kgrp[ 5].pos=  29389; kgrp[ 5].end=  37968; kgrp[ 5].loop=  1374;
  kgrp[ 6].root= 50; kgrp[ 6].high= 52; kgrp[ 6].pos=  37969; kgrp[ 6].end=  46742; kgrp[ 6].loop=  4145;
  kgrp[ 7].pos=  37969; kgrp[ 7].end=  46742; kgrp[ 7].loop=  4145;
  kgrp[ 8].pos=  46743; kgrp[ 8].end=  54734; kgrp[ 8].loop=   217;
  kgrp[ 9].root= 53; kgrp[ 9].high= 56; kgrp[ 9].pos=  54735; kgrp[ 9].end=  63224; kgrp[ 9].loop=   735;
  kgrp[10].pos=  54735; kgrp[10].end=  63224; kgrp[10].loop=   735;
  kgrp[11].pos=  63225; kgrp[11].end=  73363; kgrp[11].loop=  1833;
  kgrp[12].root= 57; kgrp[12].high= 62; kgrp[12].pos=  73364; kgrp[12].end=  81522; kgrp[12].loop=  2187;
  kgrp[13].pos=  73364; kgrp[13].end=  81522; kgrp[13].loop=  2187;
  kgrp[14].pos=  81523; kgrp[14].end=  89821; kgrp[14].loop=   146;
  kgrp[15].root= 63; kgrp[15].high= 64; kgrp[15].pos=  89822; kgrp[15].end=  98262; kgrp[15].loop=   412;
  kgrp[16].pos=  89822; kgrp[16].end=  98262; kgrp[16].loop=   412;
  kgrp[17].pos=  98263; kgrp[17].end= 107004; kgrp[17].loop=  1854;
  kgrp[18].root= 65; kgrp[18].high= 67; kgrp[18].pos= 107005; kgrp[18].end= 114995; kgrp[18].loop=   551;
  kgrp[19].pos= 107005; kgrp[19].end= 114995; kgrp[19].loop=   551;
  kgrp[20].pos= 114996; kgrp[20].end= 122972; kgrp[20].loop=  1011;
  kgrp[21].root= 68; kgrp[21].high= 71; kgrp[21].pos= 122973; kgrp[21].end= 130962; kgrp[21].loop=   387;
  kgrp[22].pos= 122973; kgrp[22].end= 130962; kgrp[22].loop=   387;
  kgrp[23].pos= 130963; kgrp[23].end= 139746; kgrp[23].loop=   622;
  kgrp[24].root= 72; kgrp[24].high= 79; kgrp[24].pos= 139747; kgrp[24].end= 148366; kgrp[24].loop=   123;
  kgrp[25].pos= 139747; kgrp[25].end= 148366; kgrp[25].loop=   123;
  kgrp[26].pos= 148367; kgrp[26].end= 158604; kgrp[26].loop=   184;
  kgrp[27].root= 80; kgrp[27].high= 81; kgrp[27].pos= 158605; kgrp[27].end= 166763; kgrp[27].loop=   462;
  kgrp[28].pos= 158605; kgrp[28].end= 166763; kgrp[28].loop=   462;
  kgrp[29].pos= 166764; kgrp[29].end= 176826; kgrp[29].loop=   308;
  kgrp[30].root= 82; kgrp[30].high= 87; kgrp[30].pos= 176827; kgrp[30].end= 187860; kgrp[30].loop=   172;
  kgrp[31].pos= 176827; kgrp[31].end= 187860; kgrp[31].loop=   172;
  kgrp[32].pos= 187861; kgrp[32].end= 199219; kgrp[32].loop=   411;
  kgrp[33].root= 88; kgrp[33].high= 90; kgrp[33].pos= 199220; kgrp[33].end= 209458; kgrp[33].loop=   122;
  kgrp[34].pos= 199220; kgrp[34].end= 209458; kgrp[34].loop=   122;
  kgrp[35].pos= 209459; kgrp[35].end= 217773; kgrp[35].loop=   291;
  kgrp[36].root= 91; kgrp[36].high= 96; kgrp[36].pos= 217774; kgrp[36].end= 228804; kgrp[36].loop=   285;
  kgrp[37].pos= 217774; kgrp[37].end= 228804; kgrp[37].loop=   285;
  kgrp[38].pos= 228805; kgrp[38].end= 239993; kgrp[38].loop=   123;
  kgrp[39].root= 97; kgrp[39].high=999; kgrp[39].pos= 239994; kgrp[39].end= 251830; kgrp[39].loop=   230;
  kgrp[40].pos= 239994; kgrp[40].end= 251830; kgrp[40].loop=   230;
  kgrp[41].pos= 251831; kgrp[41].end= 261424; kgrp[41].loop=   130;
*/
#endif /* CP_KEYGROUPS_H */
