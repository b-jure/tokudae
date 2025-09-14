/*
** ttrace.c
** Functions for low-level bytecode debugging and tracing
** See Copyright Notice in tokudae.h
*/

#define ttrace_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <stdio.h>
#include <string.h>

#include "ttrace.h"
#include "tmeta.h"
#include "tcode.h"
#include "tdebug.h"
#include "tokudaelimits.h"
#include "tobject.h"
#include "tstring.h"


/* number of spaces for 'posfix_spaces' (used when unassembling chunks) */
#define ARGSPACES       3


/* prints ARGSPACES spaces after 'e' */
#define posfix_spaces(e)    (e, printf("%*s", ARGSPACES, ""))


/* maximum size of value names when tracing execution */
#define MAXTXT      25

/* string size being displayed when tracing execution, reserve 2 for '""' */
#define MAXSTR      (MAXTXT - 2)


#define BOXED(fmt)          "[" fmt "]"
#define PFMT(what)          BOXED(what " %4d")

#define printLine(p,relpc)  printf(PFMT("LN"), tokuD_getfuncline(p, relpc))

#define printPC(relpc)      printf(PFMT("PC"), relpc)


static void prefix(const Proto *p, const Instruction *pc) {
    int relpc = cast_int(pc - p->code);
    printPC(relpc); fflush(stdout);
    posfix_spaces(printLine(p, relpc)); fflush(stdout);
}


static void posfix(void) {
    putchar('\n'); fflush(stdout);
}


static int traceOp_(OpCode op) {
    posfix_spaces(printf("%-12s", getopName(op)));
    return SIZE_INSTR;
}

#define castop(e)       cast(OpCode, e)
#define traceOp(pi)     traceOp_(castop(pi))


static int traceS(int s) {
    posfix_spaces(printf("ArgS=%-8d", s));
    return SIZE_ARG_S;
}


static int traceL(const Instruction *pc) {
    posfix_spaces(printf("ArgL=%-8d", get3bytes(pc)));
    return SIZE_ARG_L;
}


static int traceI(const Proto *p, const Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    posfix();
    return SIZE_INSTR;
}


static void traceIS(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    traceS(*pc);
    posfix();
}


static void traceISS(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    pc += traceS(*pc);
    traceS(*pc);
    posfix();
}


static void traceIL(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    traceL(pc);
    posfix();
}


static void traceILS(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    pc += traceL(pc);
    traceS(*pc);
    posfix();
}


static void traceILL(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    pc += traceL(pc);
    traceL(pc);
    posfix();
}


static void traceILLL(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    pc += traceL(pc);
    pc += traceL(pc);
    traceL(pc);
    posfix();
}


static void traceILLS(const Proto *p, const Instruction *pc) {
    OpCode op = castop(*pc);
    prefix(p, pc);
    pc += traceOp(op);
    pc += traceL(pc);
    pc += traceL(pc);
    traceS(*pc);
    posfix();
}


/*
** Trace the current OpCode and its arguments.
*/
void tokuTR_tracepc(toku_State *T, SPtr sp, const Proto *p,
                  const Instruction *pc, int tolevel) {
    SPtr oldsp = T->sp.p;
    T->sp.p = sp; /* correct stack pointer */
    tokuTR_dumpstack(T, tolevel, NULL); /* dump 'tolevel' stacks */
    printf("------------------------------------------------------------\n");
    switch (getopFormat(*pc)) { /* trace the instruction */
        case FormatI: traceI(p, pc); break;
        case FormatIS: traceIS(p, pc); break;
        case FormatISS: traceISS(p, pc); break;
        case FormatIL: traceIL(p, pc); break;
        case FormatILS: traceILS(p, pc); break;
        case FormatILL: traceILL(p, pc); break;
        case FormatILLS: traceILLS(p, pc); break;
        case FormatILLL: traceILLL(p, pc); break;
        default: toku_assert(0 && "invalid OpCode format"); break;
    }
    T->sp.p = oldsp; /* after this, the caller manages stack pointer */
}


#define traceNil()      printf("nil")

#define traceTrue()     printf("true")

#define traceFalse()    printf("false")


