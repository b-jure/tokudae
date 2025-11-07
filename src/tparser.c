/*
** @ID: src/tparser.c
** Tokudae Parser
** See Copyright Notice in tokudae.h
*/

#define tparser_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <string.h>

#include "tcode.h"
#include "tfunction.h"
#include "tgc.h"
#include "tlexer.h"
#include "tokudaelimits.h"
#include "tmem.h"
#include "tobject.h"
#include "tobject.h"
#include "tparser.h"
#include "tokudaeconf.h"
#include "tstate.h"
#include "tstring.h"
#include "ttable.h"
#include "tvm.h"


/* check if 'tok' matches current token */
#define check(lx,tok)       ((lx)->t.tk == (tok))


/* macros for controlling recursion depth */
#define enterCstack(lx)     tokuT_incCstack((lx)->T)
#define leaveCstack(lx)     ((lx)->T->nCcalls--)


/* macros for 'lastisend' in function state */
#define stmIsReturn(fs)     ((fs)->lastisend == 1)
#define stmIsBreak(fs)      ((fs)->lastisend == 2)
#define stmIsContinue(fs)   ((fs)->lastisend == 3)
#define stmIsEnd(fs)        ((fs)->lastisend)


/* expect 'cond' to be true or invoke error */
#define expect_cond(lx, cond, err) \
    { if (t_unlikely(!(cond))) tokuY_syntaxerror(lx, err); }


/* 'cfmask' */
#define CFM_LOOP        (1 << 0) /* regular loop */
#define CFM_DOWHILE     (1 << 1) /* do/while loop */
#define CFM_GENLOOP     (1 << 2) /* generic loop */
#define CFM_SWITCH      (1 << 3) /* switch */

#define CFM_MASK    (CFM_LOOP | CFM_DOWHILE | CFM_GENLOOP | CFM_SWITCH)

#define isdowhile(s)            (testbits((s)->cfmask, CFM_DOWHILE) != 0)
#define isgenloop(s)            (testbits((s)->cfmask, CFM_GENLOOP) != 0)
#define haspendingjumps(s)      (testbits((s)->cfmask, CFM_MASK) != 0)


/* lexical scope information */
typedef struct Scope {
    struct Scope *prev; /* implicit linked-list */
    int32_t nactlocals; /* number of locals outside of this scope */
    int32_t depth; /* scope depth (number of nested scopes) */
    int32_t firstgoto; /* index of first pending goto jump in this block */
    uint8_t cfmask; /* control flow mask */
    uint8_t haveupval; /* set if scope contains upvalue variable */
    uint8_t havetbcvar; /* set if scope contains to-be-closed variable */
} Scope;


/* state for class declarations/definitions */
typedef struct ClassState {
    struct ClassState *prev; /* chain of nested declarations */
    int32_t pc; /* pc of the NEWCLASS opcode (for private upvalues) */
} ClassState;


/* state for loop statements */
typedef struct LoopState {
    struct LoopState *prev; /* chain */
    Scope s; /* loop scope */
    int32_t pcloop; /* loop start pc */
} LoopState;


/* 
** Snapshot of function state.
** (Used primarily for optimizations, e.g., trimming dead code.)
** Does not snapshot or load the 'nk' as it might interfere with
** the 'kcache'.
*/
typedef struct FuncContext {
    int32_t dyd_actlocals;
    int32_t pcdo;
    int32_t prevpc;
    int32_t prevline;
    int32_t sp;
    int32_t nactlocals;
    int32_t np;
    int32_t pc;
    int32_t nabslineinfo;
    int32_t nopcodepc;
    int32_t nlocals;
    int32_t nupvals;
    int32_t lasttarget;
    int32_t lastgoto; /* last pending goto in 'gt' */
    uint8_t ismethod;
    uint8_t nonilmerge;
    uint8_t iwthabs;
    uint8_t needclose;
    uint8_t callcheck;
    uint8_t lastisend;
} FuncContext;


static void storecontext(FunctionState *fs, FuncContext *ctx) {
    ctx->dyd_actlocals = fs->lx->dyd->actlocals.len;
    ctx->pcdo = fs->pcdo;
    ctx->prevpc = fs->prevpc;
    ctx->prevline = fs->prevline;
    ctx->sp = fs->sp;
    ctx->nactlocals = fs->nactlocals;
    ctx->np = fs->np;
    ctx->pc = currPC;
    ctx->nabslineinfo = fs->nabslineinfo;
    ctx->nopcodepc = fs->nopcodepc;
    ctx->nlocals = fs->nlocals;
    ctx->nupvals = fs->nupvals;
    ctx->lasttarget = fs->lasttarget;
    ctx->lastgoto = fs->lx->dyd->gt.len;
    ctx->ismethod = fs->ismethod;
    ctx->nonilmerge = fs->nonilmerge;
    ctx->iwthabs = fs->iwthabs;
    ctx->needclose = fs->needclose;
    ctx->callcheck = fs->callcheck;
    ctx->lastisend = fs->lastisend;
}


static void loadcontext(FunctionState *fs, FuncContext *ctx) {
    fs->lx->dyd->actlocals.len = ctx->dyd_actlocals;
    fs->pcdo = ctx->pcdo;
    fs->prevpc = ctx->prevpc;
    fs->prevline = ctx->prevline;
    fs->sp = ctx->sp;
    fs->nactlocals = ctx->nactlocals;
    fs->np = ctx->np;
    currPC = ctx->pc;
    fs->nabslineinfo = ctx->nabslineinfo;
    fs->nopcodepc = ctx->nopcodepc;
    fs->nlocals = ctx->nlocals;
    fs->nupvals = ctx->nupvals;
    fs->lasttarget = ctx->lasttarget;
    fs->lx->dyd->gt.len = ctx->lastgoto;
    fs->ismethod = ctx->ismethod;
    fs->nonilmerge = ctx->nonilmerge;
    fs->iwthabs = ctx->iwthabs;
    fs->needclose = ctx->needclose;
    fs->callcheck = ctx->callcheck;
    fs->lastisend = ctx->lastisend;
}


/* TODO: use the previous token if the previous token and current
** token are not on the same line. */
/* XXX: also maybe use 'after' instead of 'near' if we are using the
** previous token. */
static t_noret expecterror(Lexer *lx, int32_t tk) {
    const char *err = tokuS_pushfstring(lx->T, "expected %s",
                                                tokuY_tok2str(lx, tk));
    tokuY_syntaxerror(lx, err);
}


static t_noret limiterror(FunctionState *fs, const char *what, int32_t limit) {
    toku_State *T = fs->lx->T;
    int32_t linenum = fs->p->defline;
    const char *where = (linenum == 0 ? "main function" :
                         tokuS_pushfstring(T, "function at line %d", linenum));
    const char *err = tokuS_pushfstring(T, "too many %s (limit is %d) in %s",
                                          what, limit, where);
    tokuY_syntaxerror(fs->lx, err);
}


/* 
** Advance scanner if 'tk' matches the current token,
** otherwise return 0. 
*/
static int32_t match(Lexer *lx, int32_t tk) {
    if (check(lx, tk)) {
        tokuY_scan(lx);
        return 1;
    }
    return 0;
}


/* check if 'tk' matches the current token if not invoke error */
static void expect(Lexer *lx, int32_t tk) {
    if (t_unlikely(!check(lx, tk)))
        expecterror(lx, tk);
}


/* same as 'expect' but this also advances the scanner */
static void expectnext(Lexer *lx, int32_t tk) {
    expect(lx, tk);
    tokuY_scan(lx);
}


/*
** Check that next token is 'what'. 
** Otherwise raise an error that the expected 'what' should 
** match a 'who' in line 'linenum'.
*/
static void expectmatch(Lexer *lx, int32_t what, int32_t who,
                                                 int32_t linenum) {
    if (t_unlikely(!match(lx, what))) {
        if (lx->line == linenum) /* same line? */
            expecterror(lx, what); /* emit usual error message */
        else /* otherwise spans across multiple lines */
            tokuY_syntaxerror(lx, tokuS_pushfstring(lx->T,
                    "expected %s (to close %s at line %d)",
                    tokuY_tok2str(lx, what), tokuY_tok2str(lx, who), linenum));
    }
}


static const char *errstmname(Lexer *lx, const char *err) {
    const char *stm;
    switch (lx->fs->lastisend) {
        case 1: stm = "return"; break;
        case 2: stm = "break"; break;
        case 3: stm = "continue"; break;
        default: return err;
    }
    return tokuS_pushfstring(lx->T,
            "%s ('%s' must be the last statement in this block)", err, stm);
}


static t_noret expecterrorblk(Lexer *lx) {
    const char *err = tokuS_pushfstring(lx->T,
                        "expected %s", tokuY_tok2str(lx, '}'));
    err = errstmname(lx, err);
    tokuY_syntaxerror(lx, err);
}


/*
** Similar to 'expectmatch' but this is invoked only
** when 'blockstm' expects delimiter '}' which is missing.
*/
static void expectmatchblk(Lexer *lx, int32_t linenum) {
    if (t_unlikely(!match(lx, '}'))) {
        if (lx->line == linenum)
            expecterrorblk(lx);
        else {
            const char *err = tokuS_pushfstring(lx->T,
                    "expected %s to close %s at line %d",
                    tokuY_tok2str(lx, '}'), tokuY_tok2str(lx, '{'), linenum);
            tokuY_syntaxerror(lx, errstmname(lx, err));
        }
    }
}


static OString *str_expectname(Lexer *lx) {
    OString *s;
    expect(lx, TK_NAME);
    s = lx->t.lit.str;
    tokuY_scan(lx);
    return s;
}


/*
** Semantic error; variant of syntax error without 'near <token>'.
*/
t_noret tokuP_semerror(Lexer *lx, const char *err) {
    lx->t.tk = 0;
    tokuY_syntaxerror(lx, err);
}


void tokuP_checklimit(FunctionState *fs, int32_t n, int32_t limit,
                                                    const char *what) {
    if (t_unlikely(n >= limit))
        limiterror(fs, what, limit);
}


/* get local variable, 'vidx' is compiler index */
static LVar *getlocalvar(FunctionState *fs, int32_t vidx) {
    return &fs->lx->dyd->actlocals.arr[fs->firstlocal + vidx];
}


