/*
** tdebug.c
** Debug and error reporting functions
** See Copyright Notice in tokudae.h
*/

#define tdebug_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <string.h>

#include "tokudae.h"
#include "tdebug.h"
#include "tapi.h"
#include "tcode.h"
#include "tfunction.h"
#include "tstring.h"
#include "tokudaelimits.h"
#include "tobject.h"
#include "tprotected.h"
#include "tmeta.h"
#include "tvm.h"
#include "tgc.h"
#include "ttable.h"


#define TokudaeClosure(cl)      ((cl) != NULL && (cl)->c.tt_ == TOKU_VTCL)


static const char strlocal[] = "local";
static const char strupval[] = "upvalue";


/*
** Gets the number of opcodes up to 'pc'.
** The sizes of opcodes are variable length, this is why we also store
** pc in 'opcodepc' for each emitted opcode. This is required in order to
** properly calculate estimate when fetching base line in O(log(n)) and
** to speed other operations when fetching debug information.
*/
static int32_t nopcode(const Proto *p, int32_t pc) {
    int32_t l = 0;
    int32_t h = p->sizeopcodepc - 1;
    int32_t m, opcodepc;
    toku_assert(h >= 0); /* must have debug information */
    while (l <= h) {
        m = l + ((h - l) / 2); /* avoid overflow */
        opcodepc = p->opcodepc[m];
        if (pc < opcodepc)
            h = m - 1;
        else if (opcodepc < pc)
            l = m + 1;
        else
            return m;
    }
    toku_assert(0); /* 'pc' does not correspond to any opcode */
    return -1; /* to avoid warnings */
}


/*
** Get a "base line" to find the line corresponding to an opcode.
** Base lines are regularly placed at MAXOWTHABS intervals, so usually
** an integer division gets the right place. When the source file has
** large sequences of empty/comment lines, it may need extra entries,
** so the original estimate needs a correction.
** If the original estimate is -1, the initial 'if' ensures that the
** 'while' will run at least once.
** The assertion that the estimate is a lower bound for the correct base
** is valid as long as the debug info has been generated with the same
** value for MAXOWTHABS or smaller.
*/
static int32_t getbaseline(const Proto *p, int32_t pc, int32_t *basepc) {
    if (p->sizeabslineinfo == 0 || pc < p->abslineinfo[0].pc) {
        *basepc = 0; /* start from the beginning */
        return p->defline + p->lineinfo[0]; /* first opcode line */
    } else {
        /* get an estimate */
        int32_t i = nopcode(p, pc) / MAXOWTHABS - 1;
        /* estimate must be a lower bound of the correct base */
        toku_assert(i < 0 || /* linedif was too large before MAXOWTHABS? */
                   (i < p->sizeabslineinfo && p->abslineinfo[i].pc <= pc));
        while (i + 1 < p->sizeabslineinfo && pc >= p->abslineinfo[i + 1].pc)
            i++; /* low estimate; adjust it */
        *basepc = p->abslineinfo[i].pc;
        return p->abslineinfo[i].line;
    }
}


/*
** Get the line corresponding to opcode 'pc' in function prototype 'p';
** first gets a base line and from there does the increments until the
** desired opcode.
*/
int32_t tokuD_getfuncline(const Proto *p, int32_t pc) {
    if (p->lineinfo == NULL) /* no debug information? */
        return -1;
    else {
        int32_t basepc;
        int32_t baseline = getbaseline(p, pc, &basepc);
        while (basepc < pc) { /* walk until given opcode */
            basepc += getopSize(p->code[basepc]); /* next opcode pc */
            toku_assert(p->lineinfo[basepc] != ABSLINEINFO);
            baseline += p->lineinfo[basepc]; /* correct line */
        }
        toku_assert(pc == basepc);
        return baseline;
    }
}


t_sinline int32_t currentpc(const CallFrame *cf) {
    return relpc(cf->u.t.savedpc, cf_func(cf)->p);
}


/* get current line number */
t_sinline int32_t getcurrentline(CallFrame *cf) {
    toku_assert(isTokudae(cf));
    return tokuD_getfuncline(cf_func(cf)->p, currentpc(cf));
}


static const char *findvararg(CallFrame *cf, SPtr *pos, int32_t n) {
    if (cf_func(cf)->p->isvararg) {
        int32_t nextra = cf->u.t.nvarargs;
        if (n >= -nextra) {
            *pos = cf->func.p - nextra - (n + 1);
            return "(vararg)";
        }
    }
    return NULL;
}


const char *tokuD_findlocal(toku_State *T, CallFrame *cf, int32_t n,
                                                          SPtr *pos) {
    SPtr base = cf->func.p + 1;
    const char *name = NULL;
    if (isTokudae(cf)) {
        if (n < 0) /* vararg ? */
            return findvararg(cf, pos, n);
        else /* otherwise local variable */
            name = tokuF_getlocalname(cf_func(cf)->p, n, currentpc(cf));
    }
    if (name == NULL) {
        SPtr limit = (cf == T->cf) ? T->sp.p : cf->next->func.p;
        if (limit - base >= n && n > 0) /* 'n' is in stack range ? */
            name = isTokudae(cf) ? "(temporary)" : "(C temporary)";
        else
            return NULL; /* no name */
    }
    if (pos) *pos = base + (n - 1);
    return name;
}


