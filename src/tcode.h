/*
** tcode.h
** Tokudae bytecode and auxiliary functions
** See Copyright Notice in tokudae.h
*/

#ifndef tcode_h
#define tcode_h


#include "tbits.h"
#include "tparser.h"


/* 
** Get current pc, this macro expects fs (FunctionState) to be in scope.
*/
#define currPC      (fs->pc)


/* get pointer to opcode from 'ExpInfo' */
#define getpi(fs,e)     (&(fs)->p->code[(e)->u.info])


/* sizes in bytes */
#define SIZE_OPCODE         (sizeof(uint8_t))
#define SIZE_ARG_S          (sizeof(uint8_t))
#define SIZE_ARG_L          (sizeof(uint8_t[3]))

/* bit widths */
#define WIDTH_OPCODE        t_nbits(uint8_t)
#define WIDTH_ARG_S         WIDTH_OPCODE
#define WIDTH_ARG_L         t_nbits(uint8_t[3])

/* limits */
#define MAX_OPCODE          ((1 << WIDTH_OPCODE) - 1)
#define MAX_ARG_S           ((1 << WIDTH_ARG_S) - 1)
#define MAX_ARG_L           ((1 << WIDTH_ARG_L) - 1)
#define MAX_CODE            MAX_ARG_L


/* gets first arg pc */
#define GET_ARG(ip)             ((ip) + SIZE_OPCODE)

/* get short/long argument pc */
#define GETPC_S(ip,o)           (GET_ARG(ip)+(cast_u32(o) * SIZE_ARG_S))
#define GETPC_L(ip,o)           (GET_ARG(ip)+(cast_u32(o) * SIZE_ARG_L))


/* get/set short parameter */
#define GET_ARG_S(ip,o)         cast_u8(*GETPC_S(ip,o))
#define SET_ARG_S(ip,o,v)       setbyte(GETPC_S(ip,0), o, v);
#define SET_ARG_LLS(ip,v)       setbyte(GET_ARG(ip), 2u * SIZE_ARG_L, v)


/* get/set long arg */
#define GET_ARG_L(ip,o)     get3bytes(GET_ARG(ip)+(cast_u32(o) * SIZE_ARG_L))
#define SET_ARG_L(ip,o,v)   set3bytes(GETPC_L(ip,o), v)


/* max code jump offset value */
#define MAXJMP      MAX_CODE

#define NOJMP       (-1)  /* value indicating there it no jump */
#define NOPC        NOJMP /* value indicating there it no pc */


/*
** Decode short immediate operand by moving the immediate operand
** sign from 8th bit to the 32nd bit.
*/
#define IMM(imm) \
        (((imm) & 0x80) ? cast_i32(~(cast_u32(imm) & 0x7f) + 1) \
                        : cast_i32(imm))

/*
** Decode long immediate operand by moving the immediate operand
** sign from 24th bit to the 32nd bit.
*/
#define IMML(imm) \
        (((imm) & 0x00800000) ? cast_i32(~(cast_u32(imm) & 0xff7fffff) + 1) \
                              : cast_i32(imm))


/* 
** Binary operations.
*/
typedef enum { /* "ORDER OP" */
    /* arithmetic operators */
    OPR_ADD, OPR_SUB, OPR_MUL,
    OPR_DIV, OPR_IDIV, OPR_MOD, OPR_POW,
    /* bitwise operators */
    OPR_SHL, OPR_SHR, OPR_BAND,
    OPR_BOR, OPR_BXOR,
    /* concat operator */
    OPR_CONCAT,
    /* comparison operators */
    OPR_NE, OPR_EQ, OPR_LT,
    OPR_LE, OPR_GT, OPR_GE,
    /* logical operators */
    OPR_AND, OPR_OR,
    OPR_NOBINOPR
} Binopr;


/* true if binary operation 'op' it foldable (arithmetic or bitwise) */
#define oprisfoldable(op)      ((op) <= OPR_BXOR)


/*
** Unary operations.
*/
typedef enum { /* "ORDER OP" */
    OPR_UNM, OPR_BNOT, OPR_NOT, OPR_NOUNOPR
} Unopr;



