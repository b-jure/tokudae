/*
** tstate.c
** Global and Thread state
** See Copyright Notice in tokudae.h
*/

#define tstate_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <string.h>

#include "ttable.h"
#include "tlist.h"
#include "tstate.h"
#include "tapi.h"
#include "tdebug.h"
#include "tprotected.h"
#include "tfunction.h"
#include "tgc.h"
#include "tmem.h"
#include "tmeta.h"
#include "tobject.h"
#include "tokudae.h"
#include "tstring.h"


/*
** These macros allow user-defined action to be taken each time
** thread is created/deleted and/or state is opened/closed.
*/
#if !defined(tokui_userstateopen)
#define tokui_userstateopen(T)          UNUSED(T)
#endif

#if !defined(tokui_userstateclose)
#define tokui_userstateclose(T)         UNUSED(T)
#endif

#if !defined(tokui_userstatethread)
#define tokui_userstatethread(T,T1)     (UNUSED(T), UNUSED(T1))
#endif

#if !defined(tokui_userstatefree)
#define tokui_userstatefree(T,T1)       (UNUSED(T), UNUSED(T1))
#endif


/*
** Preinitialize all thread fields to avoid collector
** errors.
*/
static void preinit_thread(toku_State *T, GState *gs) {
    T->ncf = 0;
    T->status = TOKU_STATUS_OK;
    T->errfunc = 0;
    T->nCcalls = 0;
    T->gclist = NULL;
    T->twups = T; /* if ('T->twups' == 'T') then no upvalues */
    G(T) = gs;
    T->errjmp = NULL;
    T->hook = NULL;
    T->hookmask = 0;
    T->oldpc = 0;
    T->basehookcount = 0;
    T->allowhook = 1;
    resethookcount(T);
    T->stack.p = T->sp.p = T->stackend.p = NULL;
    T->cf = NULL;
    T->openupval = NULL;
    T->tbclist.p = NULL;
    T->basecf.prev = T->basecf.next = NULL;
    T->transferinfo.ftransfer = T->transferinfo.ntransfer = 0;
}


static void resetCF(toku_State *T) {
    CallFrame *cf = T->cf = &T->basecf;
    cf->func.p = T->stack.p;
    setnilval(s2v(cf->func.p)); /* 'function' entry for basic 'cf' */
    cf->top.p = cf->func.p + 1 + TOKU_MINSTACK; /* +1 for 'function' entry */
    cf->status = CFST_C;
    cf->u.t.savedpc = NULL;
    cf->u.t.trap = 0;
    cf->u.t.nvarargs = 0;
    cf->nresults = cfnres(0);
    T->status = TOKU_STATUS_OK;
    T->errfunc = 0; /* stack unwind can "throw away" the error function */
}


/*
** Initialize stack and base call frame for 'T'.
** 'T' is a main thread state ('T1' == 'T' only when creating new
** state).
*/
static void stackinit(toku_State *T1, toku_State *T) {
    toku_assert(!statefullybuilt(G(T1)) == (T1 == T));
    T1->stack.p = tokuM_newarray(T, INIT_STACKSIZE + EXTRA_STACK, SValue);
    T1->tbclist.p = T1->stack.p;
    for (int32_t i = 0; i < INIT_STACKSIZE + EXTRA_STACK; i++)
        setnilval(s2v(T1->stack.p + i));
    T1->stackend.p = T1->stack.p + INIT_STACKSIZE;
    resetCF(T1);
    T1->sp.p = T1->stack.p + 1; /* +1 for function */
}


static void init_cstorage(toku_State *T, GState *gs) {
    List *clist = tokuA_new(T); 
    TValue key, val;
    setlistval(T, &gs->c_list, clist); /* gs->c_list = clist */
    tokuA_ensureindex(T, clist, TOKU_CLIST_LAST);
    setival(&key, TOKU_CLIST_MAINTHREAD);
    setthval(T, &val, T);
    tokuA_setindex(T, clist, &key, &val); /* clist[TOKU_CLIST_MAINTHREAD]=T */
    setival(&key, TOKU_CLIST_GLOBALS);
    settval(T, &val, tokuH_new(T));
    tokuA_setindex(T, clist, &key, &val); /* clist[TOKU_CLIST_GLOBALS]=gtab */
    toku_assert(clist->len == TOKU_CLIST_LAST + 1); /* sequence */
    settval(T, &gs->c_table, tokuH_new(T)); /* gs->c_table = table */
}


/*
** Initializes parts of state that may cause memory allocation
** errors.
*/
static void f_newstate(toku_State *T, void *ud) {
    GState *gs = G(T);
    UNUSED(ud);
    stackinit(T, T);
    init_cstorage(T, gs);
    tokuS_init(T); /* keep this init first */
    tokuY_init(T);
    tokuTM_init(T);
    tokuA_init(T);
    gs->gcstop = 0;
    setnilval(&gs->nil); /* signal that state is fully built */
    tokui_userstateopen(T);
}