TOKU_API const char *toku_getlocal(toku_State *T, const toku_Debug *ar,
                                                  int32_t n) {
    const char *name;
    toku_lock(T);
    if (ar == NULL) { /* information about non-active function? */
        const TValue *func = s2v(T->sp.p - 1);
        if (!ttisTclosure(func)) /* not a Tokudae function? */
            name = NULL;
        else /* consider live variables at function start (parameters) */
            name = tokuF_getlocalname(clTval(func)->p, n, 0);
    } else { /* active function; get information through 'ar' */
        SPtr pos = NULL; /* to avoid warnings */
        name = tokuD_findlocal(T, ar->cf, n, &pos);
        if (name) { /* found ? */
            setobjs2s(T, T->sp.p, pos);
            api_inctop(T);
        }
    }
    toku_unlock(T);
    return name;
}


TOKU_API const char *toku_setlocal(toku_State *T, const toku_Debug *ar,
                                                  int32_t n) {
    SPtr pos = NULL;
    const char *name;
    toku_lock(T);
    name = tokuD_findlocal(T, ar->cf, n, &pos);
    if (name) { /* found ? */
        setobjs2s(T, pos, T->sp.p - 1);
        T->sp.p--; /* pop value */
    }
    toku_unlock(T);
    return name;
}


static void getfuncinfo(Closure *cl, toku_Debug *ar) {
    if (!TokudaeClosure(cl)) {
        ar->source = "=[C]";
        ar->srclen = LL("=[C]");
        ar->defline = -1;
        ar->lastdefline = -1;
        ar->what = "C";
    } else {
        const Proto *p = cl->t.p;
        if (p->source) { /* have source? */
            ar->source = getstr(p->source);
            ar->srclen = getstrlen(p->source);
        } else {
            ar->source = "=?";
            ar->srclen = LL("=?");
        }
        ar->defline = p->defline;
        ar->lastdefline = p->deflastline;
        ar->what = (ar->lastdefline == 0) ? "main" : "Tokudae";
    }
    tokuS_chunkid(ar->shortsrc, ar->source, ar->srclen);
}


/*
** For low-level debugging of the Symbolic Execution.
*/
#if 0
#include "topnames.h"
#define traceSE(fmt,...)     printf(fmt, __VA_ARGS__)
#else
#define traceSE(fmt,...)     /* empty */
#endif


static int32_t symbexec(const Proto *p, int32_t lastpc, int32_t sp) {
    int32_t pc = 0; /* execute from start */
    int32_t symsp = p->arity - 1; /* initial stack pointer (-1 if no params) */
    int32_t pcsp = -1; /* pc of opcode that sets 'sp' (-1 if none) */
    const uint8_t *code = p->code;
    traceSE("Symbolic execution\ttop=%d\n", symsp);
    if (*code == OP_VARARGPREP) /* vararg function? */
        pc += getopSize(*code); /* skip first opcode */
    if (code[lastpc] == OP_MBIN && pc + getopSize(code[pc]) == lastpc)
        goto end; /* done; 'lastpc' is 'pc' */
    while (pc < lastpc) {
        const uint8_t *i = &code[pc];
        int32_t change; /* true if current opcode changed 'sp' */
        traceSE("%d:%d:%-20s\t", tokuD_getfuncline(p, pc), pc, opnames(*i));
        toku_assert(-1 <= symsp && symsp <= p->maxstack);
        switch (*i) {
            case OP_CHECKADJ: {
                int32_t stk = GET_ARG_L(i, 0);
                int32_t nres = GET_ARG_L(i, 1);
                change = (sp < stk && sp <= stk + nres);
                symsp = stk + nres;
                break;
            }
            case OP_RETURN: {
                int32_t stk = GET_ARG_L(i, 0);
                toku_assert(stk-1 <= symsp);
                symsp = stk - 1; /* remove results */
                change = 0;
                break;
            }
            case OP_TAILCALL: case OP_CALL: {
                int32_t stk = GET_ARG_L(i, 0);
                int32_t nresults = GET_ARG_L(i, 1) - 1;
                if (nresults == TOKU_MULTRET) nresults = 1;
                toku_assert(stk <= symsp);
                change = (stk <= sp);
                symsp = stk + nresults - 1; /* 'symsp' points to last result */
                break;
            }
            case OP_NIL: case OP_VARARG: {
                int32_t n = GET_ARG_L(i, 0);
                if (*i == OP_VARARG) {
                    if (--n == TOKU_MULTRET) n = 1;
                }
                change = (symsp < sp && sp <= symsp + n);
                symsp += n;
                break;
            }
            case OP_POP:
                symsp -= GET_ARG_L(i, 0);
                change = 0;
                break;
            case OP_CONCAT:
                symsp -= GET_ARG_L(i, 0) - 1;
                change = (symsp == sp);
                break;
            case OP_SETLIST:
                symsp = GET_ARG_L(i, 0);
                change = 0;
                break;
            case OP_MBIN: /* ignore */
                change = 0;
                break;
            case OP_SETPROPERTY: case OP_SETINDEXSTR: case OP_SETINDEX:
            case OP_SETINDEXINT: case OP_SETINDEXINTL:
                change = (sp == symsp - GET_ARG_L(i, 0));
                --symsp;
                break;
            case OP_FORPREP: {
                int32_t off = GET_ARG_L(i, 1);
                const uint8_t *ni = i + off + getopSize(*i);
                int32_t nvars = check_exp(*ni == OP_FORCALL, GET_ARG_L(ni, 1));
                symsp += nvars;
                change = 0;
                break;
            }
            case OP_FORCALL: {
                int32_t stk = GET_ARG_L(i, 0);
                int32_t nresults = GET_ARG_L(i, 1) - 1;
                toku_assert(nresults >= 0); /* at least one result */
                symsp = stk + VAR_N + nresults;
                change = (stk + 2 <= sp);
                break;
            }
            case OP_FORLOOP:
                change = (GET_ARG_L(i, 0) == sp);
                symsp -= GET_ARG_L(i, 2);
                break;
            default: {
                OpCode op = cast(OpCode, *i);
                int32_t delta = getopDelta(op);
                toku_assert(delta != VD); /* default case can't handle VD */
                if (tokuC_opproperties[op].chgsp) { /* changes symsp? */
                    check_exp(delta <= 0, symsp += delta);
                    change = (sp == symsp);
                } else {
                    int32_t npush = tokuC_opproperties[op].push;
                    change = npush && (symsp < sp && sp <= symsp + npush);
                    symsp += delta;
                }
                break;
            }
        }
        if (change) {
            pcsp = pc;
            traceSE("(change) symsp=%d, pcsp=%d [sp=%d]\n", symsp, pcsp, sp);
        } else {
            traceSE("(no change) symsp=%d [sp=%d]\n", symsp, sp);
        }
        pc += getopSize(code[pc]); /* next opcode */
        /* last opcode is MBIN and the next 'pc' is 'lastpc' */
        if (code[lastpc] == OP_MBIN && pc + getopSize(code[pc]) == lastpc)
            break; /* done; this 'pc' is 'lastpc' */
    }
end:
    traceSE("\n%s RETURNS pcsp->%d\n\n", __func__, pcsp);
    return pcsp;
}