/* get local variable debug information, 'vidx' is compiler index */
static LVarInfo *getlocalinfo(FunctionState *fs, int32_t vidx) {
    LVar *lv = check_exp(vidx <= fs->nactlocals, getlocalvar(fs, vidx));
    toku_assert(lv->s.pidx >= 0 && lv->s.pidx < fs->nlocals);
    return &fs->p->locals[lv->s.pidx];
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** stack level.
*/
static int32_t stacklevel(FunctionState *fs, int32_t nvar) {
    if (nvar-- > 0) /* have at least one variable? */
        return getlocalvar(fs, nvar)->s.sidx + 1;
    return 0; /* no variables on stack */
}


/*
** Return number of variables on the stack for the given
** function.
*/
static int32_t nvarstack(FunctionState *fs) {
    return stacklevel(fs, fs->nactlocals);
}


static void contadjust(FunctionState *fs, int32_t push) {
    int32_t ncntl = isgenloop(&fs->ls->s) * VAR_N;
    int32_t total = (fs->nactlocals - fs->ls->s.nactlocals - ncntl);
    tokuC_adjuststack(fs, push ? -total : total);
}


/*
** Adds a new 'break' jump into the goto list.
*/
static int32_t newbreakjump(Lexer *lx, int32_t pc, int32_t bk, int32_t close) {
    GotoList *gl = &lx->dyd->gt;
    int32_t n = gl->len;
    tokuM_growarray(lx->T, gl->arr, gl->size, n, INT32_MAX, "pending jumps",
                           Goto);
    gl->arr[n].pc = pc;
    gl->arr[n].nactlocals = lx->fs->nactlocals;
    gl->arr[n].close = cast_u8(close);
    gl->arr[n].bk = cast_u8(bk);
    gl->len = n + 1;
    return n;
}


/* 
** Get the most recent control flow scope, or NULL if none
** present.
*/
static const Scope *getcfscope(const FunctionState *fs) {
    const Scope *s = NULL;
    if (fs->switchscope)
        s = fs->switchscope;
    if (fs->ls && (!s || s->depth < fs->ls->s.depth))
        s = &fs->ls->s;
    toku_assert(!s || haspendingjumps(s));
    return s;
}


/*
** Add new pending (break or continue in generic loop) jump to the goto list.
** This language construct is coded as POP followed by a JMP.
** If close is needed, sequence of opcodes is CLOSE->POP->JMP.
** If the pending jump is continue inside of generic loop then only JMP
** is emitted as the popping and close is managed by 'continuestm'.
*/
static int32_t newpendingjump(Lexer *lx, int32_t bk, int32_t close,
                                                     int32_t nvars) {
    FunctionState *fs = lx->fs;
    int32_t pc;
    toku_assert(getopSize(OP_JMP) == getopSize(OP_POP));
    toku_assert(getopSize(OP_JMP) == getopSize(OP_CLOSE));
    if (close) { /* needs close? */
        const Scope *cfs = getcfscope(fs);
        toku_assert(cfs != NULL);
        tokuC_emitIL(fs, OP_CLOSE, stacklevel(fs, cfs->nactlocals));
    }
    if (bk) { /* break statement? */
        toku_assert(nvars >= 0);
        tokuC_adjuststack(fs, nvars);
    }
    pc = tokuC_jmp(fs, OP_JMP);
    return newbreakjump(lx, pc, bk, close);
}


/* 
** Remove local variables up to specified level.
*/
static void removelocals(FunctionState *fs, int32_t tolevel) {
    fs->lx->dyd->actlocals.len -= fs->nactlocals - tolevel;
    toku_assert(fs->lx->dyd->actlocals.len >= 0);
    while (fs->nactlocals > tolevel) /* set debug information */
        getlocalinfo(fs, --fs->nactlocals)->endpc = currPC;
}


/*
** Patch pending goto jumps ('break' or 'continue' in generic loop).
** NOTE: 'continue' might be jumping to optimized out false condition in
** do/while loop, so if there is no code after 'continue' (not including
** the optimized out 'while' condition), then this jump is invalid.
** However, this is not possible as all Tokudae chunks end with RETURN
** opcode even when the chunk is missing explicit 'return'.
*/
static void patchpendingjumps(FunctionState *fs, Scope *s) {
    Lexer *lx = fs->lx;
    GotoList *gl = &lx->dyd->gt;
    int32_t igt = s->firstgoto; /* first goto in the finishing block */
    toku_assert(haspendingjumps(s));
    while (igt < gl->len) {
        Goto *gt = &gl->arr[igt];
        if (gt->bk) /* 'break' ? */
            tokuC_patchtohere(fs, gt->pc);
        else { /* otherwise 'continue' in generic or do/while loop */
            toku_assert(fs->ls && fs->ls->pcloop != NOPC);
            toku_assert(s == &fs->ls->s && (isgenloop(s) || isdowhile(s)));
            toku_assert(!gt->close);
            tokuC_patch(fs, gt->pc, fs->ls->pcloop);
        }
        igt++; /* get next */
    }
    lx->dyd->gt.len = s->firstgoto; /* remove pending goto jumps */
}


/* init expression with generic information */
static void initexp(ExpInfo *e, expt et, int32_t info) {
    e->t = e->f = NOJMP;
    e->et = et;
    e->u.info = info;
}


#define voidexp(e)      initexp(e, EXP_VOID, 0)


/* 'voidexp' but in C99 initializer syntax */
#define INIT_EXP        { .et = EXP_VOID, .t = NOJMP, .f = NOJMP }


static void initvar(FunctionState *fs, ExpInfo *e, int32_t vidx) {
    e->t = e->f = NOJMP;
    e->et = EXP_LOCAL;
    e->u.var.vidx = vidx;
    e->u.var.sidx = getlocalvar(fs, vidx)->s.sidx;
}


static void initstring(ExpInfo *e, OString *s) {
    e->f = e->t = NOJMP;
    e->et = EXP_STRING;
    e->u.str = s;
}


/* add local debug information into 'locals' */
static int32_t registerlocal(Lexer *lx, FunctionState *fs, OString *name) {
    Proto *p = fs->p;
    int32_t osz = p->sizelocals;
    tokuM_growarray(lx->T, p->locals, p->sizelocals, fs->nlocals, MAXVARS,
                    "locals", LVarInfo);
    while (osz < p->sizelocals)
        p->locals[osz++].name = NULL;
    p->locals[fs->nlocals].name = name;
    p->locals[fs->nlocals].startpc = currPC;
    tokuG_objbarrier(lx->T, p, name);
    return fs->nlocals++;
}


/*
** Adjust locals by increment 'nvars' and register them
** inside 'locals'.
*/
static void adjustlocals(Lexer *lx, int32_t nvars) {
    FunctionState *fs = lx->fs;
    int32_t stacklevel = nvarstack(fs);
    for (int32_t i = 0; i < nvars; nvars--) {
        int32_t vidx = fs->nactlocals++;
        LVar *lvar = getlocalvar(fs, vidx);
        lvar->s.sidx = stacklevel++;
        lvar->s.pidx = registerlocal(lx, fs, lvar->s.name);
    }
}


static void enterscope(FunctionState *fs, Scope *s, int32_t mask) {
    s->cfmask = cast_u8(mask);
    if (fs->scope) { /* not a global scope? */
        s->depth = fs->scope->depth + 1;
        s->havetbcvar = fs->scope->havetbcvar;
    } else { /* global scope */
        s->depth = 0;
        s->havetbcvar = 0;
    }
    s->nactlocals = fs->nactlocals;
    s->firstgoto = fs->lx->dyd->gt.len;
    s->haveupval = 0;
    s->prev = fs->scope;
    fs->scope = s;
}


static void leavescope(FunctionState *fs) {
    Scope *s = fs->scope;
    int32_t stklevel = stacklevel(fs, s->nactlocals);
    int32_t nvalues = fs->nactlocals - s->nactlocals;
    if (s->prev && s->haveupval) /* need a 'close'? */
        tokuC_emitIL(fs, OP_CLOSE, stklevel);
    removelocals(fs, s->nactlocals); /* remove scope locals */
    toku_assert(s->nactlocals == fs->nactlocals);
    if (s->prev) /* not main chunk scope? */
        tokuC_pop(fs, nvalues); /* pop locals */
    if (haspendingjumps(s)) /* might have pending jumps? */
        patchpendingjumps(fs, s); /* patch them */
    fs->scope = s->prev; /* go back to the previous scope (if any) */
}


/* 
** Mark scope where variable at compiler index 'level' was defined
** in order to emit close opcode before the scope gets closed.
*/
static void scopemarkupval(FunctionState *fs, int32_t level) {
    Scope *s = fs->scope;
    while (s->nactlocals > level)
        s = s->prev;
    s->haveupval = 1;
    fs->needclose = 1;
}


/* 
** Mark current scope as scope that has a to-be-closed
** variable.
*/
static void scopemarkclose(FunctionState *fs) {
    Scope *s = fs->scope;
    s->haveupval = 1;
    s->havetbcvar = 1;
    fs->needclose = 1;
}


static void open_func(Lexer *lx, FunctionState *fs, Scope *s) {
    toku_State *T = lx->T;
    Proto *p = fs->p;
    toku_assert(p != NULL);
    fs->prev = lx->fs;
    fs->lx = lx;
    lx->fs = fs;
    fs->prevline = p->defline;
    fs->firstlocal = lx->dyd->actlocals.len;
    fs->pcdo = NOPC;
    p->source = lx->src;
    tokuG_objbarrier(T, p, p->source);
    p->maxstack = 2; /* stack slots 0/1 are always valid */
    fs->kcache = tokuH_new(T); /* create table for function */
    settval2s(T, T->sp.p, fs->kcache); /* anchor it */
    tokuT_incsp(T);
    enterscope(fs, s, 0); /* start top-level scope */
}


static void close_func(Lexer *lx) {
    FunctionState *fs = lx->fs;
    toku_State *T = lx->T;
    Proto *p = fs->p;
    toku_assert(fs->scope && !fs->scope->prev); /* this is the last scope */
    leavescope(fs); /* end final scope */
    if (!stmIsReturn(fs)) /* function missing final return? */
        tokuC_return(fs, nvarstack(fs), 0); /* add implicit return */
    tokuC_finish(fs); /* final code adjustments */
    /* shrink unused memory */
    tokuM_shrinkarray(T, p->p, p->sizep, fs->np, Proto *);
    tokuM_shrinkarray(T, p->k, p->sizek, fs->nk, TValue);
    tokuM_shrinkarray(T, p->code, p->sizecode, currPC, uint8_t);
    tokuM_shrinkarray(T, p->lineinfo, p->sizelineinfo, currPC, int8_t);
    tokuM_shrinkarray(T, p->abslineinfo, p->sizeabslineinfo, fs->nabslineinfo,
                         AbsLineInfo);
    tokuM_shrinkarray(T, p->opcodepc, p->sizeopcodepc, fs->nopcodepc, int32_t);
    tokuM_shrinkarray(T, p->locals, p->sizelocals, fs->nlocals, LVarInfo);
    tokuM_shrinkarray(T, p->upvals, p->sizeupvals, fs->nupvals, UpValInfo);
    lx->fs = fs->prev; /* go back to enclosing function (if any) */
    T->sp.p--; /* pop kcache table */
    tokuG_checkGC(T); /* try to collect garbage memory */
}


/* add function prototype */
static Proto *addproto(Lexer *lx) {
    toku_State *T = lx->T;
    FunctionState *fs = lx->fs;
    Proto *p = fs->p;
    Proto *clp; /* closure prototype */
    if (fs->np >= p->sizep) {
        int32_t osz = p->sizep;
        tokuM_growarray(T, p->p, p->sizep, fs->np, MAX_ARG_L, "functions",
                           Proto *);
        while (osz < p->sizep)
            p->p[osz++] = NULL;
    }
    p->p[fs->np++] = clp = tokuF_newproto(T);
    tokuG_objbarrier(T, p, clp);
    return clp;
}


/* set current function as vararg */
static void setvararg(FunctionState *fs, int32_t arity) {
    fs->p->isvararg = 1;
    tokuC_emitIL(fs, OP_VARARGPREP, arity);
}


/* forward declare (can be both part of statement and expression) */
static void localstm(Lexer *lx);
static void funcbody(Lexer *lx, ExpInfo *v, int32_t linenum, int32_t ismethod,
                                                             int32_t del);


/* forward declare recursive non-terminals */
static void decl(Lexer *lx);
static void stm(Lexer *lx);
static void expr(Lexer *lx, ExpInfo *e);


/* adds local variable to the 'actlocals' */
static int32_t addlocal(Lexer *lx, OString *name, int32_t linenum) {
    FunctionState *fs = lx->fs;
    DynData *dyd = lx->dyd;
    LVar *local;
    tokuP_checklimit(fs, dyd->actlocals.len + 1 - fs->firstlocal, MAXVARS,
                         "locals");
    tokuM_growarray(lx->T, dyd->actlocals.arr, dyd->actlocals.size,
                           dyd->actlocals.len, INT32_MAX, "locals", LVar);
    local = &dyd->actlocals.arr[dyd->actlocals.len++];
    local->s.kind = VARREG;
    local->s.pidx = -1; /* mark uninitialized */
    local->s.linenum = linenum;
    local->s.name = name;
    return dyd->actlocals.len - fs->firstlocal - 1;
}


#define addlocallitln(lx,lit,line) \
        addlocal(lx, tokuY_newstring(lx, "" lit, LL(lit)), line)

#define addlocallit(lx,lit)     addlocallitln(lx, lit, lx->line)


/*
** Searches for local variable 'name'.
*/
static int32_t searchlocal(FunctionState *fs, OString *name, ExpInfo *v,
                                                             int32_t lim) {
    for (int32_t i = fs->nactlocals - 1; 0 <= i && lim < i; i--) {
        LVar *lvar = getlocalvar(fs, i);
        if (eqstr(name, lvar->s.name)) { /* found? */
            initvar(fs, v, i);
            return EXP_LOCAL;
        }
    }
    return -1; /* not found */
}


/* allocate space for new 'UpValInfo' */
static UpValInfo *newupvalue(FunctionState *fs) {
    Proto *p = fs->p;
    int32_t osz = p->sizeupvals;
    tokuP_checklimit(fs, fs->nupvals + 1, MAXUPVAL, "upvalues");
    tokuM_growarray(fs->lx->T, p->upvals, p->sizeupvals, fs->nupvals,
                    MAXUPVAL, "upvalues", UpValInfo);
    while (osz < p->sizeupvals)
        p->upvals[osz++].name = NULL;
    return &p->upvals[fs->nupvals++];
}


/* add new upvalue 'name' into 'upvalues' */
static int32_t addupvalue(FunctionState *fs, OString *name, ExpInfo *v) {
    UpValInfo *uv = newupvalue(fs);
    FunctionState *prev = fs->prev;
    if (v->et == EXP_LOCAL) { /* local? */
        uv->instack = 1;
        uv->idx = v->u.var.sidx;
        uv->kind = getlocalvar(prev, v->u.var.vidx)->s.kind;
        toku_assert(eqstr(name, getlocalvar(prev, v->u.var.vidx)->s.name));
    } else { /* must be upvalue */
        toku_assert(v->et == EXP_UVAL);
        uv->instack = 0;
        uv->idx = cast_u8(v->u.info);
        uv->kind = prev->p->upvals[v->u.info].kind;
        toku_assert(eqstr(name, prev->p->upvals[v->u.info].name));
    }
    uv->name = name;
    tokuG_objbarrier(fs->lx->T, fs->p, name);
    return fs->nupvals - 1;
}


/* searches for upvalue 'name' */
static int32_t searchupvalue(FunctionState *fs, UpValInfo *up, OString *name) {
    for (int32_t i = 0; i < fs->nupvals; i++)
        if (eqstr(up[i].name, name)) 
            return i;
    return -1; /* not found */
}


/*
** Find a variable with the given name. If it is upvalue add this upvalue
** into all intermediate functions. If it is not found, set 'var' as EXP_VOID.
*/
static void varaux(FunctionState *fs, OString *name, ExpInfo *var,
                                                     int32_t base) {
    if (fs == NULL) /* last scope? */
        voidexp(var); /* not found */
    else { /* otherwise search locals/upvalues */
        if (searchlocal(fs, name, var, -1) == EXP_LOCAL) { /* local found? */
            if (!base) /* in recursive call to 'varaux'? */
                scopemarkupval(fs, var->u.var.vidx); /* use local as upvalue */
        } else { /* not found as local at current level; try upvalues */
            int32_t idx = searchupvalue(fs, fs->p->upvals, name);
            if (idx < 0) { /* upvalue not found? */
                varaux(fs->prev, name, var, 0); /* try upper levels */
                if (var->et == EXP_LOCAL || var->et == EXP_UVAL) /* found? */
                    idx = addupvalue(fs, name, var); /* add new upvalue */
                else /* it is global */
                    return; /* done */
            }
            initexp(var, EXP_UVAL, idx); /* new or old upvalue */
        }
    }
}


static void expname(Lexer *lx, ExpInfo *e) {
    initstring(e, str_expectname(lx));
}


/* find variable 'name' */
static void var(Lexer *lx, OString *varname, ExpInfo *var) {
    FunctionState *fs= lx->fs;
    varaux(lx->fs, varname, var, 1);
    if (var->et == EXP_VOID) {
        ExpInfo key;
        varaux(fs, lx->envn, var, 1); /* get environment variable */
        toku_assert(var->et != EXP_VOID); /* this one must exist */
        tokuC_exp2stack(fs, var); /* put env on stack */
        initstring(&key, varname); /* key is variable name */
        tokuC_indexed(fs, var, &key, 0); /* env[varname] */
    }
}


#define varlit(lx,l,e)      var(lx, tokuY_newstring(lx, "" l, LL(l)), e)


/* =======================================================================
**                              EXPRESSIONS
** ======================================================================= */

static int32_t explist(Lexer *lx, ExpInfo *e) {
    int32_t n = 1;
    expr(lx, e);
    while (match(lx, ',')) {
        tokuC_exp2stack(lx->fs, e);
        expr(lx, e);
        n++;
    }
    return n;
}


static void indexed(Lexer *lx, ExpInfo *var, int32_t super) {
    ExpInfo key = INIT_EXP;
    tokuY_scan(lx); /* skip '[' */
    tokuC_exp2stack(lx->fs, var);
    expr(lx, &key);
    tokuC_indexed(lx->fs, var, &key, super);
    expectnext(lx, ']');
}


static void getdotted(Lexer *lx, ExpInfo *v, int32_t super) {
    ExpInfo key = INIT_EXP;
    tokuY_scan(lx); /* skip '.' */
    tokuC_exp2stack(lx->fs, v);
    expname(lx, &key);
    tokuC_getdotted(lx->fs, v, &key, super);
}


static void superkw(Lexer *lx, ExpInfo *e) {
    FunctionState *fs = lx->fs;
    if (t_unlikely(!fs->cs))
        tokuP_semerror(lx, "'super' usage outside of a class definition");
    else if (t_unlikely(!fs->ismethod))
        tokuP_semerror(lx, "'super' usage outside of a class (meta)method");
    tokuY_scan(lx); /* skip 'super' */
    varlit(lx, "self", e); /* get instance */
    toku_assert(e->et == EXP_LOCAL); /* (must be a local variable) */
    tokuC_exp2stack(fs, e); /* put instance on stack */
    if (check(lx, '[')) /* index access? */
        indexed(lx, e, 1);
    else if (check(lx, '.')) /* field access? */
        getdotted(lx, e, 1);
    else { /* get superclass */
        tokuC_emitI(fs, OP_SUPER);
        e->et = EXP_SUPER;
    }
}


static void primaryexp(Lexer *lx, ExpInfo *e) {
    switch (lx->t.tk) {
        case '(': {
            int32_t linenum = lx->line;
            tokuY_scan(lx); /* skip '(' */
            expr(lx, e);
            expectmatch(lx, ')', '(', linenum);
            tokuC_exp2val(lx->fs, e);
            break;
        }
        case TK_NAME:
            var(lx, str_expectname(lx), e);
            break;
        case TK_SUPER:
            superkw(lx, e);
            break;
        default: tokuY_syntaxerror(lx, "unexpected symbol");
    }
}


static void call(Lexer *lx, ExpInfo *e) {
    FunctionState *fs = lx->fs;
    int32_t linenum = lx->line;
    int32_t base = fs->sp - 1;
    tokuY_scan(lx); /* skip '(' */
    if (!check(lx, ')')) { /* have arguments? */
        explist(lx, e);
        if (eismulret(e)) /* last argument is a call or vararg? */
            tokuC_setmulret(fs, e); /* it returns all values (finalize it) */
        else /* otherwise... */
            tokuC_exp2stack(fs, e); /* put last argument value on stack */
    } else /* otherwise no arguments */
        e->et = EXP_VOID;
    expectnext(lx, ')');
    initexp(e, EXP_CALL, tokuC_call(fs, base, TOKU_MULTRET));
    tokuC_fixline(fs, linenum);
    linenum = lx->line;
    if (match(lx, '?')) /* call check? */
        tokuC_callcheck(fs, base, linenum);
}


static void suffixedexp(Lexer *lx, ExpInfo *e) {
    primaryexp(lx, e);
    for (;;) {
        switch (lx->t.tk) {
            case '.':
                getdotted(lx, e, 0);
                break;
            case '[':
                indexed(lx, e, 0);
                break;
            case '(':
                tokuC_exp2stack(lx->fs, e);
                call(lx, e);
                break;
            default: return;
        }
    }
}


/* {====================================================================
** List constructor
** ===================================================================== */

typedef struct LConstructor {
    ExpInfo *l; /* list descriptor */
    ExpInfo v; /* last list item descriptor */
    int32_t narray; /* number of list elements already stored */
    int32_t tostore; /* number of list elements pending to be stored */
} LConstructor;


static void listfield(Lexer *lx, LConstructor *c) {
    expr(lx, &c->v);
    c->tostore++;
}


static void checklistlimit(FunctionState *fs, LConstructor *c) {
    int32_t size;
    if (c->narray <= INT32_MAX - c->tostore)
       size = c->narray + c->tostore;
    else /* otherwise overflow */
        size = INT32_MAX; /* force error */
    tokuP_checklimit(fs, size, INT32_MAX, "elements in a list constructor");
}


static void closelistfield(FunctionState *fs, LConstructor *c) {
    if (c->v.et == EXP_VOID) return; /* there is no list item */
    tokuC_exp2stack(fs, &c->v); /* put the item on stack */
    voidexp(&c->v); /* now empty */
    if (c->tostore == LISTFIELDS_PER_FLUSH) { /* flush? */
        checklistlimit(fs, c);
        tokuC_setlist(fs, c->l->u.info, c->narray, c->tostore);
        c->narray += c->tostore; /* add to total */
        c->tostore = 0; /* no more pending items */
    }
}


static void lastlistfield(FunctionState *fs, LConstructor *c) {
    if (c->tostore == 0) return;
    checklistlimit(fs, c);
    if (eismulret(&c->v)) { /* last item has multiple returns? */
        tokuC_setmulret(fs, &c->v);
        tokuC_setlist(fs, c->l->u.info, c->narray, TOKU_MULTRET);
        c->narray--; /* do not count last expression */
    } else {
        if (c->v.et != EXP_VOID) /* have item? */
            tokuC_exp2stack(fs, &c->v); /* ensure it is on stack */
        tokuC_setlist(fs, c->l->u.info, c->narray, c->tostore);
    }
    c->narray += c->tostore;
}


static void listdef(Lexer *lx, ExpInfo *l) {
    FunctionState *fs = lx->fs;
    int32_t linenum = lx->line;
    int32_t pc = tokuC_emitIS(fs, OP_NEWLIST, 0);
    LConstructor c = { .l = l, .v = INIT_EXP };
    initexp(l, EXP_FINEXPR, fs->sp); /* finalize list expression */
    tokuC_reserveslots(fs, 1); /* space for list */
    expectnext(lx, '[');
    do {
        toku_assert(c.v.et == EXP_VOID || c.tostore > 0);
        if (check(lx, ']')) break; /* delimiter; no more elements */
        closelistfield(fs, &c); /* try to close any pending list elements */
        listfield(lx, &c); /* get list element */
    } while (match(lx, ',') || match(lx, ';'));
    expectmatch(lx, ']', '[', linenum);
    lastlistfield(fs, &c);
    tokuC_setlistsize(fs, pc, c.narray);
}

/* }==================================================================== */


/* {====================================================================
** Table consturctor
** ===================================================================== */

typedef struct TConstructor {
    ExpInfo *t; /* table descriptor */
    ExpInfo v; /* last table item descriptor */
    int32_t nhash; /* number of table elements */
} TConstructor;


static void tabindex(Lexer *lx, ExpInfo *e) {
    expectnext(lx, '[');
    expr(lx, e);
    tokuC_exp2val(lx->fs, e);
    expectnext(lx, ']');
}


static void tabfield(Lexer *lx, TConstructor *c) {
    FunctionState *fs = lx->fs;
    ExpInfo t, k, v;
    if (check(lx, TK_NAME)) {
        tokuP_checklimit(fs, c->nhash, INT32_MAX,
                             "fields in a table constructor");
        expname(lx, &k);
    } else
        tabindex(lx, &k);
    c->nhash++;
    expectnext(lx, '=');
    t = *c->t; /* copy of table descriptor */
    tokuC_indexed(fs, &t, &k, 0);
    expr(lx, &v);
    tokuC_exp2stack(fs, &v);
    tokuC_pop(fs, tokuC_store(fs, &t) - 1); /* -1 to keep table */
}


static void tabledef(Lexer *lx, ExpInfo *t) {
    FunctionState *fs = lx->fs;
    int32_t linenum = lx->line;
    int32_t pc = tokuC_emitIS(fs, OP_NEWTABLE, 0);
    TConstructor c = { .t = t, .v = INIT_EXP };
    initexp(t, EXP_FINEXPR, fs->sp); /* finalize table expression */
    tokuC_reserveslots(fs, 1); /* space for table */
    expectnext(lx, '{');
    do {
        if (check(lx, '}')) break; /* delimiter; no more fields */
        tabfield(lx, &c);
    } while (match(lx, ',') || match(lx, ';'));
    expectmatch(lx, '}', '{', linenum);
    tokuC_settablesize(fs, pc, c.nhash);
}

/* }==================================================================== */


static int32_t codeclass(FunctionState *fs) {
    int32_t pc = tokuC_emitIS(fs, OP_NEWCLASS, 0);
    tokuC_reserveslots(fs, 1); /* space for class */
    return pc;
}


static void method(Lexer *lx) {
    ExpInfo var, dummy;
    int32_t linenum = lx->line;
    tokuY_scan(lx); /* skip 'fn' */
    expname(lx, &var);
    funcbody(lx, &dummy, 1, linenum, '(');
    tokuC_methodset(lx->fs, &var);
    tokuC_fixline(lx->fs, linenum);
}


static void checkmftable(Lexer *lx, Table *t, OString *metafield) {
    toku_State *T = lx->T;
    TValue aux;
    if (t_unlikely(!tagisempty(tokuH_getstr(t, metafield, &aux)))) {
        const char *msg = tokuS_pushfstring(T,
                "redefinition of '%s' metafield", getstr(metafield));
        tokuP_semerror(lx, msg);
    }
    setbtval(&aux);
    tokuH_setstr(T, t, metafield, &aux); /* mftable[metafield] = true */
}


static void metafield(Lexer *lx, Table *t) {
    FunctionState *fs = lx->fs;
    int32_t linenum = lx->line;
    int32_t funcline;
    OString *metafield;
    ExpInfo e;
    int32_t event = -1;
    expname(lx, &e);
    metafield = e.u.str;
    if (ismetatag(metafield)) /* is TM event? */
        event = metafield->extra - NUM_KEYWORDS - 1;
    checkmftable(lx, t, metafield);
    expectnext(lx, '=');
    funcline = lx->line;
    if (match(lx, TK_FN) || check(lx, '|')) {
        int32_t del = lx->t.tk;
        if (del == '|') funcline = lx->line;
        funcbody(lx, &e, 1, funcline, del);
    } else
        expr(lx, &e);
    tokuC_exp2stack(fs, &e);
    expectnext(lx, ';');
    if (0 <= event)
        tokuC_tmset(fs, event, linenum);
    else
        tokuC_mtset(fs, metafield, linenum);
}


static void classbody(Lexer *lx, int32_t pc, int32_t linenum, int32_t exp) {
    if (!(check(lx, '}') || check(lx, TK_EOS))) {
        FunctionState *fs = lx->fs;
        toku_State *T = lx->T;
        Table *t = tokuH_new(T); /* metafield table */
        int32_t loadsp = fs->sp - 1; /* class slot */
        int32_t nmethods = 0; /* no methods */
        int32_t havemt = 0; /* no metatable */
        Scope s;
        settval2s(T, T->sp.p, t); /* anchor it */
        tokuT_incsp(T);
        enterscope(fs, &s, 0);
        do {
            if (match(lx, TK_LOCAL)) { /* localstm? */
                if (t_unlikely(exp)) /* is this anonymous class? */
                    tokuP_semerror(lx, "class expressions can't have locals");
                localstm(lx);
                expectnext(lx, ';');
            } else {
                tokuC_load(fs, loadsp); /* class is expected to be on top */
                if (check(lx, TK_FN)) { /* method? */
                    method(lx);
                    nmethods++;
                } else { /* otherwise it must be a metafield */
                    metafield(lx, t);
                    havemt = 1;
                }
                /* (method or metafield set removes the class copy) */
            }
        } while (!check(lx, '}') && !check(lx, TK_EOS));
        leavescope(fs);
        T->sp.p--; /* remove metafield table */
        tokuC_classadjust(fs, pc, nmethods, havemt);
    }
    expectmatch(lx, '}', '{', linenum);
}


static void inherit(Lexer *lx) {
    FunctionState *fs = lx->fs;
    ExpInfo v = INIT_EXP;
    expr(lx, &v);
    tokuC_exp2stack(fs, &v);
    tokuC_emitI(fs, OP_INHERIT);
    tokuC_checkstack(fs, 2); /* when copying private upvalues (if any) */
    fs->sp--; /* inherit removes the superclass */
}


static void classexp(Lexer *lx, ExpInfo *e) {
    FunctionState *fs = lx->fs;
    ClassState cs = { .prev = fs->cs, .pc = currPC };
    int32_t pc = codeclass(fs);
    if (match(lx, TK_INHERITS))
        inherit(lx);
    if (match(lx, '{')) {
        fs->cs = &cs;
        classbody(lx, pc, lx->lastline, e != NULL);
        fs->cs = cs.prev; /* get previous class state */
    }
    if (e) initexp(e, EXP_FINEXPR, pc);
}


static void simpleexp(Lexer *lx, ExpInfo *e) {
    int32_t linenum;
    switch (lx->t.tk) {
        case TK_INT:
            initexp(e, EXP_INT, 0);
            e->u.i = lx->t.lit.i;
            break;
        case TK_FLT:
            initexp(e, EXP_FLT, 0);
            e->u.n = lx->t.lit.n;
            break;
        case TK_STRING:
            initexp(e, EXP_STRING, 0);
            e->u.str = lx->t.lit.str;
            break;
        case TK_NIL:
            initexp(e, EXP_NIL, 0);
            break;
        case TK_TRUE:
            initexp(e, EXP_TRUE, 0);
            break;
        case TK_FALSE:
            initexp(e, EXP_FALSE, 0);
            break;
        case TK_DOTS:
            expect_cond(lx, lx->fs->p->isvararg,
                        "cannot use '...' outside of vararg function");
            initexp(e, EXP_VARARG, tokuC_vararg(lx->fs, TOKU_MULTRET));
            break;
        case TK_FN:
            linenum = lx->line;
            tokuY_scan(lx); /* skip 'fn' */
            goto func;
        case '|':
            linenum = lx->line;
        func:
            funcbody(lx, e, 0, linenum, lx->t.tk);
            return;
        case TK_CLASS:
            tokuY_scan(lx); /* skip 'class' */
            classexp(lx, e);
            return;
        case '[': listdef(lx, e); return;
        case '{': tabledef(lx, e); return;
        default: suffixedexp(lx, e); return;
    }
    tokuY_scan(lx);
}


/* get unary operation matching 'token' */
static Unopr getunopr(int32_t token) {
    switch (token) {
        case '-': return OPR_UNM;
        case '~': return OPR_BNOT;
        case '!': return OPR_NOT;
        default: return OPR_NOUNOPR;
    }
}


/* get binary operation matching 'token' */
static Binopr getbinopr(int32_t token) {
    switch (token) {
        case '+': return OPR_ADD;
        case '-': return OPR_SUB;
        case '*': return OPR_MUL;
        case '/': return OPR_DIV;
        case TK_IDIV: return OPR_IDIV;
        case '%': return OPR_MOD;
        case TK_POW: return OPR_POW;
        case TK_SHR: return OPR_SHR;
        case TK_SHL: return OPR_SHL;
        case '&': return OPR_BAND;
        case '|': return OPR_BOR;
        case '^': return OPR_BXOR;
        case TK_CONCAT: return OPR_CONCAT;
        case TK_NE: return OPR_NE;
        case TK_EQ: return OPR_EQ;
        case '<': return OPR_LT;
        case TK_LE: return OPR_LE;
        case '>': return OPR_GT;
        case TK_GE: return OPR_GE;
        case TK_AND: return OPR_AND;
        case TK_OR: return OPR_OR;
        default: return OPR_NOBINOPR;
    }
}


/*
** If 'left' == 'right' then operator is associative;
** if 'left' < 'right' then operator is left associative;
** if 'left' > 'right' then operator is right associative.
*/
static const struct {
    uint8_t left;
    uint8_t right;
} priority[] = { /* "ORDER OP" */
    /* binary operators priority */
    {12, 12}, {12, 12},                         /* '+' '-' */
    {13, 13}, {13, 13}, {13, 13}, {13, 13},     /* '*' '/' '//' '%' */
    {16, 15},                                   /* '**' (right associative) */
    {9, 9}, {9, 9},                             /* '<<' '>>' */
    {6, 6}, {4, 4}, {5, 5},                     /* '&' '|' '^' */
    {11, 10},                                   /* '..' (right associative) */
    {7, 7}, {7, 7},                             /* '==' '!=' */
    {8, 8}, {8, 8},                             /* '<' '<= */
    {8, 8}, {8, 8},                             /* '>' '>= */
    {3, 3}, {2, 2},                             /* 'and' 'or' */
    {1, 1}                                      /* XXX: '?:' (ternary) */
};

#define UNARY_PRIORITY  14  /* priority for unary operators */


static Binopr subexpr(Lexer *lx, ExpInfo *e, int32_t limit) {
    Binopr op;
    Unopr uop;
    enterCstack(lx);
    uop = getunopr(lx->t.tk);
    if (uop != OPR_NOUNOPR) {
        int32_t linenum = lx->line;
        tokuY_scan(lx); /* skip operator */
        subexpr(lx, e, UNARY_PRIORITY);
        tokuC_unary(lx->fs, e, uop, linenum);
    } else
        simpleexp(lx, e);
    op = getbinopr(lx->t.tk);
    while (op != OPR_NOBINOPR && priority[op].left > limit) {
        ExpInfo e2 = INIT_EXP;
        Binopr next;
        int32_t linenum = lx->line;
        tokuY_scan(lx); /* skip operator */
        tokuC_prebinary(lx->fs, e, op, linenum);
        next = subexpr(lx, &e2, priority[op].right);
        tokuC_binary(lx->fs, e, &e2, op, linenum);
        op = next;
    }
    leaveCstack(lx);
    return op;
}


/* expr ::= subexpr */
static void expr(Lexer *lx, ExpInfo *e) {
    subexpr(lx, e, 0);
}


/* ======================================================================
**                              STATEMENTS
** ====================================================================== */

static void decl_list(Lexer *lx, int32_t blocktk) {
    while (!check(lx, TK_EOS) && !(blocktk && check(lx, blocktk))) {
        if (check(lx, TK_RETURN) || /* if return or... */
                check(lx, TK_CONTINUE) || /* continue or... */
                check(lx, TK_BREAK)) { /* ...break? */
            stm(lx); /* then it must be the last statement */
            return; /* done */
        } else /* otherwise it is a declaration */
            decl(lx);
    }
}


/* check if 'var' is 'final' (read-only) */
static void checkreadonly(Lexer *lx, ExpInfo *var) {
    FunctionState *fs = lx->fs;
    OString *varid = NULL;
    switch (var->et) {
        case EXP_UVAL: {
            UpValInfo *uv = &fs->p->upvals[var->u.info];
            if (uv->kind != VARREG)
                varid = uv->name;
            break;
        }
        case EXP_LOCAL: {
            LVar *lv = getlocalvar(fs, var->u.info);
            if (lv->s.kind != VARREG)
                varid = lv->s.name;
            break;
        }
        default: return; /* cannot be read-only */
    }
    if (varid) {
        const char *msg = tokuS_pushfstring(lx->T,
            "attempt to assign to read-only variable '%s'", getstr(varid));
        tokuP_semerror(lx, msg);
    }
}


/* adjust left and right side of an assignment */
static void adjustassign(Lexer *lx, int32_t nvars, int32_t nexps, ExpInfo *e) {
    FunctionState *fs = lx->fs;
    int32_t need = nvars - nexps;
    if (eismulret(e)) {
        need++; /* do not count '...' or the function being called */
        if (need > 0) { /* more variables than values? */
            tokuC_setreturns(fs, e, need);
            need = 0; /* no more values needed */
        } else /* otherwise more values than variables */
            tokuC_setreturns(fs, e, 0); /* call should return no values */
    } else {
        if (e->et != EXP_VOID) /* have one or more expressions? */
            tokuC_exp2stack(fs, e); /* finalize the last expression */
        if (need > 0) { /* more variables than values? */
            tokuC_nil(fs, need); /* assign them as nil */
            return; /* done */
        }
    }
    if (need > 0) /* more variables than values? */
        tokuC_reserveslots(fs, need); /* slots for call results or varargs */
    else /* otherwise more values than variables */
        tokuC_pop(fs, -need); /* pop them (if any) */
}


/*
** Structure to chain all variables on the left side of the
** assignment.
*/
struct LHS_assign {
    struct LHS_assign *prev, *next;
    ExpInfo v;
};


static int32_t dostore(FunctionState *fs, ExpInfo *v, int32_t nvars,
                                                      int32_t left) {
    if (eisindexed(v))
        return tokuC_storevar(fs, v, nvars+left-1);
    tokuC_store(fs, v);
    return 0;
}


static int32_t compound_assign(Lexer *lx, struct LHS_assign *lhs,
                                          int32_t nvars, Binopr op) {
    FunctionState *fs = lx->fs;
    int32_t nexps = 0;
    int32_t linenum = lx->line;
    int32_t first = fs->sp;
    int32_t left = 0;
    int32_t temp = nvars;
    ExpInfo e, e2;
    toku_assert(cast_u32(op) <= OPR_CONCAT);
    expectnext(lx, '=');
    nexps = explist(lx, &e);
    if (nvars != nexps)
        adjustassign(lx, nvars, nexps, &e);
    else
        tokuC_exp2stack(lx->fs, &e);
    initexp(&e2, EXP_FINEXPR, 0); /* set as finalized (on stack) */
    do { /* do 'op' and store */
        e = check_exp(lhs != NULL, lhs->v);
        toku_assert(eisvar(&e));
        if (eisindexed(&e))
            tokuC_load(fs, first-temp);
        tokuC_exp2stack(fs, &e);
        tokuC_load(fs, first + temp - 1);
        tokuC_binary(fs, &e, &e2, op, linenum);
        left += dostore(fs, &lhs->v, temp+1, left+nvars-1);
        lhs = lhs->prev;
    } while (--temp);
    tokuC_pop(fs, nvars); /* remove rhs expressions */
    return left;
}


static int32_t assign(Lexer *lx, struct LHS_assign *lhs, int32_t nvars,
                                                         int32_t *comp) {
    int32_t left = 0; /* number of values left in the stack after assignment */
    expect_cond(lx, eisvar(&lhs->v), "expect variable");
    checkreadonly(lx, &lhs->v);
    if (match(lx, ',')) { /* more vars? */
        struct LHS_assign var = { .prev = lhs, .v = INIT_EXP };
        var.prev = lhs; /* chain previous var */
        lhs->next = &var; /* chain current var into previous var */
        suffixedexp(lx, &var.v);
        enterCstack(lx); /* control recursion depth */
        left = assign(lx, &var, nvars + 1, comp);
        leaveCstack(lx);
    } else { /* right side of assignment */
        int32_t tk = lx->t.tk;
        switch (tk) {
            case '+': case '-': case '*': case '%':
            case '/': case '&': case '|': case TK_IDIV:
            case TK_POW: case TK_SHL: case TK_SHR: case TK_CONCAT: {
                Binopr op = getbinopr(tk);
                tokuY_scan(lx); /* skip operator */
                *comp = 1; /* indicate this is compound assignment */
                return compound_assign(lx, lhs, nvars, op);
            }
            default: { /* regular assign */
                ExpInfo e = INIT_EXP;
                int32_t nexps;
                expectnext(lx, '=');
                nexps = explist(lx, &e);
                if (nvars != nexps)
                    adjustassign(lx, nvars, nexps, &e);
                else
                    tokuC_exp2stack(lx->fs, &e);
            }
        }
    }
    return *comp ? left : left + dostore(lx->fs, &lhs->v, nvars, left);
}


/* '++' or '--' depending on 'op' */
static int32_t ppmm(Lexer *lx, ExpInfo *v, Binopr op) {
    FunctionState *fs = lx->fs;
    ExpInfo copy = *v; /* copy of variable we are assigning to */
    int32_t linenum = lx->line;
    tokuY_scan(lx); /* skip '+' */
    toku_assert(eisvar(v));
    if (eisindexed(v)) /* indexed? */
        tokuC_load(fs, fs->sp - 1); /* copy receiver to not lose it */
    tokuC_dischargevars(fs, v); /* make sure value is on stack */
    tokuC_binimmediate(fs, v, 1, op, linenum);
    return dostore(fs, &copy, 1, 0);
}


static void expstm(Lexer *lx) {
    struct LHS_assign v = { .v = INIT_EXP };
    suffixedexp(lx, &v.v);
    if (v.v.et == EXP_CALL) { /* call? */
        if (t_unlikely(lx->fs->callcheck))
            tokuP_semerror(lx, "can't use '?' on calls with no results");
        tokuC_setreturns(lx->fs, &v.v, 0); /* call statement has no returns */
    } else { /* otherwise it must be assignment */
        int32_t left = 0;
        if (check(lx, '=') || check(lx, ',')) {
            int32_t comp = 0;
            v.prev = NULL;
            left = assign(lx, &v, 1, &comp);
        } else { /* otherwise compound assignment to only one variable */
            int32_t tk = lx->t.tk;
            Binopr op = getbinopr(tk);
            expect_cond(lx, eisvar(&v.v), "expect variable");
            checkreadonly(lx, &v.v);
            switch (tk) {
                case '+':
                    tokuY_scan(lx);
                    if (check(lx, '+')) /* 'var++'? */
                        goto incdec;
                    goto compassign;
                case '-':
                    tokuY_scan(lx);
                    if (check(lx, '-')) { /* 'var--'? */
                    incdec:
                        left = ppmm(lx, &v.v, op);
                        break;
                    }
                    goto compassign;
                case '*': case '%': case '/': case '&':
                case '|': case TK_IDIV: case TK_POW:
                case TK_SHL: case TK_SHR: case TK_CONCAT:
                    tokuY_scan(lx);
                compassign:
                    left = compound_assign(lx, &v, 1, op);
                    break;
                default: expecterror(lx, '=');
            }
        }
        tokuC_adjuststack(lx->fs, left);
    }
}


static int32_t getlocalattribute(Lexer *lx) {
    if (match(lx, '<')) {
        const char *astr = getstr(str_expectname(lx));
        expectnext(lx, '>');
        if (strcmp(astr, "final") == 0)
            return VARFINAL; /* read-only variable */
        else if (strcmp(astr, "close") == 0)
            return VARTBC; /* to-be-closed variable */
        else
            tokuP_semerror(lx,
                tokuS_pushfstring(lx->T, "unknown attribute '%s'", astr));
    }
    return VARREG;
}


static void checkcollision(Lexer *lx, OString *name) {
    FunctionState *fs = lx->fs;
    int32_t limit = fs->scope->nactlocals - 1;
    ExpInfo var;
    if (t_unlikely(0 <= searchlocal(fs, name, &var, limit))) {
        LVar *lv = getlocalvar(fs, var.u.var.vidx);
        lx->line = lx->lastline; /* adjust line for error */
        tokuP_semerror(lx, tokuS_pushfstring(lx->T,
                    "redefinition of local variable '%s' defined on line %d",
                    getstr(name), lv->s.linenum));
    }
}


static int32_t newlocalvarln(Lexer *lx, OString *name, int32_t ign,
                                                       int32_t linenum) {
    if (!ign || !(getstrlen(name) == 1 && *getstr(name) == '_'))
        checkcollision(lx, name);
    return addlocal(lx, name, linenum);
}


#define newlocalvar(lx,name,ign)    newlocalvarln(lx, name, ign, (lx)->line)


static void checkclose(FunctionState *fs, int32_t level) {
    if (level != -1) {
        scopemarkclose(fs);
        tokuC_emitIL(fs, OP_TBC, level);
    }
}


static void localstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    int32_t toclose = -1;
    int32_t nvars = 0;
    int32_t kind, vidx;
    int32_t nexps;
    ExpInfo e = INIT_EXP;
    do {
        vidx = newlocalvar(lx, str_expectname(lx), 1);
        kind = getlocalattribute(lx);
        getlocalvar(fs, vidx)->s.kind = cast_u8(kind);
        if (kind & VARTBC) { /* to-be-closed? */
            if (toclose != -1) /* one already present? */
                tokuP_semerror(fs->lx,
                        "multiple to-be-closed variables in a local list");
            toclose = fs->nactlocals + nvars;
        }
        nvars++;
    } while (match(lx, ','));
    if (match(lx, '='))
        nexps = explist(lx, &e);
    else
        nexps = 0;
    toku_assert((nexps == 0) == (e.et == EXP_VOID));
    adjustassign(lx, nvars, nexps, &e);
    adjustlocals(lx, nvars);
    checkclose(fs, toclose);
}