typedef enum { /* "ORDER OP" */
/* ------------------------------------------------------------------------
** Legend for reading OpCodes:
** ':' - value type follows
** S - short arg (8-bit)
** L - long arg (24-bit)
** V - stack value
** V{x} - stack value at index 'x'
** K{x} - constant at index 'x'
** I(x) - 'x' is immediate operand
** U{x} - upvalue at index 'x'
** OU{x} - open upvalue at index 'x'
** G{x} - global variable, key is K{x}:string
** L{x} - local variable in 'p->locals[x]'
**
** operation     args           description
** ------------------------------------------------------------------------ */
OP_TRUE = 0,/*                'load true constant'                          */
OP_FALSE,/*                   'load false constant'                         */
OP_SUPER,/*        V          'load V.class.superclass'                     */
OP_NIL,/*          L          'load L nils'                                 */
OP_POP,/*          L          'pop L values off the stack'                  */
OP_LOAD,/*         L          'load V{L}'                                   */
OP_CONST,/*        S          'load K{S}'                                   */
OP_CONSTL,/*       L          'load K{L}'                                   */
OP_CONSTI,/*       S          'load integer S'                              */
OP_CONSTIL,/*      L          'load integer L'                              */
OP_CONSTF,/*       S          'load integer S at float'                     */
OP_CONSTFL,/*      L          'load integer L at float'                     */
OP_VARARGPREP,/*   L          'adjust function varargs (L function arity)'  */
OP_VARARG,/*       L          'load L-1 varargs'                            */
OP_CLOSURE,/*      L          'load closure(Enclosing->fns[L])'             */
OP_NEWLIST,/*      S          'create and load new array of size 1<<(S-1)'  */
OP_NEWCLASS,/*     S          'create and load new class of size 1<<(S-1)'  */
OP_NEWTABLE,/*     S          'create and load new table of size 1<<(S-1)'  */
OP_METHOD,/*       L V1 V2    'define method V2 for class V1 under key K{L}'*/
OP_SETTM,/*        S V1 V2    'V1->metatable[g->tmnames[S]] = V2'           */
OP_SETMT,/*        L V1 V2    'V1->metatable[K{L}] = V2'                    */

OP_MBIN,/*         V1 V2 S    'V1 S V2'  (S is binop)                       */

OP_ADDK,/*         V L     'V + K{L}:number'                                */
OP_SUBK,/*         V L     'V - K{L}:number'                                */
OP_MULK,/*         V L     'V * K{L}:number'                                */
OP_DIVK,/*         V L     'V / K{L}:number'                                */
OP_IDIVK,/*        V L     'V // K{L}:number'                               */
OP_MODK,/*         V L     'V % K{L}:number'                                */
OP_POWK,/*         V L     'V ** K{L}:number'                               */
OP_BSHLK,/*        V L     'V << K{L}:number'                               */
OP_BSHRK,/*        V L     'V >> K{L}:number'                               */
OP_BANDK,/*        V L     'V & K{L}:number'                                */
OP_BORK,/*         V L     'V | K{L}:number'                                */
OP_BXORK,/*        V L     'V ^ K{L}:number'                                */

OP_ADDI,/*         V L     'V + I(L)'                                       */
OP_SUBI,/*         V L     'V - I(L)'                                       */
OP_MULI,/*         V L     'V * I(L)'                                       */
OP_DIVI,/*         V L     'V / I(L)'                                       */
OP_IDIVI,/*        V L     'V // I(L)'                                      */
OP_MODI,/*         V L     'V % I(L)'                                       */
OP_POWI,/*         V L     'V ** I(L)'                                      */
OP_BSHLI,/*        V L     'V << I(L)'                                      */
OP_BSHRI,/*        V L     'V >> I(L)'                                      */
OP_BANDI,/*        V L     'V & I(L)'                                       */
OP_BORI,/*         V L     'V | I(L)'                                       */
OP_BXORI,/*        V L     'V ^ I(L)'                                       */

OP_ADD,/*          V1 V2 S 'V1 + V2'                                        */
OP_SUB,/*          V1 V2 S 'V1 - V2'                                        */
OP_MUL,/*          V1 V2 S 'V1 * V2'                                        */
OP_DIV,/*          V1 V2 S 'V1 / V2  (if (S) swap operands)'                */
OP_IDIV,/*         V1 V2 S 'V1 // V2'                                       */
OP_MOD,/*          V1 V2 S 'V1 % V2'                                        */
OP_POW,/*          V1 V2 S 'V1 ** V2'                                       */
OP_BSHL,/*         V1 V2 S 'V1 << V2'                                       */
OP_BSHR,/*         V1 V2 S 'V1 >> V2'                                       */
OP_BAND,/*         V1 V2 S 'V1 & V2'                                        */
OP_BOR,/*          V1 V2 S 'V1 | V2'                                        */
OP_BXOR,/*         V1 V2 S 'V1 ^ V2'                                        */

OP_CONCAT,/*       L           'V{-L} = V{-L} .. V{L - 1}'                  */

OP_EQK,/*          V L S       '(V == K{L}) == S'                           */

OP_EQI,/*          V L S       '(V == I(L)) == S'                           */
OP_LTI,/*          V L         'V < I(L)'                                   */
OP_LEI,/*          V L         'V <= I(L)'                                  */
OP_GTI,/*          V L         'V > I(L)'                                   */
OP_GEI,/*          V L         'V >= I(L)'                                  */ 

OP_EQ,/*           V1 V2 S     '(V1 == V2) == S'                            */
OP_LT,/*           V1 V2 S     '(V1 < V2)  (if (S) swap operands)'          */
OP_LE,/*           V1 V2 S     '(V1 <= V2)'                                 */

OP_EQPRESERVE,/*   V1 V2   'V1 == V2 (preserves V1 operand)'                */

OP_UNM,/*          V       '-V'                                             */
OP_BNOT,/*         V       '~V'                                             */
OP_NOT,/*          V       '!V'                                             */

OP_JMP,/*          L       'pc += L'                                        */
OP_JMPS,/*         L       'pc -= L'                                        */

OP_TEST,/*         V S     'if (!t_isfalse(V) == S) dojump;'                */
OP_TESTPOP,/*      V S     'if (!t_isfalse(V) == S) { dojump; } pop;'       */

OP_CALL,/*  L1 L2 S 'V{L1},...,V{L1+L2-1} = V{L1}(V{L1+1},...,V{offtp-1})'  */
OP_TAILCALL,/* L1 L2 S  '-||- (S is true if it needs close; it skips RET)   */

OP_CLOSE,/*        L           'close all open upvalues >= V{L}'            */
OP_TBC,/*          L           'mark L{L} at to-be-closed'                  */

OP_CHECKADJ,/*     L           'adjust results after callcheck to L-1'      */

OP_GETLOCAL,/*     L           'L{L}'                                       */
OP_SETLOCAL,/*     V L         'L{L} = V'                                   */

OP_GETUVAL,/*      L           'U{L}'                                       */
OP_SETUVAL,/*      V L         'U{L} = V'                                   */

OP_SETLIST,/*      L1 L2 S     'V{L1}[L2+i] = V{-S+i}, 0 <= i < S           */

OP_SETPROPERTY,/*  V L1 L2     'V{-L1}.K{L2}:string = V'                    */
OP_GETPROPERTY,/*  V  L        'V.K{L}'                                     */

OP_GETINDEX,/*     V1 V2       'V1[V2]'                                     */
OP_SETINDEX,/*     V L         'V{-L}[V{-L + 1}] = V3'                      */

OP_GETINDEXSTR,/*  V L         'V[K{L}:string]'                             */
OP_SETINDEXSTR,/*  V L1 L2     'V{-L1}[K{L2}:string] = V'                   */

OP_GETINDEXINT,/*  V S         'V[I(S):integer]'                            */
OP_GETINDEXINTL,/* V L         'V[I(L):integer]'                            */
OP_SETINDEXINT,/*  V L S       'V{-L}[I(S):integer] = V'                    */
OP_SETINDEXINTL,/* V L1 L2     'V{-L1}[I(L2):integer] = V'                  */

OP_GETSUP,/*       V L         'V.class.superclass.methods.K{L}:string'     */
OP_GETSUPIDX,/*    V1 V2       'V1.class.superclass.methods[V2]'            */

OP_INHERIT,/*     V1 V2        'V2 inherits V1'                             */
OP_FORPREP,/*     L1 L2        'create upvalue V{L1+3}; pc += L2'           */
OP_FORCALL,/*     L1 L2  'V{L1+4},...,V{L1+3+L2} = V{L1}(V{L1+1}, V{L1+2});'*/
OP_FORLOOP,/*L1 L2 L3 'if V{L1+4}!=nil {V{L1}=V{L1+2}; pc-=L2} else pop(L3)'*/

OP_RETURN,/*         L1 L2 S      'return V{L1}, ... ,V{L1+L2-2}'           */
} OpCode;