static const char *upvalname(const Proto *p, int32_t uv) {
    OString *s = check_exp(uv < p->sizeupvals, p->upvals[uv].name);
    if (s == NULL) return "?"; /* no debug information */
    else return getstr(s);
}


/*
** Find a "name" for the constant 'c'.
*/
static const char *kname(const Proto *p, int32_t index, const char **name) {
    TValue *kval = &p->k[index];
    if (ttisstring(kval)) {
        *name = getstr(strval(kval));
        return "constant";
    } else {
        *name = "?";
        return NULL;
    }
}


static const char *basicgetobjname(const Proto *p, int32_t *ppc, int32_t sp,
                                   const char **name) {
    int32_t pc = *ppc;
    traceSE("%s pc=%d, sp=%d\n", __func__, pc, sp);
    *name = tokuF_getlocalname(p, sp + 1, pc);
    if (*name) {  /* is a local? */
        traceSE("Got local '%s'\n", *name);
        return strlocal;
    }
    /* else try symbolic execution */
    *ppc = pc = symbexec(p, pc, sp);
    if (pc != -1) { /* could find opcode? */
        uint8_t *i = &p->code[pc];
        switch (*i) {
            case OP_GETLOCAL: {
                int32_t stk = GET_ARG_L(i, 0);
                toku_assert(stk < sp);
                const char *nam = basicgetobjname(p, ppc, stk, name);
                traceSE("Local name '%s'\n", nam);
                return nam;
            }
            case OP_GETUVAL:
                *name = upvalname(p, GET_ARG_L(i, 0));
                traceSE("upvalue '%s'\n", *name);
                return strupval;
            case OP_CONST: return kname(p, GET_ARG_S(i, 0), name);
            case OP_CONSTL: return kname(p, GET_ARG_L(i, 0), name);
            default: break;
        }
    }
    traceSE("could not find reasonable name (%s)\n", __func__);
    return NULL; /* could not find reasonable name */
}


/*
** Find a "name" for the stack slot 'c'.
*/
static void stkname(const Proto *p, int32_t pc, int32_t c, const char **name) {
    const char *what = basicgetobjname(p, &pc, c, name);
    if (!(what && *what == 'c')) /* did not find a constant name? */
        *name = "?";
}


/*
** Check whether value being indexed at stack slot 't' is the
** environment '__ENV'.
*/
static const char *isEnv(const Proto *p, int32_t pc, int32_t t, int32_t isup) {
    const char *name; /* name of indexed variable */
    if (isup) { /* is 't' an upvalue? */
        toku_assert(0);
        /* TODO: make OP_GETINDEXUP, which indexes upvalue these
           opcodes would be fairly common for __ENV accesses. */
        name = upvalname(p, t);
    } else { /* 't' is a stack slot */
        const char *what = basicgetobjname(p, &pc, t, &name);
        if (what != strlocal && what != strupval)
            what = NULL; /* cannot be the variable __ENV */
    }
    return (name && strcmp(name, TOKU_ENV) == 0) ? "global" : "field";
}


