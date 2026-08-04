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
  /*  0 */ {  28,  31,       0,    8926,    779 },
  /*  1 */ {   0,   0,    8927,   18989,    777 },
  /*  2 */ {   0,   0,    8927,   18989,    777 },
  /*  3 */ {  32,  35,   18990,   26853,    617 },
  /*  4 */ {   0,   0,   26854,   38680,    617 },
  /*  5 */ {   0,   0,   26854,   38680,    617 },
  /*  6 */ {  36,  39,   38681,   64113,    980 },
  /*  7 */ {   0,   0,   64114,   75139,    978 },
  /*  8 */ {   0,   0,   64114,   75139,    978 },
  /*  9 */ {  40,  43,   75140,   97245,    389 },
  /* 10 */ {   0,   0,   97246,  109232,    389 },
  /* 11 */ {   0,   0,   97246,  109232,    389 },
  /* 12 */ {  44,  47,  109233,  117227,    309 },
  /* 13 */ {   0,   0,  117228,  129198,    925 },
  /* 14 */ {   0,   0,  117228,  129198,    925 },
  /* 15 */ {  48,  51,  129199,  139498,   1712 },
  /* 16 */ {   0,   0,  139499,  151763,    490 },
  /* 17 */ {   0,   0,  139499,  151763,    490 },
  /* 18 */ {  52,  55,  151764,  163597,    195 },
  /* 19 */ {   0,   0,  163598,  176435,    195 },
  /* 20 */ {   0,   0,  163598,  176435,    195 },
  /* 21 */ {  56,  59,  176436,  191378,    309 },
  /* 22 */ {   0,   0,  191379,  203583,    309 },
  /* 23 */ {   0,   0,  191379,  203583,    309 },
  /* 24 */ {  60,  63,  203584,  215476,    490 },
  /* 25 */ {   0,   0,  215477,  227523,    735 },
  /* 26 */ {   0,   0,  215477,  227523,    735 },
  /* 27 */ {  64,  67,  227524,  249623,    389 },
  /* 28 */ {   0,   0,  249624,  272550,    486 },
  /* 29 */ {   0,   0,  249624,  272550,    486 },
  /* 30 */ {  68,  71,  272551,  296335,    463 },
  /* 31 */ {   0,   0,  296336,  308155,    155 },
  /* 32 */ {   0,   0,  296336,  308155,    155 },
  /* 33 */ {  72,  75,  308156,  319985,    306 },
  /* 34 */ {   0,   0,  319986,  332555,    307 },
  /* 35 */ {   0,   0,  319986,  332555,    307 },
  /* 36 */ {  76,  79,  332556,  348618,    583 },
  /* 37 */ {   0,   0,  348619,  364865,    292 },
  /* 38 */ {   0,   0,  348619,  364865,    292 },
  /* 39 */ {  80,  83,  364866,  377471,    194 },
  /* 40 */ {   0,   0,  377472,  391096,    155 },
  /* 41 */ {   0,   0,  377472,  391096,    155 },
  /* 42 */ {  84,  89,  391097,  403474,    185 },
  /* 43 */ {   0,   0,  403475,  419329,    154 },
  /* 44 */ {   0,   0,  403475,  419329,    154 },
  /* 45 */ {  90,  90,  419330,  429815,    219 },
  /* 46 */ {   0,   0,  419330,  429815,    219 },
  /* 47 */ {   0,   0,  419330,  429815,    219 },
  /* 48 */ {  91,  93,  429816,  446450,     98 },
  /* 49 */ {   0,   0,  429816,  446450,     98 },
  /* 50 */ {   0,   0,  429816,  446450,     98 },
  /* 51 */ {  94,  97,  446451,  458281,     98 },
  /* 52 */ {   0,   0,  458282,  468520,     97 },
  /* 53 */ {   0,   0,  458282,  468520,     97 },
  /* 54 */ {  98, 999,  468521,  480839,    108 },
  /* 55 */ {   0,   0,  480840,  495103,     93 },
  /* 56 */ {   0,   0,  480840,  495103,     93 },
};
#define RD_II_NKGRP 57

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)Rd_IIData;
   kgrp muss >= 57 sein (z.B. KGRP kgrp[57];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 28; kgrp[ 0].high= 31; kgrp[ 0].pos=      0; kgrp[ 0].end=   8926; kgrp[ 0].loop=   779;
  kgrp[ 1].pos=   8927; kgrp[ 1].end=  18989; kgrp[ 1].loop=   777;
  kgrp[ 2].pos=   8927; kgrp[ 2].end=  18989; kgrp[ 2].loop=   777;
  kgrp[ 3].root= 32; kgrp[ 3].high= 35; kgrp[ 3].pos=  18990; kgrp[ 3].end=  26853; kgrp[ 3].loop=   617;
  kgrp[ 4].pos=  26854; kgrp[ 4].end=  38680; kgrp[ 4].loop=   617;
  kgrp[ 5].pos=  26854; kgrp[ 5].end=  38680; kgrp[ 5].loop=   617;
  kgrp[ 6].root= 36; kgrp[ 6].high= 39; kgrp[ 6].pos=  38681; kgrp[ 6].end=  64113; kgrp[ 6].loop=   980;
  kgrp[ 7].pos=  64114; kgrp[ 7].end=  75139; kgrp[ 7].loop=   978;
  kgrp[ 8].pos=  64114; kgrp[ 8].end=  75139; kgrp[ 8].loop=   978;
  kgrp[ 9].root= 40; kgrp[ 9].high= 43; kgrp[ 9].pos=  75140; kgrp[ 9].end=  97245; kgrp[ 9].loop=   389;
  kgrp[10].pos=  97246; kgrp[10].end= 109232; kgrp[10].loop=   389;
  kgrp[11].pos=  97246; kgrp[11].end= 109232; kgrp[11].loop=   389;
  kgrp[12].root= 44; kgrp[12].high= 47; kgrp[12].pos= 109233; kgrp[12].end= 117227; kgrp[12].loop=   309;
  kgrp[13].pos= 117228; kgrp[13].end= 129198; kgrp[13].loop=   925;
  kgrp[14].pos= 117228; kgrp[14].end= 129198; kgrp[14].loop=   925;
  kgrp[15].root= 48; kgrp[15].high= 51; kgrp[15].pos= 129199; kgrp[15].end= 139498; kgrp[15].loop=  1712;
  kgrp[16].pos= 139499; kgrp[16].end= 151763; kgrp[16].loop=   490;
  kgrp[17].pos= 139499; kgrp[17].end= 151763; kgrp[17].loop=   490;
  kgrp[18].root= 52; kgrp[18].high= 55; kgrp[18].pos= 151764; kgrp[18].end= 163597; kgrp[18].loop=   195;
  kgrp[19].pos= 163598; kgrp[19].end= 176435; kgrp[19].loop=   195;
  kgrp[20].pos= 163598; kgrp[20].end= 176435; kgrp[20].loop=   195;
  kgrp[21].root= 56; kgrp[21].high= 59; kgrp[21].pos= 176436; kgrp[21].end= 191378; kgrp[21].loop=   309;
  kgrp[22].pos= 191379; kgrp[22].end= 203583; kgrp[22].loop=   309;
  kgrp[23].pos= 191379; kgrp[23].end= 203583; kgrp[23].loop=   309;
  kgrp[24].root= 60; kgrp[24].high= 63; kgrp[24].pos= 203584; kgrp[24].end= 215476; kgrp[24].loop=   490;
  kgrp[25].pos= 215477; kgrp[25].end= 227523; kgrp[25].loop=   735;
  kgrp[26].pos= 215477; kgrp[26].end= 227523; kgrp[26].loop=   735;
  kgrp[27].root= 64; kgrp[27].high= 67; kgrp[27].pos= 227524; kgrp[27].end= 249623; kgrp[27].loop=   389;
  kgrp[28].pos= 249624; kgrp[28].end= 272550; kgrp[28].loop=   486;
  kgrp[29].pos= 249624; kgrp[29].end= 272550; kgrp[29].loop=   486;
  kgrp[30].root= 68; kgrp[30].high= 71; kgrp[30].pos= 272551; kgrp[30].end= 296335; kgrp[30].loop=   463;
  kgrp[31].pos= 296336; kgrp[31].end= 308155; kgrp[31].loop=   155;
  kgrp[32].pos= 296336; kgrp[32].end= 308155; kgrp[32].loop=   155;
  kgrp[33].root= 72; kgrp[33].high= 75; kgrp[33].pos= 308156; kgrp[33].end= 319985; kgrp[33].loop=   306;
  kgrp[34].pos= 319986; kgrp[34].end= 332555; kgrp[34].loop=   307;
  kgrp[35].pos= 319986; kgrp[35].end= 332555; kgrp[35].loop=   307;
  kgrp[36].root= 76; kgrp[36].high= 79; kgrp[36].pos= 332556; kgrp[36].end= 348618; kgrp[36].loop=   583;
  kgrp[37].pos= 348619; kgrp[37].end= 364865; kgrp[37].loop=   292;
  kgrp[38].pos= 348619; kgrp[38].end= 364865; kgrp[38].loop=   292;
  kgrp[39].root= 80; kgrp[39].high= 83; kgrp[39].pos= 364866; kgrp[39].end= 377471; kgrp[39].loop=   194;
  kgrp[40].pos= 377472; kgrp[40].end= 391096; kgrp[40].loop=   155;
  kgrp[41].pos= 377472; kgrp[41].end= 391096; kgrp[41].loop=   155;
  kgrp[42].root= 84; kgrp[42].high= 89; kgrp[42].pos= 391097; kgrp[42].end= 403474; kgrp[42].loop=   185;
  kgrp[43].pos= 403475; kgrp[43].end= 419329; kgrp[43].loop=   154;
  kgrp[44].pos= 403475; kgrp[44].end= 419329; kgrp[44].loop=   154;
  kgrp[45].root= 90; kgrp[45].high= 90; kgrp[45].pos= 419330; kgrp[45].end= 429815; kgrp[45].loop=   219;
  kgrp[46].pos= 419330; kgrp[46].end= 429815; kgrp[46].loop=   219;
  kgrp[47].pos= 419330; kgrp[47].end= 429815; kgrp[47].loop=   219;
  kgrp[48].root= 91; kgrp[48].high= 93; kgrp[48].pos= 429816; kgrp[48].end= 446450; kgrp[48].loop=    98;
  kgrp[49].pos= 429816; kgrp[49].end= 446450; kgrp[49].loop=    98;
  kgrp[50].pos= 429816; kgrp[50].end= 446450; kgrp[50].loop=    98;
  kgrp[51].root= 94; kgrp[51].high= 97; kgrp[51].pos= 446451; kgrp[51].end= 458281; kgrp[51].loop=    98;
  kgrp[52].pos= 458282; kgrp[52].end= 468520; kgrp[52].loop=    97;
  kgrp[53].pos= 458282; kgrp[53].end= 468520; kgrp[53].loop=    97;
  kgrp[54].root= 98; kgrp[54].high=999; kgrp[54].pos= 468521; kgrp[54].end= 480839; kgrp[54].loop=   108;
  kgrp[55].pos= 480840; kgrp[55].end= 495103; kgrp[55].loop=    93;
  kgrp[56].pos= 480840; kgrp[56].end= 495103; kgrp[56].loop=    93;
*/
#endif /* RD_II_KEYGROUPS_H */
