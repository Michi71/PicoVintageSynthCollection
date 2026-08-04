#include "mcu.h"
#include "rom_tables.h"

#include <algorithm>
#include <iterator>
#include <cstring>

// Silent diagnostic counter for writes to unmapped internal I/O (see write_byte).
volatile uint32_t g_rd_unk_write_count = 0;

#define pPPC    m_ppc
#define pPC     m_pc
#define pS      m_s
#define pX      m_x
#define pD      m_d

#define PC      m_pc.w.l
#define PCD     m_pc.d
#define S       m_s.w.l
#define SD      m_s.d
#define X       m_x.w.l
#define D       m_d.w.l
#define A       m_d.b.h
#define B       m_d.b.l
#define CC      m_cc

#define EAD     m_ea.d
#define EA      m_ea.w.l

/* memory interface */

/****************************************************************************/
/* Read a byte from given memory location                                   */
/****************************************************************************/
// #define RM(Addr) (m_program.read_byte(Addr))
#define RM(Addr) (read_byte(Addr))

/****************************************************************************/
/* Write a byte to given memory location                                    */
/****************************************************************************/
// #define WM(Addr,Value) (m_program.write_byte(Addr,Value))
#define WM(Addr,Value) (write_byte(Addr,Value))

/****************************************************************************/
/* M6800_RDOP() is identical to M6800_RDMEM() except it is used for reading */
/* opcodes. In case of system with memory mapped I/O, this function can be  */
/* used to greatly speed up emulation                                       */
/****************************************************************************/
// #define M_RDOP(Addr) (m_copcodes.read_byte(Addr))
#define M_RDOP(Addr) (read_byte(Addr))

/****************************************************************************/
/* M6800_RDOP_ARG() is identical to M6800_RDOP() but it's used for reading  */
/* opcode arguments. This difference can be used to support systems that    */
/* use different encoding mechanisms for opcodes and opcode arguments       */
/****************************************************************************/
// #define M_RDOP_ARG(Addr) (m_cprogram.read_byte(Addr))
#define M_RDOP_ARG(Addr) (read_byte(Addr))

/* macros to access memory */
#define IMMBYTE(b)  b = M_RDOP_ARG(PCD); PC++
#define IMMWORD(w)  w.d = (M_RDOP_ARG(PCD)<<8) | M_RDOP_ARG((PCD+1)&0xffff); PC+=2

#define PUSHBYTE(b) WM(SD,b); --S
#define PUSHWORD(w) WM(SD,w.b.l); --S; WM(SD,w.b.h); --S
#define PULLBYTE(b) S++; b = RM(SD)
#define PULLWORD(w) S++; w.d = RM(SD)<<8; S++; w.d |= RM(SD)

/* CC masks                       HI NZVC
                                7654 3210   */
#define CLR_HNZVC   CC&=0xd0
#define CLR_NZV     CC&=0xf1
#define CLR_HNZC    CC&=0xd2
#define CLR_NZVC    CC&=0xf0
#define CLR_Z       CC&=0xfb
#define CLR_ZC      CC&=0xfa
#define CLR_C       CC&=0xfe

/* macros for CC -- CC bits affected should be reset before calling */
#define SET_Z(a)        if(!(a))SEZ
#define SET_Z8(a)       SET_Z(u8(a))
#define SET_Z16(a)      SET_Z(u16(a))
#define SET_N8(a)       CC|=(((a)&0x80)>>4)
#define SET_N16(a)      CC|=(((a)&0x8000)>>12)
#define SET_H(a,b,r)    CC|=((((a)^(b)^(r))&0x10)<<1)
#define SET_C8(a)       CC|=(((a)&0x100)>>8)
#define SET_C16(a)      CC|=(((a)&0x10000)>>16)
#define SET_V8(a,b,r)   CC|=((((a)^(b)^(r)^((r)>>1))&0x80)>>6)
#define SET_V16(a,b,r)  CC|=((((a)^(b)^(r)^((r)>>1))&0x8000)>>14)

u8 Mcu::flags8i[256]= /* increment */
{
0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x0a,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08
};


u8 Mcu::flags8d[256]= /* decrement */
{
0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08
};