/*
** Extends 'basicgetobjname' to handle field accesses.
*/
static const char *getobjname(const Proto *p, int32_t lastpc, int32_t sp,
                              const char **name) {
    traceSE("%s lastpc=%d sp=%d\n", __func__, lastpc, sp);
    const char *kind = basicgetobjname(p, &lastpc, sp, name);
    if (kind != NULL)
        return kind;
    else if (lastpc != -1) { /* could find opcode? */
        uint8_t *i = &p->code[lastpc];
        traceSE("! %s at pc %d modified stack slot %d !\n",
                 opnames(*i), lastpc, sp);
        switch (*i) {
            case OP_GETPROPERTY: case OP_GETINDEXSTR: {
                kname(p, GET_ARG_L(i, 0), name);
                return isEnv(p, lastpc, sp, 0);
            }
            case OP_GETINDEX: {
                stkname(p, lastpc, sp, name); /* key */
                return isEnv(p, lastpc, sp-1, 0);
            }
            case OP_GETINDEXINT: case OP_GETINDEXINTL: {
                *name = "integer index"; /* key */
                return "field";
            }
            case OP_GETSUP: {
                kname(p, GET_ARG_L(i, 0), name); /* key */
                return "superclass field";
            }
            case OP_GETSUPIDX: {
                stkname(p, lastpc, sp-1, name); /* key */
                return "superclass field";
            }
            default: break; /* go through to return NULL */
        }
    }
    traceSE("Could not find a reasonable name (lastpc=%d, sp=%d)\n",
             lastpc, sp);
    return NULL; /* could not find reasonable name */
}


static const char *funcnamefromcode(toku_State *T, const Proto *p, int32_t pc,
                                    const char **name) {
    int32_t event;
    uint8_t *po = &p->code[pc];
    switch (*po) {
        case OP_TAILCALL: case OP_CALL:
            return getobjname(p, pc, GET_ARG_L(po, 0), name);
        case OP_FORCALL:
            *name = "for iterator";
            return "for iterator";
        case OP_GETPROPERTY: case OP_GETINDEX: case OP_GETINDEXSTR:
        case OP_GETINDEXINT:
            event = TM_GETIDX;
            break;
        case OP_SETPROPERTY: case OP_SETINDEX: case OP_SETINDEXSTR:
        case OP_SETINDEXINT:
            event = TM_SETIDX;
            break;
        case OP_MBIN:
            event = GET_ARG_S(po, 0) & 0x7f;
            break;
        case OP_LT: case OP_LTI: case OP_GTI:
            event = TM_LT;
            break;
        case OP_LE: case OP_LEI: case OP_GEI:
            event = TM_LE;
            break;
        case OP_CLOSE: case OP_RETURN:
            event = TM_CLOSE;
            break;
        case OP_UNM: event = TM_UNM; break;
        case OP_BNOT: event = TM_BNOT; break;
        case OP_CONCAT: event = TM_CONCAT; break;
        case OP_EQ: event = TM_EQ; break;
        default: return NULL;
    }
    *name = eventname(T, event);
    return "metamethod";
}


/*
** Try to find a name for a function based on how it was called.
** WARNING: always first check if the function was called as a hook,
** as the 'cf->u.t.savedpc' might point after the relevant instruction
** and this would mess up symbolic execution (see 'tokuD_traceexec').
*/
static const char *funcnamefromcall(toku_State *T, CallFrame *cf,
                                                   const char **name) {
    if (cf->status & CFST_HOOKED) { /* was it called inside a hook? */
        *name = "?";
        return "hook";
    } else if (cf->status & CFST_FIN) { /* was it called as finalizer? */
        *name = "__gc";
        return "metamethod";
    } else if (isTokudae(cf))
        return funcnamefromcode(T, cf_func(cf)->p, currentpc(cf), name);
    else
        return NULL;
}


static const char *getfuncname(toku_State *T, CallFrame *cf,
                                              const char **name) {
    if (cf != NULL && !(cf->status & CFST_TAIL))
        return funcnamefromcall(T, cf->prev, name);
    else
        return NULL;
}


/*
** Auxiliary to 'toku_getinfo', parses 'options' and fills out the
** 'toku_Debug' accordingly. If any invalid options is specified this
** returns 0.
*/
static int32_t auxgetinfo(toku_State *T, const char *options, Closure *cl,
                                         CallFrame *cf, toku_Debug *ar) {
    int32_t status = 1;
    for (; *options; options++) {
        switch (*options) {
            case 'n':
                ar->namewhat = getfuncname(T, cf, &ar->name);
                if (ar->namewhat == NULL) { /* not found ? */
                    ar->namewhat = "";
                    ar->name = NULL;
                }
                break;
            case 's':
                getfuncinfo(cl, ar);
                break;
            case 'l':
                ar->currline = (cf && isTokudae(cf)) ? getcurrentline(cf) : -1;
                break;
            case 'r':
                if (cf == NULL || !(cf->status & CFST_HOOKED))
                    ar->ftransfer = ar->ntransfer = 0;
                else {
                    ar->ftransfer = T->transferinfo.ftransfer;
                    ar->ntransfer = T->transferinfo.ntransfer;
                }
                break;
            case 'u':
                ar->nupvals = (cl == NULL) ? 0 : cl->c.nupvals;
                if (TokudaeClosure(cl)) {
                    ar->nparams = cl->t.p->arity;
                    ar->isvararg = cl->t.p->isvararg;
                } else { /* otherwise C function/closure */
                    ar->nparams = 0; /* no named parameters */
                    ar->isvararg = 1; /* always vararg */
                }
                break;
            case 't':
                if (cf != NULL) {
                    ar->istailcall = !!(cf->status & CFST_TAIL);
                    ar->extraargs = cast_i32(cf->extraargs);
                } else {
                    ar->istailcall = 0;
                    ar->extraargs = 0;
                }
                break;
            case 'f':
            case 'L': /* handled by 'toku_getinfo' */
                break;
            default: status = 0; /* invalid option */
        }
    }
    return status;
}