/* number of 'OpCode's */
#define NUM_OPCODES     (OP_RETURN + 1)


/* opcode format */
typedef enum { /* "ORDER OPFMT" */
    FormatI,    /* opcode */
    FormatIS,   /* opcode + short arg */
    FormatISS,  /* opcode + 2x short arg */
    FormatIL,   /* opcode + long arg */
    FormatILS,  /* opcode + long arg + short arg */
    FormatILL,  /* opcode + 2x long arg */
    FormatILLS, /* opcode + 2x long arg + short arg */
    FormatILLL, /* opcode + 3x long arg */
    FormatN,    /* total number of opcode formats */
} OpFormat;


#define VD      INT_MAX /* flag for variable delta */


/* TODO: compress 'push','pop' and 'chgsp' into a single byte */
typedef struct {
    OpFormat format; /* opcode format */
    int32_t push; /* how many values the opcode pushes */
    int32_t pop; /* how many values the opcode pops */
    uint8_t chgsp; /* true if opcode changes value at current stack pointer */
} OpProperties; 


TOKUI_DEC(const OpProperties tokuC_opproperties[NUM_OPCODES];)

#define getopFormat(i)  (tokuC_opproperties[i].format)
#define getopDelta(i) \
        (tokuC_opproperties[i].push - tokuC_opproperties[i].pop)


