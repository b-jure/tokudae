/*
** tapi.c
** Tokudae API
** See Copyright Notice in tokudae.h
*/


#define TOKU_CORE

#include "tokudaeprefix.h"

#include <stdio.h>
#include <string.h>

#include "tapi.h"
#include "tdebug.h"
#include "tfunction.h"
#include "tgc.h"
#include "tlist.h"
#include "tmarshal.h"
#include "tmem.h"
#include "tmeta.h"
#include "tobject.h"
#include "tobject.h"
#include "tokudaeconf.h"
#include "tokudae.h"
#include "tokudae.h"
#include "tokudaelimits.h"
#include "tprotected.h"
#include "treader.h"
#include "tstate.h"
#include "tstring.h"
#include "ttable.h"
#include "tvm.h"
#include "topnames.h"


/* first pseudo-index for upvalues */
#define UPVALINDEX      (TOKU_CTABLE_INDEX - 1)


/* test for pseudo index */
#define ispseudo(i)         ((i) <= TOKU_CLIST_INDEX)

/* test for upvalue */
#define isupvalue(i)        ((i) <= UPVALINDEX)

/* test for valid index */
#define isvalid(T,o)        (!isempty(o) || (o) != &G(T)->nil)


/* 
** Convert index to a pointer to its value.
** Invalid indices (using upvalue index for Toku functions) return
** special nil value '&G(T)->nil'.
*/
static TValue *index2value(const toku_State *T, int index) {
    CallFrame *cf = T->cf;
    if (index >= 0) { /* absolute index? */
        SPtr o = (cf->func.p + 1) + index;
        api_check(T, index < cf->top.p - (cf->func.p + 1), "index too large");
        if (o >= T->sp.p) return &G(T)->nil;
        else return s2v(o);
    } else if (!ispseudo(index)) { /* negative index? */
        api_check(T, -index <= T->sp.p - (cf->func.p + 1), "index too small");
        return s2v(T->sp.p + index);
    } else if (index == TOKU_CLIST_INDEX) /* T list index? */
        return &G(T)->c_list;
    else if (index == TOKU_CTABLE_INDEX) /* T table index? */
        return &G(T)->c_table;
    else { /* otherwise upvalue index */
        index = UPVALINDEX - index;
        api_check(T, index < USHRT_MAX, "upvalue index too large");
        if (t_likely(ttisCclosure(s2v(cf->func.p)))) { /* T closure? */
            CClosure *ccl = clCval(s2v(cf->func.p));
            return &ccl->upvals[index];
        } else { /* light T function or Toku function (through a hook)? */
            api_check(T, ttislcf(s2v(cf->func.p)), "caller not a T function");
            return &G(T)->nil; /* no upvalues */
        }
    }
}


/*
** Convert index to a stack slot.
*/
static SPtr index2stack(const toku_State *T, int index) {
    CallFrame *cf = T->cf;
    if (index >= 0) {
        SPtr p = (cf->func.p + 1) + index;
        api_check(T, p < T->sp.p, "invalid index");
        return p;
    } else { /* negative index */
        api_check(T, -index <= (T->sp.p - (cf->func.p + 1)), "invalid index");
        api_check(T, !ispseudo(index), "invalid index");
        return T->sp.p + index; /* index is subtracted */
    }
}


/* {======================================================================
** State manipulation (other functions are defined in tstate.c)
** ======================================================================= */

TOKU_API toku_CFunction toku_atpanic(toku_State *T, toku_CFunction fpanic) {
    toku_CFunction old_panic;
    toku_lock(T);
    old_panic = G(T)->fpanic;
    G(T)->fpanic = fpanic;
    toku_unlock(T);
    return old_panic;
}


TOKU_API void toku_setallocf(toku_State *T, toku_Alloc falloc, void *ud) {
    toku_lock(T);
    G(T)->falloc = falloc;
    G(T)->ud_alloc = ud;
    toku_unlock(T);
}


TOKU_API toku_Alloc toku_getallocf(toku_State *T, void **ud) {
    toku_Alloc falloc;
    toku_lock(T);
    if (ud) *ud = G(T)->ud_alloc;
    falloc = G(T)->falloc;
    toku_unlock(T);
    return falloc;
}

/* }====================================================================== */


/* {======================================================================
** Stack manipulation
** ======================================================================= */

t_sinline void settop(toku_State *T, int n) {
    CallFrame *cf;
    SPtr func, newtop;
    ptrdiff_t diff;
    cf = T->cf;
    func = cf->func.p;
    if (n >= 0) {
        api_check(T, n <= (cf->top.p - func + 1), "new top too large");
        diff = ((func + 1) + n) - T->sp.p;
        for (; diff > 0; diff--)
            setnilval(s2v(T->sp.p++));
    } else { /* negative index */
        api_check(T, -(n+1) <= (T->sp.p-(func+1)), "new top underflow");
        diff = n + 1;
    }
    api_check(T, T->tbclist.p < T->sp.p, "previous pop of an unclosed slot");
    newtop = T->sp.p + diff;
    if (diff < 0 && T->tbclist.p >= newtop) {
        toku_assert(hastocloseCfunc(cf->nresults));
        newtop = tokuF_close(T, newtop, CLOSEKTOP);
    }
    T->sp.p = newtop; /* set new top */
}


TOKU_API void toku_setntop(toku_State *T, int n) {
    toku_lock(T);
    settop(T, n);
    toku_unlock(T);
}


TOKU_API int toku_gettop(const toku_State *T) {
    return cast_int(T->sp.p - (T->cf->func.p + 1) - 1);
}


TOKU_API int toku_absindex(toku_State *T, int index)
{
    return (index >= 0 || ispseudo(index))
            ? index
            : cast_int(T->sp.p - T->cf->func.p - 1) + index;
}


/* 
** Auxiliary to 'toku_rotate', reverses stack values starting at 'from'
** until 'to'.
*/
t_sinline void rev(toku_State *T, SPtr from, SPtr to) {
    while (from < to) {
        TValue temp;
        setobj(T, &temp, s2v(from));
        setobjs2s(T, from, to);
        setobj2s(T, to, &temp);
        from++, to--;
    }
}


/*
** Stack-array rotation between the top of the stack and the 'index' for 'n'
** times elements. Negative '-n' indicates left-rotation, while positive 'n'
** right-rotation. The absolute value of 'n' must not be greater than the size
** of the slice being rotated.
** Note that 'index' must be in stack.
**
** Example right-rotation:
** [func][0][1][2][3][4]
** toku_rotate(T, 2, 2);
** [func][0][1][3][4][2]
**
** Example left-rotation:
** [func][0][1][2][3][4]
** toku_rotate(T, 2, -2);
** [func][0][1][4][3][2]
*/
TOKU_API void toku_rotate(toku_State *T, int index, int n) {
    SPtr start, end, pivot;
    toku_lock(T);
    end = T->sp.p - 1; /* end of segment */
    start = index2stack(T, index); /* start of segment */
    api_check(T, (0 <= n ? n : -n) <= (end - start + 1), "invalid 'n'");
    pivot = (n >= 0 ? end - n : start - n - 1); /* end of prefix */
    rev(T, start, pivot);
    rev(T, pivot + 1, end);
    rev(T, start, end);
    toku_unlock(T);
}


TOKU_API void toku_copy(toku_State *T, int src, int dest) {
    TValue *from, *to;
    toku_lock(T);
    from = index2value(T, src);
    to = index2value(T, dest);
    api_check(T, isvalid(T, to), "invalid index");
    setobj(T, to, from);
    if (isupvalue(dest)) /* closure upvalue? */
        tokuG_barrier(T, clCval(s2v(T->cf->func.p)), from);
    toku_unlock(T);
}


TOKU_API int toku_checkstack(toku_State *T, int n) {
    CallFrame *cf;
    int res;
    toku_lock(T);
    cf = T->cf;
    api_check(T, n >= 0, "negative 'n'");
    if (T->stackend.p - T->sp.p >= n) /* stack large enough? */
        res = 1;
    else /* need to grow the stack */
        res = tokuT_growstack(T, n, 0);
    if (res && cf->top.p < T->sp.p + n)
        cf->top.p = T->sp.p + n; /* adjust frame top */
    toku_unlock(T);
    return res;
}


/* push without lock */
#define pushvalue(T, index) \
    { setobj2s(T, T->sp.p, index2value(T, index)); api_inctop(T); }


TOKU_API void toku_push(toku_State *T, int index) {
    toku_lock(T);
    pushvalue(T, index);
    toku_unlock(T);
}


TOKU_API void toku_xmove(toku_State *src, toku_State *dest, int n) {
    if (src == dest) return; /* same thread ? */
    toku_lock(dest);
    api_checknelems(src, n); /* have enough elements to move? */
    api_check(src, G(src) == G(dest), "moving between different states");
    api_check(src, dest->cf->top.p - dest->sp.p >= n, "dest stack overflow");
    src->sp.p -= n;
    for (int i = 0; i < n; i++) {
        setobjs2s(dest, dest->sp.p, src->sp.p + i);
        dest->sp.p++; /* already checked by 'api_check' */
    }
    toku_unlock(dest);
}

/* }====================================================================== */


/* {======================================================================
** Access functions (Stack -> T)
** ======================================================================= */

TOKU_API int toku_is_number(toku_State *T, int index) {
    toku_Number n;
    UNUSED(n);
    const TValue *o = index2value(T, index);
    return tonumber(o, n);
}


TOKU_API int toku_is_integer(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return ttisint(o);
}


TOKU_API int toku_is_string(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return ttisstring(o);
}


TOKU_API int toku_is_cfunction(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return (ttislcf(o) || ttisCclosure(o));
}


TOKU_API int toku_is_udatamethod(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return ttisusermethod(o);
}


TOKU_API int toku_is_userdata(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return (ttislightuserdata(o) || ttisfulluserdata(o));
}


TOKU_API int toku_type(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return (isvalid(T, o) ? ttype(o) : TOKU_T_NONE);
}


TOKU_API const char *toku_typename(toku_State *T, int type) {
    UNUSED(T);
    api_check(T, TOKU_T_NONE <= type && type < TOKU_T_NUM, "invalid type");
    return typename(type);
}


TOKU_API toku_Number toku_to_numberx(toku_State *T, int index, int *pisnum) {
    toku_Number n = 0.0;
    const TValue *o = index2value(T, index);
    int isnum = tonumber(o, n);
    if (pisnum)
        *pisnum = isnum;
    return n;
}