static int32_t nextline (const Proto *p, int32_t currline, int32_t pc) {
    if (p->lineinfo[pc] != ABSLINEINFO)
        return currline + p->lineinfo[pc];
    else
        return tokuD_getfuncline(p, pc);
}


static void collectvalidlines(toku_State *T, Closure *f) {
    if (!TokudaeClosure(f)) {
        setnilval(s2v(T->sp.p));
        api_inctop(T);
    } else {
        int32_t i;
        TValue v;
        const Proto *p = f->t.p;
        int32_t currline = p->defline;
        Table *t = tokuH_new(T); /* new table to store active lines */
        settval2s(T, T->sp.p, t); /* push it on stack */
        api_inctop(T);
        if (p->lineinfo != NULL) { /* have debug information? */
            setbtval(&v); /* bool 'true' to be the value of all indices */
            if (!p->isvararg) /* regular function? */
                i = 0; /* consider all opcodes */
            else { /* vararg function */
                toku_assert(p->code[0] == OP_VARARGPREP);
                currline = nextline(p, currline, 0);
                i = getopSize(OP_VARARGPREP); /* skip first opcode */
            }
            while (i < p->sizelineinfo) { /* for each opcode */
                currline = nextline(p, currline, i); /* get its line */
                tokuH_setint(T, t, currline, &v); /* table[line] = true */
                i += getopSize(p->code[i]); /* get next opcode */
            }
        }
    }
}


TOKU_API int32_t toku_getinfo(toku_State *T, const char *options,
                                             toku_Debug *ar) {
    CallFrame *cf;
    Closure *cl;
    TValue *func;
    int32_t status = 1;
    toku_lock(T);
    api_check(T, options != NULL, "'options' can't be NULL");
    if (*options == '>') {
        cf = NULL; /* not currently running */
        func = s2v(T->sp.p - 1);
        api_check(T, ttisfunction(func), "function expected");
        options++; /* skip '>' */
        T->sp.p--; /* pop function */
    } else {
        cf = ar->cf;
        func = s2v(cf->func.p);
        toku_assert(ttisfunction(func));
    }
    cl = ttisclosure(func) ? clval(func) : NULL;
    status = auxgetinfo(T, options, cl, cf, ar);
    if (strchr(options, 'f')) {
        setobj2s(T, T->sp.p, func);
        api_inctop(T);
    }
    if (strchr(options, 'L'))
        collectvalidlines(T, cl);
    toku_unlock(T);
    return status;
}


/*
** Set 'trap' for all active Tokudae frames.
** This function can be called during a signal, under "reasonable"
** assumptions. A new 'cf' is completely linked in the list before it
** becomes part of the "active" list, and we assume that pointers are
** atomic; see comment in next function.
** (A compiler doing interprocedural optimizations could, theoretically,
** reorder memory writes in such a way that the list could be temporarily
** broken while inserting a new element. We simply assume it has no good
** reasons to do that.)
*/
static void settraps(CallFrame *cf) {
    for (; cf != NULL; cf = cf->prev)
        if (isTokudae(cf))
            cf->u.t.trap = 1;
}


/*
** This function can be called during a signal, under "reasonable" assumptions.
** Fields 'basehookcount' and 'hookcount' (set by 'resethookcount')
** are for debug only, and it is no problem if they get arbitrary
** values (causes at most one wrong hook call). 'hookmask' is an atomic
** value. We assume that pointers are atomic too (e.g., gcc ensures that
** for all platforms where it runs). Moreover, 'hook' is always checked
** before being called (see 'tokuD_hook').
*/
TOKU_API void toku_sethook(toku_State *T, toku_Hook func, int32_t mask,
                                                          int32_t count) {
    if (func == NULL || mask == 0) { /* turn off hooks? */
        mask = 0;
        func = NULL;
    }
    T->hook = func;
    T->basehookcount = count;
    resethookcount(T);
    T->hookmask = cast_u8(mask);
    if (mask)
        settraps(T->cf); /* to trace inside 'tokuV_execute' */
}


TOKU_API toku_Hook toku_gethook(toku_State *T) {
    return T->hook;
}


TOKU_API int32_t toku_gethookmask(toku_State *T) {
    return T->hookmask;
}


TOKU_API int32_t toku_gethookcount(toku_State *T) {
    return T->basehookcount;
}


/* add usual debug information to 'msg' (source id and line) */
const char *tokuD_addinfo(toku_State *T, const char *msg, OString *src,
                                         int32_t line) {
    if (src == NULL) /* no debug information? */
        return tokuS_pushfstring(T, "?:?: %s", msg);
    else {
        char buff[TOKU_IDSIZE];
        tokuS_chunkid(buff, getstr(src), getstrlen(src));
        return tokuS_pushfstring(T, "%s:%d: %s", buff, line, msg);
    }
}


