/*
** tprotected.c
** Functions for calling functions in protected mode
** See Copyright Notice in tokudae.h
*/

#define tprotected_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "tapi.h"
#include "tbits.h"
#include "tdebug.h"
#include "tfunction.h"
#include "tgc.h"
#include "tmarshal.h"
#include "tmem.h"
#include "tobject.h"
#include "tparser.h"
#include "tprotected.h"
#include "treader.h"
#include "tstate.h"
#include "tstring.h"
#include "tvm.h"


#define errorstatus(s)      ((s) > TOKU_STATUS_YIELD)


/* some space for error handling */
#define ERRORSTACKSIZE      (TOKU_MAXSTACK + 200)


/*
** Bit to be set for "wanted" in 'poscall' and in 'moveresults' to check
** if the function has to-be-closed variables.
*/
#define TBCBIT      bitmask(WIDTH_ARG_L)


/*
** These macros allow user-specific actions when a thread is
** resumed/yielded.
*/
#if !defined (tokui_userstateyield)
#define tokui_userstateyield(T,nres)    (UNUSED(T), UNUSED(nres))
#endif

#if !defined (tokui_userstateresume)
#define tokui_userstateresume(T,narg)   (UNUSED(T), UNUSED(narg))
#endif


/*
** These macros are used to manipulate 'extra' in order to extract the
** total number of call, init and/or bound (meta)method calls in a chains.
** Bits 0-7 are reserved for status; bits 8-15 are reserved for counting
** call metamethods; bits 16-23 are reserved for counting init metamethods;
** bits 24-31 are reserved for counting bound methods.
*/

#define gstatus(extra)      ((extra) & 0xff)

#define incccall(extra)     ((extra) + 0x100u)
#define gccall(extra)       cast_u32(((extra) & 0xff00) >> 8u)

#define inccinit(extra)     ((extra) + 0x10000u)
#define gcinit(extra)       cast_u32(((extra) & 0xff0000) >> 16u)

#define inccmethod(extra)   ((extra) + 0x1000000u)
#define gcmethod(extra)     cast_u32(((extra) & 0xff000000) >> 24u)


/* chain list of long jump buffers */
typedef struct toku_longjmp {
    struct toku_longjmp *prev;
    jmp_buf buf;
    volatile int32_t status;
} toku_longjmp;


/*
** TOKUI_THROW/TOKUI_TRY define how Tokudae does exception handling. By
** default, Tokudae handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when available (POSIX), and with
** longjmp/setjmp otherwise.
*/
#if !defined(TOKUI_THROW)                               /* { */

#if defined(__cplusplus) && !defined(TOKU_USE_LONGJMP)  /* { */

/* C++ exceptions */
#define TOKUI_THROW(T,c)        throw(c)

static void TOKUI_TRY(toku_State *T, toku_longjmp *c, ProtectedFn f,
                                                      void *ud) {
    try {
        f(T, ud); /* call function protected */
    }
    catch (toku_longjmp *c1) { /* Tokudae error */
        if (c1 != c) /* not the correct level? */
            throw; /* rethrow to upper level */
    }
    catch (...) { /* non-Tokudae exception */
        c->status = -1; /* create some error code */
    }
}


#elif defined(TOKU_USE_POSIX)                           /* }{ */

/*
** In POSIX, use _longjmp/_setjmp
** (more efficient, does not manipulate signal mask).
*/
#define TOKUI_THROW(T,c)        _longjmp((c)->buf, 1)
#define TOKUI_TRY(T,c,f,ud)     if (_setjmp((c)->buf) == 0) ((f)(T,ud))


#else                                                   /* }{ */

/* ISO C handling with long jumps */
#define TOKUI_THROW(T,c)        longjmp((c)->buf, 1)
#define TOKUI_TRY(T,c,f,ud)     if (setjmp((c)->buf) == 0) ((f)(T,ud))

#endif                                                  /* } */

#endif                                                  /* } */


void tokuPR_seterrorobj(toku_State *T, int32_t errcode, SPtr oldsp) {
    if (errcode == TOKU_STATUS_EMEM) { /* memory error? */
        setstrval2s(T, oldsp, G(T)->memerror); /* reuse prereg. message */
    } else {
        toku_assert(errorstatus(errcode)); /* real error */
        toku_assert(!ttisnil(s2v(T->sp.p - 1))); /* with a non-nil object */
        setobjs2s(T, oldsp, T->sp.p - 1); /* move it to 'oldsp' */
    }
    T->sp.p = oldsp + 1; /* stack pointer goes to 'oldsp' plus error object */
}


/*
** Throw error to the current thread error handler, mainthread
** error handler or invoke panic if hook for it is present.
** In case none of the above occurs (or the panic doesn't jump out),
** program is aborted.
*/
t_noret tokuPR_throw(toku_State *T, int32_t errcode) {
    if (T->errjmp) { /* thread has error handler? */
        T->errjmp->status = errcode; /* set status */
        TOKUI_THROW(T, T->errjmp); /* jump to it */
    } else { /* thread has no error handler */
        GState *gs = G(T);
        toku_State *mainT = gs->mainthread;
        tokuT_resetthread(T, errcode); /* close all upvalues */
        T->status = errcode; /* mark it as "dead" */
        if (mainT->errjmp) { /* mainthread has error handler? */
            /* copy over error object */
            setobjs2s(T, mainT->sp.p++, T->sp.p - 1);
            tokuPR_throw(mainT, errcode); /* re-throw in main th. */
        } else { /* no error handlers, abort */
            if (gs->fpanic) { /* state has panic handler? */
                toku_unlock(T); /* release the lock... */
                gs->fpanic(T); /* ...and call it (last chance to jump out) */
            }
            abort();
        }
    }
}