TOKU_API toku_Integer toku_to_integerx(toku_State *T, int index, int *pisint) {
    toku_Integer i = 0;
    const TValue *o = index2value(T, index);
    int isint = tointeger(o, &i);
    if (pisint)
        *pisint = isint;
    return i;
}


TOKU_API int toku_to_bool(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return !t_isfalse(o);
}


TOKU_API const char *toku_to_lstring(toku_State *T, int index, size_t *plen) {
    const TValue *o = index2value(T, index);
    if (!ttisstring(o)) /* not a string? */
        return NULL;
    else if (plen != NULL)
        *plen = getstrlen(strval(o)); 
    return getstr(strval(o));
}


TOKU_API toku_CFunction toku_to_cfunction(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    if (ttislcf(o)) 
        return lcfval(o);
    else if (ttisCclosure(o))
        return clCval(o)->fn;
    else
        return NULL;
}


t_sinline void *touserdata(const TValue *o) {
    switch (ttypetag(o)) {
        case TOKU_VLIGHTUSERDATA: return pval(o);
        case TOKU_VUSERDATA: return getuserdatamem(udval(o));
        default: return NULL;
    }
}


/*
** Return pointer to userdata memory from the value at index.
** If the value is not userdata return NULL.
*/
TOKU_API void *toku_to_userdata(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return touserdata(o);
}


/*
** Returns a pointer to the internal representation of an object.
** Note that ISO C does not allow the conversion of a pointer to
** function to a 'void*', so the conversion here goes through
** a 'size_t'. (As the returned pointer is only informative, this
** conversion should not be a problem.)
*/
TOKU_API const void *toku_to_pointer(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VLCF: return cast_voidp(cast_sizet(lcfval(o)));
        case TOKU_VUSERDATA: case TOKU_VLIGHTUSERDATA:
            return touserdata(o);
        default: {
            if (iscollectable(o))
                return gcoval(o);
            return NULL;
        }
    }
}


TOKU_API toku_State *toku_to_thread(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    return (ttisthread(o) ? thval(o) : NULL);
}

/* }====================================================================== */


/* {======================================================================
** Ordering & Arithmetic functions
** ======================================================================= */

TOKU_API void toku_arith(toku_State *T, int op) {
    toku_lock(T);
    if (op != TOKU_OP_UNM && op != TOKU_OP_BNOT) { /* binary op? */
        api_checknelems(T, 2);
        tokuV_binarithm(T, s2v(T->sp.p-2), s2v(T->sp.p-1), T->sp.p-2, op);
        T->sp.p--; /* pop second operand */
    } else { /* unary op */
        api_checknelems(T, 1);
        tokuV_unarithm(T, s2v(T->sp.p-1), T->sp.p-1, op);
        /* done */
    }
    toku_unlock(T);
}


TOKU_API int toku_rawequal(toku_State *T, int index1, int index2) {
    const TValue *lhs = index2value(T, index1);
    const TValue *rhs = index2value(T, index2);
    return (isvalid(T, lhs) && isvalid(T, rhs)) ? tokuV_raweq(lhs, rhs) : 0;
}


TOKU_API int toku_compare(toku_State *T, int index1, int index2, int op) {
    const TValue *lhs;
    const TValue *rhs;
    int res = 0; /* to avoid warnings */
    toku_lock(T); /* might call overloaded method */
    lhs = index2value(T, index1);
    rhs = index2value(T, index2);
    if (isvalid(T, lhs) && isvalid(T, rhs)) {
        switch (op) {
            case TOKU_ORD_EQ: res = tokuV_ordereq(T, lhs, rhs); break;
            case TOKU_ORD_LT: res = tokuV_orderlt(T, lhs, rhs); break;
            case TOKU_ORD_LE: res = tokuV_orderle(T, lhs, rhs); break;
            default: api_check(T, 0, "invalid 'op'");
        }
    }
    toku_unlock(T);
    return res;
}

/* }====================================================================== */


/* {======================================================================
** Push functions (T -> stack)
** ======================================================================= */

/* Push nil value on top of the stack. */
TOKU_API void toku_push_nil(toku_State *T) {
    toku_lock(T);
    setnilval(s2v(T->sp.p));
    api_inctop(T);
    toku_unlock(T);
}


/* Push 'toku_Number' value on top of the stack. */
TOKU_API void toku_push_number(toku_State *T, toku_Number n) {
    toku_lock(T);
    setfval(s2v(T->sp.p), n);
    api_inctop(T);
    toku_unlock(T);
}


/* Push 'toku_Integer' value on top of the stack. */
TOKU_API void toku_push_integer(toku_State *T, toku_Integer i) {
    toku_lock(T);
    setival(s2v(T->sp.p), i);
    api_inctop(T);
    toku_unlock(T);
}


/* Push string value of length 'len' on top of the stack. */
TOKU_API const char *toku_push_lstring(toku_State *T, const char *str, size_t len) {
    OString *s;
    toku_lock(T);
    s = (len == 0 ? tokuS_new(T, "") : tokuS_newl(T, str, len));
    setstrval2s(T, T->sp.p, s);
    api_inctop(T);
    tokuG_checkGC(T);
    toku_unlock(T);
    return getstr(s);
}


/* Push null terminated string value on top of the stack. */
TOKU_API const char *toku_push_string(toku_State *T, const char *str) {
    toku_lock(T);
    if (str == NULL) {
        setnilval(s2v(T->sp.p));
    } else {
        OString *s = tokuS_new(T, str);
        setstrval2s(T, T->sp.p, s);
        str = getstr(s);
    }
    api_inctop(T);
    tokuG_checkGC(T);
    toku_unlock(T);
    return str;
}


TOKU_API const char *toku_push_vfstring(toku_State *T, const char *fmt, va_list ap) {
    const char *str;
    toku_lock(T);
    str = tokuS_pushvfstring(T, fmt, ap);
    tokuG_checkGC(T);
    toku_unlock(T);
    return str;
}


TOKU_API const char *toku_push_fstring(toku_State *T, const char *fmt, ...) {
    const char *str;
    va_list ap;
    toku_lock(T);
    va_start(ap, fmt);
    str = tokuS_pushvfstring(T, fmt, ap);
    va_end(ap);
    tokuG_checkGC(T);
    toku_unlock(T);
    return str;
}


/* push T closure without locking */
t_sinline void pushcclosure(toku_State *T, toku_CFunction fn, int n) {
    if (n == 0) {
        setcfval(T, s2v(T->sp.p), fn);
        api_inctop(T);
    } else {
        CClosure *cl;
        api_checknelems(T, n);
        cl = tokuF_newCclosure(T, n);
        cl->fn = fn;
        T->sp.p -= n;
        while (n--) {
            setobj(T, &cl->upvals[n], s2v(T->sp.p + n));
            toku_assert(iswhite(cl));
        }
        setclCval(T, s2v(T->sp.p), cl);
        api_inctop(T);
        tokuG_checkGC(T);
    }
}


TOKU_API void toku_push_cclosure(toku_State *T, toku_CFunction fn, int n) {
    toku_lock(T);
    pushcclosure(T, fn, n);
    toku_unlock(T);
}


TOKU_API void toku_push_bool(toku_State *T, int b) {
    toku_lock(T);
    if (b)
        setbtval(s2v(T->sp.p));
    else
        setbfval(s2v(T->sp.p));
    api_inctop(T);
    toku_unlock(T);
}


TOKU_API void toku_push_lightuserdata(toku_State *T, void *p) {
    toku_lock(T);
    setpval(s2v(T->sp.p), p);
    api_inctop(T);
    toku_unlock(T);
}


TOKU_API void *toku_push_userdata(toku_State *T, size_t sz, t_ushort nuv) {
    UserData *ud;
    toku_lock(T);
    ud = tokuTM_newuserdata(T, sz, nuv);
    setudval2s(T, T->sp.p, ud);
    api_inctop(T);
    tokuG_checkGC(T);
    toku_unlock(T);
    return getuserdatamem(ud);
}


TOKU_API void toku_push_list(toku_State *T, int sz) {
    List *l;
    toku_lock(T);
    l = tokuA_new(T);
    setlistval2s(T, T->sp.p, l);
    api_inctop(T);
    tokuA_ensure(T, l, sz);
    tokuG_checkGC(T);
    toku_unlock(T);
}


TOKU_API void toku_push_table(toku_State *T, int sz) {
    Table *t;
    toku_lock(T);
    t = tokuH_new(T);
    settval2s(T, T->sp.p, t);
    api_inctop(T);
    if (sz > 0)
        tokuH_resize(T, t, cast_uint(sz));
    tokuG_checkGC(T);
    toku_unlock(T);
}


TOKU_API int toku_push_thread(toku_State *T) {
    toku_lock(T);
    setthval2s(T, T->sp.p, T);
    api_inctop(T);
    toku_unlock(T);
    return (G(T)->mainthread == T);
}


TOKU_API void toku_push_class(toku_State *T) {
    OClass *cls;
    toku_lock(T);
    cls = tokuTM_newclass(T);
    setclsval2s(T, T->sp.p, cls);
    api_inctop(T);
    tokuG_checkGC(T);
    toku_unlock(T);
}


TOKU_API void toku_push_instance(toku_State *T, int index) {
    const TValue *o;
    SPtr func = T->sp.p;
    toku_lock(T);
    o = index2value(T, index);
    api_check(T, ttisclass(o), "class expected");
    setclsval2s(T, func, classval(o));
    api_inctop(T);
    tokuV_call(T, func, 1);
    tokuG_checkGC(T);
    toku_unlock(T);
}


TOKU_API void toku_push_boundmethod(toku_State *T, int index) {
    const TValue *o;
    toku_lock(T);
    api_checknelems(T, 1); /* method */
    o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VINSTANCE: {
            IMethod *im = tokuTM_newinsmethod(T, insval(o), s2v(T->sp.p - 1));
            setimval2s(T, T->sp.p - 1, im);
            break;
        }
        case TOKU_VUSERDATA: {
            UMethod *um = tokuTM_newudmethod(T, udval(o), s2v(T->sp.p - 1));
            setumval2s(T, T->sp.p - 1, um);
            break;
        }
        default: api_check(T, 0, "userdata/instance expected");
    }
    tokuG_checkGC(T);
    toku_unlock(T);
}

/* }====================================================================== */


/* {======================================================================
** Get functions (Toku -> stack)
** ======================================================================= */

TOKU_API int toku_get(toku_State *T, int index) {
    const TValue *o;
    toku_lock(T);
    api_checknelems(T, 1); /* key */
    o = index2value(T, index);
    tokuV_get(T, o, s2v(T->sp.p - 1), T->sp.p - 1);
    toku_unlock(T);
    return ttype(s2v(T->sp.p - 1));
}