static void localfn(Lexer *lx) {
    ExpInfo e;
    FunctionState *fs = lx->fs;
    int32_t fvar = fs->nactlocals; /* function's variable index */
    newlocalvar(lx, str_expectname(lx), 0);
    adjustlocals(lx, 1);
    funcbody(lx, &e, 0, lx->line, '(');
    /* debug information will only see the variable after this point! */
    getlocalinfo(fs, fvar)->startpc = currPC;
}


static void localclass(Lexer *lx) {
    newlocalvar(lx, str_expectname(lx), 0);
    adjustlocals(lx, 1);
    classexp(lx, NULL);
}


static void blockstm(Lexer *lx) {
    int32_t linenum = lx->line;
    Scope s;
    tokuY_scan(lx); /* skip '{' */
    enterscope(lx->fs, &s, 0); /* explicit scope */
    decl_list(lx, '}'); /* body */
    expectmatchblk(lx, linenum);
    leavescope(lx->fs);
}


static void paramlist(Lexer *lx, int32_t del) {
    FunctionState *fs = lx->fs;
    Proto *fn = fs->p;
    int32_t nparams = 0;
    int32_t isvararg = 0;
    if (!check(lx, del)) { /* have at least one arg? */
        do {
            switch (lx->t.tk) {
                case TK_NAME:
                    newlocalvar(lx, str_expectname(lx), 1);
                    nparams++;
                    break;
                case TK_DOTS:
                    tokuY_scan(lx);
                    isvararg = 1;
                    break;
                default: tokuY_syntaxerror(lx, "<name> or '...' expected");
            }
        } while (!isvararg && match(lx, ','));
    }
    adjustlocals(lx, nparams);
    fn->arity = fs->nactlocals;
    if (isvararg) setvararg(fs, fn->arity);
    tokuC_reserveslots(fs, fs->nactlocals);
}