t_noret tokuD_errormsg(toku_State *T) {
    if (T->errfunc != 0) { /* is there an error handling function? */
        SPtr errfunc = restorestack(T, T->errfunc);
        toku_assert(ttisfunction(s2v(errfunc)));
        setobjs2s(T, T->sp.p, T->sp.p - 1); /* move argument */
        setobjs2s(T, T->sp.p - 1, errfunc); /* push function */
        T->sp.p++; /* assume EXTRA_STACK */
        tokuV_call(T, T->sp.p - 2, 1); /* call it */
    }
    if (ttisnil(s2v(T->sp.p - 1))) { /* error object is nil? */
        /* change it to a proper message */
        setstrval2s(T, T->sp.p - 1, tokuS_newlit(T, "<no error object>"));
    }
    tokuPR_throw(T, TOKU_STATUS_ERUNTIME);
}


/* generic runtime error */
t_noret tokuD_runerror(toku_State *T, const char *fmt, ...) {
    CallFrame *cf = T->cf;
    const char *err;
    va_list ap;
    tokuG_checkGC(T);
    va_start(ap, fmt);
    err = tokuS_pushvfstring(T, fmt, ap);
    va_end(ap);
    if (isTokudae(cf)) { /* can add source information? */
        tokuD_addinfo(T, err, cf_func(cf)->p->source, getcurrentline(cf));
        setobj2s(T, T->sp.p - 2, s2v(T->sp.p - 1)); /* remove 'err' */
        T->sp.p--;
    }
    tokuD_errormsg(T);
}


/*
** Check whether pointer 'o' points to some value in the stack frame of
** the current function and, if so, returns its index.  Because 'o' may
** not point to a value in this stack, we cannot compare it with the
** region boundaries (undefined behavior in ISO C).
*/
static int32_t isinstack(CallFrame *cf, const TValue *o) {
    SPtr base = cf->func.p + 1;
    for (int32_t pos = 0; base + pos < cf->top.p; pos++) {
        if (o == s2v(base + pos))
            return pos;
    }
    return -1; /* not found */
}


/*
** Checks whether value 'o' came from an upvalue.
*/
static const char *getupvalname(CallFrame *cf, const TValue *o,
                                               const char **name) {
    TClosure *cl = cf_func(cf);
    for (int32_t i = 0; i < cl->nupvals; i++) {
        if (cl->upvals[i]->v.p == o) {
            *name = upvalname(cl->p, i);
            return strupval;
        }
    }
    return NULL;
}


static const char *formatvarinfo(toku_State *T, const char *kind,
                                                const char *name) {
    if (kind == NULL)
        return ""; /* no information */
    else
        return tokuS_pushfstring(T, " (%s '%s')", kind, name);
}


/*
** Build a string with a "description" for the value 'o', such as
** "variable 'x'" or "upvalue 'y'".
*/
static const char *varinfo(toku_State *T, const TValue *o) {
    CallFrame *cf = T->cf;
    const char *name = NULL;  /* to avoid warnings */
    const char *kind = NULL;
    if (isTokudae(cf)) {
        kind = getupvalname(cf, o, &name);
        if (!kind) { /* not an upvalue? */
            int32_t sp = isinstack(cf, o); /* try a stack slot */
            if (sp >= 0) /* found? */
                kind = getobjname(cf_func(cf)->p, currentpc(cf), sp, &name);
        }
    }
    return formatvarinfo(T, kind, name);
}


/*
** Raise a generic type error.
*/
static t_noret typeerror(toku_State *T, const TValue *o, const char *op,
                                                         const char *extra) {
    const char *t = tokuTM_objtypename(T, o);
    int n = !strcmp(t, typename(TOKU_T_INSTANCE)) ||
            !strcmp(t, typename(TOKU_T_USERDATA));
    tokuD_runerror(T, "attempt to %s a%s %s value%s", op, n?"n":"", t, extra);
}


/*
** Raise a type error with "standard" information about the faulty
** object 'o' (using 'varinfo').
*/
t_noret tokuD_typeerror(toku_State *T, const TValue *o, const char *op) {
    typeerror(T, o, op, varinfo(T, o));
}


/*
** Raise type error for operation over integers and numbers.
*/
t_noret tokuD_opinterror(toku_State *T, const TValue *v1, const TValue *v2,
                                                          const char *msg) {
    if (!ttisnum(v1)) /* first operand is wrong? */
        v2 = v1; /* now second is wrong */
    tokuD_typeerror(T, v2, msg);
}


/*
** Error when value is convertible to numbers, but not integers.
*/
t_noret tokuD_tointerror(toku_State *T, const TValue *v1, const TValue *v2) {
    toku_Integer temp;
    if (!tokuO_tointeger(v1, &temp, N2IEQ))
        v2 = v1;
    tokuD_runerror(T, "number%s has no integer representation", varinfo(T,v2));
}