const size_t patchToOffset[] = {
    // MKS-20
    0x000000, // Piano 1
    0x008000, // Piano 2
    0x010000, // Piano 3
    0x018000, // Harpsichord
    0x003c20, // Clavi
    0x00ab50, // Vibraphone
    0x014260, // E-Piano 1
    0x01bef0, // E-Piano 2

    // MK80
    0x000020, // Classic
    0x008000, // Special
    0x010000, // Blend
    0x018000, // Contemporary
    0x002c00, // A. Piano 1
    0x00b1f0, // A. Piano 2
    0x012910, // Clavi
    0x0199f0, // Vibraphone
  };

const u8 patchToRomSet[] = { 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 };

const int sampleRates[] = {
    // MKS-20
    20000, 20000, 20000, 32000, 32000, 20000, 20000, 32000,
    // MK80
    20000, 20000, 20000, 32000, 20000, 20000, 32000, 20000};

const char *PatchNames[] = { "MKS-20: Piano 1",   "MKS-20: Piano 2",
                             "MKS-20: Piano 3",   "MKS-20: Harpsichord",
                             "MKS-20: Clavi",     "MKS-20: Vibraphone",
                             "MKS-20: E-Piano 1", "MKS-20: E-Piano 2",
                             "MK-80: Classic",    "MK-80: Special",
                             "MK-80: Blend",      "MK-80: Contemporary",
                             "MK-80: A. Piano 1", "MK-80: A. Piano 2",
                             "MK-80: Clavi",      "MK-80: Vibraphone" };    

static u8 params_rom[0x20000];

#define SET_FLAGS8I(a)      {CC|=flags8i[(a)&0xff];}
#define SET_FLAGS8D(a)      {CC|=flags8d[(a)&0xff];}

/* combos */
#define SET_NZ8(a)          {SET_N8(a);SET_Z8(a);}
#define SET_NZ16(a)         {SET_N16(a);SET_Z16(a);}
#define SET_FLAGS8(a,b,r)   {SET_N8(r);SET_Z8(r);SET_V8(a,b,r);SET_C8(r);}
#define SET_FLAGS16(a,b,r)  {SET_N16(r);SET_Z16(r);SET_V16(a,b,r);SET_C16(r);}

/* for treating an u8 as a signed s16 */
#define SIGNED(b) (s16(b&0x80?b|0xff00:b))

/* Macros for addressing modes */
#define DIRECT IMMBYTE(EAD)
#define IMM8 EA=PC++
#define IMM16 {EA=PC;PC+=2;}
#define EXTENDED IMMWORD(m_ea)
#define INDEXED {EA=X+(u8)M_RDOP_ARG(PCD);PC++;}

/* macros to set status flags */
#if defined(SEC)
#undef SEC
#endif
#define SEC CC|=0x01
#define CLC CC&=0xfe
#define SEZ CC|=0x04
#define CLZ CC&=0xfb
#define SEN CC|=0x08
#define CLN CC&=0xf7
#define SEV CC|=0x02
#define CLV CC&=0xfd
#define SEH CC|=0x20
#define CLH CC&=0xdf
#define SEI CC|=0x10
#define CLI CC&=~0x10

/* macros for convenience */
#define DIRBYTE(b) {DIRECT;b=RM(EAD);}
#define DIRWORD(w) {DIRECT;w.d=RM16(EAD);}
#define EXTBYTE(b) {EXTENDED;b=RM(EAD);}
#define EXTWORD(w) {EXTENDED;w.d=RM16(EAD);}

#define IDXBYTE(b) {INDEXED;b=RM(EAD);}
#define IDXWORD(w) {INDEXED;w.d=RM16(EAD);}

/* Macros for branch instructions */
#define BRANCH(f) {IMMBYTE(t);if(f){PC+=SIGNED(t);}}
#define NXORV  ((CC&0x08)^((CC&0x02)<<2))
#define NXORC  ((CC&0x08)^((CC&0x01)<<3))

#include "mcu_ops.h"

