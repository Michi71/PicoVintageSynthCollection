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
  /*  0 */ {  42,  45,       0,    8742,    694 },
  /*  1 */ {   0,   0,       0,    8742,    694 },
  /*  2 */ {   0,   0,    8743,   18945,   1037 },
  /*  3 */ {  46,  49,   18946,   30909,    825 },
  /*  4 */ {   0,   0,   18946,   30909,    825 },
  /*  5 */ {   0,   0,   30910,   39489,   1374 },
  /*  6 */ {  50,  52,   39490,   50007,   1310 },
  /*  7 */ {   0,   0,   39490,   50007,   1310 },
  /*  8 */ {   0,   0,   50008,   57999,    217 },
  /*  9 */ {  53,  56,   58000,   65939,    918 },
  /* 10 */ {   0,   0,   58000,   65939,    918 },
  /* 11 */ {   0,   0,   65940,   76443,    916 },
  /* 12 */ {  57,  62,   76444,   88252,   1169 },
  /* 13 */ {   0,   0,   76444,   88252,   1169 },
  /* 14 */ {   0,   0,   88253,   96244,    146 },
  /* 15 */ {  63,  64,   96245,  108079,    824 },
  /* 16 */ {   0,   0,   96245,  108079,    824 },
  /* 17 */ {   0,   0,  108080,  120521,    619 },
  /* 18 */ {  65,  67,  120522,  132322,   1286 },
  /* 19 */ {   0,   0,  120522,  132322,   1286 },
  /* 20 */ {   0,   0,  132323,  140939,   1560 },
  /* 21 */ {  68,  71,  140940,  152743,    772 },
  /* 22 */ {   0,   0,  140940,  152743,    772 },
  /* 23 */ {   0,   0,  152744,  164570,    693 },
  /* 24 */ {  72,  79,  164571,  174780,    612 },
  /* 25 */ {   0,   0,  164571,  174780,    612 },
  /* 26 */ {   0,   0,  174781,  185018,    184 },
  /* 27 */ {  80,  81,  185019,  195252,    270 },
  /* 28 */ {   0,   0,  185019,  195252,    270 },
  /* 29 */ {   0,   0,  195253,  207084,    155 },
  /* 30 */ {  82,  87,  207085,  218921,    241 },
  /* 31 */ {   0,   0,  207085,  218921,    241 },
  /* 32 */ {   0,   0,  218922,  230760,    138 },
  /* 33 */ {  88,  90,  230761,  240999,    122 },
  /* 34 */ {   0,   0,  230761,  240999,    122 },
  /* 35 */ {   0,   0,  241000,  249627,    122 },
  /* 36 */ {  91,  96,  249628,  261459,    143 },
  /* 37 */ {   0,   0,  249628,  261459,    143 },
  /* 38 */ {   0,   0,  261460,  273297,    163 },
  /* 39 */ {  97, 999,  273298,  285134,    230 },
  /* 40 */ {   0,   0,  273298,  285134,    230 },
  /* 41 */ {   0,   0,  285135,  296974,  11840 },
};
#define CP_NKGRP 42

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)CPData;
   kgrp muss >= 42 sein (z.B. KGRP kgrp[42];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 42; kgrp[ 0].high= 45; kgrp[ 0].pos=      0; kgrp[ 0].end=   8742; kgrp[ 0].loop=   694;
  kgrp[ 1].pos=      0; kgrp[ 1].end=   8742; kgrp[ 1].loop=   694;
  kgrp[ 2].pos=   8743; kgrp[ 2].end=  18945; kgrp[ 2].loop=  1037;
  kgrp[ 3].root= 46; kgrp[ 3].high= 49; kgrp[ 3].pos=  18946; kgrp[ 3].end=  30909; kgrp[ 3].loop=   825;
  kgrp[ 4].pos=  18946; kgrp[ 4].end=  30909; kgrp[ 4].loop=   825;
  kgrp[ 5].pos=  30910; kgrp[ 5].end=  39489; kgrp[ 5].loop=  1374;
  kgrp[ 6].root= 50; kgrp[ 6].high= 52; kgrp[ 6].pos=  39490; kgrp[ 6].end=  50007; kgrp[ 6].loop=  1310;
  kgrp[ 7].pos=  39490; kgrp[ 7].end=  50007; kgrp[ 7].loop=  1310;
  kgrp[ 8].pos=  50008; kgrp[ 8].end=  57999; kgrp[ 8].loop=   217;
  kgrp[ 9].root= 53; kgrp[ 9].high= 56; kgrp[ 9].pos=  58000; kgrp[ 9].end=  65939; kgrp[ 9].loop=   918;
  kgrp[10].pos=  58000; kgrp[10].end=  65939; kgrp[10].loop=   918;
  kgrp[11].pos=  65940; kgrp[11].end=  76443; kgrp[11].loop=   916;
  kgrp[12].root= 57; kgrp[12].high= 62; kgrp[12].pos=  76444; kgrp[12].end=  88252; kgrp[12].loop=  1169;
  kgrp[13].pos=  76444; kgrp[13].end=  88252; kgrp[13].loop=  1169;
  kgrp[14].pos=  88253; kgrp[14].end=  96244; kgrp[14].loop=   146;
  kgrp[15].root= 63; kgrp[15].high= 64; kgrp[15].pos=  96245; kgrp[15].end= 108079; kgrp[15].loop=   824;
  kgrp[16].pos=  96245; kgrp[16].end= 108079; kgrp[16].loop=   824;
  kgrp[17].pos= 108080; kgrp[17].end= 120521; kgrp[17].loop=   619;
  kgrp[18].root= 65; kgrp[18].high= 67; kgrp[18].pos= 120522; kgrp[18].end= 132322; kgrp[18].loop=  1286;
  kgrp[19].pos= 120522; kgrp[19].end= 132322; kgrp[19].loop=  1286;
  kgrp[20].pos= 132323; kgrp[20].end= 140939; kgrp[20].loop=  1560;
  kgrp[21].root= 68; kgrp[21].high= 71; kgrp[21].pos= 140940; kgrp[21].end= 152743; kgrp[21].loop=   772;
  kgrp[22].pos= 140940; kgrp[22].end= 152743; kgrp[22].loop=   772;
  kgrp[23].pos= 152744; kgrp[23].end= 164570; kgrp[23].loop=   693;
  kgrp[24].root= 72; kgrp[24].high= 79; kgrp[24].pos= 164571; kgrp[24].end= 174780; kgrp[24].loop=   612;
  kgrp[25].pos= 164571; kgrp[25].end= 174780; kgrp[25].loop=   612;
  kgrp[26].pos= 174781; kgrp[26].end= 185018; kgrp[26].loop=   184;
  kgrp[27].root= 80; kgrp[27].high= 81; kgrp[27].pos= 185019; kgrp[27].end= 195252; kgrp[27].loop=   270;
  kgrp[28].pos= 185019; kgrp[28].end= 195252; kgrp[28].loop=   270;
  kgrp[29].pos= 195253; kgrp[29].end= 207084; kgrp[29].loop=   155;
  kgrp[30].root= 82; kgrp[30].high= 87; kgrp[30].pos= 207085; kgrp[30].end= 218921; kgrp[30].loop=   241;
  kgrp[31].pos= 207085; kgrp[31].end= 218921; kgrp[31].loop=   241;
  kgrp[32].pos= 218922; kgrp[32].end= 230760; kgrp[32].loop=   138;
  kgrp[33].root= 88; kgrp[33].high= 90; kgrp[33].pos= 230761; kgrp[33].end= 240999; kgrp[33].loop=   122;
  kgrp[34].pos= 230761; kgrp[34].end= 240999; kgrp[34].loop=   122;
  kgrp[35].pos= 241000; kgrp[35].end= 249627; kgrp[35].loop=   122;
  kgrp[36].root= 91; kgrp[36].high= 96; kgrp[36].pos= 249628; kgrp[36].end= 261459; kgrp[36].loop=   143;
  kgrp[37].pos= 249628; kgrp[37].end= 261459; kgrp[37].loop=   143;
  kgrp[38].pos= 261460; kgrp[38].end= 273297; kgrp[38].loop=   163;
  kgrp[39].root= 97; kgrp[39].high=999; kgrp[39].pos= 273298; kgrp[39].end= 285134; kgrp[39].loop=   230;
  kgrp[40].pos= 273298; kgrp[40].end= 285134; kgrp[40].loop=   230;
  kgrp[41].pos= 285135; kgrp[41].end= 296974; kgrp[41].loop= 11840;
*/
#endif /* CP_KEYGROUPS_H */
