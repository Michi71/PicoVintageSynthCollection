/*
  mdaEPianoInstruments.h  --  Instrument-Tabelle fuer die mdaEPiano-Engine.

  Verfuegbare Instrumente (0..5):
    0 = Rd I   (mda-Default, epianoData)
    1 = Rd II  (rhodes_suitcase)
    2 = Wr     (wurlitzer_200a)
    3 = Clv    (clavinet)
    4 = Pno    (Piano)
    5 = CP     (yamaha_cp80)

  Die Daten- und Keygroup-Header werden von tools/sampleprep generiert und
  nach include/ kopiert. Dieses Header wird NUR von src/mdaEPiano.cpp eingebunden
  (die grossen const-Datenarrays duerfen nicht in mehreren TU landen).

  Auto-generiert/verwaltet - Instrumente bitte via Configs + build_instrument.py
  aendern, nicht von Hand.
*/
#ifndef MDAEPIANO_INSTRUMENTS_H
#define MDAEPIANO_INSTRUMENTS_H

#include "mdaEPiano.h"   /* struct KGRP */

#include "Rd_IIData.h"
#include "Rd_IIKeygroups.h"
#include "WrData.h"
#include "WrKeygroups.h"
#include "ClvData.h"
#include "ClvKeygroups.h"
#include "PnoData.h"
#include "PnoKeygroups.h"
#include "CPData.h"
#include "CPKeygroups.h"

#ifndef MDA_NINSTR
#define MDA_NINSTR 6   /* Instrumente 0..5 */
#endif

struct MdaInstrument {
  const char*    name;
  const int16_t* data;        /* nullptr -> mda-Default (Konstruktor) */
  const KGRP*    kgrp;        /* nullptr -> mda-Default */
  int32_t        nKeygroups;  /* 0 -> mda-Default */
  int32_t        noteonStep;  /* Velocity-Layer je Region (3) */
};

static const MdaInstrument mdaInstruments[MDA_NINSTR] = {
  { "Rd I",  nullptr,   nullptr,                     0,               3 },  // 0
  { "Rd II", Rd_IIData, (const KGRP*)Rd_IIKgrp,      RD_II_NKGRP,     3 },  // 1
  { "Wr",    WrData,    (const KGRP*)WrKgrp,         WR_NKGRP,        3 },  // 2
  { "Clv",   ClvData,   (const KGRP*)ClvKgrp,        CLV_NKGRP,       3 },  // 3
  { "Pno",   PnoData,   (const KGRP*)PnoKgrp,        PNO_NKGRP,       3 },  // 4
  { "CP",    CPData,    (const KGRP*)CPKgrp,         CP_NKGRP,        3 },  // 5
};

#endif /* MDAEPIANO_INSTRUMENTS_H */
