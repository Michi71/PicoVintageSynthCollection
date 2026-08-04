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
  /* 12 */ {  55,  59,   44549,   54102,   1141 },
  /* 13 */ {   0,   0,   44549,   54102,   1141 },
  /* 14 */ {   0,   0,   44549,   54102,   1141 },
  /* 15 */ {  60,  66,   54103,   65202,    123 },
  /* 16 */ {   0,   0,   54103,   65202,    123 },
  /* 17 */ {   0,   0,   54103,   65202,    123 },
  /* 18 */ {  67,  71,   65203,   73256,     82 },
  /* 19 */ {   0,   0,   65203,   73256,     82 },
  /* 20 */ {   0,   0,   65203,   73256,     82 },
  /* 21 */ {  72,  78,   73257,   83925,    366 },
  /* 22 */ {   0,   0,   73257,   83925,    366 },
  /* 23 */ {   0,   0,   73257,   83925,    366 },
  /* 24 */ {  79,  83,   83926,   96137,    164 },
  /* 25 */ {   0,   0,   83926,   96137,    164 },
  /* 26 */ {   0,   0,   83926,   96137,    164 },
  /* 27 */ {  84,  90,   96138,  106369,    275 },
  /* 28 */ {   0,   0,   96138,  106369,    275 },
  /* 29 */ {   0,   0,   96138,  106369,    275 },
  /* 30 */ {  91,  95,  106370,  114361,    285 },
  /* 31 */ {   0,   0,  106370,  114361,    285 },
  /* 32 */ {   0,   0,  106370,  114361,    285 },
  /* 33 */ {  96, 103,  114362,  126195,     91 },
  /* 34 */ {   0,   0,  114362,  126195,     91 },
  /* 35 */ {   0,   0,  114362,  126195,     91 },
  /* 36 */ { 104, 107,  126196,  138032,     58 },
  /* 37 */ {   0,   0,  126196,  138032,     58 },
  /* 38 */ {   0,   0,  126196,  138032,     58 },
  /* 39 */ { 108, 999,  138033,  149872,  11840 },
  /* 40 */ {   0,   0,  138033,  149872,  11840 },
  /* 41 */ {   0,   0,  138033,  149872,  11840 },
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
  kgrp[12].root= 55; kgrp[12].high= 59; kgrp[12].pos=  44549; kgrp[12].end=  54102; kgrp[12].loop=  1141;
  kgrp[13].pos=  44549; kgrp[13].end=  54102; kgrp[13].loop=  1141;
  kgrp[14].pos=  44549; kgrp[14].end=  54102; kgrp[14].loop=  1141;
  kgrp[15].root= 60; kgrp[15].high= 66; kgrp[15].pos=  54103; kgrp[15].end=  65202; kgrp[15].loop=   123;
  kgrp[16].pos=  54103; kgrp[16].end=  65202; kgrp[16].loop=   123;
  kgrp[17].pos=  54103; kgrp[17].end=  65202; kgrp[17].loop=   123;
  kgrp[18].root= 67; kgrp[18].high= 71; kgrp[18].pos=  65203; kgrp[18].end=  73256; kgrp[18].loop=    82;
  kgrp[19].pos=  65203; kgrp[19].end=  73256; kgrp[19].loop=    82;
  kgrp[20].pos=  65203; kgrp[20].end=  73256; kgrp[20].loop=    82;
  kgrp[21].root= 72; kgrp[21].high= 78; kgrp[21].pos=  73257; kgrp[21].end=  83925; kgrp[21].loop=   366;
  kgrp[22].pos=  73257; kgrp[22].end=  83925; kgrp[22].loop=   366;
  kgrp[23].pos=  73257; kgrp[23].end=  83925; kgrp[23].loop=   366;
  kgrp[24].root= 79; kgrp[24].high= 83; kgrp[24].pos=  83926; kgrp[24].end=  96137; kgrp[24].loop=   164;
  kgrp[25].pos=  83926; kgrp[25].end=  96137; kgrp[25].loop=   164;
  kgrp[26].pos=  83926; kgrp[26].end=  96137; kgrp[26].loop=   164;
  kgrp[27].root= 84; kgrp[27].high= 90; kgrp[27].pos=  96138; kgrp[27].end= 106369; kgrp[27].loop=   275;
  kgrp[28].pos=  96138; kgrp[28].end= 106369; kgrp[28].loop=   275;
  kgrp[29].pos=  96138; kgrp[29].end= 106369; kgrp[29].loop=   275;
  kgrp[30].root= 91; kgrp[30].high= 95; kgrp[30].pos= 106370; kgrp[30].end= 114361; kgrp[30].loop=   285;
  kgrp[31].pos= 106370; kgrp[31].end= 114361; kgrp[31].loop=   285;
  kgrp[32].pos= 106370; kgrp[32].end= 114361; kgrp[32].loop=   285;
  kgrp[33].root= 96; kgrp[33].high=103; kgrp[33].pos= 114362; kgrp[33].end= 126195; kgrp[33].loop=    91;
  kgrp[34].pos= 114362; kgrp[34].end= 126195; kgrp[34].loop=    91;
  kgrp[35].pos= 114362; kgrp[35].end= 126195; kgrp[35].loop=    91;
  kgrp[36].root=104; kgrp[36].high=107; kgrp[36].pos= 126196; kgrp[36].end= 138032; kgrp[36].loop=    58;
  kgrp[37].pos= 126196; kgrp[37].end= 138032; kgrp[37].loop=    58;
  kgrp[38].pos= 126196; kgrp[38].end= 138032; kgrp[38].loop=    58;
  kgrp[39].root=108; kgrp[39].high=999; kgrp[39].pos= 138033; kgrp[39].end= 149872; kgrp[39].loop= 11840;
  kgrp[40].pos= 138033; kgrp[40].end= 149872; kgrp[40].loop= 11840;
  kgrp[41].pos= 138033; kgrp[41].end= 149872; kgrp[41].loop= 11840;
*/
#endif /* PNO_KEYGROUPS_H */