int32_t tokuPR_runprotected(toku_State *T, ProtectedFn f, void *ud) {
    uint32_t old_nCcalls = T->nCcalls;
    toku_longjmp lj;
    lj.status = TOKU_STATUS_OK;
    lj.prev = T->errjmp;
    T->errjmp = &lj;
    TOKUI_TRY(T, &lj, f, ud); /* call 'f' catching errors */
    T->errjmp = lj.prev;
    T->nCcalls = old_nCcalls;
    return lj.status;
}


/* convert stack pointers into relative stack offsets */
static void relstack(toku_State *T) {
    T->sp.offset = savestack(T, T->sp.p);
    T->tbclist.offset = savestack(T, T->tbclist.p);
    for (UpVal *uv = T->openupval; uv != NULL; uv = uv->u.open.next)
        uv->v.offset = savestack(T, uvlevel(uv));
    for (CallFrame *cf = T->cf; cf != NULL; cf = cf->prev) {
        cf->func.offset = savestack(T, cf->func.p);
        cf->top.offset = savestack(T, cf->top.p);
        if (isTokudae(cf) && cf->u.t.savedsp.p != NULL)
            cf->u.t.savedsp.offset = savestack(T, cf->u.t.savedsp.p);
    }
}


/* convert relative stack offsets into stack pointers */
static void correctstack(toku_State *T) {
    T->sp.p = restorestack(T, T->sp.offset);
    T->tbclist.p = restorestack(T, T->tbclist.offset);
    for (UpVal *uv = T->openupval; uv != NULL; uv = uv->u.open.next)
        uv->v.p = s2v(restorestack(T, uv->v.offset));
    for (CallFrame *cf = T->cf; cf != NULL; cf = cf->prev) {
        cf->func.p = restorestack(T, cf->func.offset);
        cf->top.p = restorestack(T, cf->top.offset);
        if (isTokudae(cf)) {
            cf->u.t.trap = 1; /* signal to update 'trap' in 'tokuV_execute' */
            if (cf->u.t.savedsp.offset != 0) /* have stack pointer offset? */
                cf->u.t.savedsp.p = restorestack(T, cf->u.t.savedsp.offset);
        }
    }
}


// TODO: update docs
t_noret tokuPR_errerr(toku_State *T) {
    OString *msg = tokuS_newlit(T, "error in error handling");
    setsvalue2s(T, T->sp.p, msg);
    T->sp.p++; /* assume EXTRA_STACK */
    tokuPR_throw(T, TOKU_STATUS_EERR);
}


/* reallocate stack to new size */
int32_t tokuPR_reallocstack(toku_State *T, int32_t nsz, int32_t raiseerr) {
    int32_t osz = stacksize(T);
    int32_t old_stopem = G(T)->gcstopem;
    SPtr newstack;
    toku_assert(nsz <= TOKU_MAXSTACK || nsz == ERRORSTACKSIZE);
    relstack(T); /* change pointers to offsets */
    G(T)->gcstopem = 1; /* no emergency collection when reallocating stack */
    newstack = tokuM_reallocarray(T, T->stack.p, osz + EXTRA_STACK,
                                                 nsz + EXTRA_STACK, SValue);
    G(T)->gcstopem = cast_u8(old_stopem);
    if (t_unlikely(newstack == NULL)) {
        correctstack(T); /* change offsets back to pointers */
        if (raiseerr)
            tokuM_error(T);
        else return 0;
    }
    T->stack.p = newstack;
    correctstack(T); /* change offsets back to pointers */
    T->stackend.p = T->stack.p + nsz;
    for (int32_t i = osz + EXTRA_STACK; i < nsz + EXTRA_STACK; i++)
        setnilval(s2v(newstack + i)); /* erase new segment */
    return 1;
}


/* 
** Grow stack to accommodate 'n' values. When 'raiseerr' is true,
** raises any error; otherwise, return 0 in case of errors.
*/
int32_t tokuPR_growstack(toku_State *T, int32_t n, int32_t raiseerr) {
    int32_t size = stacksize(T);
    if (t_unlikely(size > TOKU_MAXSTACK)) { /* overflown already ? */
        /* if stack is larger than maximum, thread is already using the
        ** extra space reserved for errors, that is, thread is handling
        ** a stack error; cannot grow further than that. */
        toku_assert(size == ERRORSTACKSIZE);
        if (raiseerr)
            tokuPR_errerr(T); /* stack error inside message handler */
        return 0;
    } else if (n < TOKU_MAXSTACK) { /* avoid arithmetic overflow */
        int32_t nsize = size * 2;
        int32_t needed = cast_i32((T)->sp.p - (T)->stack.p) + n;
        if (nsize > TOKU_MAXSTACK)
            nsize = TOKU_MAXSTACK; /* clamp it */
        if (nsize < needed) /* size still too small? */
            nsize = needed; /* grow to needed size */
        if (t_likely(nsize <= TOKU_MAXSTACK)) /* new size is ok? */
            return tokuPR_reallocstack(T, nsize, raiseerr);
    } /* else stack overflow */
    /* add extra size to be able to handle the error message */
    tokuPR_reallocstack(T, ERRORSTACKSIZE, raiseerr);
    if (raiseerr)
        tokuD_runerror(T, "stack overflow");
    return 0;
}