TOKU_API int toku_get_raw(toku_State *T, int index) {
    const TValue *o;
    toku_lock(T);
    api_checknelems(T, 1); /* key */
    o = index2value(T, index);
    tokuV_rawget(T, o, s2v(T->sp.p - 1), T->sp.p - 1);
    toku_unlock(T);
    return ttype(s2v(T->sp.p - 1));
}


t_sinline int getfieldstr(toku_State *T, Table *t, const char *key) {
    OString *k = tokuS_new(T, key);
    TValue slot;
    t_ubyte tag = tokuH_getstr(t, k, &slot);
    if (!tagisempty(tag)) {
        setobj2s(T, T->sp.p, &slot);
    } else
        setnilval(s2v(T->sp.p));
    api_inctop(T);
    toku_unlock(T);
    return novariant(tag);
}


TOKU_API int toku_get_global_str(toku_State *T, const char *name) {
    toku_lock(T);
    return getfieldstr(T, tval(GT(T)), name);
}


static int aux_getindex(toku_State *T, List *l, toku_Integer i) {
    tokuA_getindex(l, i, s2v(T->sp.p));
    api_inctop(T);
    toku_unlock(T);
    return ttype(s2v(T->sp.p - 1));
}


t_sinline List *getlist(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    api_check(T, ttislist(o), "list expected");
    return listval(o);
}


TOKU_API int toku_get_index(toku_State *T, int index, toku_Integer i) {
    toku_lock(T);
    return aux_getindex(T, getlist(T, index), i);
}


TOKU_API int toku_get_cindex(toku_State *T, toku_Integer i) {
    toku_lock(T);
    return aux_getindex(T, listval(CL(T)), i);
}


t_sinline Table *gettable(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VINSTANCE: return insval(o)->fields;
        case TOKU_VTABLE: return tval(o); 
        default:  {
            api_check(T, 0, "instance/table expected");
            return NULL; /* to avoid warnings */
        }
    }
}


t_sinline int getfield(toku_State *T, t_ubyte tag, TValue *value) {
    if (tagisempty(tag))
        setnilval(s2v(T->sp.p));
    else
        setobj2s(T, T->sp.p, value);
    api_inctop(T);
    toku_unlock(T);
    return novariant(tag);
}


TOKU_API int toku_get_field(toku_State *T, int index) {
    Table *t;
    t_ubyte tag;
    TValue value;
    toku_lock(T);
    api_checknelems(T, 1); /* key */
    t = gettable(T, index);
    tag = tokuH_get(t, s2v(T->sp.p - 1), &value);
    T->sp.p--; /* remove key */
    return getfield(T, tag, &value);
}


TOKU_API int toku_get_field_str(toku_State *T, int index, const char *key) {
    Table *t;
    toku_lock(T);
    t = gettable(T, index);
    return getfieldstr(T, t, key);
}


TOKU_API int toku_get_field_int(toku_State *T, int index, toku_Integer key) {
    Table *t;
    TValue value;
    t_ubyte tag;
    toku_lock(T);
    t = gettable(T, index);
    tag = tokuH_getint(t, key, &value);
    return getfield(T, tag, &value);
}


TOKU_API int toku_get_cfield_str(toku_State *T, const char *key) {
    toku_lock(T);
    return getfieldstr(T, tval(CT(T)), key);
}


TOKU_API int toku_get_class(toku_State *T, int index) {
    int t;
    const TValue *o;
    toku_lock(T);
    o = index2value(T, index);
    if (ttisinstance(o)) {
        setclsval2s(T, T->sp.p, insval(o)->oclass);
        api_inctop(T);
        t = TOKU_T_CLASS;
    } else
        t = TOKU_T_NONE;
    toku_unlock(T);
    return t;
}


TOKU_API int toku_get_superclass(toku_State *T, int index) {
    OClass *scl = NULL; /* to avoid warnings */
    int res = 1;
    const TValue *o;
    toku_lock(T);
    o = index2value(T, index);
    switch (ttype(o)) {
        case TOKU_T_CLASS: scl = classval(o)->sclass; break;
        case TOKU_T_INSTANCE: scl = insval(o)->oclass->sclass; break;
        default: api_check(T, 0, "instance/class expected");
    }
    if (scl) {
        setclsval2s(T, T->sp.p, scl);
        api_inctop(T);
    } else
        res = 0;
    toku_unlock(T);
    return res;
}


TOKU_API int toku_get_method(toku_State *T, int index) {
    TValue *m = NULL; /* to avoid warnings */
    const TValue *o;
    toku_lock(T);
    o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VIMETHOD:
            m = &imval(o)->method;
            break;
        case TOKU_VUMETHOD:
            m = &umval(o)->method;
            break;
        default: api_check(T, 0, "bound method expected");
    }
    setobj2s(T, T->sp.p, m);
    api_inctop(T);
    toku_unlock(T);
    return ttype(s2v(T->sp.p - 1));
}


TOKU_API int toku_get_self(toku_State *T, int index) {
    int tt = TOKU_T_NONE; /* to avoid warnings */
    const TValue *o;
    toku_lock(T);
    o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VUMETHOD:
            tt = TOKU_T_USERDATA;
            setudval2s(T, T->sp.p, umval(o)->ud);
            break;
        case TOKU_VIMETHOD:
            tt = TOKU_T_INSTANCE;
            setinsval2s(T, T->sp.p, imval(o)->ins);
            break;
        default: api_check(T, 0, "bound method expected");
    }
    api_inctop(T);
    toku_unlock(T);
    return tt;
}


t_sinline UserData *getuserdata(toku_State *T, int index) {
    const TValue *o = index2value(T, index);
    api_check(T, ttisfulluserdata(o), "userdata expected");
    return udval(o);
}


TOKU_API int toku_get_uservalue(toku_State *T, int index, t_ushort n) {
    UserData *ud;
    int t;
    toku_lock(T);
    ud = getuserdata(T, index);
    if (ud->nuv <= n) {
        setnilval(s2v(T->sp.p));
        t = TOKU_T_NONE;
    } else {
        setobj2s(T, T->sp.p, &ud->uv[n].val);
        t = ttype(s2v(T->sp.p));
    }
    api_inctop(T);
    toku_unlock(T);
    return t;
}


TOKU_API int toku_get_methodtable(toku_State *T, int index) {
    int res = 0;
    const TValue *o;
    Table *t;
    toku_lock(T);
    o = index2value(T, index);
    api_check(T, ttisclass(o), "class expected");
    if ((t = classval(o)->methods)) {
        settval2s(T, T->sp.p, t);
        api_inctop(T);
        res = 1;
    }
    toku_unlock(T);
    return res;
}


TOKU_API int toku_get_metatable(toku_State *T, int index) {
    const TValue *obj;
    Table *mt;
    int res = 0;
    toku_lock(T);
    obj = index2value(T, index);
    switch (ttype(obj)) {
        case TOKU_T_INSTANCE:
            mt = insval(obj)->oclass->metatable;
            break;
        case TOKU_T_CLASS:
            mt = classval(obj)->metatable;
            break;
        case TOKU_T_USERDATA:
            mt = udval(obj)->metatable;
            break;
        default: mt = NULL;
    }
    if (mt) { /* have metatable? */
        settval2s(T, T->sp.p, mt);
        api_inctop(T);
        res = 1;
    }
    toku_unlock(T);
    return res;
}


TOKU_API void toku_get_fieldtable(toku_State *T, int index) {
    const TValue *o;
    toku_lock(T);
    o = index2value(T, index);
    api_check(T, ttisinstance(o), "instance expected");
    settval2s(T, T->sp.p, insval(o)->fields);
    api_inctop(T);
    toku_unlock(T);
}

/* }====================================================================== */


/* {======================================================================
** Set functions (stack -> Toku)
** ======================================================================= */

TOKU_API void toku_set(toku_State *T, int obj) {
    TValue *o;
    toku_lock(T);
    api_checknelems(T, 2); /* value and key */
    o = index2value(T, obj);
    tokuV_set(T, o, s2v(T->sp.p - 2), s2v(T->sp.p - 1));
    T->sp.p -= 2; /* remove value and key */
    toku_unlock(T);
}


TOKU_API void toku_set_raw(toku_State *T, int obj) {
    TValue *o;
    toku_lock(T);
    api_checknelems(T, 2); /* key and value */
    o = index2value(T, obj);
    tokuV_rawset(T, o, s2v(T->sp.p - 2), s2v(T->sp.p - 1));
    T->sp.p -= 2; /* remove value and key */
    toku_unlock(T);
}


t_sinline void rawsetstr(toku_State *T, Table *t, const char *key,
                                        TValue *value) {
    int hres;
    OString *k = tokuS_new(T, key);
    tokuV_fastset(t, k, value, hres, tokuH_psetstr);
    if (hres == HOK) {
        tokuV_finishfastset(T, t, value);
        T->sp.p--; /* remove value */
    } else {
        setstrval2s(T, T->sp.p, k); /* anchor it */
        api_inctop(T);
        tokuH_finishset(T, t, s2v(T->sp.p - 1), value, hres);
        tokuG_barrierback(T, obj2gco(t), value);
        invalidateTMcache(t);
        T->sp.p -= 2; /* remove key and value */
    }
    toku_unlock(T);
}


TOKU_API void toku_set_global_str(toku_State *T, const char *name) {
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    rawsetstr(T, tval(GT(T)), name, s2v(T->sp.p - 1));
}


static void aux_setindex(toku_State *T, List *l, toku_Integer i) {
    TValue v;
    setival(&v, i);
    tokuA_setindex(T, l, &v, s2v(T->sp.p - 1));
    T->sp.p--; /* remove value */
    toku_unlock(T);
}


TOKU_API void toku_set_index(toku_State *T, int index, toku_Integer i) {
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    aux_setindex(T, getlist(T, index), i);
}


TOKU_API void toku_set_cindex(toku_State *T, toku_Integer i) {
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    aux_setindex(T, listval(CL(T)), i);
}


TOKU_API void toku_set_field(toku_State *T, int obj) {
    int hres;
    Table *t;
    TValue *value, *key;
    toku_lock(T);
    api_checknelems(T, 2); /* key and value */
    t = gettable(T, obj);
    key = s2v(T->sp.p - 2);
    value = s2v(T->sp.p - 1);
    tokuV_fastset(t, key, value, hres, tokuH_pset);
    if (hres == HOK)
        tokuV_finishfastset(T, t, value);
    else {
        tokuH_finishset(T, t, key, value, hres);
        tokuG_barrierback(T, obj2gco(t), value);
        invalidateTMcache(t);
    }
    T->sp.p -= 2; /* remove key and value */
    toku_unlock(T);
}


