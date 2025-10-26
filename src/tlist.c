/*
** tlist.c
** List manipulation functions
** See Copyright Notice in tokudae.h
*/

#define tlist_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include "tlist.h"
#include "tokudaelimits.h"
#include "tstring.h"
#include "tgc.h"
#include "tmem.h"
#include "tmeta.h"
#include "tdebug.h"
#include "tlexer.h"


/*
** More ergonomic way to get list values.
*/
#define lget(l,i,res)   setobj(cast(toku_State*, NULL), res, &l->arr[i])


/*
** Size of non empty lists must be a power of 2 greater than 4 in
** order to unroll certain loops. As sz is of type int32_t there is no
** need to check if it is less than INT_MAX.
*/
#define listszinvariant(sz)     (4 <= (sz) && t_ispow2(sz))


t_sinline void ensure(toku_State *T, List *l, int32_t space) {
    int32_t oldsz = l->size;
    toku_assert(oldsz == 0 || listszinvariant(oldsz));
    tokuM_ensurearray(T, l->arr, l->size, l->len, space, INT_MAX,
                         "list elements", TValue);
    toku_assert(0 == space || listszinvariant(l->size));
    for (int32_t i = oldsz; i < l->size; i += 4) { /* clear new part (if any) */
        setnilval(&l->arr[i]);
        setnilval(&l->arr[i + 1]);
        setnilval(&l->arr[i + 2]);
        setnilval(&l->arr[i + 3]);
    }
}


t_sinline void setindexinbounds(toku_State *T, List *l, toku_Unsigned ui,
                                                        const TValue *v) {
    toku_assert(ui <= cast_u32(l->len));
    if (!ttisnil(v)) { /* 'v' is an actual value? */
        int32_t append = (ui == cast_u32(l->len));
        ensure(T, l, append);
        setobj(T, &l->arr[ui], v);
        l->len += append;
    } else if (ui < cast_u32(l->len)) { /* not appending? */
        l->len = cast_i32(t_castU2S(ui)); /* 'ui' is the end of sequence */
        setnilval(&l->arr[ui]); /* ('v' is nil) */
    } /* otherwise appending 'nil' is a no-op */
}


/*
** WARNING: when using this function the caller probably needs to
** check a GC barrier.
*/
void tokuA_setindex(toku_State *T, List *l, const TValue *k,
                                            const TValue *v) {
    toku_Unsigned ui = t_castS2U(ival(k));
    if (t_likely(ui <= cast_u32(l->len))) /* 'ui' in bounds? */
        setindexinbounds(T, l, ui, v);
    else /* otherwise 'k' is out of bounds */
        tokuD_indexboundserror(T, l, k);
}


t_sinline void setlistfield(toku_State *T, List *l, const TValue *k,
                                                    const TValue *v) {
    int32_t lf = gLF(strval(k));
    switch (lf) {
        case LFLAST: /* set the last element */
            setindexinbounds(T, l, cast_u32(l->len - (0<l->len)), v);
            break;
        case LFX: case LFY: case LFZ: /* set 1st, 2nd or 3rd element */
            lf -= LFX;
            if (t_likely(lf <= l->len))
                setindexinbounds(T, l, cast_u32(lf), v);
            else
                tokuD_indexboundserror(T, l, k);
            break;
        default: tokuD_lfseterror(T, lf);
    }
}


/*
** WARNING: when using this function the caller probably needs to
** check a GC barrier.
*/
void tokuA_setstr(toku_State *T, List *l, const TValue *k, const TValue *v) {
    if (t_unlikely(!islistfield(strval(k))))
        tokuD_unknownlf(T, k);
    setlistfield(T, l, k, v);
}


