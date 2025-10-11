/*
** tmem.h
** Functions for memory management
** See Copyright Notice in tokudae.h
*/

#ifndef tmem_h
#define tmem_h


#include "tokudaelimits.h"


/* memory error */
#define tokuM_error(T)      tokuPR_throw(T, TOKU_STATUS_EMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 'e' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define tokuM_testsize(n,e) \
        (sizeof(n) >= sizeof(size_t) && cast_sizet(n)+1 > TOKU_MAXSIZET/(e))

#define tokuM_checksize(L,n,e) \
        (tokuM_testsize(n,e) ? tokuM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'TOKU_MAXSIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
#define tokuM_limitN(n,t)  \
    ((cast_sizet(n) <= TOKU_MAXSIZET/sizeof(t)) ? (n) :  \
        cast_int((TOKU_MAXSIZET/sizeof(t))))


#define tokuM_new(T,t)              cast(t*, tokuM_malloc_(T, sizeof(t), 0u))
#define tokuM_newobj(T,tag,sz)      tokuM_malloc_(T, sz, cast_ubyte(tag))
#define tokuM_newarray(T,n,t) \
        cast(t*, tokuM_malloc_(T, cast_sizet(n)*sizeof(t), 0u))
#define tokuM_newarraychecked(T,n,t) \
        (tokuM_checksize(T, n, sizeof(t)), tokuM_newarray(T, n, t))

#define tokuM_reallocarray(T,p,os,ns,t) \
        cast(t *, tokuM_realloc_(T, p, cast_sizet(os)*sizeof(t), \
                                       cast_sizet(ns)*sizeof(t)))

#define tokuM_ensurearray(T,p,size,nelems,space,limit,err,t) \
        ((p) = cast(t *, tokuM_growarr_(T, p, &(size), nelems, sizeof(t), \
                                        space, tokuM_limitN(limit, t), err)))

#define tokuM_growarray(T,p,size,nelems,limit,err,t) \
        tokuM_ensurearray(T, p, size, nelems, 1, limit, err, t)


#define tokuM_free(T,p)         tokuM_free_(T, p, sizeof(*(p)))
#define tokuM_freemem(T,p,sz)   tokuM_free_((T), (p), (sz))
#define tokuM_freearray(T,p,n)  tokuM_free_((T), (p), (n)*sizeof(*(p)))
#define tokuM_shrinkarray(T,p,size,f,t) \
        ((p) = cast(t *, tokuM_shrinkarr_(T, p, &(size), f, sizeof(t))))


TOKUI_FUNC void *tokuM_malloc_(toku_State *T, t_umem size, t_ubyte tag);
TOKUI_FUNC void *tokuM_realloc_(toku_State *T, void *ptr, t_umem osize,
                                t_umem nsize);
TOKUI_FUNC void *tokuM_saferealloc(toku_State *T, void *ptr, t_umem osize,
                                   t_umem nsize);
TOKUI_FUNC t_noret tokuM_toobig(toku_State *T);
TOKUI_FUNC void tokuM_free_(toku_State *T, void *ptr, t_umem osize);
TOKUI_FUNC void *tokuM_growarr_(toku_State *T, void *ptr, int *sizep, int len,
                                               int elemsz, int ensure,
                                               int lim, const char *what);
TOKUI_FUNC void *tokuM_shrinkarr_(toku_State *T, void *ptr, int *sizep,
                                                 int final, int elemsz);
TOKUI_FUNC int tokuM_growstack(toku_State *T, int n);

#endif