TOKU_API void toku_set_field_str(toku_State *T, int index, const char *key) {
    Table *t;
    toku_lock(T);
    api_checknelems(T, 1);
    t = gettable(T, index);
    rawsetstr(T, t, key, s2v(T->sp.p - 1));
}


TOKU_API void toku_set_field_int(toku_State *T, int index, toku_Integer key) {
    int hres;
    Table *t;
    TValue *value;
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    t = gettable(T, index);
    value = s2v(T->sp.p - 1);
    tokuV_fastset(t, key, value, hres, tokuH_psetint);
    if (hres == HOK)
        tokuV_finishfastset(T, t, value);
    else {
        TValue k;
        setival(&k, key);
        tokuH_finishset(T, t, &k, value, hres);
        tokuG_barrierback(T, obj2gco(t), value);
        invalidateTMcache(t);
    }
    T->sp.p--; /* remove value */
    toku_unlock(T);
}


TOKU_API void toku_set_cfield_str(toku_State *T, const char *key) {
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    rawsetstr(T, tval(CT(T)), key, s2v(T->sp.p - 1));
}


TOKU_API void toku_set_superclass(toku_State *T, int index) {
    const TValue *o;
    OClass *sc;
    toku_lock(T);
    api_checknelems(T, 1); /* superclass value */
    o = index2value(T, index);
    api_check(T, ttisclass(o), "class expected");
    if (ttisnil(s2v(T->sp.p - 1)))
        sc = NULL;
    else {
        api_check(T, ttisclass(s2v(T->sp.p - 1)), "class expected");
        sc = classval(s2v(T->sp.p - 1));
    }
    classval(o)->sclass = sc;
    if (sc) tokuG_objbarrier(T, gcoval(o), sc);
    T->sp.p--; /* remove class or nil */
    toku_unlock(T);
}


TOKU_API void toku_set_metatable(toku_State *T, int index) {
    const TValue *o;
    Table *mt;
    toku_lock(T);
    api_checknelems(T, 1); /* metatable value */
    o = index2value(T, index);
    if (ttisnil(s2v(T->sp.p - 1)))
        mt = NULL;
    else {
        api_check(T, ttistable(s2v(T->sp.p - 1)), "table expected");
        mt = tval(s2v(T->sp.p - 1));
    }
    switch (ttype(o)) {
        case TOKU_T_CLASS:
            classval(o)->metatable = mt;
            if (mt) tokuG_objbarrier(T, gcoval(o), mt);
            break;
        case TOKU_T_USERDATA:
            udval(o)->metatable = mt;
            if (mt) {
                tokuG_objbarrier(T, gcoval(o), mt);
                tokuG_checkfin(T, gcoval(o), mt);
            }
            break;
        default: api_check(T, 0, "class/userdata expected");
    }
    T->sp.p--; /* remove metatable or nil */
    toku_unlock(T);
}


TOKU_API int toku_set_uservalue(toku_State *T, int index, t_ushort n) {
    int res;
    UserData *ud;
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    ud = getuserdata(T, index);
    if (ud->nuv <= n)
        res = 0; /* 'n' not in [0, ud->nuv) */
    else {
        setobj(T, &ud->uv[n].val, s2v(T->sp.p - 1));
        tokuG_barrierback(T, obj2gco(ud), s2v(T->sp.p - 1));
        res = 1;
    }
    T->sp.p--; /* remove value */
    toku_unlock(T);
    return res;
}


TOKU_API void toku_set_methodtable(toku_State *T, int index) {
    Table *t;
    const TValue *o;
    toku_lock(T);
    api_checknelems(T, 1); /* table or nil */
    o = index2value(T, index);
    api_check(T, ttisclass(o), "class expected");
    if (ttisnil(s2v(T->sp.p - 1)))
        t = NULL;
    else {
        api_check(T, ttistable(s2v(T->sp.p - 1)), "table expected");
        t = tval(s2v(T->sp.p - 1));
    }
    classval(o)->methods = t;
    if (t) tokuG_objbarrier(T, gcoval(o), t);
    T->sp.p--; /* remove table or nil */
    toku_unlock(T);
}


TOKU_API void toku_set_fieldtable(toku_State *T, int index) {
    const TValue *o;
    Table *t;
    toku_lock(T);
    api_checknelems(T, 1);
    o = index2value(T, index);
    api_check(T, ttisinstance(o), "instance expected");
    api_check(T, ttistable(s2v(T->sp.p - 1)), "table expected");
    t = tval(s2v(T->sp.p - 1));
    insval(o)->fields = t;
    tokuG_objbarrier(T, gcoval(o), t);
    T->sp.p--;
    toku_unlock(T);
}

/* }====================================================================== */


/* {======================================================================
** Status and Error reporting
** ======================================================================= */

TOKU_API int toku_status(toku_State *T) {
    return T->status;
}


TOKU_API int toku_error(toku_State *T) {
    TValue *errobj;
    toku_lock(T);
    api_checknelems(T, 1); /* errobj */
    errobj = s2v(T->sp.p - 1);
    if (ttisshrstring(errobj) && eqshrstr(strval(errobj), G(T)->memerror)) {
        tokuM_error(T); /* raise a memory error */
    } else
        tokuD_errormsg(T);
    /* toku_unlock() is called after control leaves the core */
    toku_assert(0);
    return 0; /* to prevent warnings */
}

/* }====================================================================== */


/* {======================================================================
** Call/Load/Dump Tokudae chunks
** ======================================================================= */

#define checkresults(T,nargs,nres) \
     api_check(T, (nres) == TOKU_MULTRET \
               || (T->cf->top.p - T->sp.p >= (nres) - (nargs)), \
        "results from function overflow current stack size")


TOKU_API void toku_call(toku_State *T, int nargs, int nresults) {
    SPtr func;
    toku_lock(T);
    api_checknelems(T, nargs + 1); /* args + func */
    api_check(T, T->status == TOKU_STATUS_OK,
                 "can't do calls on non-normal thread");
    checkresults(T, nargs, nresults);
    func = T->sp.p - nargs - 1;
    tokuV_call(T, func, nresults);
    adjustresults(T, nresults);
    toku_unlock(T);
}


struct PCallData {
    SPtr func;
    int nresults;
};


static void fcall(toku_State *T, void *ud) {
    struct PCallData *pcd = cast(struct PCallData*, ud);
    tokuV_call(T, pcd->func, pcd->nresults);
}


TOKU_API int toku_pcall(toku_State *T, int nargs, int nresults, int absmsgh) {
    struct PCallData pcd;
    int status;
    ptrdiff_t func;
    toku_lock(T);
    api_checknelems(T, nargs+1); /* args + func */
    api_check(T, T->status == TOKU_STATUS_OK,
                 "can't do calls on non-normal thread");
    checkresults(T, nargs, nresults);
    if (absmsgh < 0)
        func = 0;
    else {
        SPtr o = index2stack(T, absmsgh);
        api_check(T, ttisfunction(s2v(o)), "error handler must be a function");
        func = savestack(T, o);
    }
    pcd.func = T->sp.p - (nargs + 1); /* function to be called */
    pcd.nresults = nresults;
    status = tokuPR_call(T, fcall, &pcd, savestack(T, pcd.func), func);
    adjustresults(T, nresults);
    toku_unlock(T);
    return status;
}


t_sinline TClosure *posload(toku_State *T) {
    TClosure *cl = clTval(s2v(T->sp.p - 1)); /* get new function */
    if (cl->nupvals >= 1) { /* does it have an upvalue? */
        const TValue *gt = GT(T); /* get global table from clist */
        /* set global table as 1st upvalue of 'cl' (may be TOKU_ENV) */
        setobj(T, cl->upvals[0]->v.p, gt);
        tokuG_barrier(T, cl->upvals[0], gt);
    }
    return cl;
}


TOKU_API int toku_load(toku_State *T, toku_Reader reader, void *userdata,
                       const char *chunkname, const char *mode) {
    BuffReader Z = {0};
    int status;
    toku_lock(T);
    if (!chunkname) chunkname = "?";
    tokuR_init(T, &Z, reader, userdata);
    status = tokuPR_parse(T, &Z, chunkname, mode);
    if (status == TOKU_STATUS_OK) /* no errors? */
        posload(T);
    toku_unlock(T);
    return status;
}


#define FUNCTION        "(fn();)();\n"

static const char *reader(toku_State *T, void *data, size_t *szread) {
    UNUSED(T);
    if ((*cast(int *, data))--) {
        *szread = LL(FUNCTION);
        return FUNCTION;
    } else {
        *szread = 0;
        return NULL;
    }
}


TOKU_API int toku_combine(toku_State *T, const char *chunkname, int n) {
    BuffReader Z = {0};
    int status;
    int i = n;
    toku_lock(T);
    api_checknelems(T, n);
    if (!chunkname) chunkname = "=(combined)";
    tokuR_init(T, &Z, reader, &i);
    status = tokuPR_parse(T, &Z, chunkname, NULL);
    if (status == TOKU_STATUS_OK) {
        Proto *f = posload(T)->p;
        SPtr first = T->sp.p - n - 1;
        for (int i=0; i < n; i++) {
            TValue *o = s2v(first + i);
            api_check(T, ttisTclosure(o), "Tokudae function expected");
            f->p[i] = getproto(o);
            tokuG_objbarrier(T, f, f->p[i]);
            toku_assert(f->p[i]->sizeupvals == 0 ||
                        f->p[i]->upvals[0].instack == 0);
        }
    }
    toku_unlock(T);
    return status;
}


/*
** Dump a Tokudae function, calling 'fw' to write its parts. Ensure
** the stack returns with its original size.
*/
TOKU_API int toku_dump(toku_State *T, toku_Writer fw, void *data, int strip) {
    int status;
    ptrdiff_t otop = savestack(T, T->sp.p); /* original top */
    TValue *f = s2v(T->sp.p - 1); /* function to be dumped */
    toku_lock(T);
    api_checknelems(T, 1); /* function */
    api_check(T, ttisTclosure(f), "Tokudae function expected");
    status = tokuZ_dump(T, clTval(f)->p, fw, data, strip);
    T->sp.p = restorestack(T, otop);  /* restore top */
    toku_unlock(T);
    return status;
}