static int32_t stackinuse(toku_State *T) {
    int32_t res;
    SPtr lim = T->sp.p;
    for (CallFrame *cf = T->cf; cf != NULL; cf = cf->prev) {
        if (lim < cf->top.p)
            lim = cf->top.p;
    }
    toku_assert(lim <= T->stackend.p + EXTRA_STACK);
    res = cast_i32(lim - T->stack.p) + 1; /* part of stack in use */
    if (res < TOKU_MINSTACK)
        res = TOKU_MINSTACK; /* ensure a minimum size */
    return res;
}


/*
** Shrink stack if the current stack size is more than 3 times the
** current use. This also rolls back the stack to its original maximum
** size 'TOKU_MAXSTACK' in case the stack was previously handling stack
** overflow.
*/
void tokuT_shrinkstack(toku_State *T) {
    int32_t inuse = stackinuse(T);
    int32_t limit = (inuse >= TOKU_MAXSTACK / 3 ? TOKU_MAXSTACK : inuse * 3);
    if (inuse <= TOKU_MAXSTACK && stacksize(T) > limit) {
        int32_t nsize = (inuse < (TOKU_MAXSTACK/2)) ?(inuse*2):TOKU_MAXSTACK;
        tokuPR_reallocstack(T, nsize, 0); /* this can fail */
    }
}


/* increment stack pointer */
void tokuPR_incsp(toku_State *T) {
    tokuPR_checkstack(T, 1);
    T->sp.p++;
}


int32_t tokuPR_pcall(toku_State *T, ProtectedFn f, void *ud, ptrdiff_t oldsp,
                     ptrdiff_t ef) {
    int32_t status;
    CallFrame *old_cf = T->cf;
    uint8_t old_allowhook = T->allowhook;
    ptrdiff_t old_errfunc = T->errfunc;
    T->errfunc = ef;
    status = tokuPR_runprotected(T, f, ud);
    if (t_unlikely(status != TOKU_STATUS_OK)) {
        T->cf = old_cf;
        T->allowhook = old_allowhook;
        status = tokuPR_close(T, oldsp, status);
        tokuPR_seterrorobj(T, status, restorestack(T, oldsp));
        tokuT_shrinkstack(T); /* restore stack size in case of overflow */
    }
    T->errfunc = old_errfunc;
    return status;
}


/*
** Executes a return hook for Tokudae and C functions and sets/corrects
** 'oldpc'. (Note that this correction is needed by the line hook, so it
** is done even when return hooks are off.)
*/
static void rethook(toku_State *T, CallFrame *cf, int32_t nres) {
    if (T->hookmask & TOKU_MASK_RET) { /* is return hook on? */
        SPtr firstres = T->sp.p - nres; /* index of first result */
        int32_t delta = 0; /* correction for vararg functions */
        int32_t ftransfer;
        if (isTokudae(cf)) {
            Proto *p = cf_func(cf)->p;
            delta = !!p->isvararg * (cf->u.t.nvarargs + p->arity + 1);
        }
        cf->func.p += delta; /* if vararg, back to virtual function */
        ftransfer = cast_i32(firstres - cf->func.p - 1);
        toku_assert(ftransfer >= 0);
        tokuD_hook(T, TOKU_HOOK_RET, -1, ftransfer, nres); /* call it */
        cf->func.p -= delta; /* if vararg, back to original function */
    }
    if (isTokudae(cf = cf->prev))
        T->oldpc = relpc(cf->u.t.savedpc, cf_func(cf)->p); /* set 'oldpc' */
}


/* generic case for 'moveresults' */
t_sinline void genmoveresults(toku_State *T, SPtr res, int32_t nres,
                              int32_t wanted) {
    SPtr firstresult = T->sp.p - nres; /* index of first result */
    int32_t i;
    if (nres > fwanted) /* have extra results? */
        nres = fwanted; /* discard them */
    for (i = 0; i < nres; i++) /* move all the results */
        setobjs2s(T, res + i, firstresult + i);
    for (; i < fwanted; i++) /* complete wanted number of results */
        setnilval(s2v(res + i));
    T->sp.p = res + fwanted; /* stack pointer points after the last result */
}