t_sinline int32_t differentclasses(const TValue *v1, const TValue *v2) {
    int32_t t1 = ttype(v1);
    return (t1 == ttype(v2) && t1 == TOKU_T_INSTANCE && /* instances with */
            (insval(v1)->oclass != insval(v2)->oclass)); /* class mismatch? */
}


static t_noret classerror(toku_State *T, const char *op) {
    tokuD_runerror(T, "attempt to %s instances of differing classes", op);
}


t_noret tokuD_classerror(toku_State *T, TM event) {
    classerror(T, eventname(T, event));
}


t_noret tokuD_binoperror(toku_State *T, const TValue *v1,
                         const TValue *v2, TM event) {
    switch (event) {
        case TM_BAND: case TM_BOR: case TM_BXOR:
        case TM_BSHL: case TM_BSHR: case TM_BNOT:
            if (ttisnum(v1) && ttisnum(v2))
                tokuD_tointerror(T, v1, v2);
            else
                tokuD_opinterror(T, v1, v2, "perform bitwise operation on");
            break; /* to avoid warnings */
        default: tokuD_opinterror(T, v1, v2, "perform arithmetic on");
    }
}


t_noret tokuD_ordererror(toku_State *T, const TValue *v1, const TValue *v2) {
    if (differentclasses(v1, v2))
        classerror(T, "compare");
    else {
        const char *t1 = tokuTM_objtypename(T, v1);
        const char *t2 = tokuTM_objtypename(T, v2);
        if (strcmp(t1, t2) == 0)
            tokuD_runerror(T, "attempt to compare two %s values", t1);
        else
            tokuD_runerror(T, "attempt to compare %s with %s", t1, t2);
    }
}


t_noret tokuD_concaterror(toku_State *T, const TValue *v1, const TValue *v2) {
    if (differentclasses(v1, v2))
        classerror(T, eventname(T, TM_CONCAT));
    else {
        if (ttisstring(v1)) v1 = v2;
        tokuD_typeerror(T, v1, "concatenate");
    }
}


/*
** Raise an error for calling a non-callable object. Try to find a name
** for the object based on how it was called ('funcnamefromcall'); if it
** cannot get a name there, try 'varinfo'.
*/
t_noret tokuD_callerror(toku_State *T, const TValue *o) {
    CallFrame *cf = T->cf;
    const char *name = NULL; /* to avoid warnings */
    const char *kind = funcnamefromcall(T, cf, &name);
    const char *extra = kind ? formatvarinfo(T, kind, name) : varinfo(T, o);
    typeerror(T, o, "call", extra);
}


t_noret tokuD_lfseterror(toku_State *T, int32_t lf) {
    OString *str = G(T)->listfields[lf];
    tokuD_runerror(T, "attempt to set immutable list field '%s'", getstr(str));
}


t_noret tokuD_indexboundserror(toku_State *T, List *l, const TValue *k) {
    toku_Integer i = (ttisstring(k)) ? gLF(strval(k)) : ival(k);
    const char *e = varinfo(T, k);
    tokuD_runerror(T, "list index '%I'%s out of bounds of range [0, %d]",
                       i, e, l->len);
}


t_noret tokuD_invindexerror(toku_State *T, const TValue *k) {
    const char *t = tokuTM_objtypename(T, k);
    const char *e = varinfo(T, k);
    tokuD_runerror(T, "%s%s is invalid list index", t, e);
}


t_noret tokuD_unknownlf(toku_State *T, const TValue *field) {
    const char *e = varinfo(T, field);
    tokuD_runerror(T, "unknown list field '%s'%s", getstr(strval(field)), e);
}


/*
** Check whether new opcode 'newpc' is in a different line from
** previous opcode 'oldpc'. More often than not, 'newpc' is only
** one or a few opcodes after 'oldpc' (it must be after, see
** caller), so try to avoid calling 'tokuD_getfuncline'. If they are
** too far apart, there is a good chance of a ABSLINEINFO in the way,
** so it goes directly to 'tokuD_getfuncline'.
*/
static int32_t changedline(const Proto *p, int32_t oldpc, int32_t newpc) {
    if (p->lineinfo == NULL) /* no debug information? */
        return 0;
    /* opcodes are not too far apart? */
    if (nopcode(p, newpc) - nopcode(p, oldpc) < MAXOWTHABS / 2) {
        int32_t delta = 0; /* line difference */
        int32_t pc = oldpc;
        for (;;) {
            int32_t lineinfo;
            pc += getopSize(p->code[pc]);
            lineinfo = p->lineinfo[pc];
            if (lineinfo == ABSLINEINFO)
                break; /* cannot compute delta; fall through */
            delta += lineinfo;
            if (pc == newpc)
                return (delta != 0); /* delta computed successfully */
        }
    }
    /* either opcodes are too far apart or there is an absolute line
       info in the way; compute line difference explicitly */
    return (tokuD_getfuncline(p, oldpc) != tokuD_getfuncline(p, newpc));
}


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'T->hook' and 'T->hookmask', which trigger this
** function, can be changed asynchronously by signals.)
*/
void tokuD_hook(toku_State *T, int32_t event, int32_t line,
                               int32_t ftransfer, int32_t ntransfer) {
    toku_Hook hook = T->hook;
    if (hook && T->allowhook) { /* make sure there is a hook */
        CallFrame *cf = T->cf;
        ptrdiff_t sp = savestack(T, T->sp.p); /* preserve original 'sp' */
        ptrdiff_t cf_top = savestack(T, cf->top.p); /* idem for 'cf->top' */
        toku_Debug ar = { .event = event, .currline = line, .cf = cf };
        T->transferinfo.ftransfer = ftransfer;
        T->transferinfo.ntransfer = ntransfer;
        tokuPR_checkstack(T, TOKU_MINSTACK); /* ensure minimum stack size */
        if (cf->top.p < T->sp.p + TOKU_MINSTACK)
            cf->top.p = T->sp.p + TOKU_MINSTACK;
        T->allowhook = 0; /* cannot call hooks inside a hook */
        cf->status |= CFST_HOOKED;
        toku_unlock(T);
        (*hook)(T, &ar); /* call hook function */
        toku_lock(T);
        toku_assert(!T->allowhook);
        T->allowhook = 1; /* hook finished; once again enable hooks */
        T->sp.p = restorestack(T, sp);
        cf->top.p = restorestack(T, cf_top);
        cf->status &= cast_u8(~CFST_HOOKED);
    }
}