/* emit closure opcode */
static void codeclosure(Lexer *lx, ExpInfo *e, int32_t linenum) {
    FunctionState *fs = lx->fs->prev;
    initexp(e, EXP_FINEXPR, tokuC_emitIL(fs, OP_CLOSURE, fs->np - 1));
    tokuC_fixline(fs, linenum);
    tokuC_reserveslots(fs, 1); /* space for closure */
}


static void funcbody(Lexer *lx, ExpInfo *v, int32_t ismethod, int32_t linenum,
                                                              int32_t del) {
    FunctionState newfs = {
        .p = addproto(lx),
        .ismethod = cast_u8(ismethod)
    };
    int32_t matchdel = (del == '(') ? ')' : '|';
    Scope scope;
    newfs.p->defline = linenum;
    open_func(lx, &newfs, &scope);
    expectnext(lx, del);
    if (ismethod) { /* is this method ? */
        toku_assert(newfs.prev->cs); /* enclosing func. must have ClassState */
        newfs.cs = newfs.prev->cs; /* set ClassState */
        addlocallitln(lx, "self", 1); /* create 'self' local  on line 1 */
        adjustlocals(lx, 1); /* 'paramlist()' reserves stack slots */
    }
    paramlist(lx, matchdel); /* get function parameters */
    expectmatch(lx, matchdel, del, linenum);
    match(lx, TK_DBCOLON); /* skip optional separator (if any) */
    if (match(lx, '{')) {
        int32_t curly_linenum = lx->line;
        decl_list(lx, '}');
        newfs.p->deflastline = lx->line;
        expectmatch(lx, '}', '{', curly_linenum);
    } else {
        stm(lx);
        newfs.p->deflastline = lx->line;
    }
    codeclosure(lx, v, linenum);
    toku_assert(ismethod == (newfs.cs != NULL && (newfs.cs == newfs.prev->cs)));
    newfs.cs = NULL; /* clear ClassState (if any) */
    close_func(lx);
}