/* move call results and if needed close variables */
t_sinline void moveresults(toku_State *T, SPtr res, int32_t nres,
                           uint32_t fwanted) {
    int32_t i;
    SPtr firstresult;
    switch (fwanted) {
        case 0 + 1: /* no values needed */
            T->sp.p = res;
            return; /* done */
        case 1 + 1: /* one value needed */
            if (nres == 0) /* no results? */
                setnilval(s2v(res)); /* adjust with nil */
            else /* otherwise at least one result */
                setobjs2s(T, res, T->sp.p - nres); /* move to proper place */
            T->sp.p = res + 1;
            return; /* done */
        case TOKU_MULTRET + 1: /* all values needed */
            genmoveresults(T, res, nres, nres);
            break;
        default: { /* two/more results and/or to-be-closed variables */
            int32_t wanted = (fwanted & ~(TBCBIT)) - 1;
            if (fwanted & TBCBIT) { /* to-be-closed variables? */
                T->cf->u2.nres = nres;
                T->cf->status |= CFST_CLSRET; /* in case of yields */
                res = tokuF_close(T, res, CLOSEKTOP, 1);
                T->cf->status &= ~CFST_CLSRET;
                if (T->hookmask) { /* if needed, call hook after '__close's */
                    ptrdiff_t savedres = savestack(T, res);
                    rethook(T, T->cf, nres);
                    res = restorestack(T, savedres); /* hook can move stack */
                }
                if (wanted == TOKU_MULTRET)
                    wanted = nres; /* we want all results */
            }
            genmoveresults(T, res, nres, wanted);
        }
    }
}


/* get the next call frame or allocate a new one */
#define next_cf(T)   ((T)->cf->next ? (T)->cf->next : tokuT_newcf(T))


t_sinline CallFrame *prepCallFrame(toku_State *T, SPtr func,
                                   uint32_t extra, int32_t nres, SPtr top) {
    CallFrame *cf = T->cf = next_cf(T);
    cf->func.p = func;
    cf->top.p = top;
    cf->nresults = cfnres(nres);
    cf->extraargs = gccall(extra) + gcinit(extra) + gcmethod(extra);
    cf->status = cast_u16(gstatus(extra));
    toku_assert((cf->status & ~(CFST_C)) == 0);
    return cf;
}


/* move the results into correct place and return to caller */
void tokuPR_poscall(toku_State *T, CallFrame *cf, int32_t nres) {
    uint8_t tbc = ((cf->status & CFST_TBC) != 0);
    uint32_t fwanted = cf->nresults | (TBCBIT * tbc);
    if (t_unlikely(T->hookmask) && !(fwanted & TBCBIT))
        rethook(T, cf, nres); /* check return hook */
    /* move results to proper place */
    moveresults(T, cf->func.p, nres, fwanted);
    /* function cannot be in any of these cases when returning */
    toku_assert(!(cf->status &
                  (CFST_HOOKED | CFST_YPCALL | CFST_FIN | CFST_CLSRET)));
    T->cf = cf->prev; /* back to caller (after closing variables) */
}


t_sinline int32_t precallC(toku_State *T, SPtr func,
                           uint32_t extra, int32_t nres, toku_CFunction f) {
    int32_t n;
    CallFrame *cf;
    checkstackGCp(T, TOKU_MINSTACK, func); /* ensure minimum stack space */
    T->cf = cf = prepCallFrame(T, func, extra | CFST_C, nres,
                                  T->sp.p + TOKU_MINSTACK);
    toku_assert(cf->top.p <= T->stackend.p);
    if (t_unlikely(T->hookmask & TOKU_MASK_CALL)) {
        int32_t narg = cast_i32(T->sp.p - func) - 1;
        tokuD_hook(T, TOKU_HOOK_CALL, -1, 0, narg);
    }
    toku_unlock(T);
    n = (*f)(T);
    toku_lock(T);
    api_checknelems(T, n);
    tokuPR_poscall(T, cf, n);
    return n;
}


/* 
** Shifts stack by one slot in direction of stack pointer,
** and inserts 'f' in place of 'func'.
** WARNING: this function assumes there is enough space for 'f'.
*/
t_sinline void auxinsertf(toku_State *T, SPtr func, const TValue *f) {
    for (SPtr p = T->sp.p; p > func; p--)
        setobjs2s(T, p, p-1);
    T->sp.p++;
    setobj2s(T, func, f);
}


t_sinline int32_t callclass(toku_State *T, SPtr *func, uint32_t extra) {
    const TValue *tm = NULL;
    OClass *cls = classval(s2v(*func));
    Table *mt = cls->metatable;
    Instance *ins = tokuTM_newinstance(T, cls);
    tokuG_checkfin(T, obj2gco(ins), mt);
    setinsval2s(T, *func, ins); /* replace class with its instance */
    if (fasttm(T, mt, TM_INIT)) { /* have __init (before GC)? */
        checkstackGCp(T, 1, *func); /* space for 'tm' */
        tm = fasttm(T, ins->oclass->metatable, TM_INIT); /* (after GC) */
    }
    if (tm) { /* have __init (after GC)? */
        auxinsertf(T, *func, tm);
        if (t_unlikely(gcinit(extra) == CALLCHAIN_MAX))
            tokuD_runerror(T, "'__init' chain too long");
        return 1; /* try again */
    } else
        return 0;
}


#define callbmethod(T,func,as,set,self,extra,t) \
    { t *bm_ = as(s2v(func)); \
      checkstackGCp(T, 1, func); /* space for method and instance */ \
      auxinsertf(T, func, &bm_->method); /* insert method object... */ \
      set(T, func + 1, bm_->self); /* and self as first arg */ \
      if (t_unlikely(gcmethod(extra) >= CALLCHAIN_MAX)) \
          tokuD_runerror(T, "bound method call chain too long"); }