/*
** Executes a call hook for Tokudae functions. This function is called
** whenever 'hookmask' is not zero, so it checks whether call hooks are
** active.
*/
void tokuD_hookcall(toku_State *T, CallFrame *cf) {
    T->oldpc = 0; /* set 'oldpc' for new function */
    if (T->hookmask & TOKU_MASK_CALL) { /* is call hook on? */
        int32_t event = (cf->status & CFST_TAIL) ? TOKU_HOOK_TAILCALL
                                                 : TOKU_HOOK_CALL;
        cf->u.t.savedpc++; /* hooks assume pc is already incremented */
        tokuD_hook(T, event, -1, 0, cf_func(cf)->p->arity);
        cf->u.t.savedpc--; /* correct pc */
    }
}


/*
** Traces Tokudae calls. If code is running the first opcode of
** a function, and function is not vararg, calls 'tokuD_hookcall'
** (Vararg functions will call 'tokuD_hookcall' after adjusting its
** variable arguments; otherwise, they could call a line/count hook
** before the call hook when fetching the first opcode.)
*/
int32_t tokuD_tracecall(toku_State *T) {
    CallFrame *cf = T->cf;
    Proto *p = cf_func(cf)->p;
    cf->u.t.trap = 1; /* ensure hooks will be checked */
    if (cf->u.t.savedpc == p->code) { /* not returning? */
        if (p->isvararg) /* vararg function? */
            return 0; /* hooks will start at VARARGPREP opcode */
        else /* otherwise check 'call' hook */
            tokuD_hookcall(T, cf);
    }
    return 1; /* keep 'trap' on */
}


/*
** Traces the execution of a Tokudae function. Called before the execution
** of each opcode, when debug is on. 'T->oldpc' stores the last
** opcode traced, to detect line changes. When entering a new
** function, 'npci' will be zero and will test as a new line whatever
** the value of 'oldpc'. Some exceptional conditions may return to
** a function without setting 'oldpc'. In that case, 'oldpc' may be
** invalid; if so, use zero as a valid value.
** (A wrong but valid 'oldpc' at most causes an extra call to a line hook.)
** WARNING: This function should correct 'T->sp.p' before calling anything
** that can run the GC or raise errors.
*/
int32_t tokuD_traceexec(toku_State *T, const uint8_t *pc, ptrdiff_t stacksz) {
    CallFrame *cf = T->cf;
    const Proto *p = cf_func(cf)->p;
    uint8_t mask = cast_u8(T->hookmask);
    int32_t counthook;
    if (!(mask & (TOKU_MASK_LINE | TOKU_MASK_COUNT))) { /* no hooks? */
        cf->u.t.trap = 0; /* don't need to stop again */
        return 0; /* turn off 'trap' */
    }
    cf->u.t.savedpc = pc + 1; /* hooks assume current opcode is consumed */
    counthook = (mask & TOKU_MASK_COUNT) && (--T->hookcount == 0);
    if (counthook) { /* opcode count hook is on and count reached 0? */
        resethookcount(T); /* reset count */
        T->sp.p = cf->func.p + 1 + stacksz; /* save 'sp' */
        tokuD_hook(T, TOKU_HOOK_COUNT, -1, 0, 0); /* call count hook */
    } else if (!(mask & TOKU_MASK_LINE))
        return 1; /* no line hook and count != 0; nothing to be done now */
    if (mask & TOKU_MASK_LINE) { /* line hook? */
        /* 'T->oldpc' may be invalid; use zero in this case */
        int32_t oldpc = (T->oldpc < p->sizecode) ? T->oldpc : 0;
        int32_t npci = cast_i32(pc - p->code);
        if (npci <= oldpc || /* call hook when jump back (loop), */
                changedline(p, oldpc, npci)) { /* or when enter new line */
            int32_t newline = tokuD_getfuncline(p, npci);
            T->sp.p = cf->func.p + 1 + stacksz; /* save 'sp' */
            tokuD_hook(T, TOKU_HOOK_LINE, newline, 0, 0); /* call line hook */
        }
        T->oldpc = npci; /* 'pc' of last call to line hook */
    }
    return 1; /* keep 'trap' on */
}