static void traceString(OString *s) {
    char buff[MAXSTR];
    tokuS_trimstr(buff, MAXSTR, getstr(s), getstrlen(s));
    printf("\"%s\"", buff);
}


static void traceNumber(const TValue *o) {
    char buff[TOKU_N2SBUFFSZ];
    tokuS_tostringbuff(o, buff);
    printf("%s", buff);
}


static void traceValue(const TValue *o) {
    switch (ttypetag(o)) {
        case TOKU_VNIL: traceNil(); break;
        case TOKU_VTRUE: traceTrue(); break;
        case TOKU_VFALSE: traceFalse(); break;
        case TOKU_VSHRSTR: case TOKU_VLNGSTR: traceString(strval(o)); break;
        case TOKU_VNUMINT: case TOKU_VNUMFLT: traceNumber(o); break;
        default: toku_assert(0 && "invalid 'o' type"); break;
    }
}


static void traceK(const Proto *p, int index) {
    posfix_spaces((printf("K@%d=", index), traceValue(&p->k[index])));
}


static void unasm(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    posfix();
}


static void unasmL(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    pc += traceOp(*pc);
    traceL(pc);
    posfix();
}


static void unasmKL(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceK(p, GET_ARG_L(pc, 0));
    posfix();
}


static void unasmK(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceK(p, GET_ARG_S(pc, 0));
    posfix();
}


static int getimm(Instruction *pc, int off, int l) {
    int imm = (l) ? GET_ARG_L(pc, off) : GET_ARG_S(pc, off);
    return (l) ? IMML(imm) : IMM(imm);
}


static void traceImmediateInt(Instruction *pc, int off, int l) {
    posfix_spaces(printf("IMM%s=%d", (l) ? "L" : "", getimm(pc, off, l)));
}


static void unasmIMMint(const Proto *p, Instruction *pc, int l) {
    prefix(p, pc);
    traceOp(*pc);
    traceImmediateInt(pc, 0, l);
    posfix();
}


static void unasmIMMflt(const Proto *p, Instruction *pc, int l) {
    prefix(p, pc);
    traceOp(*pc);
    traceImmediateInt(pc, 0, l);
    posfix();
}


static void traceSize(int size) {
    if (size > 0) size = 1 << (size - 1);
    posfix_spaces(printf("size=%d", size));
}


static void unasmNewObject(const Proto *p, Instruction *pc) {
    int size;
    prefix(p, pc);
    traceOp(*pc);
    size = GET_ARG_S(pc, 0);
    traceSize((*pc == OP_NEWCLASS) ? size & 0x7f : size);
    if (*pc == OP_NEWCLASS) {
        if (size & 0x80)
            traceTrue();
        else
            traceFalse();
    }
    posfix();
}


static void unasmEQK(const Proto *p, Instruction *pc) {
    TValue aux;
    prefix(p, pc);
    traceOp(*pc);
    traceK(p, GET_ARG_L(pc, 0));
    setival(&aux, GET_ARG_S(pc, SIZE_ARG_L));
    posfix_spaces(traceNumber(&aux));
    posfix();
}


static void traceCond(int cond) {
    posfix_spaces(printf("%s", (cond ? "equal" : "not equal")));
}


static void unasmEQI(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceImmediateInt(pc, 0, 1);
    traceCond(GET_ARG_S(pc, SIZE_ARG_L));
    posfix();
}


static void unasmIMMord(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceImmediateInt(pc, 0, 1);
    posfix();
}


static void traceNparams(int nparams) {
    posfix_spaces(printf("nparams=%d", nparams));
}


static void unasmVarargPrep(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceNparams(GET_ARG_L(pc, 0));
    posfix();
}


static void unasmS(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceS(GET_ARG_S(pc, 0));
    posfix();
}


static void traceStackSlot(int index) {
    if (index < 0)
        posfix_spaces(printf("S@Top%d", index));
    else
        posfix_spaces(printf("S@%d", index));
}


static void traceNres(int nres) {
    const char *res = NULL;
    nres--;
    if (nres < 0)
        res = "multiple";
    else if (nres == 0)
        res = "none";
    if (res)
        posfix_spaces(printf("results=%s", res));
    else
        posfix_spaces(printf("results=%d", nres));
}