Mcu::op_func Mcu::hd63701_insn[0x100] = {
&Mcu::trap,   &Mcu::nop,    &Mcu::trap,   &Mcu::trap,   &Mcu::lsrd,   &Mcu::asld,   &Mcu::tap,    &Mcu::tpa,    // 0
&Mcu::inx,    &Mcu::dex,    &Mcu::clv,    &Mcu::sev,    &Mcu::clc,    &Mcu::sec,    &Mcu::cli,    &Mcu::sei,
&Mcu::sba,    &Mcu::cba,    &Mcu::undoc1, &Mcu::undoc2, &Mcu::trap,   &Mcu::trap,   &Mcu::tab,    &Mcu::tba,    // 1
&Mcu::xgdx,   &Mcu::daa,    &Mcu::slp,    &Mcu::aba,    &Mcu::trap,   &Mcu::trap,   &Mcu::trap,   &Mcu::trap,
&Mcu::bra,    &Mcu::brn,    &Mcu::bhi,    &Mcu::bls,    &Mcu::bcc,    &Mcu::bcs,    &Mcu::bne,    &Mcu::beq,    // 2
&Mcu::bvc,    &Mcu::bvs,    &Mcu::bpl,    &Mcu::bmi,    &Mcu::bge,    &Mcu::blt,    &Mcu::bgt,    &Mcu::ble,
&Mcu::tsx,    &Mcu::ins,    &Mcu::pula,   &Mcu::pulb,   &Mcu::des,    &Mcu::txs,    &Mcu::psha,   &Mcu::pshb,   // 3
&Mcu::pulx,   &Mcu::rts,    &Mcu::abx,    &Mcu::rti,    &Mcu::pshx,   &Mcu::mul,    &Mcu::wai,    &Mcu::swi,
&Mcu::nega,   &Mcu::trap,   &Mcu::trap,   &Mcu::coma,   &Mcu::lsra,   &Mcu::trap,   &Mcu::rora,   &Mcu::asra,   // 4
&Mcu::asla,   &Mcu::rola,   &Mcu::deca,   &Mcu::trap,   &Mcu::inca,   &Mcu::tsta,   &Mcu::trap,   &Mcu::clra,
&Mcu::negb,   &Mcu::trap,   &Mcu::trap,   &Mcu::comb,   &Mcu::lsrb,   &Mcu::trap,   &Mcu::rorb,   &Mcu::asrb,   // 5
&Mcu::aslb,   &Mcu::rolb,   &Mcu::decb,   &Mcu::trap,   &Mcu::incb,   &Mcu::tstb,   &Mcu::trap,   &Mcu::clrb,
&Mcu::neg_ix, &Mcu::aim_ix, &Mcu::oim_ix, &Mcu::com_ix, &Mcu::lsr_ix, &Mcu::eim_ix, &Mcu::ror_ix, &Mcu::asr_ix, // 6
&Mcu::asl_ix, &Mcu::rol_ix, &Mcu::dec_ix, &Mcu::tim_ix, &Mcu::inc_ix, &Mcu::tst_ix, &Mcu::jmp_ix, &Mcu::clr_ix,
&Mcu::neg_ex, &Mcu::aim_di, &Mcu::oim_di, &Mcu::com_ex, &Mcu::lsr_ex, &Mcu::eim_di, &Mcu::ror_ex, &Mcu::asr_ex, // 7
&Mcu::asl_ex, &Mcu::rol_ex, &Mcu::dec_ex, &Mcu::tim_di, &Mcu::inc_ex, &Mcu::tst_ex, &Mcu::jmp_ex, &Mcu::clr_ex,
&Mcu::suba_im,&Mcu::cmpa_im,&Mcu::sbca_im,&Mcu::subd_im,&Mcu::anda_im,&Mcu::bita_im,&Mcu::lda_im, &Mcu::trap,   // 8
&Mcu::eora_im,&Mcu::adca_im,&Mcu::ora_im, &Mcu::adda_im,&Mcu::cpx_im ,&Mcu::bsr,    &Mcu::lds_im, &Mcu::trap,
&Mcu::suba_di,&Mcu::cmpa_di,&Mcu::sbca_di,&Mcu::subd_di,&Mcu::anda_di,&Mcu::bita_di,&Mcu::lda_di, &Mcu::sta_di, // 9
&Mcu::eora_di,&Mcu::adca_di,&Mcu::ora_di, &Mcu::adda_di,&Mcu::cpx_di ,&Mcu::jsr_di, &Mcu::lds_di, &Mcu::sts_di,
&Mcu::suba_ix,&Mcu::cmpa_ix,&Mcu::sbca_ix,&Mcu::subd_ix,&Mcu::anda_ix,&Mcu::bita_ix,&Mcu::lda_ix, &Mcu::sta_ix, // A
&Mcu::eora_ix,&Mcu::adca_ix,&Mcu::ora_ix, &Mcu::adda_ix,&Mcu::cpx_ix ,&Mcu::jsr_ix, &Mcu::lds_ix, &Mcu::sts_ix,
&Mcu::suba_ex,&Mcu::cmpa_ex,&Mcu::sbca_ex,&Mcu::subd_ex,&Mcu::anda_ex,&Mcu::bita_ex,&Mcu::lda_ex, &Mcu::sta_ex, // B
&Mcu::eora_ex,&Mcu::adca_ex,&Mcu::ora_ex, &Mcu::adda_ex,&Mcu::cpx_ex ,&Mcu::jsr_ex, &Mcu::lds_ex, &Mcu::sts_ex,
&Mcu::subb_im,&Mcu::cmpb_im,&Mcu::sbcb_im,&Mcu::addd_im,&Mcu::andb_im,&Mcu::bitb_im,&Mcu::ldb_im, &Mcu::trap,   // C
&Mcu::eorb_im,&Mcu::adcb_im,&Mcu::orb_im, &Mcu::addb_im,&Mcu::ldd_im, &Mcu::trap,   &Mcu::ldx_im, &Mcu::trap,
&Mcu::subb_di,&Mcu::cmpb_di,&Mcu::sbcb_di,&Mcu::addd_di,&Mcu::andb_di,&Mcu::bitb_di,&Mcu::ldb_di, &Mcu::stb_di, // D
&Mcu::eorb_di,&Mcu::adcb_di,&Mcu::orb_di, &Mcu::addb_di,&Mcu::ldd_di, &Mcu::std_di, &Mcu::ldx_di, &Mcu::stx_di,
&Mcu::subb_ix,&Mcu::cmpb_ix,&Mcu::sbcb_ix,&Mcu::addd_ix,&Mcu::andb_ix,&Mcu::bitb_ix,&Mcu::ldb_ix, &Mcu::stb_ix, // E
&Mcu::eorb_ix,&Mcu::adcb_ix,&Mcu::orb_ix, &Mcu::addb_ix,&Mcu::ldd_ix, &Mcu::std_ix, &Mcu::ldx_ix, &Mcu::stx_ix,
&Mcu::subb_ex,&Mcu::cmpb_ex,&Mcu::sbcb_ex,&Mcu::addd_ex,&Mcu::andb_ex,&Mcu::bitb_ex,&Mcu::ldb_ex, &Mcu::stb_ex, // F
&Mcu::eorb_ex,&Mcu::adcb_ex,&Mcu::orb_ex, &Mcu::addb_ex,&Mcu::ldd_ex, &Mcu::std_ex, &Mcu::ldx_ex, &Mcu::stx_ex
};

