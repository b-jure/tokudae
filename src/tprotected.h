/*
** tprotected.h
** Functions for calling functions in protected mode
** See Copyright Notice in tokudae.h
*/

#ifndef tprotected_h
#define tprotected_h


#include "treader.h"
#include "tobject.h"


/* type for functions with error handler */
typedef void (*ProtectedFn)(toku_State *T, void *userdata);


/* save/restore stack position */
#define savestack(T,ptr)        (cast_charp(ptr) - cast_charp((T)->stack.p))
#define restorestack(T,n)       cast(SPtr, cast_charp((T)->stack.p) + (n))


/* 
** Check if stack needs to grow if so, do 'pre' then grow and
** then do 'pos'.
*/
#define tokuPR_checkstackaux(T,n,pre,pos) \
    if (t_unlikely((T)->stackend.p - (T)->sp.p <= (n))) \
        { pre; tokuPR_growstack(T, (n), 1); pos; } \
    else { condmovestack(T, pre, pos); }


/* check if stack needs to grow */
#define tokuPR_checkstack(T,n)    tokuPR_checkstackaux(T,n,(void)0,(void)0)


/* check if stack needs to grow, preserving 'p' */
#define checkstackp(T,n,p) \
        tokuPR_checkstackaux(T, n, \
            ptrdiff_t p_ = savestack(T, p), \
            p = restorestack(T, p_))


/* check GC then check stack, preserving 'p' */
#define checkstackGCp(T,n,p) \
        tokuPR_checkstackaux(T,n, \
            ptrdiff_t p_ = savestack(T,p); tokuG_checkGC(T), \
            p = restorestack(T, p_))


/* check GC then check stack */
#define checkstackGC(T,n)   tokuPR_checkstackaux(T,n,tokuG_checkGC(T),(void)0)


/*
** Maximum depth for nested C calls, syntactical nested non-terminals,
** and other features implemented through recursion in C. (Value must
** fit in a 16-bit unsigned integer. It must also be compatible with
** the size of the C stack.)
*/
#if !defined(TOKUI_MAXCCALLS)
#define TOKUI_MAXCCALLS     200
#endif


TOKUI_FUNC t_noret tokuPR_throw(toku_State *T, int32_t errcode);
TOKUI_FUNC t_noret tokuPR_throwbaselevel(toku_State *T, int32_t errcode);
TOKUI_FUNC t_noret tokuPR_errerr(toku_State *T);
TOKUI_FUNC void tokuPR_incsp(toku_State *T);
TOKUI_FUNC void tokuPR_seterrorobj(toku_State *T, int32_t errcode, SPtr oldsp);
TOKUI_FUNC int32_t tokuPR_growstack(toku_State *T, int32_t n,
                                    int32_t raiseerr);
TOKUI_FUNC int32_t tokuPR_reallocstack(toku_State *T, int32_t nsz,
                                       int32_t raiseerr);
TOKUI_FUNC int32_t tokuPR_runprotected(toku_State *T, ProtectedFn fn, void *ud);
TOKUI_FUNC int32_t tokuPR_pcall(toku_State *T, ProtectedFn fn, void *ud,
                                ptrdiff_t oldsp, ptrdiff_t errfunc);
TOKUI_FUNC void tokuPR_poscall(toku_State *T, CallFrame *cf, int32_t nres);
TOKUI_FUNC CallFrame *tokuPR_precall(toku_State *T, SPtr func, int32_t nres);
TOKUI_FUNC int32_t tokuPR_pretailcall(toku_State *T, CallFrame *cf, SPtr func,
                                      int32_t narg1, int32_t delta);
TOKUI_FUNC void tokuPR_call(toku_State *T, SPtr func, int32_t nres);
TOKUI_FUNC void tokuPR_callnoyield(toku_State *T, SPtr func, int32_t nres);
TOKUI_FUNC int32_t tokuPR_close(toku_State *T, ptrdiff_t lvl, int32_t status);
TOKUI_FUNC int32_t tokuPR_parse(toku_State *T, BuffReader *Z, const char *name,
                                const char *mode); 

#endif