t_sinline uint32_t trymetacall(toku_State *T, SPtr func, uint32_t extra) {
    const TValue *f = tokuTM_objget(T, s2v(func), TM_CALL);
    if (t_unlikely(notm(f))) /* missing __call? */
        tokuD_callerror(T, s2v(func));
    auxinsertf(T, func, f);
    if (t_unlikely(gccall(extra) == CALLCHAIN_MAX))
        tokuD_runerror(T, "'__call' chain too long");
    return incccall(extra);
}


CallFrame *tokuPR_precall(toku_State *T, SPtr func, int32_t nres) {
    uint32_t extra = 0; /* extraargs + status */
retry:
    switch (ttypetag(s2v(func))) {
        case TOKU_VCCL: /* C closure */
            precallC(T, func, extra, nres, clCval(s2v(func))->fn);
            return NULL; /* done */
        case TOKU_VLCF: /* light C function */
            precallC(T, func, extra, nres, lcfval(s2v(func)));
            return NULL; /* done */
        case TOKU_VTCL: { /* Tokudae closure */
            CallFrame *cf;
            Proto *f = clTval(s2v(func))->p;
            int32_t narg = cast_i32(T->sp.p - func - 1); /* num. arguments */
            int32_t nparams = f->arity; /* number of fixed parameters */
            int32_t fsize = f->maxstack; /* frame size */
            checkstackGCp(T, fsize, func);
            T->cf = cf = prepCallFrame(T, func, extra, nres, func+1+fsize);
            cf->u.t.savedpc = f->code; /* set starting point */
            for (; narg < nparams; narg++)
                setnilval(s2v(T->sp.p++)); /* set missing as 'nil' */
            if (!f->isvararg) /* not a vararg function? */
                T->sp.p = func + 1 + nparams; /* might have extra args */
            toku_assert(cf->top.p <= T->stackend.p);
            return cf; /* new call frame */
        }
        case TOKU_VIMETHOD: /* Instance method */
            callbmethod(T, func, imval, setinsval2s, ins, extra, IMethod);
            goto retry_bm;
        case TOKU_VUMETHOD: /* UserData method */
            callbmethod(T, func, umval, setudval2s, ud, extra, UMethod);
        retry_bm:
            extra = inccmethod(extra);
            goto retry; /* try again with method object */
        case TOKU_VCLASS: { /* Class object */
            if (!callclass(T, &func, extra)) { /* no __init? */
                toku_assert(!hastocloseCfunc(nres));
                moveresults(T, func, 1, nres, 0); /* instance is returned */
                return NULL; /* done */
            }
            extra = inccinit(extra);
            goto retry; /* try again with __init */
        }
        default: /* try __call metamethod */
            checkstackGCp(T, 1, func); /* space for func */
            extra = trymetacall(T, func, extra);
            goto retry; /* try again with __call */
    }
}


/*
** Prepare a function for a tail call, building its call frame on top
** of the current call frame. 'narg1' is the number of arguments plus 1
** (so that it includes the function itself). Return the number of
** results, if it was a C function, or -1 for a Tokudae function.
*/
int32_t tokuPR_pretailcall(toku_State *T, CallFrame *cf, SPtr func,
                                  int32_t narg1, int32_t delta) {
    uint32_t extra = 0; /* extraargs + status */
retry:
    switch (ttypetag(s2v(func))) {
        case TOKU_VCCL: /* C closure */
            return precallC(T, func, extra, TOKU_MULTRET, clCval(s2v(func))->fn);
        case TOKU_VLCF: /* light C function */
            return precallC(T, func, extra, TOKU_MULTRET, lcfval(s2v(func)));
        case TOKU_VTCL: { /* Tokudae function */
            Proto *f = clTval(s2v(func))->p;
            int32_t nparams = f->arity; /* number of fixed parameters */
            int32_t fsize = f->maxstack; /* frame size */
            checkstackGCp(T, fsize - delta, func);
            cf->func.p -= delta; /* restore 'func' (if vararg) */
            /* move down function and arguments */
            for (int32_t i = 0; i < narg1; i++)
                setobjs2s(T, cf->func.p + i, func + i);
            func = cf->func.p; /* moved-down function */
            for (; narg1 <= nparams; narg1++)
                setnilval(s2v(func + narg1)); /* complete missing arguments */
            cf->top.p = func + 1 + fsize; /* top for new function */
            toku_assert(cf->top.p <= T->stackend.p);
            cf->u.t.savedpc = f->code; /* set starting point */
            cf->status |= CFST_TAIL;
            if (!f->isvararg) /* not a vararg function? */
                T->sp.p = func + nparams + 1; /* (leave only parameters) */
            else /* otherwise leave all arguments */
                T->sp.p = func + narg1;
            return -1;
        }
        case TOKU_VIMETHOD: /* Instance method */
            callbmethod(T, func, imval, setinsval2s, ins, extra, IMethod);
            goto retry_bm;
        case TOKU_VUMETHOD: /* UserData method */
            callbmethod(T, func, umval, setudval2s, ud, extra, UMethod);
        retry_bm:
            extra = inccmethod(extra);
            /* return tokuPR_pretailcall(T, cf, func, narg1 + 2, delta); */
            narg1 += 2;
            goto retry; /* try again with method object */
        case TOKU_VCLASS: { /* Class object */
            if (!callclass(T, &func, extra)) { /* no __init? */
                T->sp.p = func + 1; /* leave only class instance */
                return 1; /* returns class instance */
            }
            extra = inccinit(extra);
            goto retry1; /* try again with __init */
        }
        default:
            checkstackGCp(T, 1, func); /* space for func */
            extra = trymetacall(T, func, extra); /* try __call metamethod */
        retry1:
            /* return tokuPR_pretailcall(T, cf, func, narg1 + 1, delta); */
            narg1++;
            goto retry; /* try again */
    }
}