#define XX 4 // illegal opcode unknown cycle count
u8 Mcu::cycles_63701[256] =
{
    /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
  /*0*/ XX, 1,XX,XX, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*1*/  1, 1,XX,XX,XX,XX, 1, 1, 2, 2, 4, 1,XX,XX,XX,XX,
  /*2*/  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  /*3*/  1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1,10, 5, 7, 9,12,
  /*4*/  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
  /*5*/  1,XX,XX, 1, 1,XX, 1, 1, 1, 1, 1,XX, 1, 1,XX, 1,
  /*6*/  6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,
  /*7*/  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,
  /*8*/  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3, 5, 3,XX,
  /*9*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,
  /*A*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
  /*B*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,
  /*C*/  2, 2, 2, 3, 2, 2, 2,XX, 2, 2, 2, 2, 3,XX, 3,XX,
  /*D*/  3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
  /*E*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
  /*F*/  4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5
};

#define TCSR_IEDG   0x02
#define TCSR_EICI   0x10
#define TCSR_ICF    0x80
#define CT      m_counter.w.l

// Can be 13 bit or 14 bit depending on the model
#define UNSCRAMBLE_ADDR_CPUB(i) \
  bitswap<14>(i,13,12,11,8,9,10,7,6,5,4,3,2,1,0)
#define UNSCRAMBLE_DATA_CPUB(_data) \
  bitswap<8>(_data,7,0,6,1,5,2,4,3)

#define UNSCRAMBLE_ADDR_PARAMS(i) \
  bitswap<17>(i,16,15,13,12,14,11,8,9,10,7,6,5,4,3,2,1,0)
#define UNSCRAMBLE_DATA_PARAMS(_data) \
  bitswap<8>(_data,7,0,6,1,5,2,4,3)


static u8 s_program_ram[0x2000];