/* }====================================================================== */


/* {======================================================================
** Garbage collector
** ======================================================================= */

TOKU_API int toku_gc(toku_State *T, int option, ...) {
    va_list argp;
    int res = 0;
    GState *gs = G(T);
    if (gs->gcstop & (GCSTP | GCSTPCLS)) /* internal stop? */
        return -1; /* all options are invalid when stopped */
    toku_lock(T);
    va_start(argp, option);
    switch (option) {
        case TOKU_GC_STOP: /* stop garbage collector */
            gs->gcstop = GCSTPUSR; /* stopped by user */
            break;
        case TOKU_GC_RESTART: /* restart GC */
            tokuG_setgcdebt(gs, 0);
            gs->gcstop = 0; /* clear stop */
            break;
        case TOKU_GC_CHECK: /* check and clear collection flag */
            res = gs->gccheck; /* get check flag */
            gs->gccheck = 0; /* clear check flag */
            break;
        case TOKU_GC_COLLECT: /* start GC cycle */
            tokuG_fullinc(T, 0);
            break;
        case TOKU_GC_COUNT: /* total GC memory count (in Kbytes) */
            res = cast_int(gettotalbytes(gs) >> 10);
            break;
        case TOKU_GC_COUNTBYTES: /* remainder bytes of totalbytes/1024 */
            res = cast_int(gettotalbytes(gs) & 0x3FF); /* all before bit 10 */
            break;
        case TOKU_GC_STEP: { /* perform GC step */
            int data = va_arg(argp, int); /* Kbytes */
            t_mem gcdebt = 1; /* true if GC did work */
            t_ubyte old_gcstop = gs->gcstop;
            gs->gcstop = 0; /* allow GC to run */
            if (data == 0) {
                tokuG_setgcdebt(gs, 0); /* force to run one basic step */
                tokuG_step(T);
            } else { /* add 'data' to total gcdebt */
                /* convert 'data' to bytes (data = bytes/2^10) */
                gcdebt = cast(t_mem, data) * 1024 + gs->gcdebt;
                tokuG_setgcdebt(gs, gcdebt);
                tokuG_checkGC(T);
            }
            gs->gcstop = old_gcstop; /* restore previous state */
            if (gcdebt > 0 && gs->gcstate == GCSpause) /* end of cycle? */
                res = 1; /* signal it */
            break;
        }
        case TOKU_GC_PARAM: {
            int param = va_arg(argp, int);
            int value = va_arg(argp, int);
            api_check(T, 0 <= param && param < TOKU_GCP_NUM,
                         "invalid parameter");
            res = cast_int(getgcparam(gs->gcparams[param]));
            if (value >= 0) {
                if (param == TOKU_GCP_STEPSIZE)
                    gs->gcparams[param] = cast_ubyte(value);
                else
                    setgcparam(gs->gcparams[param], value);
            }
            break;
        }
        case TOKU_GC_ISRUNNING: /* check if GC is running */
            res = gcrunning(gs);
            break;
        default: res = -1; /* invalid option */
    }
    va_end(argp);
    toku_unlock(T);
    return res;
}

/* }====================================================================== */


/* {======================================================================
** Warning-related functions
** ======================================================================= */

TOKU_API void toku_setwarnf(toku_State *T, toku_WarnFunction fwarn, void *ud) {
    toku_lock(T);
    G(T)->fwarn = fwarn;
    G(T)->ud_warn = ud;
    toku_unlock(T);
}


TOKU_API void toku_warning(toku_State *T, const char *msg, int cont) {
    toku_lock(T);
    tokuT_warning(T, msg, cont);
    toku_unlock(T);
}

/* }====================================================================== */


/* {======================================================================
** Miscellaneous functions
** ====================================================================== */

TOKU_API unsigned toku_numbertocstring(toku_State *T, int index, char *buff) {
    const TValue *o = index2value(T, index);
    if (ttisnum(o)) {
        t_uint len = tokuS_tostringbuff(o, buff);
        buff[len++] = '\0'; /* terminate */
        return len;
    } else
        return 0;
}


TOKU_API size_t toku_stringtonumber(toku_State *T, const char *s, int *f) {
    size_t sz = tokuS_tonum(s, s2v(T->sp.p), f);
    if (sz != 0) /* no conversion errors? */
        api_inctop(T);
    return sz;
}


TOKU_API toku_Number toku_version(toku_State *T) {
    UNUSED(T);
    return TOKU_VERSION_NUM;
}


TOKU_API toku_Unsigned toku_len(toku_State *T, int index) {
    Table *t;
    const TValue *o = index2value(T, index);
    switch (ttypetag(o)) {
        case TOKU_VSHRSTR: return strval(o)->shrlen;
        case TOKU_VLNGSTR: return strval(o)->u.lnglen;
        case TOKU_VLIST: return cast_uint(listval(o)->len);
        case TOKU_VTABLE:
            t = tval(o);
            goto tlen;
        case TOKU_VINSTANCE:
            t = insval(o)->fields;
        tlen:
            return cast_uint(tokuH_len(t));
        case TOKU_VCLASS:
            t = classval(o)->methods;
            if (t) return cast_uint(tokuH_len(t));
            /* fall through */
        default: return 0;
    }
}


TOKU_API size_t toku_lenudata(toku_State *T, int index) {
    return getuserdata(T, index)->size;
}


TOKU_API int toku_nextfield(toku_State *T, int obj) {
    Table *t;
    int more;
    toku_lock(T);
    api_checknelems(T, 1); /* key */
    t = gettable(T, obj);
    more = tokuH_next(T, t, T->sp.p - 1);
    if (more) {
        api_inctop(T);
    } else
        T->sp.p--; /* remove key */
    toku_unlock(T);
    return more;
}


TOKU_API void toku_concat(toku_State *T, int n) {
    toku_lock(T);
    api_checknelems(T, n);
    if (n > 0) {
        tokuV_concat(T, n);
        tokuG_checkGC(T);
    } else { /* nothing to concatenate */
        setstrval2s(T, T->sp.p, tokuS_newl(T, "", 0));
        api_inctop(T);
    }
    toku_unlock(T);
}


TOKU_API void toku_toclose(toku_State *T, int index) {
    SPtr o;
    int nresults;
    toku_lock(T);
    o = index2stack(T, index);
    api_check(T, T->tbclist.p < o,
                  "given level below or equal to the last marked slot");
    tokuF_newtbcvar(T, o); /* create new to-be-closed upvalue */
    nresults = T->cf->nresults;
    if (!hastocloseCfunc(nresults)) /* function not yet marked? */
        T->cf->nresults = codeNresults(nresults); /* mark it */
    toku_assert(hastocloseCfunc(T->cf->nresults)); /* must be marked */
    toku_unlock(T);
}


TOKU_API void toku_closeslot(toku_State *T, int index) {
    SPtr level;
    toku_lock(T);
    level = index2stack(T, index);
    api_check(T, hastocloseCfunc(T->cf->nresults) && T->tbclist.p == level,
                 "no variable to close at the given level");
    level = tokuF_close(T, level, CLOSEKTOP);
    setnilval(s2v(level)); /* closed */
    toku_unlock(T);
}


TOKU_API int toku_shrinklist(toku_State *T, int index) {
    int res;
    toku_lock(T);
    res = tokuA_shrink(T, getlist(T, index));
    toku_unlock(T);
    return res;
}


TOKU_API unsigned short toku_numuservalues(toku_State *T, int index) {
    return getuserdata(T, index)->nuv;
}

/* }====================================================================== */


/* {{=====================================================================
** Debug functions (other functions are defined in tdebug.c)
** ======================================================================= */

/*
** Sets 'frame' in 'toku_Debug'; 'level' is 'CallFrame' level.
** To traverse the call stack backwards (up), then level should be
** greater than 0. For example if you wish for currently active 'CallFrame',
** then 'level' should be 0, if 'level' is 1 then the 'CallFrame' of the
** function that called the current function is considered.
** If 'level' is found, therefore 'cf' is set, then this function returns 1,
** otherwise 0.
*/
TOKU_API int toku_getstack(toku_State *T, int level, toku_Debug *ar) {
    int status = 0;
    CallFrame *cf;
    if (level >= 0) { /* valid level? */
        toku_lock(T);
        for (cf = T->cf; level > 0 && cf != &T->basecf; cf = cf->prev)
            level--;
        if (level == 0 && cf != &T->basecf) { /* level found ? */
            ar->cf = cf;
            status = 1; /* signal it */
        }
        toku_unlock(T);
    }
    return status;
}


TOKU_API int toku_stackinuse(toku_State *T) {
    toku_assert(savestack(T, T->sp.p) <= INT_MAX);
    return cast_int(savestack(T, T->sp.p));
}


static const char *aux_upvalue(const TValue *func, int n, TValue **val,
                               GCObject **owner) {
    switch (ttypetag(func)) {
        case TOKU_VCCL: { /* T closure */
            CClosure *f = clCval(func);
            if (!(cast_uint(n) < cast_uint(f->nupvals)))
                return NULL;  /* 'n' not in [0, cl->nupvals) */
            *val = &f->upvals[n];
            if (owner) *owner = obj2gco(f);
            return "";
        }
        case TOKU_VTCL: { /* Toku closure */
            OString *name;
            TClosure *f = clTval(func);
            if (!(cast_uint(n) < cast_uint(f->p->sizeupvals)))
                return NULL; /* 'n' not in [0, fn->sizeupvals) */
            *val = f->upvals[n]->v.p;
            if (owner) *owner = obj2gco(f->upvals[n]);
            name = f->p->upvals[n].name;
            return (name == NULL) ? "(no name)" : getstr(name);
        }
        default: return NULL; /* not a closure */
    }
}


/*
** If object is a bound method, this returns reference to the
** underlying function object (which again might be the bound method,
** but at that point we don't care, it's up to the user).
*/
static TValue *rawfunc(toku_State *T, int index) {
    TValue *o = index2value(T, index);
    if (ttisinstancemethod(o))
        o = &imval(o)->method;
    else if (ttisusermethod(o))
        o = &umval(o)->method;
    return o;
}


TOKU_API const char *toku_getupvalue(toku_State *T, int index, int n) {
    TValue *upval = NULL; /* to avoid warnings */
    const TValue *o = index2value(T, index);
    const char *name;
    toku_lock(T);
    name = aux_upvalue(o, n, &upval, NULL);
    if (name) { /* have upvalue ? */
        setobj2s(T, T->sp.p, upval);
        api_inctop(T);
    }
    toku_unlock(T);
    return name;
}