static void dottedname(Lexer *lx, ExpInfo *v) {
    var(lx, str_expectname(lx), v);
    while (check(lx, '.'))
        getdotted(lx, v, 0);
}


static void fnstm(Lexer *lx, int32_t linenum) {
    FunctionState *fs = lx->fs;
    ExpInfo var, e;
    tokuY_scan(lx); /* skip 'fn' */
    dottedname(lx, &var);
    funcbody(lx, &e, 0, linenum, '(');
    checkreadonly(lx, &var);
    tokuC_storepop(fs, &var, linenum);
}


static void classstm(Lexer *lx, int32_t linenum) {
    FunctionState *fs = lx->fs;
    int32_t level = fs->nactlocals;
    int32_t nvars = 1;
    ExpInfo var;
    tokuY_scan(lx); /* skip 'class' */
    dottedname(lx, &var);
    if (eisindexed(&var) && nvars++) /* 'var' is indexed? */
        addlocallit(lx, "(temporary)"); /* account for temporary value */
    addlocallit(lx, "(class temporary)"); /* class object temporary */
    adjustlocals(lx, nvars);
    classexp(lx, NULL);
    checkreadonly(lx, &var);
    tokuC_storepop(fs, &var, linenum);
    removelocals(fs, level);
}


typedef enum { CNONE, CDFLT, CASE, CMATCH, CMISMATCH } SwitchCase;


