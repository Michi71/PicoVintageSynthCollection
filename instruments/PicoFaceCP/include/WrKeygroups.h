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
  /*  0 */ {  36,  42,       0,   12411,   2442 },
  /*  1 */ {   0,   0,   12412,   23382,    489 },
  /*  2 */ {   0,   0,   23383,   36261,   2442 },
  /*  3 */ {  43,  47,   36262,   49128,   1631 },
  /*  4 */ {   0,   0,   49129,   61370,    979 },
  /*  5 */ {   0,   0,   61371,   73417,    327 },
  /*  6 */ {  48,  54,   73418,   85293,   1468 },
  /*  7 */ {   0,   0,   85294,   97331,    490 },
  /*  8 */ {   0,   0,   97332,  109317,    490 },
  /*  9 */ {  55,  59,  109318,  121119,    654 },
  /* 10 */ {   0,   0,  121120,  132921,    164 },
  /* 11 */ {   0,   0,  132922,  144177,    328 },
  /* 12 */ {  60,  66,  144178,  156104,    613 },
  /* 13 */ {   0,   0,  156105,  168083,    246 },
  /* 14 */ {   0,   0,  168084,  180284,    491 },
  /* 15 */ {  67,  71,  180285,  192730,    492 },
  /* 16 */ {   0,   0,  192731,  205270,    492 },
  /* 17 */ {   0,   0,  205271,  217977,    165 },
  /* 18 */ {  72,  78,  217978,  229872,    368 },
  /* 19 */ {   0,   0,  229873,  241698,    429 },
  /* 20 */ {   0,   0,  241699,  253604,    368 },
  /* 21 */ {  79,  83,  253605,  265435,    246 },
  /* 22 */ {   0,   0,  265436,  277267,    246 },
  /* 23 */ {   0,   0,  277268,  289093,    205 },
  /* 24 */ {  84,  90,  289094,  300921,    184 },
  /* 25 */ {   0,   0,  300922,  312747,    123 },
  /* 26 */ {   0,   0,  312748,  324585,    184 },
  /* 27 */ {  91,  95,  324586,  336423,    103 },
  /* 28 */ {   0,   0,  336424,  348261,    123 },
  /* 29 */ {   0,   0,  348262,  360156,    103 },
  /* 30 */ {  96, 999,  360157,  371994,     62 },
  /* 31 */ {   0,   0,  371995,  383828,     77 },
  /* 32 */ {   0,   0,  383829,  395665,     93 },
};
#define WR_NKGRP 33

/* === mda-Stil: zum Einfuegen in den mdaEPiano-Konstruktor ===
   waves = (short*)WrData;
   kgrp muss >= 33 sein (z.B. KGRP kgrp[34];)
   noteon: while(note > (kgrp[k].high + s)) k += 3;  dann if(vel>48) k++; if(vel>80) k++;

  kgrp[ 0].root= 36; kgrp[ 0].high= 42; kgrp[ 0].pos=      0; kgrp[ 0].end=  12411; kgrp[ 0].loop=  2442;
  kgrp[ 1].pos=  12412; kgrp[ 1].end=  23382; kgrp[ 1].loop=   489;
  kgrp[ 2].pos=  23383; kgrp[ 2].end=  36261; kgrp[ 2].loop=  2442;
  kgrp[ 3].root= 43; kgrp[ 3].high= 47; kgrp[ 3].pos=  36262; kgrp[ 3].end=  49128; kgrp[ 3].loop=  1631;
  kgrp[ 4].pos=  49129; kgrp[ 4].end=  61370; kgrp[ 4].loop=   979;
  kgrp[ 5].pos=  61371; kgrp[ 5].end=  73417; kgrp[ 5].loop=   327;
  kgrp[ 6].root= 48; kgrp[ 6].high= 54; kgrp[ 6].pos=  73418; kgrp[ 6].end=  85293; kgrp[ 6].loop=  1468;
  kgrp[ 7].pos=  85294; kgrp[ 7].end=  97331; kgrp[ 7].loop=   490;
  kgrp[ 8].pos=  97332; kgrp[ 8].end= 109317; kgrp[ 8].loop=   490;
  kgrp[ 9].root= 55; kgrp[ 9].high= 59; kgrp[ 9].pos= 109318; kgrp[ 9].end= 121119; kgrp[ 9].loop=   654;
  kgrp[10].pos= 121120; kgrp[10].end= 132921; kgrp[10].loop=   164;
  kgrp[11].pos= 132922; kgrp[11].end= 144177; kgrp[11].loop=   328;
  kgrp[12].root= 60; kgrp[12].high= 66; kgrp[12].pos= 144178; kgrp[12].end= 156104; kgrp[12].loop=   613;
  kgrp[13].pos= 156105; kgrp[13].end= 168083; kgrp[13].loop=   246;
  kgrp[14].pos= 168084; kgrp[14].end= 180284; kgrp[14].loop=   491;
  kgrp[15].root= 67; kgrp[15].high= 71; kgrp[15].pos= 180285; kgrp[15].end= 192730; kgrp[15].loop=   492;
  kgrp[16].pos= 192731; kgrp[16].end= 205270; kgrp[16].loop=   492;
  kgrp[17].pos= 205271; kgrp[17].end= 217977; kgrp[17].loop=   165;
  kgrp[18].root= 72; kgrp[18].high= 78; kgrp[18].pos= 217978; kgrp[18].end= 229872; kgrp[18].loop=   368;
  kgrp[19].pos= 229873; kgrp[19].end= 241698; kgrp[19].loop=   429;
  kgrp[20].pos= 241699; kgrp[20].end= 253604; kgrp[20].loop=   368;
  kgrp[21].root= 79; kgrp[21].high= 83; kgrp[21].pos= 253605; kgrp[21].end= 265435; kgrp[21].loop=   246;
  kgrp[22].pos= 265436; kgrp[22].end= 277267; kgrp[22].loop=   246;
  kgrp[23].pos= 277268; kgrp[23].end= 289093; kgrp[23].loop=   205;
  kgrp[24].root= 84; kgrp[24].high= 90; kgrp[24].pos= 289094; kgrp[24].end= 300921; kgrp[24].loop=   184;
  kgrp[25].pos= 300922; kgrp[25].end= 312747; kgrp[25].loop=   123;
  kgrp[26].pos= 312748; kgrp[26].end= 324585; kgrp[26].loop=   184;
  kgrp[27].root= 91; kgrp[27].high= 95; kgrp[27].pos= 324586; kgrp[27].end= 336423; kgrp[27].loop=   103;
  kgrp[28].pos= 336424; kgrp[28].end= 348261; kgrp[28].loop=   123;
  kgrp[29].pos= 348262; kgrp[29].end= 360156; kgrp[29].loop=   103;
  kgrp[30].root= 96; kgrp[30].high=999; kgrp[30].pos= 360157; kgrp[30].end= 371994; kgrp[30].loop=    62;
  kgrp[31].pos= 371995; kgrp[31].end= 383828; kgrp[31].loop=    77;
  kgrp[32].pos= 383829; kgrp[32].end= 395665; kgrp[32].loop=    93;
*/
#endif /* WR_KEYGROUPS_H */