TOKU_API const char *toku_setupvalue(toku_State *T, int index, int n) {
    const char *name;
    TValue *upval = NULL; /* to avoid warnings */
    GCObject *owner = NULL; /* to avoid warnings */
    const TValue *o = index2value(T, index);
    toku_lock(T);
    api_checknelems(T, 1); /* value */
    name = aux_upvalue(o, n, &upval, &owner);
    if (name) { /* found upvalue ? */
        T->sp.p--;
        setobj(T, upval, s2v(T->sp.p));
        tokuG_barrier(T, owner, upval);
    }
    toku_unlock(T);
    return name;
}


static UpVal **getupvalref(toku_State *T, int index, int n, TClosure **pf) {
    static const UpVal *const nullup = NULL;
    const TValue *fi = rawfunc(T, index);
    TClosure *f;
    api_check(T, ttisTclosure(fi), "Toku function expected");
    f = clTval(fi);
    if (pf) *pf = f;
    if (0 <= n && n < f->p->sizeupvals)
        return &f->upvals[n]; /* get its upvalue pointer */
    else
        return (UpVal**)&nullup;
}


TOKU_API void *toku_upvalueid(toku_State *T, int index, int n) {
    const TValue *fi = rawfunc(T, index);
    switch (ttypetag(fi)) {
        case TOKU_VTCL: /* Toku closure */
            return *getupvalref(T, index, n, NULL);
        case TOKU_VCCL: { /* T closure */
            CClosure *f = clCval(fi);
            if (0 <= n && n < f->nupvals)
                return &f->upvals[n];
            /* else */
        } /* fall through */
        case TOKU_VLCF: return NULL; /* light T functions have no upvalues */
        default:
            api_check(T, 0, "function expected");
            return NULL; /* to avoid warnings */
    }
}


TOKU_API void toku_upvaluejoin(toku_State *T, int index1, int n1,
                                              int index2, int n2) {
    TClosure *f1;
    UpVal **up1 = getupvalref(T, index1, n1, &f1);
    UpVal **up2 = getupvalref(T, index2, n2, NULL);
    api_check(T, *up1 != NULL && *up2 != NULL, "invalid upvalue index");
    *up1 = *up2;
    tokuG_objbarrier(T, f1, *up1);
}

/* }{=====================================================================
** Opcode
** ======================================================================= */

static void fill_opcode_args(Instruction *pi, toku_Opcode *opc) {
    switch (getopFormat(*pi)) {
        case FormatI:
            opc->args[0] = -1;
            opc->args[1] = -1;
            opc->args[2] = -1;
            break;
        case FormatIS:
            opc->args[0] = cast_int(GET_ARG_S(pi, 0));
            opc->args[1] = -1;
            opc->args[2] = -1;
            break;
        case FormatISS:
            opc->args[0] = cast_int(GET_ARG_S(pi, 0));
            opc->args[1] = cast_int(GET_ARG_S(pi, 1));
            opc->args[2] = -1;
            break;
        case FormatIL:
            opc->args[0] = GET_ARG_L(pi, 0);
            opc->args[1] = -1;
            opc->args[2] = -1;
            break;
        case FormatILS:
            opc->args[0] = GET_ARG_L(pi, 0);
            opc->args[1] = cast_int(GET_ARG_S(pi + SIZE_ARG_L, 0));
            opc->args[2] = -1;
            break;
        case FormatILL:
            opc->args[0] = GET_ARG_L(pi, 0);
            opc->args[1] = GET_ARG_L(pi, 1);
            opc->args[2] = -1;
            break;
        case FormatILLS:
            opc->args[0] = GET_ARG_L(pi, 0);
            opc->args[1] = GET_ARG_L(pi, 1);
            opc->args[2] = cast_int(GET_ARG_S(pi + SIZE_ARG_L*2, 0));
            break;
        case FormatILLL:
            opc->args[0] = GET_ARG_L(pi, 0);
            opc->args[1] = GET_ARG_L(pi, 1);
            opc->args[2] = GET_ARG_L(pi, 2);
            break;
        default: toku_assert(0); /* unreachable */
    }
}


static void fillopcode(Proto *f, int pc, toku_Opcode *opc) {
    Instruction *pi = &f->code[pc];
    fill_opcode_args(pi, opc);
    opc->line = tokuD_getfuncline(f, pc);
    opc->offset = pc;
    opc->op = *pi;
    if (TOKU_OPCNAMESIZE > 1) /* names enabled? */
        strcpy(opc->name, opnames[*pi]);
    else /* otherwise names are disabled */
        opc->name[0] = '\0';
    opc->f = f;
}


TOKU_API int toku_getopcode(toku_State *T, const toku_Cinfo *ci, int n,
                            toku_Opcode *opc) {
    Proto *f = ci->f;
    int pc;
    UNUSED(T);
    if (f->sizeinstpc > 0) { /* have debug info? */
        if (n < 0) /* negative 'n'? */
            n = f->sizeinstpc + n;
        if (0 <= n && n < f->sizeinstpc) /* 'n' in range? */
            pc = f->instpc[n];
         else /* otherwise 'n' too large */
            return 0;
    } else if (n < f->sizecode) { /* 'n' seems to be in range? */
        Instruction *code = f->code;
        int limitpc = f->sizecode - getopSize(OP_RETURN);
        toku_assert(code[limitpc] == OP_RETURN);
        if (0 <= n) { /* positive 'n'? */
            for (pc = 0; 0 < n && pc < limitpc; n--)
                pc += getopSize(code[pc]); /* walk the bytecode */
            if (n != 0) /* 'n' too large? */
                return 0;
        } else { /* otherwise negative 'n' */
            int limit = (n < 0) ? -n - 1 : n;
            if (limitpc <= limit) /* 'limit' (obviously) out of range? */
                return 0;
            else { /* otherwise 'limit' might be in range */
                int targetpc = 0;
                int distance = 0;
                pc = 0;
                while (pc < limitpc) { /* walk until last instruction */
                    if (distance == limit)
                        targetpc += getopSize(code[targetpc]);
                    else
                        distance++;
                    pc += getopSize(code[pc]);
                }
                if (distance != limit) /* not found? */
                    return 0;
                pc = targetpc;
            }
        }
    } else /* otherwise 'n' is (obviously) out of range */
        return 0;
    fillopcode(f, pc, opc);
    return 1; /* ok */
}


/* {=====================================================================
** Opcode Description
** ====================================================================== */

/* absence of instruction argument */
#define NOARG       (-1)

/* shorter alias */
#define MAXD        (TOKU_OPCDESCSIZE)

#define D(msg)          snprintf(opd->desc, MAXD, msg)
#define DA(n,msg)       snprintf(&opd->desc[n], cast_sizet(MAXD-(n)), msg)
#define DX(fmt,...)     snprintf(opd->desc, MAXD, fmt, __VA_ARGS__)
#define DAX(x,fmt,...) \
        snprintf(&opd->desc[x], cast_sizet(MAXD-(x)), fmt, __VA_ARGS__)

#define COMMENT(n,msg)          DA(n, "/* "msg" */")
#define COMMENTX(n,fmt,...)     DAX(n, "/* "fmt" */", __VA_ARGS__)

#define preCall(n,nres) \
    { toku_assert((nres) >= TOKU_MULTRET); \
      if ((nres) == TOKU_MULTRET) \
        COMMENT(n, "returns all results"); \
      else \
        COMMENTX(n, "returns %d result%s", nres, (nres)==1 ? "" : "s"); }


#define setExtraStr(opd,str) \
    { (opd)->extra.type = TOKU_T_STRING; (opd)->extra.value.s = (str); }

#define setExtraNum(opd,num) \
    { (opd)->extra.type = TOKU_T_NUMBER; (opd)->extra.value.n = (num); }

static const char *setExtraTagstr(toku_State *T, toku_Opdesc *opd, int event) {
    const char *tm = getstr(eventstring(T, event));
    setExtraStr(opd, tm);
    return tm;
}

static void setExtraK(Proto *f, toku_Opdesc *opd, int k) {
    TValue *kv = &f->k[k];
    opd->extra.type = ttype(kv);
    switch (opd->extra.type) {
        case TOKU_T_STRING: {
            OString *str = strval(kv);
            opd->extra.value.s = getstr(str);
            break;
        }
        case TOKU_T_NUMBER:
            if (ttisflt(kv))
                opd->extra.value.n = fval(kv);
            else
                opd->extra.value.n = cast_num(ival(kv));
            break;
        case TOKU_T_BOOL:
            opd->extra.value.b = ttistrue(kv);
            break;
        default: break;
    }
}

static int setExtraI(toku_Opdesc *opd, int imm, int l) {
    imm = l ? IMML(imm) : IMM(imm);
    setExtraNum(opd, cast_num(imm));
    return imm;
}

#define setExtraB(opd,bt) \
    { (opd)->extra.type = TOKU_T_BOOL; \
      (opd)->extra.value.b = ((bt) == TOKU_VTRUE); }


/*
**
** Description legend:
**
** pc           <- integer, current offset in the bytecode
** nextra       <- integer, total number of extra arguments
** arity        <- integer, total number of named parameters
** nreturns     <- integer, number of results this call returns,
**                 value of -1 means it returns all results
**
** sptr         <- stack pointer
** base         <- base pointer, at base[-1] is the currently active function
**                 and after the adjustment, below the base[-1], are the extra
**                 arguments, in the interval [base[-nextra - 1], base[-2]]
**                 (this interval might be empty aka no extra arguments)
**
** functions    <- array, precompiled functions defined in the active function
** constants    <- array, constants of the active function
** upvalues     <- array, upvalues of the active function
**
** callee       <- macro equivalent to base[-1]
** top          <- macro equivalent to sptr[-1]
** btop         <- macro equivalent to sptr[-2]
** varg(x)      <- macro equivalent to base[-nextra - 1 + x]
**
** isfalse(x)   <- function, returns true if 'x' is false or nil
** getsuper(x)  <- function, returns the superclass of 'x'
** copytable(x) <- function, if 'x' is a table it returns a copy of 'x',
**                 otherwise it returns nil
** concat(x,y)  <- function, return concatenation of 'x' with 'y'
** markclose(x) <- function, marks 'x' (pointer to stack) as to-be-closed
** close(x)     <- function, close appropriate values in ['x', &top]
** new_cl(x)    <- function, returns new closure from function 'x'
** new_list(x)  <- function, returns new list of size 'x'
** new_table(x) <- function, returns new table of size 'x'
** new_class(mt,n)  <- function, returns new class, if 'mt' is true class
**                     will have newly allocated empty metatable, 'n' is the
**                     size of method table
** setsuper(x,y)    <- function, sets 'y' as superclass of class 'x'
** stackrange(x,y)  <- function, 'x' and 'y' are pointers to stack, so this
**                     returns all the stack values in ['x', 'y']
** append(l, ...)   <- function, 'l' is list, all the values after are
**                     appended into the list 'l'
** stackcpy(x,y,n)  <- function, equivalent to memcpy, 'x' and 'y' are stack
**                     pointers and 'n' is the number of stack values
**
*/