Mcu::Mcu(const u8 temp_model)
  : sound_chip(temp_model)
{
  // Copy the 8KB program ROM into RAM: the interpreter fetches from it
  // on every emulated instruction, XIP flash access is far too slow.
  memcpy(s_program_ram, rd_program_rom, sizeof(s_program_ram));
  program_rom = s_program_ram;

  loadSounds(temp_model, 0x00);
}

void Mcu::reset()
{
  m_ppc.d = 0;
  m_pc.d = 0;
  m_s.d = 0;
  m_x.d = 0;
  m_d.d = 0;
  m_cc = 0;
  m_wai_state = 0;
  m_nmi_state = 0;
  m_nmi_pending = 0;
  std::fill(std::begin(m_irq_state), std::end(m_irq_state), 0);

  m_cc = 0xc0;
  SEI; /* IRQ disabled */
  PCD = RM16(0xfffe);

  m_wai_state = 0;
  m_nmi_state = 0;
  m_nmi_pending = 0;

  // We need to run the CPU for a bit before being able to send commands to it
  for (size_t i = 0; i < 1024 * 8; i++)
    execute_one();
}

Mcu::~Mcu() {}

void Mcu::take_trap()
{
  enter_interrupt("TRAP", 0xffee);
}

/* check the IRQ lines for pending interrupts */
void RD_HOT_FUNC(Mcu::check_irq_lines)()
{
  if (m_nmi_pending)
  {
    m_wai_state &= ~M6800_SLP;
    m_nmi_pending = false;
    enter_interrupt("NMI", 0xfffc);
  }
  else if (m_irq_state[M6800_IRQ_LINE] != CLEAR_LINE)
  {
    /* standard IRQ */
    m_wai_state &= ~M6800_SLP;

    if (!(CC & 0x10))
    {
      // standard_irq_callback(M6800_IRQ_LINE, m_pc.w.l);
      enter_interrupt("IRQ1", 0xfff8);
    }
  }
  else if ((m_tcsr & (TCSR_EICI|TCSR_ICF)) == (TCSR_EICI|TCSR_ICF))
  {
    // if (!(m_cc & 0x10))
    // 	standard_irq_callback(M6801_TIN_LINE, m_pc.w.l);
    
    m_wai_state &= ~M6800_SLP;

    if (!(CC & 0x10))
      enter_interrupt("ICI", 0xfff6);
  }
}

void RD_HOT_FUNC(Mcu::eat_cycles)()
{
  if (m_icount > 0)
    increment_counter(m_icount);
}

u32 RD_HOT_FUNC(Mcu::RM16)(u32 Addr)
{
  u32 result = RM(Addr) << 8;
  return result | RM((Addr+1) & 0xffff);
}

void RD_HOT_FUNC(Mcu::WM16)(u32 Addr, PAIR *p)
{
  WM(Addr, p->b.h);
  WM((Addr+1) & 0xffff, p->b.l);
}

/* IRQ enter */
void Mcu::enter_interrupt(const char *message, u16 irq_vector)
{
  int cycles_to_eat = 0;

  if (m_wai_state & M6800_WAI)
  {
    cycles_to_eat = 4;
    m_wai_state &= ~M6800_WAI;
  }
  else
  {
    PUSHWORD(pPC);
    PUSHWORD(pX);
    PUSHBYTE(A);
    PUSHBYTE(B);
    PUSHBYTE(CC);
    cycles_to_eat = 12;
  }
  SEI;
  PCD = RM16(irq_vector);

  increment_counter(cycles_to_eat);
}

void RD_HOT_FUNC(Mcu::increment_counter)(int amount)
{
  m_icount -= amount;
}

void Mcu::execute_set_input(int irqline, int state)
{
  switch (irqline)
  {
  case INPUT_LINE_NMI:
    if (!m_nmi_state && state != CLEAR_LINE)
      m_nmi_pending = true;
    m_nmi_state = state;
    break;

  case M6801_TIN_LINE:
    if (state != m_irq_state[M6801_TIN_LINE])
    {
      // printf("irq state %x\n", m_irq_state[irqline]);
      m_irq_state[M6801_TIN_LINE] = state;
      //edge = (state == CLEAR_LINE) ? 2 : 0;
      if (((m_tcsr & TCSR_IEDG) ^ (state == CLEAR_LINE ? TCSR_IEDG : 0)) == 0)
        return;
      /* active edge in */
      m_tcsr |= TCSR_ICF;
      m_pending_tcsr |= TCSR_ICF;
      m_input_capture = CT;
    }
    break;

  default:
    m_irq_state[irqline] = state;
    break;
  }
}

