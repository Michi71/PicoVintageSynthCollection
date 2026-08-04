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
  /*  0 */ {  42,  46,       0,   11839,  11840 },
  /*  1 */ {   0,   0,   11840,   22073,   2765 },
  /*  2 */ {   0,   0,   22074,   30023,    690 },
  /*  3 */ {  47,  51,   30024,   38018,    518 },
  /*  4 */ {   0,   0,   38019,   46702,    518 },
  /*  5 */ {   0,   0,   46703,   58695,    514 },
  /*  6 */ {  52,  56,   58696,   70513,   1164 },
  /*  7 */ {   0,   0,   70514,   80707,    389 },
  /*  8 */ {   0,   0,   80708,   92866,    389 },
  /*  9 */ {  57,  61,   92867,  103391,    293 },
  /* 10 */ {   0,   0,  103392,  111325,    584 },
  /* 11 */ {   0,   0,  111326,  119937,    582 },
  /* 12 */ {  62,  68,  119938,  131105,    436 },
  /* 13 */ {   0,   0,  131106,  141530,    218 },
  /* 14 */ {   0,   0,  141531,  151793,    218 },
  /* 15 */ {  69,  73,  151794,  164075,    439 },
  /* 16 */ {   0,   0,  164076,  172074,    146 },
  /* 17 */ {   0,   0,  172075,  183929,    145 },
  /* 18 */ {  74,  78,  183930,  196184,    110 },
  /* 19 */ {   0,   0,  196185,  208247,    219 },
  /* 20 */ {   0,   0,  208248,  220136,    328 },
  /* 21 */ {  79,  83,  220137,  232643,    900 },
  /* 22 */ {   0,   0,  232644,  244762,    164 },
  /* 23 */ {   0,   0,  244763,  255019,    244 },
  /* 24 */ {  84,  88,  255020,  267354,    674 },
  /* 25 */ {   0,   0,  267355,  279243,    368 },
  /* 26 */ {   0,   0,  279244,  291114,    183 },
  /* 27 */ {  89,  94,  291115,  303494,    460 },
  /* 28 */ {   0,   0,  303495,  315427,    368 },
  /* 29 */ {   0,   0,  315428,  327424,    274 },
  /* 30 */ {  95,  99,  327425,  339256,    228 },
  /* 31 */ {   0,   0,  339257,  351254,    390 },
  /* 32 */ {   0,   0,  351255,  363115,    130 },
  /* 33 */ { 100, 999,  363116,  374951,    171 },
  /* 34 */ {   0,   0,  374952,  386974,    171 },
  /* 35 */ {   0,   0,  386975,  398812,     98 },
};
#define CLV_NKGRP 36

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)ClvData;
   kgrp muss >= 36 sein (z.B. KGRP kgrp[36];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 42; kgrp[ 0].high= 46; kgrp[ 0].pos=      0; kgrp[ 0].end=  11839; kgrp[ 0].loop= 11840;
  kgrp[ 1].pos=  11840; kgrp[ 1].end=  22073; kgrp[ 1].loop=  2765;
  kgrp[ 2].pos=  22074; kgrp[ 2].end=  30023; kgrp[ 2].loop=   690;
  kgrp[ 3].root= 47; kgrp[ 3].high= 51; kgrp[ 3].pos=  30024; kgrp[ 3].end=  38018; kgrp[ 3].loop=   518;
  kgrp[ 4].pos=  38019; kgrp[ 4].end=  46702; kgrp[ 4].loop=   518;
  kgrp[ 5].pos=  46703; kgrp[ 5].end=  58695; kgrp[ 5].loop=   514;
  kgrp[ 6].root= 52; kgrp[ 6].high= 56; kgrp[ 6].pos=  58696; kgrp[ 6].end=  70513; kgrp[ 6].loop=  1164;
  kgrp[ 7].pos=  70514; kgrp[ 7].end=  80707; kgrp[ 7].loop=   389;
  kgrp[ 8].pos=  80708; kgrp[ 8].end=  92866; kgrp[ 8].loop=   389;
  kgrp[ 9].root= 57; kgrp[ 9].high= 61; kgrp[ 9].pos=  92867; kgrp[ 9].end= 103391; kgrp[ 9].loop=   293;
  kgrp[10].pos= 103392; kgrp[10].end= 111325; kgrp[10].loop=   584;
  kgrp[11].pos= 111326; kgrp[11].end= 119937; kgrp[11].loop=   582;
  kgrp[12].root= 62; kgrp[12].high= 68; kgrp[12].pos= 119938; kgrp[12].end= 131105; kgrp[12].loop=   436;
  kgrp[13].pos= 131106; kgrp[13].end= 141530; kgrp[13].loop=   218;
  kgrp[14].pos= 141531; kgrp[14].end= 151793; kgrp[14].loop=   218;
  kgrp[15].root= 69; kgrp[15].high= 73; kgrp[15].pos= 151794; kgrp[15].end= 164075; kgrp[15].loop=   439;
  kgrp[16].pos= 164076; kgrp[16].end= 172074; kgrp[16].loop=   146;
  kgrp[17].pos= 172075; kgrp[17].end= 183929; kgrp[17].loop=   145;
  kgrp[18].root= 74; kgrp[18].high= 78; kgrp[18].pos= 183930; kgrp[18].end= 196184; kgrp[18].loop=   110;
  kgrp[19].pos= 196185; kgrp[19].end= 208247; kgrp[19].loop=   219;
  kgrp[20].pos= 208248; kgrp[20].end= 220136; kgrp[20].loop=   328;
  kgrp[21].root= 79; kgrp[21].high= 83; kgrp[21].pos= 220137; kgrp[21].end= 232643; kgrp[21].loop=   900;
  kgrp[22].pos= 232644; kgrp[22].end= 244762; kgrp[22].loop=   164;
  kgrp[23].pos= 244763; kgrp[23].end= 255019; kgrp[23].loop=   244;
  kgrp[24].root= 84; kgrp[24].high= 88; kgrp[24].pos= 255020; kgrp[24].end= 267354; kgrp[24].loop=   674;
  kgrp[25].pos= 267355; kgrp[25].end= 279243; kgrp[25].loop=   368;
  kgrp[26].pos= 279244; kgrp[26].end= 291114; kgrp[26].loop=   183;
  kgrp[27].root= 89; kgrp[27].high= 94; kgrp[27].pos= 291115; kgrp[27].end= 303494; kgrp[27].loop=   460;
  kgrp[28].pos= 303495; kgrp[28].end= 315427; kgrp[28].loop=   368;
  kgrp[29].pos= 315428; kgrp[29].end= 327424; kgrp[29].loop=   274;
  kgrp[30].root= 95; kgrp[30].high= 99; kgrp[30].pos= 327425; kgrp[30].end= 339256; kgrp[30].loop=   228;
  kgrp[31].pos= 339257; kgrp[31].end= 351254; kgrp[31].loop=   390;
  kgrp[32].pos= 351255; kgrp[32].end= 363115; kgrp[32].loop=   130;
  kgrp[33].root=100; kgrp[33].high=999; kgrp[33].pos= 363116; kgrp[33].end= 374951; kgrp[33].loop=   171;
  kgrp[34].pos= 374952; kgrp[34].end= 386974; kgrp[34].loop=   171;
  kgrp[35].pos= 386975; kgrp[35].end= 398812; kgrp[35].loop=    98;
*/
#endif /* CLV_KEYGROUPS_H */