/* 'switch' statement state. */
typedef struct {
    TValue v; /* constant expression value */
    uint8_t isconst; /* true if 'e' is constant */
    uint8_t nomatch; /* true if switch has no compile-time match */
    uint8_t havedefault; /* if switch has 'default' case */
    uint8_t havenil; /* if switch has 'nil' case */
    uint8_t havetrue; /* if switch has 'true' case */
    uint8_t havefalse; /* if switch has 'false' case */
    int32_t firstli; /* first literal value in parser state 'literals' array */
    int32_t jmp; /* jump that needs patch if 'case' expression is not 'CMATCH' */
    SwitchCase c; /* current case */
} SwitchState;


/* convert literal information into text */
static const char *literal2text(toku_State *T, LiteralInfo *li) {
    switch (li->tt) {
        case TOKU_VNUMINT: return tokuS_pushfstring(T, " (%I)", li->lit.i);
        case TOKU_VNUMFLT: return tokuS_pushfstring(T, " (%f)", li->lit.n);
        case TOKU_VSHRSTR:case TOKU_VLNGSTR:
            return tokuS_pushfstring(T, " (%s)", getstr(li->lit.str));
        default: toku_assert(0); return NULL; /* invalid literal */
    }
}


/* find literal info 'li' in 'literals' */
static int32_t findliteral(Lexer *lx, LiteralInfo *li, int32_t first) {
    DynData *dyd = lx->dyd;
    for (int32_t i = first; i < dyd->literals.len; i++) { /* O(n) */
        LiteralInfo *curr = &dyd->literals.arr[i];
        if (li->tt != curr->tt) /* types don't match? */
            continue; /* skip */
        switch (li->tt) {
            case TOKU_VSHRSTR: case TOKU_VLNGSTR:
                if (eqstr(li->lit.str, curr->lit.str))
                    return i; /* found */
                break;
            case TOKU_VNUMINT:
                if (li->lit.i == curr->lit.i)
                    return i; /* found */
                break;
            case TOKU_VNUMFLT:
                if (tokui_numeq(li->lit.n, curr->lit.n))
                    return i; /* found */
                break;
            default: toku_assert(0); break; /* invalid literal */
        }
    }
    return -1; /* not found */
}


/*
** Checks if expression is a duplicate literal value.
*/
static int32_t checkliteral(SwitchState *ss, ExpInfo *e, const char **what) {
    switch (e->et) {
        case EXP_FALSE:
            if (t_unlikely(ss->havefalse))
                *what = "false";
            ss->havefalse = 1;
            break;
        case EXP_TRUE:
            if (t_unlikely(ss->havetrue))
                *what = "true";
            ss->havetrue = 1;
            break;
        case EXP_NIL:
            if (t_unlikely(ss->havenil))
                *what = "nil";
            ss->havenil = 1;
            break;
        default: return 0;
    }
    return 1;
}


/*
** Checks if 'e' is a duplicate constant value and fills the relevant info.
** If 'li' is a duplicate, 'what' and 'extra' are filled accordingly.
*/
static void checkK(Lexer *lx, ExpInfo *e, LiteralInfo *li, int32_t first,
                   int32_t *extra, const char **what) {
    switch (e->et) {
        case EXP_STRING:
            *what = "string";
            li->lit.str = e->u.str;
            li->tt = e->u.str->tt_;
            goto findliteral;
        case EXP_INT:
            *what = "integer";
            li->lit.i = e->u.i;
            li->tt = TOKU_VNUMINT;
            goto findliteral;
        case EXP_FLT:
            *what = "number";
            li->lit.n = e->u.n;
            li->tt = TOKU_VNUMFLT;
        findliteral:
            if (t_likely(findliteral(lx, li, first) < 0))
                *what = NULL;
            else
                *extra = 1;
            break;
        default: toku_assert(0); /* 'e' is not a literal expression */
    }
}


/* check for duplicate literal otherwise fill the relevant info */
static void checkduplicate(Lexer *lx, SwitchState *ss, ExpInfo *e,
                           LiteralInfo *li) {
    int32_t extra = 0;
    const char *what = NULL;
    if (!checkliteral(ss, e, &what))
         checkK(lx, e, li, ss->firstli, &extra, &what);
    if (t_unlikely(what)) { /* have duplicate? */
        const char *msg = tokuS_pushfstring(lx->T,
                            "duplicate %s literal%s in switch statement",
                            what, (extra ? literal2text(lx->T, li) : ""));
        tokuP_semerror(lx, msg);
    }
}


static void addliteralinfo(Lexer *lx, SwitchState *ss, ExpInfo *e) {
    DynData *dyd = lx->dyd;
    LiteralInfo li;
    checkduplicate(lx, ss, e, &li);
    tokuP_checklimit(lx->fs, dyd->literals.len, MAX_CODE, "switch cases");
    tokuM_growarray(lx->T, dyd->literals.arr, dyd->literals.size,
                    dyd->literals.len, MAX_CODE, "switch literals",
                    LiteralInfo);
    dyd->literals.arr[dyd->literals.len++] = li;
}


/* return values of 'checkmatch' */
#define NONEMATCH   0 /* both expressions are not constant expressions */
#define NOMATCH     1 /* both expressions are constants that do not match */
#define MATCH       2 /* expressions are compile time match */

/*
** Checks if 'e' is a compile-time match with the switch expression.
** Additionally it remembers the 'e' if it is a constant value and
** adds it to the list of literals; any duplicate literal value in switch
** is a compile-time error.
*/
static int32_t checkmatch(Lexer *lx, SwitchState *ss, ExpInfo *e) {
    if (eisconstant(e)) {
        addliteralinfo(lx, ss, e);
        if (ss->isconst) { /* both are constant values? */
            TValue v;
            tokuC_const2v(lx->fs, e, &v);
            return tokuV_raweq(&ss->v, &v) + 1; /* NOMATCH or MATCH */
        } /* else fall-through */
    } /* else fall-through */
    ss->nomatch = 0; /* we don't know... */
    return NONEMATCH;
}


/* 
** Tries to preserve expression 'e' after consuming it, in order
** to enable more optimizations. Additionally 'nonilmerge' is set,
** meaning if 'e' is nil, then it should not be merged with previous,
** as it might get optimized out.
*/
static uint8_t codeconstexp(FunctionState *fs, ExpInfo *e) {
    uint8_t res = 0;
    fs->nonilmerge = 1;
    tokuC_exp2val(fs, e);
    if (eisconstant(e)) {
        ExpInfo pres = *e;
        tokuC_exp2stack(fs, e);
        *e = pres;
        res = 1; /* true; 'e' is a constant */
    }
    fs->nonilmerge = 0;
    return res;
}


static void removeliterals(Lexer *lx, int32_t nliterals) {
    DynData *dyd = lx->dyd;
    if (dyd->literals.len < dyd->literals.size / 3) /* too many literals? */
        tokuM_shrinkarray(lx->T, dyd->literals.arr, dyd->literals.size,
                          dyd->literals.size / 2, LiteralInfo);
    dyd->literals.len = nliterals;
}


static void switchbody(Lexer *lx, SwitchState *ss, FuncContext *ctxbefore) {
    FunctionState *fs = lx->fs;
    int32_t ftjmp = NOJMP; /* fall-through jump */
    FuncContext ctxdefault = {0};
    FuncContext ctxcase = {0};
    FuncContext ctxend = {0};
    ctxend.pc = -1;
    while (!check(lx, '}') && !check(lx, TK_EOS)) { /* while switch body... */
        if (check(lx, TK_CASE) || match(lx, TK_DEFAULT)) { /* has case?... */
            if (ss->c == CASE && check(lx, TK_CASE)) {
                /* had previous case that is followed by another case */
                toku_assert(ftjmp == NOJMP); /* can't have open 'ftjump' */
                ftjmp = tokuC_jmp(fs, OP_JMP); /* new fall-through jump */
                tokuC_patchtohere(fs, ss->jmp); /* patch test jump */
            }
            if (match(lx, TK_CASE)) { /* 'case'? */
                ExpInfo e = INIT_EXP; /* case expression */
                int32_t match, linenum;
                if (t_unlikely(ss->havedefault))
                    tokuP_semerror(lx, "'default' must be the last case");
                storecontext(fs, &ctxcase); /* case might get optimized away */
                linenum = lx->line;
                expr(lx, &e);           /* get the case expression... */
                codeconstexp(fs, &e);   /* ...and put it on stack */
                expectnext(lx, ':');
                match = checkmatch(lx, ss, &e);
                if (match == MATCH) { /* case is compile-time match? */
                    ss->nomatch = 0; /* case is the match */
                    ss->c = CMATCH;
                    loadcontext(fs, ctxbefore);/* load context before switch */
                } else if (match == NOMATCH || ss->c == CMATCH) {
                    /* compile-time mismatch or previous case is a match */
                    if (ss->c != CMATCH) ss->c = CMISMATCH;
                    loadcontext(fs, &ctxcase); /* remove case expression */
                } else { /* else must check for match */
                    toku_assert(!ss->nomatch);
                    ss->c = CASE; /* regular case */
                    tokuC_emitI(fs, OP_EQPRESERVE); /* EQ but preserves lhs */
                    ss->jmp = tokuC_test(fs, OP_TESTPOP, 0, linenum);
                }
            } else if (!ss->havedefault) { /* don't have 'default'? */
                expectnext(lx, ':');
                toku_assert(ftjmp == NOJMP);/* 'default' does not have ftjmp */
                if (ss->nomatch) { /* all cases are resolved without match? */
                    ss->nomatch = 0; /* default is the match */
                    loadcontext(fs, ctxbefore); /* remove them */
                } else if (ss->c == CASE) /* have test jump? */
                    tokuC_patchtohere(fs, ss->jmp); /* fix it */
                ss->havedefault = 1; /* now have 'default' */
                ss->c = CDFLT;
                storecontext(fs, &ctxdefault); /* store 'default' context */
            } else /* otherwise duplicate 'default' case */
                tokuP_semerror(lx, "multiple default cases in switch");
            if (ftjmp != NOJMP) { /* have fall-through jump to patch? */
                tokuC_patchtohere(fs, ftjmp); /* patch it */
                ftjmp = NOJMP; /* reset */
            }
        } else if (ss->c != CNONE) { /* or have previous case?... */
            stm(lx);
            if (ss->c == CMATCH /* current case is a match, */
                    && stmIsEnd(fs)) { /* and statement ends control flow? */
                toku_assert(ctxend.pc == NOPC); /* context must be free */
                storecontext(fs, &ctxend); /* set current state as end */
            } else if (ss->c == CMISMATCH) /* case optimized away? */
                loadcontext(fs, &ctxcase); /* remove statement */
        } else /* ...otherwise error */
            tokuP_semerror(lx,"expect at least one 'case' or 'default' label");
    }
    toku_assert(ftjmp == NOJMP); /* no more fall-through jumps */
    if (ctxend.pc != -1) /* had a compile-time match and 'ctxend' is stored? */
        loadcontext(fs, &ctxend); /* trim off dead code */
    else if (ss->c == CASE) /* 'case' is last (have test)? */
        tokuC_patchtohere(fs, ss->jmp); /* patch it */
    else if (ss->nomatch) /* compile-time no match? */
        loadcontext(fs, ctxbefore); /* remove the whole switch */
    removeliterals(lx, ss->firstli);
}