static void unasmCall(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceNres(GET_ARG_L(pc, 1));
    posfix();
}


static void traceMetaName(toku_State *T, int mm) {
    posfix_spaces(printf("%s", getstr(G(T)->tmnames[mm])));
}


static void traceSwap(int swap) {
    posfix_spaces(printf("swap=%s", (swap) ? "yes" : "no"));
}


static void unasmSetEvent(toku_State *T, const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceMetaName(T, GET_ARG_S(pc, 0));
    posfix();
}


static void unasmMMBin(toku_State *T, const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceMetaName(T, GET_ARG_S(pc, 0));
    posfix();
}


static void unasmIndexedSetInt(const Proto *p, Instruction *pc, int l) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceImmediateInt(pc, 1, l);
    posfix();
}


static void traceImmediateK(const Proto *p, Instruction *pc, int off, int l) {
    traceK(p, getimm(pc, off, l));
}


static void unasmIndexedSetStr(const Proto *p, Instruction *pc, int l) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(-GET_ARG_L(pc, 0));
    traceImmediateK(p, pc, 1, l);
    posfix();
}


static void unasmSetArray(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    posfix_spaces(printf("lastindex=%d", GET_ARG_L(pc, 1)));
    posfix_spaces(printf("tostore=%d", GET_ARG_S(pc, SIZE_ARG_L*2)));
    posfix();
}


static void traceLocal(int index) {
    posfix_spaces(printf("L@%d", index));
}


static void unasmLocal(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceLocal(GET_ARG_L(pc, 0));
    posfix();
}


static void traceUpVal(UpValInfo *uv, int index) {
    const char *str = getstr(uv[index].name);
    posfix_spaces(printf("U@%d=%s", index, str));
}


static void unasmUpvalue(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceUpVal(p->upvals, GET_ARG_L(pc, 0));
    posfix();
}


static void traceClose(int close) {
    posfix_spaces(printf("close=%s", close ? "true" : "false"));
}


static void traceN(int npop) {
    posfix_spaces(printf("N=%d", npop));
}


static void unasmN(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceN(GET_ARG_L(pc, 0) - (*pc == OP_VARARG));
    posfix();
}


static void unasmVararg(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceNres(GET_ARG_L(pc, 0));
    posfix();
}


static void unasmCheck(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(p, 0));
    traceNres(GET_ARG_L(p, 1));
    posfix();
}


static int traceOffset(Instruction *pc) {
    int offset = 0; /* to prevent warnings */
    switch (*pc) {
        case OP_JMP: case OP_JMPS: {
            offset = GET_ARG_L(pc, 0);
            break;
        }
        case OP_FORPREP: case OP_FORLOOP: {
            offset = GET_ARG_L(pc, 1);
            break;
        }
        default: toku_assert(0); /* invalid opcode */
    }
    posfix_spaces(printf("offset=%d", offset));
    return (*pc == OP_JMPS || *pc == OP_FORLOOP) ? -offset : offset;
}


static void traceTarget(const Proto *p, Instruction *pc, int offset) {
    int target = cast_int(pc - p->code) + getopSize(*pc) + offset;
    posfix_spaces(printf("target=%d", target));
}


static void traceJump(const Proto *p, Instruction *pc) {
    traceTarget(p, pc, traceOffset(pc));
}


static void unasmJmp(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceJump(p, pc);
    posfix();
}


static void unasmForPrep(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceJump(p, pc);
    posfix();
}


static void unasmForCall(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceNres(GET_ARG_L(pc, 1));
    posfix();
}


static void unasmForLoop(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceJump(p, pc);
    traceNres(GET_ARG_L(pc, 2));
    posfix();
}


static void unasmRet(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    traceNres(GET_ARG_L(pc, 1));
    traceClose(GET_ARG_S(pc, (2*SIZE_ARG_L)));
    posfix();
}


static void unasmStackOp(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(GET_ARG_L(pc, 0));
    posfix();
}


static void unasmIndexedGet(const Proto *p, Instruction *pc, int l) {
    prefix(p, pc);
    traceOp(*pc);
    traceImmediateInt(pc, 0, l);
    posfix();
}