/* uint8_t format sizes in bytes (or in units of 'uint8_t's) */
TOKUI_DEC(const uint8_t tokuC_opsize[FormatN];)
#define getopSize(i)    (tokuC_opsize[getopFormat(i)])


/*
** All chunks and functions end with OP_RETURN opcode.
*/
#define lastpc(f)   cast_u32((f)->sizecode - getopSize(OP_RETURN))


/* 
** Number of list items to accumulate before a SETLIST opcode.
** Keep this value under MAX_ARG_S.
*/
#define LISTFIELDS_PER_FLUSH     50


#define prevOP(fs)  (((fs)->pc == 0) ? NULL : &(fs)->p->code[(fs)->prevpc])


#define tokuC_store(fs,v)    tokuC_storevar(fs, v, 0)

#define tokuC_storepop(fs,v,ln) { \
    int32_t left_ = tokuC_storevar(fs, v, 0); tokuC_fixline(fs, ln); \
    tokuC_pop(fs, left_); }

TOKUI_FUNC int32_t tokuC_emitS(FunctionState *fs, int32_t a);
TOKUI_FUNC int32_t tokuC_emitI(FunctionState *fs, uint8_t i);
TOKUI_FUNC int32_t tokuC_emitIS(FunctionState *fs, uint8_t i, int32_t a);
TOKUI_FUNC int32_t tokuC_emitIL(FunctionState *fs, uint8_t i, int32_t a);
TOKUI_FUNC int32_t tokuC_emitILS(FunctionState *fs, uint8_t op, int32_t a,
                                                                int32_t b);
TOKUI_FUNC int32_t tokuC_emitILL(FunctionState *fs, uint8_t i, int32_t a,
                                                               int32_t b);
TOKUI_FUNC int32_t tokuC_emitILLL(FunctionState *fs, uint8_t i, int32_t a,
                                                     int32_t b, int32_t c);