void RD_HOT_FUNC(Mcu::execute_run)(int cycles)
{
  if (!commands_queue.empty())
    execute_set_input(M6801_TIN_LINE, ASSERT_LINE);

  if (sound_chip.m_irq_triggered)
      execute_set_input(0, ASSERT_LINE);
  check_irq_lines();

  m_icount += cycles;
  if (m_icount > 10000) m_icount = cycles; // failsafe

  do {
    if (m_wai_state & (M6800_WAI | M6800_SLP)) eat_cycles();
    else execute_one();
  } while (m_icount > 0);
}

void RD_HOT_FUNC(Mcu::execute_one)()
{
  pPPC = pPC;
  u8 ireg = M_RDOP(PCD);
  PC++;
  // printf("PC: %04x, ireg: %02x\n", PCD, ireg);
  (this->*hd63701_insn[ireg])();
  increment_counter(cycles_63701[ireg]);
}

u8 RD_HOT_FUNC(Mcu::tcsr_r)()
{
  m_pending_tcsr = 0;
  return m_tcsr;
}

void RD_HOT_FUNC(Mcu::tcsr_w)(u8 data)
{
  data &= 0x1f;

  m_tcsr = data | (m_tcsr & 0xe0);
  m_pending_tcsr &= m_tcsr;
  check_irq_lines();
}


u8 RD_HOT_FUNC(Mcu::read_byte)(u16 addr)
{
  // program rom
  if (addr >= 0xc000)
    return program_rom[(addr - 0xc000) & 0xdfff];
  
  // port 1 DATA
  else if (addr == 0x0002) {
    u8 data_comm_bus = 0xff;

    // HACK: only works with the RD200 ROM
    if (!commands_queue.empty() && (PCD == 0xE12B || PCD == 0xE15E || PCD == 0xE168))
    // if (!commands_queue.empty() && (PCD == 0xE0E4 || PCD == 0xE111 || PCD == 0xE11B))
    {
      data_comm_bus = commands_queue.front();
      commands_queue.pop();
      // printf("data\n");
    }

    // printf("%04x: read port1 %02x\n", PCD, data_comm_bus);
    return data_comm_bus;
  }
  
  // port 2 CONTROL
  else if (addr == 0x0003) {
    // printf("%04x: read port2\n", PCD);

    // HACK: only works with the RD200 ROM
    if (PCD == 0xE15A) return 0xFF;
    // if (PCD == 0xE10D) return 0xFF;
    return 0x00;
  }

  // tcsr
  else if (addr == 0x0008)
    return tcsr_r();
  else if (addr == 0x000d) {
    if (!(m_pending_tcsr & TCSR_ICF))
      m_tcsr &= ~TCSR_ICF;
    return (m_input_capture >> 0) & 0xff;
  }
  else if (addr == 0x000e)
    return (m_input_capture >> 8) & 0xff;
  
  else if (addr < 0x20) {
    //printf("%04x: unk device read %04x\n", addr, PCD);
    return 0xFF;
  }
  
  // ram
  else if (addr < 0x1000)
    return ram[addr];
  
  // sound chip
  else if (addr < 0x2000)
    return sound_chip.read(addr - 0x1000);
  
  // params rom
  else if (addr >= 0x4000 && addr <= 0xbfff)
    return params_rom[(addr - 0x4000) | ((latch_val & 0b11) << 15)];
  
  //printf("%04x: unk read %04x\n", PCD, addr);
  return 0xFF;
}

void RD_HOT_FUNC(Mcu::write_byte)(u16 addr, u8 data)
{
  // port dir
  if (addr == 0x0000 || addr == 0x0001) {
    // noop
  }

  // port 1 DATA
  else if (addr == 0x0002) {
    // printf("%04x: port1 write %04x=%02x\n", PCD, addr, data);
  }
  
  // port 2 CONTROL
  else if (addr == 0x0003) {
    // printf("%04x: port2 write %04x=%02x\n", PCD, addr, data);

    // TODO: Currently not working, investigate
    current_sample_rate = (data >> 2) & 1;

    execute_set_input(M6801_TIN_LINE, CLEAR_LINE);
  }
  
  // tcsr
  else if (addr == 0x0008) {
    tcsr_w(data);
  }
  
  else if (addr < 0x20) {
    // Unmapped internal I/O (front-panel/LED latches of the original unit).
    // NO printf here: this runs inside the audio DMA IRQ, and a single
    // blocking 115200-baud UART line costs ~3-4 ms = audible crackle on
    // every note event. Count silently instead.
    g_rd_unk_write_count++;
  }
  
  // ram
  else if (addr < 0x1000) {
    ram[addr] = data;
  }
  
  // sound chip
  else if (addr >= 0x1000 && addr < 0x2000) {
    sound_chip.write(addr - 0x1000, data);
    // printf("%04x: SA write %04x=%02x\n", PCD, addr, data);
    // fflush(stdout);

    if (sound_chip.m_irq_triggered) {
      sound_chip.m_irq_triggered = false;
      execute_set_input(0, CLEAR_LINE);
    }
  }
  
  // latch
  else {
    latch_val = data;
    // printf("latch write %04x=%02x\n", addr, data);
  }
}