static void switchstm(Lexer *lx) {
    Scope s;
    int32_t linenum;
    ExpInfo e;
    FuncContext ctxbefore;
    FunctionState *fs = lx->fs;
    Scope *prev = fs->switchscope;
    SwitchState ss = {
        .isconst = 0, .nomatch = 1,
        .firstli = lx->dyd->literals.len,
        .jmp = NOJMP,
        .c = CNONE
    };
    enterscope(fs, &s, CFM_SWITCH);
    storecontext(fs, &ctxbefore);
    fs->switchscope = &s; /* set the current 'switch' scope */
    tokuY_scan(lx); /* skip 'switch' */
    expr(lx, &e); /* get the 'switch' expression... */
    if ((ss.isconst = codeconstexp(fs, &e))) /* constant expression? */
        tokuC_const2v(fs, &e, &ss.v); /* get its value */
    addlocallit(lx, "(switch)"); /* switch expression temporary */
    adjustlocals(lx, 1); /* register switch temporary */
    linenum = lx->line;
    expectnext(lx, '{');
    switchbody(lx, &ss, &ctxbefore);
    expectmatch(lx, '}', '{', linenum);
    leavescope(fs);
    fs->switchscope = prev;
}


/* data for 'condbody()' */
typedef struct CondBodyState {
    FuncContext ctxbefore;  /* snapshot before the condition (and body) */
    FuncContext ctxend;     /* 'ctxend.pc != NOPC' if dead code was found */
    ExpInfo e;              /* condition expression */
    OpCode opT;             /* test opcode */
    OpCode opJ;             /* jump opcode */
    int32_t isif;           /* true if this is 'if' statement body */
    int32_t pcCond;         /* pc of condition */
    int32_t pcClause;       /* pc of last 'for' loop clause */
    int32_t condline;       /* condition line (for test opcode) */
} CondBodyState;


/* condition statement body; for 'forstm', 'whilestm' and 'ifstm' */
static void condbody(Lexer *lx, CondBodyState *cb) {
    FunctionState *fs = lx->fs;
    int32_t optaway, target;
    int32_t bodypc = currPC;
    int32_t istrue = eistrue(&cb->e);
    int32_t test = NOJMP, jump = NOJMP;
    cb->ctxend.pc = NOPC; /* no dead code to begin with */
    optaway = (eisconstant(&cb->e) && !istrue);
    if (!optaway) { /* statement will not be optimized away? */
        if (istrue) { /* condition is true? */
            if (cb->pcClause == NOJMP) { /* not a forloop? */
                loadcontext(fs, &cb->ctxbefore); /* remove condition */
                bodypc = currPC; /* adjust bodypc */
            } /* (otherwise condition is already optimized out) */
        } else /* otherwise emit condition test */
            test = tokuC_test(fs, cb->opT, 0, cb->condline);
    }
    stm(lx); /* loop/if body */
    if (optaway) /* optimize away this statement? */
        loadcontext(fs, &cb->ctxbefore);
    else if (istrue && (stmIsEnd(fs) || cb->isif)) {
        /* condition is true and body is cflow. end or this is 'if' */
        storecontext(fs, &cb->ctxend); /* this is end, rest is dead code */
    } else { /* otherwise unknown condition value */
        jump = tokuC_jmp(fs, cb->opJ); /* loop or if jump */
        if (!cb->isif) { /* loop statement? */
            if (cb->pcClause != NOJMP) /* 'for' loop? */
                tokuC_patch(fs, jump, cb->pcClause); /* jump to last clause */
            else if (istrue) { /* 'while' loop with true condition? */
                tokuC_patch(fs, jump, bodypc); /* jump to start of the body */
                fs->ls->pcloop = bodypc; /* convert it to infinite loop */ 
            } else /* 'while' loop with non-constant condition */
                tokuC_patch(fs, jump, cb->pcCond); /* jump back to condition */
        }
    }
    target = currPC; /* set test jump target */
    if (cb->isif) { /* is if statement? */
        if (match(lx, TK_ELSE)) /* have else? */
            stm(lx); /* else body */
        if (target == currPC) { /* no else branch or it got removed? */
            if (jump != NOJMP) { /* have if jump (opJ)? */
                toku_assert(*prevOP(fs) == cb->opJ);
                tokuC_removelastjump(fs); /* remove that jump */
                target = currPC; /* adjust test target */
            } else toku_assert(optaway || istrue);
        } else if (jump != NOJMP) /* have else branch and 'if' jump (opJ) */
            tokuC_patchtohere(fs, jump); /* (it jumps over else statement) */
    }
    if (test != NOJMP) /* have condition test? */
        tokuC_patch(fs, test, target); /* patch it */
    if (cb->ctxend.pc != NOPC) /* statement has "dead" code? */
        loadcontext(fs, &cb->ctxend); /* trim off dead code */
}


static void ifstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    CondBodyState cb = {
        .e = INIT_EXP,
        .opT = OP_TESTPOP, .opJ = OP_JMP,
        .isif = 1, .pcCond = currPC, .pcClause = NOJMP,
    };
    storecontext(fs, &cb.ctxbefore);
    tokuY_scan(lx); /* skip 'if' */
    cb.condline = lx->line;
    expr(lx, &cb.e);
    codeconstexp(fs, &cb.e);
    condbody(lx, &cb);
}


static void enterloop(FunctionState *fs, struct LoopState *ls, int32_t mask) {
    enterscope(fs, &ls->s, mask);
    ls->pcloop = currPC;
    ls->prev = fs->ls; /* chain it */
    fs->ls = ls;
}


static void leaveloop(FunctionState *fs) {
    leavescope(fs);
    fs->ls = check_exp(fs->ls != NULL, fs->ls->prev);
}


static void whilestm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    struct LoopState ls;
    CondBodyState cb = {
        .e = INIT_EXP,
        .opT = OP_TESTPOP, .opJ = OP_JMPS,
        .isif = 0, .pcCond = currPC, .pcClause = NOJMP,
    };
    tokuY_scan(lx); /* skip 'while' */
    enterloop(fs, &ls, CFM_LOOP);
    storecontext(fs, &cb.ctxbefore);
    cb.condline = lx->line;
    expr(lx, &cb.e);
    codeconstexp(fs, &cb.e);
    condbody(lx, &cb);
    leaveloop(fs);
}


static void dowhilestm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    ExpInfo e = INIT_EXP;
    int32_t old_pcdo = fs->pcdo;
    int32_t isend = 0;
    FuncContext ctxbefore;
    struct LoopState ls;
    Scope s; /* scope for 'decl_list' */
    int32_t linenum;
    tokuY_scan(lx); /* skip 'do' */
    enterloop(fs, &ls, CFM_DOWHILE);
    fs->pcdo = currPC;
    linenum = lx->line;
    expectnext(lx, '{');
    enterscope(fs, &s, 0);
    if (!check(lx, '}')) {
        decl_list(lx, '}');
        isend = stmIsReturn(fs) || stmIsBreak(fs);
    }
    expectmatch(lx, '}', '{', linenum);
    expectnext(lx, TK_WHILE);
    storecontext(fs, &ctxbefore);
    linenum = lx->line;
    fs->ls->pcloop = currPC; /* do/while loop starts here */
    expr(lx, &e); /* condition */
    codeconstexp(fs, &e);
    if (isend || eisconstant(&e)) { /* end or condition is a constant? */
        loadcontext(fs, &ctxbefore); /* remove the condition expression */
        leavescope(fs);
        if (!isend && eistrue(&e)) /* condition is true? */
            tokuC_patch(fs, tokuC_jmp(fs, OP_JMPS), fs->pcdo); /* inf. loop */
        /* otherwise nothing else to be done; fall through */
    } else { /* otherwise condition is a variable */
        int32_t nvars = fs->nactlocals - s.nactlocals;
        int32_t test = tokuC_test(fs, OP_TESTPOP, 0, linenum);
        //int32_t skip;
        leavescope(fs);
        tokuC_patch(fs, tokuC_jmp(fs, OP_JMPS), fs->pcdo);
        /* adjust stack for compiler and symbolic execution */
        tokuC_adjuststack(fs, -nvars); /* unreached */
        tokuC_patch(fs, test, currPC);
        if (s.haveupval)
            tokuC_emitIL(fs, OP_CLOSE, stacklevel(fs, s.nactlocals));
        tokuC_pop(fs, nvars);
        //skip = tokuC_jmp(fs, OP_JMP); /* jump that skips adjustment */
        ///* adjust stack for compiler and symbolic execution */
        //tokuC_adjuststack(fs, -nvars); /* unreached */
        //tokuC_patch(fs, skip, currPC);
    }
    leaveloop(fs);
    fs->pcdo = old_pcdo;
    expectnext(lx, ';');
}


/* patch for loop jump */
static void patchforjmp(FunctionState *fs, int32_t pc, int32_t target,
                                                       int32_t back) {
    uint8_t *jmp = &fs->p->code[pc];
    int32_t offset = target - (pc + getopSize(*jmp));
    if (back) offset = -offset;
    toku_assert(offset >= 0);
    if (t_unlikely(offset > MAXJMP))
        tokuY_syntaxerror(fs->lx, "control structure (for loop) too long");
    SET_ARG_L(jmp, 1, offset);
}


/* generic for loop expressions */
static int32_t forexplist(Lexer *lx, ExpInfo *e, int32_t limit) {
    int32_t nexpr = 1;
    expr(lx, e);
    while (match(lx, ',')) {
        tokuC_exp2stack(lx->fs, e);
        expr(lx, e);
        nexpr++;
    }
    if (t_unlikely(nexpr > limit))
        limiterror(lx->fs, "'foreach' expressions", limit);
    return nexpr;
}


static void foreachstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    struct LoopState ls;
    int32_t forend, prep;
    int32_t nvars = 1; /* number of results for interator */
    int32_t base = fs->sp;
    int32_t linenum;
    ExpInfo e = INIT_EXP;
    Scope s;
    enterloop(fs, &ls, CFM_GENLOOP); /* (scope for control variables) */
    tokuY_scan(lx); /* skip 'foreach' */
    addlocallit(lx, "(foreach1)");  /* iterator         (base)   */
    addlocallit(lx, "(foreach2)");  /* invariant state  (base+1) */
    addlocallit(lx, "(foreach3)");  /* control var      (base+2) */
    addlocallit(lx, "(foreach4)");  /* to-be-closed var (base+3) */
    /* create locals variables */
    newlocalvar(lx, str_expectname(lx), 1); /* at least one var. expected */
    while (match(lx, ',')) {
        newlocalvar(lx, str_expectname(lx), 1);
        nvars++;
    }
    expectnext(lx, TK_IN);
    linenum = lx->line;
    adjustassign(lx, VAR_N, forexplist(lx, &e, VAR_N), &e);
    adjustlocals(lx, VAR_N); /* register control variables */
    scopemarkclose(fs); /* last control variable might get closed */
    tokuC_checkstack(fs, 3); /* extra space to call generator */
    prep = tokuC_emitILL(fs, OP_FORPREP, base, 0);
    enterscope(fs, &s, 0); /* scope for declared locals */
    adjustlocals(lx, nvars); /* register delcared locals */
    tokuC_reserveslots(fs, nvars); /* space for declared locals */
    stm(lx); /* body */
    leavescope(fs); /* leave declared locals scope */
    patchforjmp(fs, prep, currPC, 0);
    fs->ls->pcloop = currPC; /* generic loop starts here */
    tokuC_emitILL(fs, OP_FORCALL, base, nvars);
    tokuC_fixline(fs, linenum);
    forend = tokuC_emitILLL(fs, OP_FORLOOP, base, 0, nvars);
    patchforjmp(fs, forend, prep + getopSize(OP_FORPREP), 1);
    tokuC_fixline(fs, linenum);
    leaveloop(fs); /* leave loop (pops control variables) */
}


/* 'for' loop initializer */
void forinitializer(Lexer *lx) {
    if (!match(lx, ';')) { /* have for loop initializer? */
        if (match(lx, TK_LOCAL)) /* 'local' statement? */
            localstm(lx);
        else /* otherwise expression statement */
            expstm(lx);
        expectnext(lx, ';');
    }
}