/*
** Free all 'CallFrame' structures NOT in use by a thread.
*/
static void freeCF(toku_State *T) {
    CallFrame *cf = T->cf;
    CallFrame *next = cf->next;
    cf->next = NULL;
    while ((cf = next) != NULL) {
        next = cf->next;
        tokuM_free(T, cf);
        T->ncf--;
    }
}


/*
** Free thread stack and the call frames.
*/
static void freestack(toku_State *T) {
    if (T->stack.p != NULL) { /* stack fully built? */
        T->cf = &T->basecf; /* free the entire 'cf' list */
        freeCF(T);
        toku_assert(T->ncf == 0 && T->basecf.next == NULL);
        tokuM_freearray(T, T->stack.p, cast_sizet(stacksize(T) + EXTRA_STACK));
    }
}


static void freestate(toku_State *T) {
    GState *gs = G(T);
    toku_assert(T == G(T)->mainthread);
    if (!statefullybuilt(gs)) /* closing partially built state? */
        tokuG_freeallobjects(T); /* collect only objects */
    else { /* otherwise closing a fully built state */
        resetCF(T); /* undwind call stack */
        tokuPR_close(T, 1, TOKU_STATUS_OK); /* close all upvalues */
        T->sp.p = T->stack.p + 1; /* empty the stack to run finalizers */
        tokuG_freeallobjects(T); /* collect all objects */
        tokui_userstateclose(T);
    }
    tokuM_freearray(T, G(T)->strtab.hash, cast_sizet(G(T)->strtab.size));
    freestack(T);
    /* only global state remains, free it */
    toku_assert(gettotalbytes(gs) == sizeof(XSG));
    (*gs->falloc)(fromstate(T), gs->ud_alloc, sizeof(XSG), 0);
}


/*
** Initialize garbage collection parameters.
*/
static void initGCparams(GState *gs) {
    setgcparam(gs->gcparams[TOKU_GCP_PAUSE], TOKUI_GCP_PAUSE);
    setgcparam(gs->gcparams[TOKU_GCP_STEPMUL], TOKUI_GCP_STEPMUL);
    gs->gcparams[TOKU_GCP_STEPSIZE] = TOKUI_GCP_STEPSIZE;
}


/*
** Allocate new thread and global state with 'falloc' and
** userdata 'ud', from here on 'falloc' will be the allocator.
** The returned thread state is mainthread.
** In case of errors NULL is returned.
*/
TOKU_API toku_State *toku_newstate(toku_Alloc falloc, void *ud, uint32_t seed) {
    GState *gs;
    toku_State *T;
    XSG *xsg = cast(XSG *, falloc(NULL, ud, 0, sizeof(XSG)));
    if (t_unlikely(xsg == NULL)) return NULL;
    gs = &xsg->gs;
    T = &xsg->xs.t;
    T->tt_ = TOKU_VTHREAD;
    gs->whitebit = bitmask(WHITEBIT0);
    T->mark = tokuG_white(gs);
    preinit_thread(T, gs);
    T->next = NULL;
    incnny(T); /* main thread is always non yieldable */
    gs->objects = obj2gco(T);
    gs->totalbytes = sizeof(XSG);
    gs->seed = seed; /* initial seed for hashing */
    gs->strtab.hash = NULL;
    gs->strtab.nuse = gs->strtab.size = 0;
    gs->gcdebt = 0;
    gs->gcstate = GCSpause;
    gs->gcstopem = 0;
    initGCparams(gs);
    gs->gcstop = GCSTP; /* no GC while creating state */
    gs->gcemergency = 0;
    gs->gccheck = 0;
    gs->sweeppos = NULL;
    gs->fixed = gs->fin = gs->tobefin = NULL;
    gs->graylist = gs->grayagain = NULL;
    setnilval(&gs->c_list);
    setnilval(&gs->c_table);
    gs->falloc = falloc;
    gs->ud_alloc = ud;
    gs->fpanic = NULL; /* no panic handler by default */
    setival(&gs->nil, 0); /* signals that state is not yet fully initialized */
    gs->mainthread = T;
    gs->twups = NULL;
    gs->fwarn = NULL; gs->ud_warn = NULL;
    toku_assert(gs->totalbytes == sizeof(XSG) && gs->gcdebt == 0);
    if (tokuPR_runprotected(T, f_newstate, NULL) != TOKU_STATUS_OK) {
        freestate(T);
        T = NULL;
    }
    return T;
}