static void unasmSetIndex(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceStackSlot(-GET_ARG_L(pc, 0));
    posfix();
}


static void unasmBinOp(const Proto *p, Instruction *pc) {
    prefix(p, pc);
    traceOp(*pc);
    traceSwap(GET_ARG_S(pc, 0));
    posfix();
}


static void printFunc(const Proto *p) {
    char id[TOKU_IDSIZE];
    tokuS_chunkid(id, getstr(p->source), getstrlen(p->source));
    if (p->defline == 0) /* main chunk? */
        printf("MAIN %s {\n", id);
    else /* otherwise subroutine */
        printf("FUNCTION %s:%d {\n", id, p->defline);
}


/*
** Disassemble all of the bytecode in 'p->code'.
** This function provides more detailed human readable information
** compared to 'tokuTR_trace' when tracing OpCode and its arguments.
** > "ORDER OP" (in case of changes to bytecode) <
*/
void tokuTR_disassemble(toku_State *T, const Proto *p) {
    printFunc(p);
    for (Instruction *pc = p->code; pc < &p->code[p->sizecode];) {
        printf("    ");
        switch (*pc) {
            case OP_TRUE: case OP_FALSE: case OP_SUPER:
            case OP_NOT: case OP_UNM: case OP_BNOT:
            case OP_EQPRESERVE: case OP_GETINDEX:
            case OP_GETSUPIDX: case OP_INHERIT:
                unasm(p, pc);
                break;
            case OP_LT: case OP_LE: 
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_IDIV:
            case OP_DIV: case OP_MOD: case OP_POW: case OP_BSHL:
            case OP_BSHR: case OP_BAND: case OP_BOR: case OP_BXOR:
                unasmBinOp(p, pc);
                break;
            case OP_POP: case OP_NIL: case OP_CONCAT:
                unasmN(p, pc);
                break;
            case OP_JMP: case OP_JMPS:
                unasmJmp(p, pc);
                break;
            case OP_LOAD: case OP_CLOSE: case OP_TBC:
                unasmStackOp(p, pc);
                break;
            case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
            case OP_IDIVK: case OP_MODK: case OP_POWK: case OP_BSHLK:
            case OP_BSHRK: case OP_BANDK: case OP_BORK: case OP_BXORK:
            case OP_CONSTL: case OP_GETPROPERTY: case OP_GETINDEXSTR:
            case OP_METHOD: case OP_SETMT: case OP_GETSUP:
                unasmKL(p, pc);
                break;
            case OP_ADDI: case OP_SUBI: case OP_MULI:
            case OP_DIVI: case OP_IDIVI: case OP_MODI: case OP_POWI:
            case OP_BSHLI: case OP_BSHRI: case OP_BANDI:
            case OP_BORI: case OP_BXORI:
                unasmIMMint(p, pc, 1);
                break;
            case OP_NEWCLASS: case OP_NEWLIST: case OP_NEWTABLE:
                unasmNewObject(p, pc);
                break;
            case OP_LTI: case OP_LEI: case OP_GTI: case OP_GEI:
                unasmIMMord(p, pc);
                break;
            case OP_VARARGPREP:
                unasmVarargPrep(p, pc);
                break;
            case OP_CHECKADJ:
                unasmCheck(p, pc);
                break;
            case OP_GETLOCAL: case OP_SETLOCAL:
                unasmLocal(p, pc);
                break;
            case OP_GETUVAL: case OP_SETUVAL:
                unasmUpvalue(p, pc);
                break;
            case OP_SETPROPERTY: case OP_SETINDEXSTR:
                unasmIndexedSetStr(p, pc, 1);
                break;
            case OP_EQ: case OP_TEST: case OP_TESTPOP:
                unasmS(p, pc);
                break;
            case OP_VARARG: unasmVararg(p, pc); break;
            case OP_GETINDEXINT: unasmIndexedGet(p, pc, 0); break;
            case OP_GETINDEXINTL: unasmIndexedGet(p, pc, 1); break;
            case OP_SETINDEX: unasmSetIndex(p, pc); break;
            case OP_CLOSURE: unasmL(p, pc); break;
            case OP_FORPREP: unasmForPrep(p, pc); break;
            case OP_FORCALL: unasmForCall(p, pc); break;
            case OP_SETINDEXINT: unasmIndexedSetInt(p, pc, 0); break;
            case OP_SETINDEXINTL: unasmIndexedSetInt(p, pc, 1); break;
            case OP_SETLIST: unasmSetArray(p, pc); break;
            case OP_MBIN: unasmMMBin(T, p, pc); break;
            case OP_SETTM: unasmSetEvent(T, p, pc); break;
            case OP_CONSTI: unasmIMMint(p, pc, 0); break;
            case OP_CONSTIL: unasmIMMint(p, pc, 1); break;
            case OP_CONSTF: unasmIMMflt(p, pc, 0); break;
            case OP_CONSTFL: unasmIMMflt(p, pc, 1); break;
            case OP_FORLOOP: unasmForLoop(p, pc); break;
            case OP_CONST: unasmK(p, pc); break;
            case OP_EQK: unasmEQK(p, pc); break;
            case OP_EQI: unasmEQI(p, pc); break;
            case OP_CALL: unasmCall(p, pc); break;
            case OP_RETURN: unasmRet(p, pc); break;
            default: toku_assert(0 && "invalid OpCode"); break;
        }
        pc += getopSize(*pc);
    }
    printf("}\n");
    fflush(stdout);
}