/* 'for' loop condition */
int32_t forcondition(Lexer *lx, CondBodyState *cb) {
    int32_t linenum = lx->line;
    if (!match(lx, ';')) { /* have condition? */
        expr(lx, &cb->e);
        codeconstexp(lx->fs, &cb->e);
        linenum = lx->line; /* update line */
        expectnext(lx, ';');
    } else /* otherwise no condition (infinite loop) */
        initexp(&cb->e, EXP_TRUE, 0);
    return linenum;
}


/* 'for' loop last clause */
void forlastclause(Lexer *lx, CondBodyState *cb, int32_t initsp) {
    FunctionState *fs = lx->fs;
    int32_t inf = eistrue(&cb->e);
    toku_assert(cb->pcClause == NOJMP);
    if (inf) /* infinite loop? */
        loadcontext(fs, &cb->ctxbefore); /* remove condition */
    if (!(check(lx, ')') || check(lx, ';'))) { /* have end clause? */
        int32_t bodyjmp = tokuC_jmp(fs, OP_JMP); /* insert jump in-between */
        int32_t oldsp = fs->sp;
        tokuC_fixline(fs, cb->condline); /* this is condition jump */
        cb->pcClause = currPC; /* set end clause pc */
        fs->sp = initsp; /* set stack index to value at 'for initializer' */
        expstm(lx); /* get the end clause expression statement */
        if (!inf) { /* loop is not infinite? */
            int32_t loopjmp = tokuC_jmp(fs, OP_JMPS);/* emit jump to cond... */
            tokuC_patch(fs, loopjmp, fs->ls->pcloop);/* ...and patch it */
        }
        tokuC_patchtohere(fs, bodyjmp); /* patch jump from condition to body */
        fs->ls->pcloop = cb->pcClause; /* loop starts at end clause pc */
        fs->sp = oldsp; /* restore stack index */
    } /* otherwise 'for' is a 'while' loop */
}


static void forstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    struct LoopState ls;
    CondBodyState cb = {
        .e = INIT_EXP,
        .opT = OP_TESTPOP, .opJ = OP_JMPS,
        .isif = 0, .pcClause = NOJMP
    };
    Scope s;
    int32_t linenum, opt, oldsp;
    tokuY_scan(lx); /* skip 'for' */
    enterscope(fs, &s, 0); /* enter initializer scope */
    linenum = lx->line;
    opt = match(lx, '(');
    /* 1st clause (initializer) */
    forinitializer(lx); 
    oldsp = fs->sp; /* stack index before condition (for last clause) */
    enterloop(fs, &ls, CFM_LOOP); /* enter loop scope */
    cb.pcCond = fs->ls->pcloop = currPC; /* loop is after initializer clause */
    storecontext(fs, &cb.ctxbefore); /* store context at loop start */
    /* 2nd clause (condition) */
    cb.condline = forcondition(lx, &cb);
    /* 3rd clause */
    forlastclause(lx, &cb, oldsp);
    if (opt) /* have optional ')' ? */
        expectmatch(lx, ')', '(', linenum);
    else /* otherwise must terminate last clause with separator */
        expectnext(lx, ';');
    condbody(lx, &cb); /* forbody */
    leaveloop(fs); /* leave loop scope */
    leavescope(fs); /* leave initializer scope */
}


static void loopstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    int32_t old_pcdo = fs->pcdo;
    struct LoopState ls;
    tokuY_scan(lx); /* skip 'loop' */
    enterloop(fs, &ls, CFM_LOOP);
    fs->pcdo = currPC;
    stm(lx);
    if (!stmIsEnd(fs)) { /* statement (body) does not end control flow? */
        int32_t jmp = tokuC_jmp(fs, OP_JMPS);
        tokuC_patch(fs, jmp, fs->pcdo); /* jump back to loop start */
    }
    leaveloop(fs);
    fs->pcdo = old_pcdo;
}


/*
** Return true if jump from current scope to 'limit' scope needs a close.
** If 'limit' is NULL, then 'limit' is considered to be the outermost scope.
*/
static int32_t needtoclose(Lexer *lx, const Scope *limit) {
    Scope *s = lx->fs->scope;
    toku_assert(!limit || limit->depth <= s->depth);
    while (s != limit) {
        if (s->haveupval)
            return 1; /* yes */
        s = s->prev;
    }
    return 0; /* no */
}


static void continuestm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    LoopState *ls = fs->ls;
    int32_t nvars = 0;
    tokuY_scan(lx); /* skip 'continue' */
    if (t_unlikely(ls == NULL)) /* not in a loop? */
        tokuP_semerror(lx, "'continue' outside of a loop statement");
    if (isdowhile(&ls->s)) { /* innermost loop is do/while? */
        Scope *s = &ls->s;
        Scope *curr = fs->scope;
        if (s->depth+2 < curr->depth) { /* there is 3rd scope in do/while? */
            do { /* get that scope */
                curr = curr->prev;
            } while (s->depth+2 < curr->depth);
            nvars = fs->nactlocals - curr->nactlocals;
        }
        tokuC_adjuststack(fs, nvars);
    } else { /* otherwise some other loop */
        if (needtoclose(lx, ls->s.prev))
            tokuC_emitIL(fs, OP_CLOSE, stacklevel(fs, ls->s.nactlocals));
        contadjust(fs, 0);
    }
    if (isgenloop(&ls->s) || isdowhile(&ls->s)) /* generic or do/while loop? */
        newpendingjump(lx, 0, 0, 0); /* 'continue' compiles as 'break' */
    else /* otherwise regular loop */
        tokuC_patch(fs, tokuC_jmp(fs, OP_JMPS), ls->pcloop); /* backpatch */
    expectnext(lx, ';');
    /* adjust stack for compiler and symbolic execution */
    if (isdowhile(&ls->s))
        tokuC_adjuststack(fs, -nvars);
    else
        contadjust(fs, 1);
    fs->lastisend = 3; /* statement is a continue */
}


static void breakstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    const Scope *cfs = getcfscope(fs); /* control flow scope */
    int32_t nvars;
    tokuY_scan(lx); /* skip 'break' */
    if (t_unlikely(cfs == NULL)) /* no control flow scope? */
        tokuP_semerror(lx, "'break' outside of a loop or switch statement");
    nvars = fs->nactlocals - cfs->nactlocals;
    newpendingjump(lx, 1, needtoclose(lx, cfs->prev), nvars);
    expectnext(lx, ';');
    /* adjust stack for compiler and symbolic execution */
    tokuC_adjuststack(fs, -nvars);
    fs->lastisend = 2; /* statement is a break */
}


/*
** Returns true if the current token is the beggining of a statement,
** declaration or ending of a block ('}'), expression group (')'),
** function argument list (')'), file/stream (TK_EOS) or the token
** is expression list separator (',').
*/
static int32_t boundary(Lexer *lx) {
    switch (lx->t.tk) {
        case TK_BREAK: case TK_CONTINUE: case TK_CASE: case TK_DEFAULT:
        case TK_FOR: case TK_FOREACH: case TK_IF: case TK_ELSE:
        case TK_SWITCH: case TK_WHILE: case TK_LOOP: case TK_LOCAL:
        case ',': case ')': case TK_DO: case '}': case TK_EOS:
            return 1;
        default: return 0;
    }
}


static void returnstm(Lexer *lx) {
    FunctionState *fs = lx->fs;
    int32_t first = fs->sp;
    int32_t nres = 0;
    ExpInfo e = INIT_EXP;
    tokuY_scan(lx); /* skip 'return' */
    if (!boundary(lx) && !check(lx, ';')) { /* have return values ? */
        nres = explist(lx, &e); /* get return values */
        if (eismulret(&e)) { /* last expressions is a call or vararg? */
            int32_t iscall = (e.et == EXP_CALL);
            tokuC_setmulret(fs, &e); /* returns all results (finalize it) */
            if (iscall && nres==1 && !fs->scope->havetbcvar) { /* tail call? */
                toku_assert(getopSize(OP_TAILCALL) == getopSize(OP_CALL));
                *getpi(fs, &e) = OP_TAILCALL;
            }
            nres = TOKU_MULTRET; /* 'return' will return all values */
        } else /* otherwise 'return' will return 'nres' values */
            tokuC_exp2stack(fs, &e); /* finalize last expression */
    }
    tokuC_return(fs, first, nres);
    match(lx, ';'); /* optional ';' */
    fs->sp = first; /* removes all temp values */
    fs->lastisend = 1; /* statement is a return */
}


static void stm_(Lexer *lx) {
    int32_t tk = lx->t.tk;
    switch (tk) {
        case TK_FN: fnstm(lx, lx->line); break;
        case TK_CLASS: classstm(lx, lx->line); break;
        case TK_WHILE: whilestm(lx); break;
        case TK_DO: dowhilestm(lx); break;
        case TK_FOR: forstm(lx); break;
        case TK_FOREACH: foreachstm(lx); break;
        case TK_IF: ifstm(lx); break;
        case TK_SWITCH: switchstm(lx); break;
        case '{': blockstm(lx); break;
        case TK_CONTINUE: continuestm(lx); break;
        case TK_BREAK: breakstm(lx); break;
        case TK_RETURN: returnstm(lx); break;
        case TK_LOOP: loopstm(lx); break;
        case ';': tokuY_scan(lx); break;
        default: expstm(lx); expectnext(lx, ';');
    }
    if (!((tk == TK_RETURN) | (tk == TK_CONTINUE) | (tk == TK_BREAK)))
        lx->fs->lastisend = 0; /* clear flag */
}


#define stackinvariant(fs) \
        (fs->sp <= fs->p->maxstack && nvarstack(fs) <= fs->sp)


static void decl(Lexer *lx) {
    enterCstack(lx);
    switch (lx->t.tk) {
        case TK_LOCAL:
            tokuY_scan(lx); /* skip 'local' */
            if (match(lx, TK_FN))
                localfn(lx);
            else if (match(lx, TK_CLASS))
                localclass(lx);
            else {
                localstm(lx);
                expectnext(lx, ';');
            }
            lx->fs->lastisend = 0; /* clear flag */
            break;
        default: stm_(lx);
    }
    toku_assert(stackinvariant(lx->fs));
    leaveCstack(lx);
}


static void stm(Lexer *lx) {
    enterCstack(lx);
    stm_(lx);
    toku_assert(stackinvariant(lx->fs));
    leaveCstack(lx);
}


/* compile main function */
static void mainfunc(FunctionState *fs, Lexer *lx) {
    Scope s;
    UpValInfo *env;
    open_func(lx, fs, &s);
    setvararg(fs, 0); /* main function is always vararg */
    env = newupvalue(fs);
    env->name = lx->envn;
    env->idx = 0;
    env->instack = 0; /* be consistent for 'toku_combine' */
    env->kind = VARREG;
    tokuG_objbarrier(lx->T, fs->p, env->name);
    tokuY_scan(lx); /* scan for first token */
    decl_list(lx, 0); /* parse main body */
    expect(lx, TK_EOS);
    close_func(lx);
}


TClosure *tokuP_parse(toku_State *T, BuffReader *Z, Buffer *buff,
                      DynData *dyd, const char *name, int32_t firstchar) {
    Lexer lx = {0};
    FunctionState fs = {0};
    TClosure *cl = tokuF_newTclosure(T, 1);
    setclTval2s(T, T->sp.p, cl); /* anchor main function closure */
    tokuT_incsp(T);
    lx.tab = tokuH_new(T); /* create table for scanner */
    settval2s(T, T->sp.p, lx.tab); /* anchor it */
    tokuT_incsp(T);
    fs.p = cl->p = tokuF_newproto(T);
    tokuG_objbarrier(T, cl, cl->p);
    fs.p->source = tokuS_new(T, name);
    tokuG_objbarrier(T, fs.p, fs.p->source);
    lx.buff = buff;
    lx.dyd = dyd;
    tokuY_setinput(T, &lx, Z, fs.p->source, firstchar);
    mainfunc(&fs, &lx);
    toku_assert(!fs.prev && fs.nupvals == 1 && !lx.fs);
    /* all scopes should be correctly finished */
    toku_assert(dyd->actlocals.len == 0 && dyd->gt.len == 0);
    T->sp.p--; /* remove scanner table */
    return cl; /* (closure is also on the stack) */
}