static void DPushBool(toku_Opdesc *opd, int t) {
    toku_assert(t == TOKU_VTRUE || t == TOKU_VFALSE);
    DX("*sptr++ = %s;", (t == TOKU_VTRUE) ? "true" : "false");
    setExtraB(opd, t);
}

#define DSuper(opd)      D("top = getsuper(top);");

static void DNil(toku_Opdesc *opd, int n) {
    DX("int n=%d; while (n--) { *sptr++ = nil; }", n);
    opd->extra.type = TOKU_T_NIL;
}

#define DPop(opd,n)     DX("sptr -= %d;", n);

static void DK(Proto *f, toku_Opdesc *opd, int k) {
    setExtraK(f, opd, k);
    DX("*sptr++ = constants[%d];", k);
}

static void DI(toku_Opdesc *opd, int imm, int l, int f) {
    imm = setExtraI(opd, imm, l);
    if (f) /* float? */
        DX("*sptr++ = "TOKU_NUMBER_FMT";", cast_num(imm));
    else /* otherwise integer */
        DX("*sptr++ = %d;", imm);
}

static void DVarargPrep(toku_Opdesc *opd, int arity) {
    int n = DX("for (int i=-1; i<%d", arity);
    n += COMMENT(n, "arity");
    n += DA(n, "; i++) { *sptr++ = base[i]; }");
    DAX(n, " base = sptr - %d;", arity);
}

static void DVarargs(toku_Opdesc *opd, int n) {
    if (n == TOKU_MULTRET)
        D("for (int i=0; i<nextra; i++) *stpr++ = varg(i);");
    else
        DX("for (int i=0; i<%d; i++) *sptr++ = varg(i);", n);
}

static void DClosure(Proto *f, toku_Opdesc *opd, int i) {
    Proto *cl = f->p[i];
    int n = DX("*sptr++ = new_cl(functions[%d]); ", i);
    COMMENTX(n, "%p", cast_voidp(cl));
}

#define decodesize(sz)      (((sz) > 0) ? (1<<((sz)-1)) : 0)

#define DList(opd,sz)       DX("*sptr++ = new_list(%d);", decodesize(sz));

#define DTable(opd,sz)      DX("*sptr++ = new_table(%d);", decodesize(sz));

static void DClass(toku_Opdesc *opd, int mask) {
    int hasmt = mask & 0x80;
    int nmethods = mask & 0x7f;
    nmethods = decodesize(nmethods);
    DX("*sptr++ = new_class(%d, %d);", hasmt, nmethods);
}

static void DClassProp(Proto *f, toku_Opdesc *opd, int k, const char *what) {
    setExtraK(f, opd, k);
    DX("btop.%s[constants[%d]] = top; sptr--;", what, k);
}

static void DTagMethod(toku_State *T, toku_Opdesc *opd, int event) {
    const char *tm = setExtraTagstr(T, opd, event);
    DX("btop.metatable.%s = top; sptr--;", tm);
}

static void DMetaBin(toku_State *T, toku_Opdesc *opd, int event) {
    const char *tm = setExtraTagstr(T, opd, event);
    DX("btop = btop.metatable.%s(btop, top); sptr--;", tm);
}

static const char *binops[] = {
    "+", "-", "*", "/", "//", "%", "**",
    "<<", ">>", "&", "|", "^"
};

static void descBinK(Proto *f, toku_Opdesc *opd, int k, int op) {
    setExtraK(f, opd, k);
    DX("top %s= constants[%d];", binops[op - OP_ADDK], k);
}

static void descBinI(toku_Opdesc *opd, int imm, int binop) {
    imm = setExtraI(opd, imm, 1);
    DX("top %s= %d;", binops[binop- OP_ADDI], imm);
}

static void descBin(toku_Opdesc *opd, int swap, int binop) {
    const char *sym = binops[binop - OP_ADD];
    if (!swap)
        DX("btop %s= top; sptr--;", sym);
    else
        DX("btop = top %s btop; sptr--;", sym);
}

static void DEqK(Proto *f, toku_Opdesc *opd, int k) {
    setExtraK(f, opd, k);
    DX("top = (top == constants[%d]);", k);
}

static void DEqI(toku_Opdesc *opd, int imm) {
    imm = setExtraI(opd, imm, 1);
    DX("top = (top == %d);", imm);
}

static void DOrd(toku_Opdesc *opd, int ordop) {
    static const char *ops[] = { "==", "<", "<=" };
    DX("btop = (btop %s top); sptr--;", ops[ordop - OP_EQ]);
}

#define DEqp(opd)    D("top = (btop == top);");

static void descOrdI(toku_Opdesc *opd, int imm, int ordop) {
    static const char *ops[] = { "<", "<=", ">", ">=" };
    imm = setExtraI(opd, imm, 1);
    DX("top = (top %s %d);", ops[ordop - OP_LTI], imm);
}

static void DConcat(toku_Opdesc *opd, int i) {
    int n = DX("for (int i=%d; 1<i; i--)", i);
    n += DA(n, " { sptr[-i] = concat(sptr[-i], sptr[-i+1]); } ");
    DAX(n, "sptr -= %d;", i - 1);
}

static void DUnop(toku_Opdesc *opd, int unop) {
    static const char *ops[] = { "-", "~", "!" };
    DX("top = %stop;", ops[unop - OP_UNM]);
}

#define jumpoffset(off,back)        ((back) ? -(off) : (off))
#define jumptarget(from,off,op)     ((from) + getopSize(op) + (off))

static void DJump(toku_Opdesc *opd, int pc, int off, int back) {
    int offset = jumpoffset(off, back);
    int target = jumptarget(pc, offset, OP_JMP);
    toku_assert(getopSize(OP_JMP) == getopSize(OP_JMPS));
    int n = DX("pc += %d; ", offset);
    COMMENTX(n, "jump to %d", target);
}

static void DTest(toku_Opdesc *opd, int pc, int cond, int pop) {
    const char *extra = pop ? " sptr--;" : "";
    int opsz = getopSize(OP_JMP);
    int off = jumpoffset(opsz, 0);
    int n = DX("if (!isfalse(top) == %d) { pc += %d; ", cond, opsz);
    toku_assert(getopSize(OP_TEST) == getopSize(OP_TESTPOP));
    n += COMMENTX(n, "jump to %d", jumptarget(pc, off, OP_TEST));
    DAX(n, " }%s", extra);

}

static void DCall(toku_Opdesc *opd, int func, int nres) {
    int n = DX("base[%d](stackrange(&base[%d], &top)); ", func, func+1);
    preCall(n, nres);
}

#define DTbc(opd,slot)      DX("markclose(&base[%d]);", slot)

#define DClose(opd,slot)    DX("close(&base[%d]);", slot)

static void DCheck(toku_Opdesc *opd, int first) {
    int n = DX("if (!base[%d]) { return ", first);
    n += COMMENT(n, "all results from previous call");
    DA(n, " }");
}

static void DLocal(toku_Opdesc *opd, int slot, int get) {
    if (get)
        DX("*sptr++ = base[%d];", slot);
    else
        DX("base[%d] = *--sptr;", slot);
}

static void DUpvalue(toku_Opdesc *opd, int idx, int get) {
    if (get)
        DX("*sptr++ = upvalues[%d];", idx);
    else
        DX("upvalues[%d] = *--sptr;", idx);

}

static void DSetlist(toku_Opdesc *opd, int slot, int len) {
    int n = DX("append(base[%d], stackrange(&base[%d], &top)); ", slot, slot+1);
    COMMENTX(n, "list length is %d (before append)", len);
}

static void DIndex(toku_Opdesc *opd, int slot, int get) {
    if (get)
        D("btop = btop[top];");
    else
        DX("base[%d][base[%d]] = *--sptr;", slot, slot+1);
}

static void DIndexK(Proto *f, toku_Opdesc *opd, int slot, int k, int dot) {
    int n;
    if (slot == NOARG)
        n = DX("top = top[constant[%d]];", k);
    else
        n = DX("base[%d][constant[%d]] = *--sptr;", slot, k);
    if (dot) {
        n += DA(n, " ");
        COMMENT(n, "raw index");
    }
    setExtraK(f, opd, k);
}

static void DIndexI(toku_Opdesc *opd, int slot, int imm, int l) {
    imm = setExtraI(opd, imm, l);
    if (slot != NOARG)
        DX("top = top[%d];", imm);
    else
        DX("base[%d][%d] = *--sptr;", slot, imm);
}

static void DGetSuper(Proto *f, toku_Opdesc *opd, int k) {
    if (k != NOARG) {
        DX("top = getsuper(top)[constant[%d]]; ", k);
        setExtraK(f, opd, k);
    } else
         D("dtop = getsuper(dtop)[*--sptr];");
}

static void DInherit(toku_Opdesc *opd) {
    int n = D("dtop.methods = copytable(top.methods); ");
    n += DA(n, "dtop.metatable = copytable(top.metatable); ");
    DA(n, "setsuper(dtop, top);");
}

static void DForPrep(toku_Opdesc *opd, int slot, int offset) {
    slot += VAR_TBC;
    int n = DX("if (base[%d]) { markclose(&base[%d]); }", slot, slot);
    DAX(n, " pc += %d;", offset);
}

static void DForCall(toku_Opdesc *opd, int src, int nres) {
    int dest, n;
    src += VAR_ITER;
    dest = src + VAR_N;
    n = DX("stackcpy(&base[%d], &base[%d], %d); ", dest, src, VAR_N - 1);
    n += DAX(n, "sptr = &base[%d]; ", src + VAR_N + VAR_CNTL + 1);
    n += DAX(n, "base[%d](base[%d], base[%d]); ", dest, dest+1, dest+2);
    preCall(n, nres);
}

static void DForLoop(toku_Opdesc *opd, int slot, int offset, int npop) {
    int n = DX("if (base[%d] != nil) { base[%d] = base[%d]; pc -= %d; }",
                slot+VAR_N, slot+VAR_CNTL, slot+VAR_N+VAR_ITER, offset);
    DAX(n, " else { sptr -= %d; }", npop);
}