static void *getptr(const TValue *obj) {
    switch (ttypetag(obj)) {
        case TOKU_VLCF: return cast(void *, cast_sizet(lcfval(obj)));
        case TOKU_VUSERDATA: return getuserdatamem(udval(obj));
        case TOKU_VLIGHTUSERDATA: return pval(obj);
        default: return gcoval(obj);
    }
}


static const char *objtxt(const TValue *obj) {
    static char buff[MAXTXT];
    int tt = ttypetag(obj);
    switch (tt) {
        case TOKU_VNUMFLT:
            t_snprintf(buff, sizeof(buff), TOKU_NUMBER_FMT, fval(obj));
            break;
        case TOKU_VNUMINT:
            t_snprintf(buff, sizeof(buff), TOKU_INTEGER_FMT, ival(obj));
            break;
        case TOKU_VSHRSTR: case TOKU_VLNGSTR: {
            size_t len;
            OString *os = strval(obj);
            tokuS_trimstr(buff, MAXSTR, getstr(os), getstrlen(os));
            len = strlen(buff);
            toku_assert(len <= MAXSTR);
            memmove(buff+1, buff, len * sizeof(char));
            buff[0] = '"'; buff[len+1] = '"'; buff[len+2] = '\0';
            break;
        }
        case TOKU_VTRUE: case TOKU_VFALSE: {
            const char *s = (tt == TOKU_VTRUE) ? "true" : "false";
            t_snprintf(buff, sizeof(buff), "%s", s);
            break;
        }
        case TOKU_VNIL:
            t_snprintf(buff, sizeof(buff), "%s", "nil");
            break;
        default: {
            const void *p = getptr(obj);
            const char *s = typename(ttype(obj));
            int l = t_snprintf(buff, sizeof(buff), "%s: ", s);
            t_snprintf(buff + l, sizeof(buff) - cast_uint(l), "%p", p);
            break;
        }
    }
    return buff;
}


void tokuTR_dumpstack(toku_State *T, int level, const char *fmt, ...) {
    CallFrame *cf = T->cf;
    SPtr prevtop = T->sp.p;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
        fflush(stdout);
    }
    for (int i = 0; cf != NULL && level-- != 0; i++) {
        SPtr base = cf->func.p + 1;
        printf("[L %3d] %-25s %s ", i, objtxt(s2v(cf->func.p)),
                                       (cf != T->cf) ? "--" : ">>");
        fflush(stdout);
        if (base < prevtop) {
            for (SPtr sp = base; sp < prevtop; sp++)
                printf("[%s]", objtxt(s2v(sp)));
        } else {
            toku_assert(base <= prevtop);
            printf("empty");
        }
        printf("\n"); fflush(stdout);
        prevtop = cf->func.p;
        cf = cf->prev;
    }
    fflush(stdout);
}
