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



/*
** {======================================================
** Error-recovery functions
** =======================================================
*/


/* chain list of long jump buffers */
typedef struct toku_longjmp {
    struct toku_longjmp *prev;
    jmp_buf buf;
    volatile int status;
} toku_longjmp;


/*
** TOKUI_THROW/TOKUI_TRY define how Tokudae does exception handling. By
** default, Tokudae handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when available (POSIX), and with
** longjmp/setjmp otherwise.
*/
#if !defined(TOKUI_THROW)				/* { */

#if defined(__cplusplus) && !defined(TOKU_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define TOKUI_THROW(T,c)	throw(c)

static void TOKUI_TRY(toku_State *T, toku_longjmp *c, ProtectedFn f, void *ud) {
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


#elif defined(TOKU_USE_POSIX)				/* }{ */

/*
** In POSIX, use _longjmp/_setjmp
** (more efficient, does not manipulate signal mask).
*/
#define TOKUI_THROW(T,c)	_longjmp((c)->buf, 1)
#define TOKUI_TRY(T,c,f,ud)	if (_setjmp((c)->buf) == 0) ((f)(T,ud))


#else						        /* }{ */

/* ISO C handling with long jumps */
#define TOKUI_THROW(T,c)	longjmp((c)->buf, 1)
#define TOKUI_TRY(T,c,f,ud)	if (setjmp((c)->buf) == 0) ((f)(T,ud))

#endif							/* } */

#endif							/* } */


void tokuPR_seterrorobj(toku_State *T, int errcode, SPtr oldtop) {
    switch (errcode) {
        case TOKU_STATUS_EMEM: { /* memory error? */
            setstrval2s(T, oldtop, G(T)->memerror);
            break;
        }
        case TOKU_STATUS_EERROR: { /* error while handling error? */
            setstrval2s(T, oldtop, tokuS_newlit(T, "error in error handling"));
            break;
        }
        case TOKU_STATUS_OK: { /* closing upvalue? */
            setnilval(s2v(oldtop)); /* no error message */
            break;
        }
        default: {
            toku_assert(errcode > TOKU_STATUS_OK); /* real error */
            setobjs2s(T, oldtop, T->sp.p - 1); /* error msg on current top */
            break;
        }
    }
    T->sp.p = oldtop + 1;
}


/*
** Throw error to the current thread error handler, mainthread
** error handler or invoke panic if hook for it is present.
** In case none of the above occurs, program is aborted.
*/
t_noret tokuPR_throw(toku_State *T, int errcode) {
    if (T->errjmp) { /* thread has error handler? */
        T->errjmp->status = errcode; /* set status */
        TOKUI_THROW(T, T->errjmp); /* jump to it */
    } else { /* thread has no error handler */
        GState *gs = G(T);
        tokuT_resetthread(T, errcode); /* close all */
        if (gs->mainthread->errjmp) { /* mainthread has error handler? */
            /* copy over error object */
            setobjs2s(T, gs->mainthread->sp.p++, T->sp.p - 1);
            tokuPR_throw(gs->mainthread, errcode); /* re-throw in main th. */
        } else { /* no error handlers, abort */
            if (gs->fpanic) { /* state has panic handler? */
                toku_unlock(T); /* release the lock... */
                gs->fpanic(T); /* ...and call it */
            }
            abort();
        }
    }
}


int tokuPR_rawcall(toku_State *T, ProtectedFn f, void *ud) {
    t_uint32 old_nCcalls = T->nCcalls;
    toku_longjmp lj;
    lj.status = TOKU_STATUS_OK;
    lj.prev = T->errjmp;
    T->errjmp = &lj;
    TOKUI_TRY(T, &lj, f, ud); /* call 'f' catching errors */
    T->errjmp = lj.prev;
    T->nCcalls = old_nCcalls;
    return lj.status;
}

/* }====================================================== */


int tokuPR_call(toku_State *T, ProtectedFn f, void *ud, ptrdiff_t old_top,
                                                        ptrdiff_t ef) {
    int status;
    CallFrame *old_cf = T->cf;
    t_ubyte old_allowhook = T->allowhook;
    ptrdiff_t old_errfunc = T->errfunc;
    T->errfunc = ef;
    status = tokuPR_rawcall(T, f, ud);
    if (t_unlikely(status != TOKU_STATUS_OK)) {
        T->cf = old_cf;
        T->allowhook = old_allowhook;
        status = tokuPR_close(T, old_top, status);
        tokuPR_seterrorobj(T, status, restorestack(T, old_top));
        tokuT_shrinkstack(T); /* restore stack (overflow might of happened) */
    }
    T->errfunc = old_errfunc;
    return status;
}


/* auxiliary structure to call 'tokuF_close' in protected mode */
struct PCloseData {
    SPtr level;
    int status;
};


/* auxiliary function to call 'tokuF_close' in protected mode */
static void closep(toku_State *T, void *ud) {
    struct PCloseData *cd = cast(struct PCloseData *, ud);
    tokuF_close(T, cd->level, cd->status);
}


/* call 'tokuF_close' in protected mode */
int tokuPR_close(toku_State *T, ptrdiff_t level, int status) {
    CallFrame *old_cf = T->cf;
    t_ubyte old_allowhook = T->allowhook;
    for (;;) { /* keep closing upvalues until no more errors */
        struct PCloseData cd = {
            .level = restorestack(T, level),
            .status = status
        };
        status = tokuPR_rawcall(T, closep, &cd);
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
    int c = zgetc(p->Z);
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
int tokuPR_parse(toku_State *T, BuffReader *Z, const char *name,
                                               const char *mode) {
    int status;
    struct PParseData p = { .Z = Z, .name = name, .mode = mode };
    incnnyc(T);
    status = tokuPR_call(T, pparse, &p, savestack(T, T->sp.p), T->errfunc);
    tokuR_freebuffer(T, &p.buff);
    tokuM_freearray(T, p.dyd.actlocals.arr, cast_sizet(p.dyd.actlocals.size));
    tokuM_freearray(T, p.dyd.literals.arr, cast_sizet(p.dyd.literals.size));
    tokuM_freearray(T, p.dyd.gt.arr, cast_sizet(p.dyd.gt.size));
    decnnyc(T);
    return status;
}