TOKUI_FUNC int32_t tokuC_call(FunctionState *fs, int32_t base, int32_t nres);
TOKUI_FUNC int32_t tokuC_vararg(FunctionState *fs, int32_t nres);
TOKUI_FUNC void tokuC_fixline(FunctionState *fs, int32_t line);
TOKUI_FUNC void tokuC_removelastjump(FunctionState *fs);
TOKUI_FUNC void tokuC_checkstack(FunctionState *fs, int32_t n);
TOKUI_FUNC void tokuC_reserveslots(FunctionState *fs, int32_t n);
TOKUI_FUNC void tokuC_setreturns(FunctionState *fs, ExpInfo *e, int32_t nret);
TOKUI_FUNC void tokuC_setmulret(FunctionState *fs, ExpInfo *e);
TOKUI_FUNC int32_t tokuC_nil(FunctionState *fs, int32_t n);
TOKUI_FUNC void tokuC_load(FunctionState *fs, int32_t stk);
TOKUI_FUNC int32_t tokuC_remove(FunctionState *fs, int32_t n);
TOKUI_FUNC int32_t tokuC_pop(FunctionState *fs, int32_t n);
TOKUI_FUNC void tokuC_adjuststack(FunctionState *fs, int32_t nextra);
TOKUI_FUNC int32_t tokuC_return(FunctionState *fs, int32_t first,
                                int32_t nres);
TOKUI_FUNC void tokuC_callcheck(FunctionState *fs, int32_t base,
                                int32_t linenum);
TOKUI_FUNC void tokuC_methodset(FunctionState *fs, ExpInfo *e);
TOKUI_FUNC void tokuC_tmset(FunctionState *fs, int32_t mt, int32_t linenum);
TOKUI_FUNC void tokuC_mtset(FunctionState *fs, OString *field,
                            int32_t linenum);
TOKUI_FUNC void tokuC_classadjust(FunctionState *fs, int32_t pc,
                                  int32_t nmethods, int32_t havemt);
TOKUI_FUNC int32_t tokuC_storevar(FunctionState *fs, ExpInfo *var,
                                  int32_t left);
TOKUI_FUNC void tokuC_setlistsize(FunctionState *fs, int32_t pc, int32_t lsz);
TOKUI_FUNC void tokuC_setlist(FunctionState *fs, int32_t base,
                              int32_t nelems, int32_t tostore);
TOKUI_FUNC void tokuC_settablesize(FunctionState *fs, int32_t pc, int32_t hsz);
TOKUI_FUNC void tokuC_const2v(FunctionState *fs, ExpInfo *e, TValue *v);
TOKUI_FUNC TValue *tokuC_getconstant(FunctionState *fs, ExpInfo *v);
TOKUI_FUNC int32_t tokuC_dischargevars(FunctionState *fs, ExpInfo *e);
TOKUI_FUNC void tokuC_exp2stack(FunctionState *fs, ExpInfo *e);
TOKUI_FUNC void tokuC_exp2val(FunctionState *fs, ExpInfo *e);
TOKUI_FUNC void tokuC_getdotted(FunctionState *fs, ExpInfo *var, ExpInfo *key,
                                int32_t issuper);
TOKUI_FUNC void tokuC_indexed(FunctionState *fs, ExpInfo *var, ExpInfo *key,
                              int32_t issuper);
TOKUI_FUNC void tokuC_unary(FunctionState *fs, ExpInfo *e, Unopr op,
                            int32_t linenum);
TOKUI_FUNC int32_t tokuC_jmp(FunctionState *fs, OpCode opJ);
TOKUI_FUNC int32_t tokuC_test(FunctionState *fs, OpCode opT, int32_t cond,
                              int32_t linenum);
TOKUI_FUNC void tokuC_concatjl(FunctionState *fs, int32_t *l1, int32_t l2);
TOKUI_FUNC void tokuC_patch(FunctionState *fs, int32_t pc, int32_t target);
TOKUI_FUNC void tokuC_patchtohere(FunctionState *fs, int32_t pc);
TOKUI_FUNC void tokuC_prebinary(FunctionState *fs, ExpInfo *e, Binopr op,
                                int32_t linenum);
TOKUI_FUNC void tokuC_binary(FunctionState *fs, ExpInfo *e1, ExpInfo *e2,
                             Binopr op, int32_t linenum);
TOKUI_FUNC void tokuC_binimmediate(FunctionState *fs, ExpInfo *e1, int32_t imm,
                                   Binopr op, int32_t linenum);
TOKUI_FUNC void tokuC_finish(FunctionState *fs);

#endif