static void DReturn(Proto *f, toku_Opdesc *opd, int slot, int nres, int close) {
    int n = 0;
    if (close)
        n = D("close(&base[0]); ");
    if (nres != TOKU_MULTRET)
        n += DAX(n, "int nres = %d; sptr = &base[%d] + nres; ", nres, slot);
    else
        n += DAX(n, "int nres = top - &base[%d]; ", slot);
    if (f->isvararg)
        n += DA(n, "base -= (nextra + arity + 1); ");
    n += DA(n, "if (nres > nreturns) nres = nreturns; ");
    n += DA(n, "stackcpy(&base[0], sptr - nres, nres); ");
    n += DA(n, "while (nres < nreturns) { base[nres++] = nil; } ");
    DA(n, "sptr = base + nreturns; ");
}

static void setdescription(toku_State *T, toku_Opdesc *opd,
                                          const toku_Opcode *opc) {
    Proto *f = opc->f;
    int flag = 0;
    int get = 1;
    switch (opc->op) {
        case OP_TRUE: DPushBool(opd, TOKU_VTRUE); break;
        case OP_FALSE: DPushBool(opd, TOKU_VFALSE); break;
        case OP_NIL: DNil(opd, opc->args[0]); break;
        case OP_POP: DPop(opd, opc->args[0]); break;
        case OP_SUPER: DSuper(opd); break;
        case OP_VARARGPREP: DVarargPrep(opd, opc->args[0]); break;
        case OP_VARARG: DVarargs(opd, opc->args[0]); break;
        case OP_CLOSURE: DClosure(f, opd, opc->args[0]); break;
        case OP_NEWLIST: DList(opd, opc->args[0]); break;
        case OP_NEWCLASS: DClass(opd, opc->args[0]); break;
        case OP_NEWTABLE: DTable(opd, opc->args[0]); break;
        case OP_METHOD: DClassProp(f, opd, opc->args[0], "methods"); break;
        case OP_SETTM: DTagMethod(T, opd, opc->args[0]); break;
        case OP_SETMT: DClassProp(f, opd, opc->args[0], "metatable"); break;
        case OP_MBIN: DMetaBin(T, opd, opc->args[0]); break;
        case OP_EQK: DEqK(f, opd, opc->args[0]); break;
        case OP_EQI: DEqI(opd, opc->args[0]); break;
        case OP_EQPRESERVE: DEqp(opd); break;
        case OP_CONCAT: DConcat(opd, opc->args[0]); break;
        case OP_CALL: DCall(opd, opc->args[0]+1, opc->args[1]-1); break;
        case OP_CLOSE: DClose(opd, opc->args[0]); break;
        case OP_TBC: DTbc(opd, opc->args[0]); break;
        case OP_CHECKADJ: DCheck(opd, opc->args[0]); break;
        case OP_GETSUP: DGetSuper(f, opd, opc->args[0]); break;
        case OP_GETSUPIDX: DGetSuper(f, opd, NOARG); break;
        case OP_GETINDEX: DIndex(opd, NOARG, 1); break;
        case OP_SETINDEX: DIndex(opd, opc->args[0], 0); break;
        case OP_SETLIST: DSetlist(opd, opc->args[0], opc->args[1]); break;
        case OP_INHERIT: DInherit(opd); break;
        case OP_FORPREP: DForPrep(opd, opc->args[0], opc->args[1]); break;
        case OP_FORCALL: DForCall(opd, opc->args[0], opc->args[1]-1); break;
        case OP_SETLOCAL:
            get = 0; /* fall through */
        case OP_LOAD: case OP_GETLOCAL:
            DLocal(opd, opc->args[0], get);
            break;
        case OP_SETUVAL:
            get = 0; /* fall through */
        case OP_GETUVAL:
            DUpvalue(opd, opc->args[0], get);
            break;
        case OP_CONSTFL:
            flag = 1; /* fall through */
        case OP_CONSTF:
            DI(opd, opc->args[0], flag, 1);
            break;
        case OP_CONSTIL:
            flag = 1; /* fall through */
        case OP_CONSTI:
            DI(opd, opc->args[0], flag, 0);
            break;
        case OP_CONST: case OP_CONSTL:
            DK(f, opd, opc->args[0]);
            break;
        case OP_EQ: case OP_LT: case OP_LE:
            DOrd(opd, opc->op);
            break;
        case OP_UNM: case OP_BNOT: case OP_NOT:
            DUnop(opd, opc->op);
            break;
        case OP_JMPS:
            flag = 1; /* fall through */
        case OP_JMP:
            DJump(opd, opc->offset, opc->args[0], flag);
            break;
        case OP_TESTPOP:
            flag = 1; /* fall through */
        case OP_TEST:
            DTest(opd, opc->offset, opc->args[0], flag);
            break;
        case OP_FORLOOP:
            DForLoop(opd, opc->args[0], opc->args[1], opc->args[2]);
            break;
        case OP_RETURN:
            DReturn(f, opd, opc->args[0], opc->args[1]-1, opc->args[2]);
            break;
        case OP_GETINDEXINTL:
            flag = 1; /* fall through */
        case OP_GETINDEXINT:
            DIndexI(opd, NOARG, opc->args[0], flag);
            break;
        case OP_SETINDEXINTL:
            flag = 1; /* fall through */
        case OP_SETINDEXINT:
            DIndexI(opd, opc->args[0], opc->args[1], flag);
            break;
        case OP_GETPROPERTY:
            flag = 1; /* fall through */
        case OP_GETINDEXSTR:
            DIndexK(f, opd, NOARG, opc->args[0], flag);
            break;
        case OP_SETPROPERTY:
            flag = 1; /* fall through */
        case OP_SETINDEXSTR:
            DIndexK(f, opd, opc->args[0], opc->args[1], flag);
            break;
        case OP_LTI: case OP_LEI: case OP_GTI: case OP_GEI:
            descOrdI(opd, opc->args[0], opc->op);
            break;
        case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
        case OP_IDIVK: case OP_MODK: case OP_POWK: case OP_BSHLK:
        case OP_BSHRK: case OP_BANDK: case OP_BORK: case OP_BXORK:
            descBinK(f, opd, opc->args[0], opc->op);
            break;
        case OP_ADDI: case OP_SUBI: case OP_MULI: case OP_DIVI:
        case OP_IDIVI: case OP_MODI: case OP_POWI: case OP_BSHLI:
        case OP_BSHRI: case OP_BANDI: case OP_BORI: case OP_BXORI:
            descBinI(opd, opc->args[0], opc->op);
            break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_IDIV: case OP_MOD: case OP_POW: case OP_BSHL:
        case OP_BSHR: case OP_BAND: case OP_BOR: case OP_BXOR:
            descBin(opd, opc->args[0], opc->op);
            break;
        default: toku_assert(0); /* unreachable */
    }
}


TOKU_API void toku_getopdesc(toku_State *T, toku_Opdesc *opd,
                                            const toku_Opcode *opc) {
    opd->extra.type = TOKU_T_NONE;
    opd->extra.value.b = 0;
    setdescription(T, opd, opc);
}

/* }===================================================================== */


t_sinline void setcompileinfo(toku_Cinfo *ci) {
    Proto *f = ci->f;
    ci->func = cast_cvoidp(f);
    ci->constants = cast_cvoidp(f->k);
    ci->functions = cast_cvoidp(f->p);
    ci->code = cast_cvoidp(f->code);
    if (f->source) {
        ci->source = getstr(f->source);
        ci->srclen = getstrlen(f->source);
    } else {
        ci->source = "=?";
        ci->srclen = LL("=?");
    }
    ci->defline = f->defline;
    ci->lastdefline = f->deflastline;
    ci->nupvals = f->sizeupvals;
    ci->nlocals = f->sizelocals;
    ci->nparams = f->arity;
    ci->nconstants = f->sizek;
    ci->nfunctions = f->sizep;
    ci->ncode = f->sizecode;
    ci->nslots = f->maxstack;
}


TOKU_API int toku_getcompinfo(toku_State *T, int idx, toku_Cinfo *ci) {
    const TValue *o = index2value(T, idx);
    api_check(T, ttisfunction(o), "function expected");
    if (ttisTclosure(o)) {
        ci->f = getproto(o);
        setcompileinfo(ci);
        return 1; /* true; got prototype information */
    }
    return 0; /* false; only Tokudae closures have prototypes */
}


TOKU_API int toku_getconstant(toku_State *T, const toku_Cinfo *ci, int n) {
    Proto *f = ci->f;
    int t;
    toku_lock(T);
    if (cast_uint(n) < cast_uint(f->sizek)) {
        TValue *k = &f->k[cast_uint(n)];
        t = ttype(k);
        setobj2s(T, T->sp.p, k);
        api_inctop(T);
    } else
        t = TOKU_T_NONE;
    toku_unlock(T);
    return t;
}


TOKU_API void toku_getshortsrc(toku_State *T, const toku_Cinfo *ci, char *out) {
    UNUSED(T);
    tokuS_chunkid(out, ci->source, ci->srclen);
}


TOKU_API const char *toku_getlocalinfo(toku_State *T, toku_Cinfo *ci, int n) {
    Proto *f = ci->f;
    UNUSED(T);
    if (cast_uint(n) < cast_uint(f->sizelocals)) {
        LVarInfo *lvar = &f->locals[cast_uint(n)];
        ci->info.l.startoff = lvar->startpc;
        ci->info.l.endoff = lvar->endpc;
        return getstr(lvar->name);
    }
    return NULL;
}


TOKU_API const char *toku_getupvalueinfo(toku_State *T, toku_Cinfo *ci, int n) {
    Proto *f = ci->f;
    UNUSED(T);
    if (cast_uint(n) < cast_uint(f->sizeupvals)) {
        UpValInfo *uvar = &f->upvals[cast_uint(n)];
        ci->info.u.idx = uvar->idx;
        ci->info.u.instack = uvar->instack;
        return uvar->name ? getstr(uvar->name) : "-";
    }
    return NULL;
}


TOKU_API int toku_getfunction(toku_State *T, const toku_Cinfo *src,
                                             toku_Cinfo *dest, int n) {
    UNUSED(T);
    if (cast_uint(n) < cast_uint(src->f->sizep)) { /* 'n' in bounds? */
        dest->f = src->f->p[cast_uint(n)];
        setcompileinfo(dest);
        return 1;
    }
    return 0;
}

/* }}====================================================================== */
