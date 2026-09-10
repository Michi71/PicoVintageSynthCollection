// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71

/* Auto-generiert von build_instrument.py - NICHT MANUELL AENDERN
   Instrument: Rd II   Keygroups: 57 (19 Regionen x 3 Velocity-Layer)
   Engine: k += 3;  Velocity-Schwellen [48, 80]
   mda-KGRP-Semantik: loop = loop-Laenge (end+1-loop_start)
   -> kgrp[] muss >= 57 sein (mda hat 34). */
#ifndef RD_II_KEYGROUPS_H
#define RD_II_KEYGROUPS_H
#include <stdint.h>

/* spiegelt struct KGRP aus mdaEPiano.h */
static const struct { int32_t root; int32_t high; int32_t pos;
                         int32_t end; int32_t loop; } Rd_IIKgrp[57] = {
  /*  0 */ {  28,  31,       0,    8717,    780 },
  /*  1 */ {   0,   0,    8718,   18780,    777 },
  /*  2 */ {   0,   0,    8718,   18780,    777 },
  /*  3 */ {  32,  35,   18781,   26644,    617 },
  /*  4 */ {   0,   0,   26645,   35393,    617 },
  /*  5 */ {   0,   0,   26645,   35393,    617 },
  /*  6 */ {  36,  39,   35394,   59271,    490 },
  /*  7 */ {   0,   0,   59272,   70786,    490 },
  /*  8 */ {   0,   0,   59272,   70786,    490 },
  /*  9 */ {  40,  43,   70787,   90565,    777 },
  /* 10 */ {   0,   0,   90566,   98776,    389 },
  /* 11 */ {   0,   0,   90566,   98776,    389 },
  /* 12 */ {  44,  47,   98777,  106771,    309 },
  /* 13 */ {   0,   0,  106772,  114738,    309 },
  /* 14 */ {   0,   0,  106772,  114738,    309 },
  /* 15 */ {  48,  51,  114739,  122699,    979 },
  /* 16 */ {   0,   0,  122700,  132722,    246 },
  /* 17 */ {   0,   0,  122700,  132722,    246 },
  /* 18 */ {  52,  55,  132723,  140672,   1747 },
  /* 19 */ {   0,   0,  140673,  150954,    195 },
  /* 20 */ {   0,   0,  140673,  150954,    195 },
  /* 21 */ {  56,  59,  150955,  163960,    309 },
  /* 22 */ {   0,   0,  163961,  172810,    309 },
  /* 23 */ {   0,   0,  163961,  172810,    309 },
  /* 24 */ {  60,  63,  172811,  183114,    490 },
  /* 25 */ {   0,   0,  183115,  193572,    490 },
  /* 26 */ {   0,   0,  183115,  193572,    490 },
  /* 27 */ {  64,  67,  193573,  214897,    292 },
  /* 28 */ {   0,   0,  214898,  236213,    389 },
  /* 29 */ {   0,   0,  214898,  236213,    389 },
  /* 30 */ {  68,  71,  236214,  259183,    386 },
  /* 31 */ {   0,   0,  259184,  270186,    387 },
  /* 32 */ {   0,   0,  259184,  270186,    387 },
  /* 33 */ {  72,  75,  270187,  278164,    430 },
  /* 34 */ {   0,   0,  278165,  290245,    307 },
  /* 35 */ {   0,   0,  278165,  290245,    307 },
  /* 36 */ {  76,  79,  290246,  305339,    292 },
  /* 37 */ {   0,   0,  305340,  320783,    195 },
  /* 38 */ {   0,   0,  305340,  320783,    195 },
  /* 39 */ {  80,  83,  320784,  329709,    193 },
  /* 40 */ {   0,   0,  329710,  342218,    194 },
  /* 41 */ {   0,   0,  329710,  342218,    194 },
  /* 42 */ {  84,  89,  342219,  354596,    185 },
  /* 43 */ {   0,   0,  354597,  370145,    154 },
  /* 44 */ {   0,   0,  354597,  370145,    154 },
  /* 45 */ {  90,  90,  370146,  380316,    122 },
  /* 46 */ {   0,   0,  370146,  380316,    122 },
  /* 47 */ {   0,   0,  370146,  380316,    122 },
  /* 48 */ {  91,  93,  380317,  396951,     98 },
  /* 49 */ {   0,   0,  380317,  396951,     98 },
  /* 50 */ {   0,   0,  380317,  396951,     98 },
  /* 51 */ {  94,  97,  396952,  408782,     98 },
  /* 52 */ {   0,   0,  408783,  418856,     97 },
  /* 53 */ {   0,   0,  408783,  418856,     97 },
  /* 54 */ {  98, 999,  418857,  430048,     77 },
  /* 55 */ {   0,   0,  430049,  442396,     77 },
  /* 56 */ {   0,   0,  430049,  442396,     77 },
};
#define RD_II_NKGRP 57

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)Rd_IIData;
   kgrp muss >= 57 sein (z.B. KGRP kgrp[57];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 28; kgrp[ 0].high= 31; kgrp[ 0].pos=      0; kgrp[ 0].end=   8717; kgrp[ 0].loop=   780;
  kgrp[ 1].pos=   8718; kgrp[ 1].end=  18780; kgrp[ 1].loop=   777;
  kgrp[ 2].pos=   8718; kgrp[ 2].end=  18780; kgrp[ 2].loop=   777;
  kgrp[ 3].root= 32; kgrp[ 3].high= 35; kgrp[ 3].pos=  18781; kgrp[ 3].end=  26644; kgrp[ 3].loop=   617;
  kgrp[ 4].pos=  26645; kgrp[ 4].end=  35393; kgrp[ 4].loop=   617;
  kgrp[ 5].pos=  26645; kgrp[ 5].end=  35393; kgrp[ 5].loop=   617;
  kgrp[ 6].root= 36; kgrp[ 6].high= 39; kgrp[ 6].pos=  35394; kgrp[ 6].end=  59271; kgrp[ 6].loop=   490;
  kgrp[ 7].pos=  59272; kgrp[ 7].end=  70786; kgrp[ 7].loop=   490;
  kgrp[ 8].pos=  59272; kgrp[ 8].end=  70786; kgrp[ 8].loop=   490;
  kgrp[ 9].root= 40; kgrp[ 9].high= 43; kgrp[ 9].pos=  70787; kgrp[ 9].end=  90565; kgrp[ 9].loop=   777;
  kgrp[10].pos=  90566; kgrp[10].end=  98776; kgrp[10].loop=   389;
  kgrp[11].pos=  90566; kgrp[11].end=  98776; kgrp[11].loop=   389;
  kgrp[12].root= 44; kgrp[12].high= 47; kgrp[12].pos=  98777; kgrp[12].end= 106771; kgrp[12].loop=   309;
  kgrp[13].pos= 106772; kgrp[13].end= 114738; kgrp[13].loop=   309;
  kgrp[14].pos= 106772; kgrp[14].end= 114738; kgrp[14].loop=   309;
  kgrp[15].root= 48; kgrp[15].high= 51; kgrp[15].pos= 114739; kgrp[15].end= 122699; kgrp[15].loop=   979;
  kgrp[16].pos= 122700; kgrp[16].end= 132722; kgrp[16].loop=   246;
  kgrp[17].pos= 122700; kgrp[17].end= 132722; kgrp[17].loop=   246;
  kgrp[18].root= 52; kgrp[18].high= 55; kgrp[18].pos= 132723; kgrp[18].end= 140672; kgrp[18].loop=  1747;
  kgrp[19].pos= 140673; kgrp[19].end= 150954; kgrp[19].loop=   195;
  kgrp[20].pos= 140673; kgrp[20].end= 150954; kgrp[20].loop=   195;
  kgrp[21].root= 56; kgrp[21].high= 59; kgrp[21].pos= 150955; kgrp[21].end= 163960; kgrp[21].loop=   309;
  kgrp[22].pos= 163961; kgrp[22].end= 172810; kgrp[22].loop=   309;
  kgrp[23].pos= 163961; kgrp[23].end= 172810; kgrp[23].loop=   309;
  kgrp[24].root= 60; kgrp[24].high= 63; kgrp[24].pos= 172811; kgrp[24].end= 183114; kgrp[24].loop=   490;
  kgrp[25].pos= 183115; kgrp[25].end= 193572; kgrp[25].loop=   490;
  kgrp[26].pos= 183115; kgrp[26].end= 193572; kgrp[26].loop=   490;
  kgrp[27].root= 64; kgrp[27].high= 67; kgrp[27].pos= 193573; kgrp[27].end= 214897; kgrp[27].loop=   292;
  kgrp[28].pos= 214898; kgrp[28].end= 236213; kgrp[28].loop=   389;
  kgrp[29].pos= 214898; kgrp[29].end= 236213; kgrp[29].loop=   389;
  kgrp[30].root= 68; kgrp[30].high= 71; kgrp[30].pos= 236214; kgrp[30].end= 259183; kgrp[30].loop=   386;
  kgrp[31].pos= 259184; kgrp[31].end= 270186; kgrp[31].loop=   387;
  kgrp[32].pos= 259184; kgrp[32].end= 270186; kgrp[32].loop=   387;
  kgrp[33].root= 72; kgrp[33].high= 75; kgrp[33].pos= 270187; kgrp[33].end= 278164; kgrp[33].loop=   430;
  kgrp[34].pos= 278165; kgrp[34].end= 290245; kgrp[34].loop=   307;
  kgrp[35].pos= 278165; kgrp[35].end= 290245; kgrp[35].loop=   307;
  kgrp[36].root= 76; kgrp[36].high= 79; kgrp[36].pos= 290246; kgrp[36].end= 305339; kgrp[36].loop=   292;
  kgrp[37].pos= 305340; kgrp[37].end= 320783; kgrp[37].loop=   195;
  kgrp[38].pos= 305340; kgrp[38].end= 320783; kgrp[38].loop=   195;
  kgrp[39].root= 80; kgrp[39].high= 83; kgrp[39].pos= 320784; kgrp[39].end= 329709; kgrp[39].loop=   193;
  kgrp[40].pos= 329710; kgrp[40].end= 342218; kgrp[40].loop=   194;
  kgrp[41].pos= 329710; kgrp[41].end= 342218; kgrp[41].loop=   194;
  kgrp[42].root= 84; kgrp[42].high= 89; kgrp[42].pos= 342219; kgrp[42].end= 354596; kgrp[42].loop=   185;
  kgrp[43].pos= 354597; kgrp[43].end= 370145; kgrp[43].loop=   154;
  kgrp[44].pos= 354597; kgrp[44].end= 370145; kgrp[44].loop=   154;
  kgrp[45].root= 90; kgrp[45].high= 90; kgrp[45].pos= 370146; kgrp[45].end= 380316; kgrp[45].loop=   122;
  kgrp[46].pos= 370146; kgrp[46].end= 380316; kgrp[46].loop=   122;
  kgrp[47].pos= 370146; kgrp[47].end= 380316; kgrp[47].loop=   122;
  kgrp[48].root= 91; kgrp[48].high= 93; kgrp[48].pos= 380317; kgrp[48].end= 396951; kgrp[48].loop=    98;
  kgrp[49].pos= 380317; kgrp[49].end= 396951; kgrp[49].loop=    98;
  kgrp[50].pos= 380317; kgrp[50].end= 396951; kgrp[50].loop=    98;
  kgrp[51].root= 94; kgrp[51].high= 97; kgrp[51].pos= 396952; kgrp[51].end= 408782; kgrp[51].loop=    98;
  kgrp[52].pos= 408783; kgrp[52].end= 418856; kgrp[52].loop=    97;
  kgrp[53].pos= 408783; kgrp[53].end= 418856; kgrp[53].loop=    97;
  kgrp[54].root= 98; kgrp[54].high=999; kgrp[54].pos= 418857; kgrp[54].end= 430048; kgrp[54].loop=    77;
  kgrp[55].pos= 430049; kgrp[55].end= 442396; kgrp[55].loop=    77;
  kgrp[56].pos= 430049; kgrp[56].end= 442396; kgrp[56].loop=    77;
*/
#endif /* RD_II_KEYGROUPS_H */