/*
** Call a function (C or Tokudae) through C. 'inc' can be 1 (increment
** number of recursive invocations in the C stack) or nyci (the same
** plus increment number of non-yieldable calls).
** This function can be called with some use of EXTRA_STACK, so it should
** check the stack before doing anything else. 'tokuPR_precall' already
** does that.
*/
t_sinline void ccall(toku_State *T, SPtr func, int32_t nres, uint32_t inc) {
    CallFrame *cf;
    T->nCcalls += inc;
    if (t_unlikely(getCcalls(T) >= TOKUI_MAXCCALLS)) {
        checkstackp(T, 0, func); /* free any use of EXTRA_STACK */
        tokuT_checkCstack(T);
    }
    if ((cf = tokuPR_precall(T, func, nres)) != NULL) { /* Tokudae function? */
        cf->status = CFST_FRESH; /* mark it as a "fresh" execute */
        tokuV_execute(T, cf); /* call it */
    }
    T->nCcalls -= inc;
}


/*
** External interface for 'ccall'
*/
void tokuPR_call(toku_State *T, SPtr func, int32_t nres) {
    ccall(T, func, nres, 1);
}


/*
** Similar to 'tokuPR_call', but does not allow yields during the call.
*/
void tokuPR_callnoyield(toku_State *T, SPtr func, int32_t nres) {
    ccall(T, func, nres, nyci);
}


TOKU_API int toku_yieldk(toku_State *T, int32_t nresults, toku_KContext cx,
                         toku_KFunction k) {
    CallFrame *cf;
    tokui_userstateyield(T, nresults);
    toku_lock(T);
    cf = T->cf;
    api_checknelems(T, nresults);
    if (t_unlikely(!yieldable(T))) { /* can't yield? */
        if (T != G(T)->mainthread) /* not the main thread? */
            tokuD_runerror(T, "attempt to yield across a C-call boundary");
        else /* otherwise tried to yield the main thread */
            tokuD_runerror(T, "attempt to yield from outside a coroutine");
    }
    T->status = TOKU_STATUS_YIELD;
    cf->u2.nyield = nresults; /* save number of results */
    if (isTokudae(cf)) { /* inside a hook? */
        toku_assert(!isTokudaecode(cf));
        api_check(T, nresults == 0, "hooks cannot yield values");
        api_check(T, k == NULL, "hooks cannot continue after yielding");
    } else { /* usual yield */
        if ((cf->u.c.k = k) != NULL) /* is there a continuation? */
            cf->u.c.cx = cx; /* save context */
        tokuPR_throw(T, TOKU_STATUS_YIELD); /* do the yield */
    }
    toku_assert(cf->status & CFST_HOOKED); /* must be inside a hook */
    toku_unlock(T);
    return 0; /* return to 'tokuD_hook' */
}


/*
** Finish the job of 'toku_pcallk' after it was interrupted by an yield.
** (The caller, 'finishCcall', does the final call to 'adjustresults'.)
** The main job is to complete the 'tokuPR_pcall' called by 'toku_pcallk'.
** If a '__close' method yields here, eventually control will be back
** to 'finishCcall' (when that '__close' method finally returns) and
** 'finishpcallk' will run again and close any still pending '__close'
** methods. Similarly, if a '__close' method errors, 'precover' calls
** 'unroll' which calls 'finishCcall' and we are back here again, to
** close any pending '__close' methods.
** Note that, up to the call to 'tokuF_close', the corresponding
** 'CallFrame' is not modified, so that this repeated run works like the
** first one (except that it has at least one less '__close' to do). In
** particular, field CFST_RECST preserves the error status across these
** multiple runs, changing only if there is a new error.
*/
static int32_t finishpcallk(toku_State *T,  CallFrame *cf) {
    int32_t status = getcfstrecst(cf); /* get original status */
    if (t_likely(status == TOKU_STATUS_OK)) /* no error? */
        status = TOKU_STATUS_YIELD; /* was interrupted by an yield */
    else { /* otherwise error */
        SPtr func = restorestack(T, cf->u2.funcidx);
        T->allowhook = getoah(cf); /* restore original 'allowhook' */
        func = tokuF_close(T, func, status, 1); /* can yield or raise error */
        tokuPR_seterrorobj(T, status, func);
        tokuT_shrinkstack(T); /* restore stack size in case of overflow */
        setcfstrecst(cf, TOKU_STATUS_OK); /* clear original status */
    }
    cf->status &= ~CFST_YPCALL;
    T->errfunc = cf->u.c.old_errfunc;
    /* if it is here, there were errors or yields; unlike 'toku_pcallk',
       do not change status */
    return status;
}