#if defined(TARGET_RP2350) || defined(PICO_BUILD)
#include "pico/time.h"
volatile uint32_t g_rd_prof_chip_us = 0;
#endif
#if defined(TARGET_RP2350) || defined(PICO_BUILD)
volatile uint32_t g_rd_prof_cpu_us  = 0;
#endif

s32 RD_HOT_FUNC(Mcu::generate_next_sample)(bool sampleRate32)
{
#if defined(TARGET_RP2350) || defined(PICO_BUILD)
    uint32_t t0 = time_us_32();
    s32 sample = sound_chip.update();
    g_rd_prof_chip_us += (time_us_32() - t0);
    uint32_t t1 = time_us_32();
    execute_run(sampleRate32 ? 62 : 100);
    g_rd_prof_cpu_us += (time_us_32() - t1);
    return sample;
#else
    s32 sample = sound_chip.update();
    execute_run(sampleRate32 ? 62 : 100);
    return sample;
#endif
}

void Mcu::sendMidiCmd(u8 data1, u8 data2, u8 data3)
{
  uint8_t command = data1 >> 4;

  // program change
  if (command == 0xC) {
    //commands_queue.push(0x30 | (data2 & 0xF));
  }

  // note off (atomic 3-byte enqueue: partial commands desync the firmware)
  else if (command == 0x8 || (command == 0x9 && data3 == 0)) {
    commands_queue.push3(0xB0, data2, 0x00);
  }

  // note on
  else if (command == 0x9) {
    commands_queue.push3(0xC0, data2, data3);
  }

  // sustain
  else if (command == 0xB && data2 == 64) {
     commands_queue.push(0x50 | (data3 >= 64 ? 0xF : 0x0));
  }
}

void Mcu::loadSounds(const u8 temp_model, size_t from_addr)
{
  const u8* params_rom_tmp = nullptr;

  sound_chip.load_samples(temp_model);

  if ((temp_model==0) || (temp_model==1)) { 
    params_rom_tmp = (const u8*)&mks20a_params_rom; 
  } else { params_rom_tmp = (const u8*)&mk80_params_rom; }

  // memset/memcpy instead of byte loops: this runs on a patch change while
  // the audio producer is stalled -- the loops took milliseconds, this ~100us.
  memset(params_rom, 0xff, 0x20000);

  size_t from_addr_aligned = from_addr >> 15 << 15;
  memcpy(params_rom + 0x8000, params_rom_tmp + from_addr_aligned, 0x8000);

  size_t target = (from_addr - from_addr_aligned) + 0x4000;
  params_rom[0x00] = 0x01;
  params_rom[0x01] = (target >> 8) & 0xff;
  params_rom[0x02] = target & 0xff;
}

void Mcu::loadPatch(const u8 temp_patch)
{
  loadSounds(patchToRomSet[temp_patch], patchToOffset[temp_patch]);
  commands_queue.push(0x31);
  commands_queue.push(0x30);
  current_patch = temp_patch;
  current_smple_rate = sampleRates[current_patch];
  // Full 16 voices everywhere: capping below what the firmware allocates would
  // FREEZE voices mid-note (they resume later with stale envelopes = artifacts).
  sound_chip.set_voice_cap(16);
}

s32 Mcu::get_next_sample(void) {
  bool mode32khz = current_smple_rate == 32000;
  return(generate_next_sample(mode32khz));
}

u8 Mcu::getPatch(void)
{
  return(current_patch);
}

void Mcu::getPatchName(char *patchName)
{
  strcpy(patchName, PatchNames[current_patch]);
}