/* free state (global state + mainthread) */
TOKU_API void toku_close(toku_State *T) {
    toku_lock(T);
    toku_State *mt = G(T)->mainthread;
    freestate(mt);
}


/*
** Create new thread state.
*/
TOKU_API toku_State *toku_newthread(toku_State *T) {
    GState *gs = G(T);
    GCObject *o;
    toku_State *T1;
    toku_lock(T);
    tokuG_checkGC(T);
    o = tokuG_newoff(T, sizeof(XS), TOKU_VTHREAD, offsetof(XS, t));
    T1 = gco2th(o);
    setthval2s(T, T->sp.p, T1);
    api_inctop(T);
    preinit_thread(T1, gs);
    T1->hookmask = T->hookmask;
    T1->basehookcount = T->basehookcount;
    T1->hook = T->hook;
    resethookcount(T1);
    memcpy(toku_getextraspace(T1), toku_getextraspace(gs->mainthread),
           TOKU_EXTRASPACE);
    tokui_userstatethread(T, T1);
    stackinit(T1, T);
    toku_unlock(T);
    return T1;
}


int32_t tokuT_resetthread(toku_State *T, int32_t status) {
    CallFrame *cf = T->cf = &T->basecf;
    setnilval(s2v(T->stack.p)); /* 'basecf' func */
    cf->func.p = T->stack.p;
    cf->status = CFST_C;
    T->status = TOKU_STATUS_OK; /* so we can run '__close' */
    status = tokuPR_close(T, 1, status);
    if (status != TOKU_STATUS_OK) /* error? */
        tokuPR_seterrorobj(T, status, T->stack.p + 1);
    else
        T->sp.p = T->stack.p + 1;
    cf->top.p = T->sp.p + TOKU_MINSTACK;
    tokuPR_reallocstack(T, cast_i32(cf->top.p - T->sp.p), 0);
    return status;
}


/* TODO: add docs (by removing 'toku_resetthread') */
TOKU_API int32_t toku_closethread(toku_State *T, toku_State *from) {
    int32_t status;
    toku_lock(T);
    T->nCcalls = (from) ? getCcalls(from) : 0;
    status = tokuT_resetthread(T, T->status);
    if (T == from) /* closing itself? */
        tokuPR_throwbaselevel(T, status);
    toku_unlock(T);
    return status;
}


CallFrame *tokuT_newcf(toku_State *T) {
    CallFrame *cf;
    toku_assert(T->cf->next == NULL);
    cf = tokuM_new(T, CallFrame);
    toku_assert(T->cf->next == NULL);
    T->cf->next = cf;
    cf->prev = T->cf;
    cf->next = NULL;
    cf->u.t.trap = 0;
    T->ncf++;
    return cf;
}


/*
** Called when 'getCcalls' is >= TOKUI_MAXCCALLS.
** If equal to TOKUI_MAXCCALLS then overflow error is invoked.
** Otherwise it is ignored in order to resolve the current
** overflow error, unless the number of calls is significantly
** higher than TOKUI_MAXCCALLS.
*/
void tokuT_checkCstack(toku_State *T) {
    if (getCcalls(T) == TOKUI_MAXCCALLS) /* not handling error ? */
        tokuD_runerror(T, "C stack overflow");
    else if (getCcalls(T) >= (TOKUI_MAXCCALLS / 10 * 11))
        tokuPR_errerr(T); /* error while handling stack error */
}


/* Increment number of T calls and check for overflow. */
void tokuT_incCstack(toku_State *T) {
    T->nCcalls++;
    if (getCcalls(T) >= TOKUI_MAXCCALLS)
        tokuT_checkCstack(T);
}


void tokuT_warning(toku_State *T, const char *msg, int32_t cont) {
    toku_WarnFunction fwarn = G(T)->fwarn;
    if (fwarn)
        fwarn(G(T)->ud_warn, msg, cont);
}


/* generate a warning from an error message */
void tokuT_warnerror(toku_State *T, const char *where) {
    TValue *errobj = s2v(T->sp.p - 1);
    const char *msg = (ttisstring(errobj))
                      ? getstr(strval(errobj))
                      : "error object is not a string";
    tokuT_warning(T, "error in ", 1);
    tokuT_warning(T, where, 1);
    tokuT_warning(T, " (", 1);
    tokuT_warning(T, msg, 1);
    tokuT_warning(T, ")", 0);
}


void tokuT_free(toku_State *T, toku_State *T1) {
    XS *xs = fromstate(T1);
    tokuF_closeupval(T1, T1->stack.p);  /* close all upvalues */
    toku_assert(T1->openupval == NULL);
    tokui_userstatefree(T, T1);
    freestack(T1);
    tokuM_free(T, xs);
}