/*
** Completes the execution of a C function interrupted by an yield.
** The interruption must have happened while the function was either
** closing its tbc variables in 'moveresults' or executing
** 'toku_callk'/'toku_pcallk'. In the first case, it just redoes
** 'tokuPR_poscall'. In the second case, the call to 'finishpcallk'
** finishes the interrupted execution of 'toku_pcallk'.  After that, it
** calls the continuation of the interrupted function and finally it
** completes the job of the 'tokuPR_call' that called the function.  In
** the call to 'adjustresults', we do not know the number of results
** of the function called by 'toku_callk'/'toku_pcallk', so we are
** conservative and use TOKU_MULTRET (always adjust).
*/
static void finishCcall(toku_State *T, CallFrame *cf) {
    int n; /* actual number of results from C function */
    if (cf->status & CFST_CLSRET) { /* was closing TBC variable? */
        toku_assert(cf->status & CFST_TBC); /* must have mark */
        n = cf->u2.nres; /* just redo 'tokuPR_poscall' */
        /* don't need to reset CFST_CLSRET, as it will be set again anyway */
    } else {
        int32_t status = TOKU_STATUS_YIELD; /* default status, if no errors */
        toku_KFunction kf = cf->u.c.k; /* continuation function */
        /* must have a continuation and must be able to call it */
        toku_assert(kf != NULL && yieldable(T));
        if (cf->status & CFST_YPCALL) /* was inside a 'toku_pcallk'? */
            status = finishpcallk(T, cf); /* finish it */
        adjustresults(T, TOKU_MULTRET); /* finish 'toku_callk' */
        toku_unlock(T);
        n = (*kf)(T, status, cf->u.c.ctx); /* call continuation */
        toku_lock(T);
        api_checknelems(T, n);
    }
    tokuPR_poscall(T, cf, n); /* finish 'tokuPR_call' */
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty
** (or another interruption long-jumps out of the loop).
*/
static void unroll(toku_State *T, void *ud) {
    UNUSED(ud);
    for (CallFrame *cf = T->cf; cf != &T->basecf; cf = T->cf) {
        if (!isTokudae(cf)) /* C function? */
            finishCcall(T, cf); /* complete its execution */
        else { /* otherwise Tokudae function */
            tokuV_finishOp(T); /* finish interrupted opcode */
            tokuV_execute(T, cf); /* execute down to higher C 'boundary' */
        }
    }
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallFrame *findpcall(toku_State *T) {
    CallFrame *cf;
    for (cf = T->cf; cf != NULL; cf = cf->prev) { /* search for a pcall */
        if (cf->status & CFST_YPCALL)
            return cf;
    }
    return NULL; /* no pending pcall */
}


/*
** Signal an error in the call to 'toku_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error(toku_State *T, const char *msg, int narg) {
    api_checkpop(T, narg);
    T->sp.p -= narg; /* remove args from the stack */
    setsvalue2s(T, T->sp.p, tokuS_new(T, msg)); /* push error message */
    api_inctop(T);
    toku_unlock(T);
    return TOKU_STATUS_ERUN;
}


/*
** Do the work for 'toku_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume(toku_State *T, void *ud) {
    int n = *(cast(int32_t*, ud)); /* number of arguments */
    SPtr firstarg = T->sp.p - n; /* first argument */
    CallFrame *cf = T->cf;
    if (T->status == TOKU_STATUS_OK) /* starting a coroutine? */
        ccall(T, firstarg - 1, TOKU_MULTRET, 0); /* just call its body */
    else { /* resuming from previous yield */
        toku_assert(T->status == TOKU_STATUS_YIELD);
        T->status = TOKU_STATUS_OK; /* mark that it is running (again) */
        if (isTokudae(cf)) { /* yielded inside a hook? */
            toku_assert(cf->status & CFST_HOOKYIELD); /* (will skip hook) */
            cf->u.t.savedpc--; /* undo increment made by 'tokuD_traceexec' */
            T->sp.p = firstarg; /* discard arguments */
            tokuV_execute(T, cf); /* just continue running Tokudae code */
        } else { /* 'common' yield */
            if (cf->u.c.k != NULL) { /* does it have a continuation? */
                toku_unlock(T);
                /* call continuation */
                n = (*cf->u.c.k)(T, TOKU_STATUS_YIELD, cf->u.c.ctx);
                toku_lock(T);
                api_checknelems(T, n);
            }
            tokuPR_poscall(T, cf, n); /* finish 'tokuPR_call' */
        }
        unroll(T, NULL); /* run continuation */
    }
}


/*
** Unrolls a coroutine in protected mode while there are recoverable
** errors, that is, errors inside a protected call. (Any error
** interrupts 'unroll', and this loop protects it again so it can
** continue.) Stops with a normal end (status == TOKU_STATUS_OK),
** an yield (status == TOKU_STATUS_YIELD), or an unprotected error
** ('findpcall' doesn't find a recover point).
*/
static int32_t precover(toku_State *T, int32_t status) {
    CallFrame *cf;
    while (errorstatus(status) && (cf = findpcall(T)) != NULL) {
        T->cf = cf; /* go down to recovery functions */
        setcfstrecst(cf, status); /* status to finish 'pcall' */
        status = tokuPR_runprotected(T, unroll, NULL);
    }
    return status;
}