/*
** WARNING: when using this function the caller probably needs to
** check a GC barrier.
*/
void tokuA_set(toku_State *T, List *l, const TValue *k, const TValue *v) {
    toku_Integer i;
    if (t_likely(tointeger(k, &i))) /* index is integer? */
        tokuA_setindex(T, l, k, v);
    else if (ttisstring(k)) { /* index is a string? */
        tokuA_setstr(T, l, k, v);
    } else /* otherwise invalid index value */
        tokuD_invindexerror(T, k);
}


t_sinline void getlistfield(List *l, int32_t lf, TValue *r) {
    switch (lf) {
        case LFLEN: setival(r, l->len); break;
        case LFSIZE: setival(r, l->size); break;
        case LFLAST: /* get the last element */
            if (0 < l->len) { /* at least one element? */
                lget(l, l->len - 1, r);
            } else /* otherwise empty list */
                setnilval(r);
            break;
        case LFX: case LFY: case LFZ: { /* get 1st, 2nd or 3rd element */
            int32_t i = lf - LFX;
            toku_assert(0 <= i);
            if (i < l->len) { /* 'i' in bounds? */
                lget(l, i, r);
            } else /* otherwise 'i' out of bounds */
                setnilval(r);
            break;
        }
        default: toku_assert(0); /* unreachable */
    }
}


void tokuA_getstr(toku_State *T, List *l, const TValue *k, TValue *r) {
    OString *str = strval(k);
    if (t_unlikely(!islistfield(str)))
        tokuD_unknownlf(T, k);
    getlistfield(l, gLF(str), r);
}


void tokuA_getindex(List *l, toku_Integer i, TValue *r) {
    if (t_likely(t_castS2U(i) < cast_u32(l->len))) { /* 'i' in bounds? */
        setobj(cast(toku_State *, NULL), r, &l->arr[t_castS2U(i)]);
    } else /* otherwise 'i' out of bounds */
        setnilval(r);
}


void tokuA_get(toku_State *T, List *l, const TValue *k, TValue *r) {
    toku_Integer i;
    if (t_likely(tointeger(k, &i))) /* index is integer? */
        tokuA_getindex(l, i, r);
    else if (ttisstring(k)) { /* index is a string? */
        tokuA_getstr(T, l, k, r);
    } else /* otherwise invalid index value */
        tokuD_invindexerror(T, k);
}


void tokuA_init(toku_State *T) {
    static const char *fields[LFNUM] = {"len","size","last","x","y","z"};
    toku_assert(FIRST_LF + LFNUM <= UINT8_MAX);
    for (int32_t i = 0; i < LFNUM; i++) {
        OString *s = tokuS_new(T, fields[i]);
        s->extra = cast_u8(i + FIRST_LF);
        G(T)->listfields[i] = s;
        tokuG_fix(T, obj2gco(G(T)->listfields[i]));
    }
}


List *tokuA_new(toku_State *T) {
    GCObject *o = tokuG_new(T, sizeof(List), TOKU_VLIST);
    List *l = gco2list(o);
    l->size = l->len = 0;
    l->arr = NULL;
    return l;
}


/* if 'x' is zero then zero is returned */
t_sinline uint32_t next_highest_pow2(uint32_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return ++x;
}


int32_t tokuA_shrink(toku_State *T, List *l) {
    if (l->len < l->size) {
        uint32_t fsz = next_highest_pow2(cast_u32(l->len));
        if (fsz < cast_u32(l->size)) { /* final size < current size? */
            tokuM_shrinkarray(T, l->arr, l->size, cast_i32(fsz), TValue);
            return 1; /* true; list was shrunk */
        } else toku_assert(cast_i32(fsz) == l->size);
    }
    return 0; /* false; list didn't shrink */
}


void tokuA_ensure(toku_State *T, List *l, int32_t len) {
    toku_assert(0 <= len);
    if (l->len < len)
        ensure(T, l, len - l->len);
}


void tokuA_free(toku_State *T, List *l) {
    tokuM_freearray(T, l->arr, cast_u32(l->size));
    tokuM_free(T, l);
}