TOKU_API int32_t toku_resume(toku_State *T, toku_State *from, int32_t narg,
                             int32_t *nres) {
    int32_t status;
    toku_lock(T);
    if (T->status == TOKU_STATUS_OK) { /* may be starting a coroutine */
        if (T->cf != &T->basecf) /* not in base level? */
            return resume_error(T, "cannot resume non-suspended coroutine", narg);
        else if (T->sp.p - (T->cf->func.p + 1) == narg) /* no function? */
            return resume_error(T, "cannot resume dead coroutine", narg);
    } else if (T->status != TOKU_STATUS_YIELD) /* ended with errors? */
        return resume_error(T, "cannot resume dead coroutine", narg);
    T->nCcalls = from ? getCcalls(from) : 0;
    if (getCcalls(T) >= TOKUI_MAXCCALLS)
        return resume_error(T, "C stack overflow", narg);
    T->nCcalls++;
    tokui_userstateresume(T, narg);
    api_checknelems(T, (T->status == TOKU_STATUS_OK) ? narg + 1 : narg);
    status = tokuPR_runprotected(T, resume, &narg);
    /* continue running after recoverable errors */
    status = precover(T, status);
    if (t_likely(!errorstatus(status)))
        toku_assert(status == T->status); /* normal end or yield */
    else { /* unrecoverable error */
        T->status = status; /* mark thread as 'dead' */
        tokuPR_seterrorobj(T, status, T->sp.p); /* push error message */
        T->cf->top.p = T->sp.p;
    }
    *nres = (status == TOKU_STATUS_YIELD)
           ? T->cf->u2.nyield /* yielded values */
           : cast_i32(T->sp.p - (T->cf->func.p + 1)); /* all on the stack */
    toku_unlock(T);
    return status;
}


TOKU_API int32_t toku_isyieldable(toku__State *T) {
    return yieldable(T);
}


/* auxiliary structure to call 'tokuF_close' in protected mode */
struct PCloseData {
    SPtr level;
    int32_t status;
};


/* auxiliary function to call 'tokuF_close' in protected mode */
static void closep(toku_State *T, void *ud) {
    struct PCloseData *cd = cast(struct PCloseData *, ud);
    tokuF_close(T, cd->level, cd->status);
}


/* call 'tokuF_close' in protected mode */
int32_t tokuPR_close(toku_State *T, ptrdiff_t level, int32_t status) {
    CallFrame *old_cf = T->cf;
    uint8_t old_allowhook = T->allowhook;
    for (;;) { /* keep closing upvalues until no more errors */
        struct PCloseData cd = {
            .level = restorestack(T, level),
            .status = status
        };
        status = tokuPR_runprotected(T, closep, &cd);
        if (t_likely(status == TOKU_STATUS_OK))
            return cd.status;
        else { /* error occurred; restore saved state and repeat */
            T->cf = old_cf;
            T->allowhook = old_allowhook;
        }
    }
}


/* auxiliary structure to call 'tokuP_parse' in protected mode */
struct PParseData {
    BuffReader *Z;
    Buffer buff;
    DynData dyd;
    const char *name;
    const char *mode;
};


static void checkmode(toku_State *T, const char *mode, const char *x) {
    if (t_unlikely(strchr(mode, x[0]) == NULL)) {
        tokuS_pushfstring(T,
                "attempt to load a %s chunk (mode is '%s')", x, mode);
        tokuPR_throw(T, TOKU_STATUS_ESYNTAX);
    }
}


/* auxiliary function to call 'tokuP_pparse' in protected mode */
static void pparse(toku_State *T, void *userdata) {
    TClosure *cl;
    struct PParseData *p = cast(struct PParseData *, userdata);
    const char *mode = p->mode ? p->mode : "bt";
    int32_t c = zgetc(p->Z);
    if (c == TOKU_SIGNATURE[0]) { /* binary chunk? */
        checkmode(T, mode, "binary");
        cl = tokuZ_undump(T, p->Z, p->name);
    } else { /* otherwise text */
        checkmode(T, mode, "text");
        cl = tokuP_parse(T, p->Z, &p->buff, &p->dyd, p->name, c);
    }
    toku_assert(cl->nupvals == cl->p->sizeupvals);
    tokuF_initupvals(T, cl);
}


/* call 'tokuP_parse' in protected mode */
int32_t tokuPR_parse(toku_State *T, BuffReader *Z, const char *name,
                                                   const char *mode) {
    int32_t status;
    struct PParseData p = { .Z = Z, .name = name, .mode = mode };
    incnny(T);
    status = tokuPR_pcall(T, pparse, &p, savestack(T, T->sp.p), T->errfunc);
    tokuR_freebuffer(T, &p.buff);
    tokuM_freearray(T, p.dyd.actlocals.arr, cast_sizet(p.dyd.actlocals.size));
    tokuM_freearray(T, p.dyd.literals.arr, cast_sizet(p.dyd.literals.size));
    tokuM_freearray(T, p.dyd.gt.arr, cast_sizet(p.dyd.gt.size));
    decnny(T);
    return status;
}
