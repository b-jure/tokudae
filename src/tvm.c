/*
** tvm.c
** Tokudae Virtual Machine
** See Copyright Notice in tokudae.h
*/

#define tvm_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <string.h>

#include "tapi.h"
#include "tlist.h"
#include "tokudaeconf.h"
#include "tfunction.h"
#include "tgc.h"
#include "ttable.h"
#include "tokudae.h"
#include "tokudaelimits.h"
#include "tobject.h"
#include "tdebug.h"
#include "tobject.h"
#include "tstate.h"
#include "tcode.h"
#include "tvm.h"
#include "tmeta.h"
#include "tstring.h"
#include "tprotected.h"


#if 0 /* (by default do not trace bytecode execution) */

#include <stdio.h>
#include "topnames.h"

#define debugline(p,pc) \
        printf("%-12s on line: %d\n", \
                opnames[I], tokuD_getfuncline(p, relpc(pc, p)));

#define TOKU_USE_JUMPTABLE      0

#else /* otherwise do not trace lines */

#define debugline(p,pc)     ((void)0)

#if !defined(TOKU_USE_JUMPTABLE)
#if defined(__GNUC__)
#define TOKU_USE_JUMPTABLE      1
#else
#define TOKU_USE_JUMPTABLE      0
#endif
#endif

#endif


/* cast OpCode to TM */
#define asTM(op)    cast(TM, op)


/*
** 't_intfitsf' checks whether a given integer is in the range that
** can be converted to a float without rounding. Used in comparisons.
*/

/* number of bits in the mantissa of a float */
#define NBM             (t_floatatt(MANT_DIG))

/*
** Check whether some integers may not fit in a float, testing whether
** (maxinteger >> NBM) > 0. (That implies (1 << NBM) <= maxinteger.)
** (The shifts are done in parts, to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(long) == 32.)
*/
#if ((((TOKU_INTEGER_MAX >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
        >> (NBM - (3 * (NBM / 4))))  >  0

/* limit for integers that fit in a float */
#define MAXINTFITSF     ((toku_Unsigned)1 << NBM)

/* check whether 'i' is in the interval [-MAXINTFITSF, MAXINTFITSF] */
#define t_intfitsf(i)   ((MAXINTFITSF + t_castS2U(i)) <= (2 * MAXINTFITSF))

#else /* all integers fit in a float precisely */

#define t_intfitsf(i)   1

#endif


#if !defined(t_swap)
#define t_swap(v1_,v2_) { TValue *v = (v1_); (v1_) = (v2_); (v2_) = v; }
#define t_cswap(v1_,v2_) { const TValue *v = (v1_); (v1_) = (v2_); (v2_) = v; }
#endif


static uint8_t booleans[2] = { TOKU_VFALSE, TOKU_VTRUE };


/*
** Allocate new Tokudae closure, push it on stack and initialize
** its upvalues.
*/
static void pushclosure(toku_State *T, Proto *p, UpVal **encup, SPtr base) {
    int32_t nup = p->sizeupvals;
    UpValInfo *uv = p->upvals;
    TClosure *cl = tokuF_newTclosure(T, nup);
    cl->p = p;
    setclTval2s(T, T->sp.p++, cl); /* anchor to stack */
    for (int32_t i = 0; i < nup; i++) { /* fill its upvalues */
        if (uv[i].instack) /* upvalue refers to local variable? */
            cl->upvals[i] = tokuF_findupval(T, base + uv[i].idx);
        else /* get upvalue from enclosing function */
            cl->upvals[i] = encup[uv[i].idx];
        tokuG_objbarrier(T, cl, cl->upvals[i]);
    }
}


/*
** Integer division; handles division by 0 and possible
** overflow if 'y' == '-1' and 'x' == TOKU_INTEGER_MIN.
*/
toku_Integer tokuV_divi(toku_State *T, toku_Integer x, toku_Integer y) {
    if (t_unlikely(t_castS2U(y) + 1 <= 1)) { /* 'y' == '0' or '-1' */
        if (y == 0)
            tokuD_runerror(T, "divide by zero");
        return intop(-, 0, x);
    } else {
        toku_Integer q = x / y; /* perform C division */
        if ((x ^ y) < 0 && x % y != 0) /*'m/n' would be negative non-integer?*/
            q -= 1; /* correct result for different rounding */
        return q;
    }
}


/*
** Integer modulus; handles modulo by 0 and overflow
** as explained in 'tokuV_divi()'.
*/
toku_Integer tokuV_modi(toku_State *T, toku_Integer x, toku_Integer y) {
    if (t_unlikely(t_castS2U(y) + 1 <= 1)) {
        if (y == 0)
            tokuD_runerror(T, "attempt to 'n%%0'");
        return 0;
    } else {
        toku_Integer r = x % y;
        if (r != 0 && (r ^ y) < 0) /* 'x/y' would be non-integer negative? */
            r += y; /* correct result for different rounding */
        return r;
    }
}


/* floating point modulus */
toku_Number tokuV_modf(toku_State *T, toku_Number x, toku_Number y) {
    toku_Number r;
    tokui_nummod(T, x, y, r);
    return r;
}


/*
** Perform binary arithmetic operations on objects, this function is free
** to call metamethods in cases where raw arithmetics are not possible.
*/
void tokuV_binarithm(toku_State *T, const TValue *v1, const TValue *v2,
                     SPtr res, int32_t op) {
    if (!tokuO_arithmraw(T, v1, v2, s2v(res), op))
        tokuTM_trybin(T, v1, v2, res, asTM((op-TOKU_OP_ADD) + TM_ADD));
}


/*
** Perform unary arithmetic operations on objects, this function is free
** to call metamethods in cases where raw arithmetics are not possible.
*/
void tokuV_unarithm(toku_State *T, const TValue *v, SPtr res, int32_t op) {
    TValue aux;
    setival(&aux, 0);
    if (!tokuO_arithmraw(T, v, &aux, s2v(T->sp.p - 1), op))
        tokuTM_tryunary(T, v, res, asTM((op - TOKU_OP_UNM) + TM_UNM));
}


t_sinline int32_t intLEfloat(toku_Integer i, toku_Number f) {
    if (t_intfitsf(i))
        return tokui_numle(cast_num(i), f); /* compare them as floats */
    else {  /* i <= f <=> i <= floor(f) */
        toku_Integer fi;
        if (tokuO_n2i(f, &fi, N2IFLOOR)) /* fi = floor(f) */
            return i <= fi; /* compare them as integers */
        else /* 'f' is either greater or less than all integers */
            return f > 0; /* greater? */
    }
}


t_sinline int32_t floatLEint(toku_Number f, toku_Integer i) {
    if (t_intfitsf(i))
        return tokui_numle(f, cast_num(i)); /* compare them as floats */
    else {  /* f <= i <=> ceil(f) <= i */
        toku_Integer fi;
        if (tokuO_n2i(f, &fi, N2ICEIL)) /* fi = ceil(f) */
            return fi <= i; /* compare them as integers */
        else /* 'f' is either greater or less than all integers */
            return f < 0; /* less? */
    }
}


/* less equal ordering on numbers */
t_sinline int32_t LEnum(const TValue *v1, const TValue *v2) {
    toku_assert(ttisnum(v1) && ttisnum(v2));
    if (ttisint(v1)) {
        toku_Integer i1 = ival(v1);
        if (ttisint(v2))
            return i1 <= ival(v2);
        else
            return intLEfloat(i1, fval(v2));
    } else {
        toku_Number n1 = fval(v1);
        if (ttisint(v2))
            return floatLEint(n1, ival(v2));
        else
            return tokui_numle(n1, fval(v2));
    }
}


/* less equal ordering on non-number values */
t_sinline int32_t LEother(toku_State *T, const TValue *v1, const TValue *v2) {
    if (ttisstring(v1) && ttisstring(v2))
        return (tokuS_cmp(strval(v1), strval(v2)) <= 0);
    else
        return tokuTM_order(T, v1, v2, TM_LE);
}


/* 'less or equal' ordering '<=' */
int32_t tokuV_orderle(toku_State *T, const TValue *v1, const TValue *v2) {
    if (ttisnum(v1) && ttisnum(v2))
        return LEnum(v1, v2);
    return LEother(T, v1, v2);
}


t_sinline int32_t intLTfloat(toku_Integer i, toku_Number f) {
    if (t_intfitsf(i))
        return tokui_numlt(cast_num(i), f);
    else { /* i < f <=> i < ceil(f) */
        toku_Integer fi;
        if (tokuO_n2i(f, &fi, N2ICEIL)) /* fi = ceil(f) */
            return i < fi; /* compare them as integers */
        else /* 'f' is either greater or less than all integers */
            return f > 0; /* greater? */
    }
}


t_sinline int32_t floatLTint(toku_Number f, toku_Integer i) {
    if (t_intfitsf(i))
        return tokui_numlt(f, cast_num(i)); /* compare them as floats */
    else { /* f < i <=> floor(f) < i */
        toku_Integer fi;
        if (tokuO_n2i(f, &fi, N2IFLOOR)) /* fi = floor(f) */
            return fi < i; /* compare them as integers */
        else /* 'f' is either greater or less than all integers */
            return f < 0; /* less? */
    }
}


/* 'less than' ordering '<' on number values */
t_sinline int32_t LTnum(const TValue *v1, const TValue *v2) {
    toku_assert(ttisnum(v1) && ttisnum(v2));
    if (ttisint(v1)) {
        toku_Integer i1 = ival(v1);
        if (ttisint(v2))
            return i1 < ival(v2);
        else
            return intLTfloat(i1, fval(v2));
    } else {
        toku_Number n1 = fval(v1);
        if (ttisflt(v2))
            return tokui_numlt(n1, fval(v2));
        else
            return floatLTint(n1, ival(v2));
    }
}


/* 'less than' ordering '<' on non-number values */
t_sinline int32_t LTother(toku_State *T, const TValue *v1, const TValue *v2) {
    if (ttisstring(v1) && ttisstring(v2))
        return tokuS_cmp(strval(v1), strval(v2)) < 0;
    else
        return tokuTM_order(T, v1, v2, TM_LT);
}


/* 'less than' ordering '<' */
int32_t tokuV_orderlt(toku_State *T, const TValue *v1, const TValue *v2) {
    if (ttisnum(v1) && ttisnum(v2))
        return LTnum(v1, v2);
    return LTother(T, v1, v2);
}


/* 
** Equality ordering '=='.
** In case 'T' is NULL perform raw equality (without invoking '__eq').
*/
int32_t tokuV_ordereq(toku_State *T, const TValue *v1, const TValue *v2) {
    toku_Integer i1, i2;
    const TValue *tm;
    if (ttypetag(v1) != ttypetag(v2)) {
        if (ttype(v1) != ttype(v2) || ttype(v1) != TOKU_T_NUMBER)
            return 0;
        return (tokuO_tointeger(v1, &i1, N2IEQ) &&
                tokuO_tointeger(v2, &i2, N2IEQ) && i1 == i2);
    }
    switch (ttypetag(v1)) {
        case TOKU_VNIL: case TOKU_VFALSE: case TOKU_VTRUE: return 1;
        case TOKU_VNUMINT: return ival(v1) == ival(v2);
        case TOKU_VNUMFLT: return tokui_numeq(fval(v1), fval(v2));
        case TOKU_VLCF: return lcfval(v1) == lcfval(v2);
        case TOKU_VLIGHTUSERDATA: return pval(v1) == pval(v2);
        case TOKU_VSHRSTR: return eqshrstr(strval(v1), strval(v2));
        case TOKU_VLNGSTR: return tokuS_eqlngstr(strval(v1), strval(v2));
        case TOKU_VIMETHOD: return tokuTM_eqim(imval(v1), imval(v2));
        case TOKU_VUMETHOD: return tokuTM_equm(umval(v1), umval(v2));
        case TOKU_VUSERDATA: {
            Table *mt1 = udval(v1)->metatable;
            Table *mt2 = udval(v2)->metatable;
            if  (T == NULL ||(!(tm = fasttm(T, mt1, TM_EQ)) &&
                        !(tm = fasttm(T, mt2, TM_EQ))))
                return udval(v1) == udval(v2);
            break;
        }
        case TOKU_VINSTANCE: {
            Table *mt1 = insval(v1)->oclass->metatable;
            if (T == NULL || (insval(v1)->oclass != insval(v2)->oclass) ||
                    !(tm = fasttm(T, mt1, TM_EQ)))
                return insval(v1) == insval(v2);
            break;
        }
        default: return gcoval(v1) == gcoval(v2);
    }
    tokuTM_callbinres(T, tm, v1, v2, T->sp.p);
    return !t_isfalse(s2v(T->sp.p));
}


/* generic set */
#define tokuV_setgen(T,o,k,v,f) \
    { const TValue *tm = tokuTM_objget(T, o, TM_SETIDX); \
      if (notm(tm)) { f(T, o, k, v); } \
      else { tokuTM_callset(T, tm, o, k, v); }}


void tokuV_rawsetstr(toku_State *T, const TValue *o, const TValue *k,
                                                     const TValue *v) {
    Table *t;
    switch (ttypetag(o)) {
        case TOKU_VLIST: {
            List *l = listval(o);
            tokuV_setlist(T, l, k, v, tokuA_setstr);
            break;
        }
        case TOKU_VTABLE:
            t = tval(o);
            goto set_table;
        case TOKU_VINSTANCE:
            t = insval(o)->fields;
        set_table: {
            int32_t hres;
            tokuV_fastset(t, strval(k), v, hres, tokuH_psetstr);
            if (hres == HOK)
                tokuV_finishfastset(T, t, v);
            else {
                tokuH_finishset(T, t, k, v, hres);
                tokuG_barrierback(T, obj2gco(t), v);
                invalidateTMcache(t);
            }
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_setstr(toku_State *T, const TValue *o, const TValue *k,
                                                  const TValue *v) {
    tokuV_setgen(T, o, k, v, tokuV_rawsetstr);
}


void tokuV_rawsetint(toku_State *T, const TValue *o, const TValue *k,
                                                     const TValue *v) {
    Table *t;
    switch (ttypetag(o)) {
        case TOKU_VLIST: {
            List *l = listval(o);
            tokuV_setlist(T, l, k, v, tokuA_setindex);
            break;
        }
        case TOKU_VTABLE:
            t = tval(o);
            goto set_table;
        case TOKU_VINSTANCE:
            t = insval(o)->fields;
        set_table: {
            int32_t hres;
            tokuV_fastset(t, ival(k), v, hres, tokuH_psetint);
            if (hres == HOK)
                tokuV_finishfastset(T, t, v);
            else {
                tokuH_finishset(T, t, k, v, hres);
                tokuG_barrierback(T, obj2gco(t), v);
            }
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_setint(toku_State *T, const TValue *o, const TValue *k,
                                                  const TValue *v) {
    tokuV_setgen(T, o, k, v, tokuV_rawsetint);
}


void tokuV_rawset(toku_State *T, const TValue *o, const TValue *k,
                                                  const TValue *v) {
    Table *t;
    switch (ttypetag(o)) {
        case TOKU_VLIST: {
            List *l = listval(o);
            tokuV_setlist(T, l, k, v, tokuA_set);
            break;
        }
        case TOKU_VTABLE:
            t = tval(o);
            goto set_table;
        case TOKU_VINSTANCE:
            t = insval(o)->fields;
        set_table: {
            int32_t hres;
            tokuV_fastset(t, k, v, hres, tokuH_pset);
            if (hres == HOK)
                tokuV_finishfastset(T, t, v);
            else {
                tokuH_finishset(T, t, k, v, hres);
                tokuG_barrierback(T, obj2gco(t), v);
                invalidateTMcache(t);
            }
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_set(toku_State *T, const TValue *o, const TValue *k,
                                               const TValue *v) {
    tokuV_setgen(T, o, k, v, tokuV_rawset);
}


#define tokuV_getgen(T,o,k,res,f) \
    { const TValue *tm = tokuTM_objget(T, o, TM_GETIDX); \
      if (notm(tm)) { f(T, o, k, res); } \
      else { tokuTM_callgetres(T, tm, o, k, res); }}


#define finishTget(T,tag,val,res) \
    { if (!tagisempty(tag)) { setobj2s(T, res, val); } \
      else setnilval(s2v(res)); }


#define newboundmethod(T,inst,method,res) \
        setimval2s(T, res, tokuTM_newinsmethod(T, inst, method))


#define trynewboundmethod(T,slot,tag,inst,res) \
    { if (!tagisempty(tag)) { \
          newboundmethod(T, inst, slot, res); \
          break; }}


void tokuV_rawgetstr(toku_State *T, const TValue *o, const TValue *k,
                                                     SPtr res) {
    switch (ttypetag(o)) {
        case TOKU_VLIST:
            tokuA_getstr(T, listval(o), k, s2v(res));
            break;
        case TOKU_VTABLE: {
            TValue ret;
            uint8_t tag = tokuH_getstr(tval(o), strval(k), &ret);
            finishTget(T, tag, &ret, res);
            break;
        }
        case TOKU_VINSTANCE: {
            TValue ret;
            Instance *inst = insval(o);
            uint8_t tag = tokuH_getstr(inst->fields, strval(k), &ret);
            if (tagisempty(tag)) { /* field not found? */
                if (inst->oclass->methods) { /* have methods table? */
                    tag = tokuH_getstr(inst->oclass->methods, strval(k), &ret);
                    trynewboundmethod(T, &ret, tag, inst, res);
                }
                /* fall through */
            } else { /* otherwise found the field */
                setobj2s(T, res, &ret);
                break;
            }
            /* no such field or method */
            setnilval(s2v(res));
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_getstr(toku_State *T, const TValue *o, const TValue *k, SPtr res) {
    tokuV_getgen(T, o, k, res, tokuV_rawgetstr);
}


void tokuV_rawgetint(toku_State *T, const TValue *o, const TValue *k,
                                                     SPtr res) {
    switch (ttypetag(o)) {
        case TOKU_VLIST:
            tokuA_getindex(listval(o), ival(k), s2v(res));
            break;
        case TOKU_VTABLE: {
            TValue value;
            uint8_t tag = tokuH_getint(tval(o), ival(k), &value);
            finishTget(T, tag, &value, res);
            break;
        }
        case TOKU_VINSTANCE: {
            TValue ret;
            Instance *inst = insval(o);
            uint8_t tag = tokuH_getint(inst->fields, ival(k), &ret);
            if (tagisempty(tag)) {
                if (inst->oclass->methods) {
                    tag = tokuH_getint(inst->oclass->methods, ival(k), &ret);
                    trynewboundmethod(T, &ret, tag, inst, res);
                }
                /* fall through */
            } else {
                setobj2s(T, res, &ret);
                break;
            }
            /* no such field or method */
            setnilval(s2v(res));
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_getint(toku_State *T, const TValue *o, const TValue *k, SPtr res) {
    tokuV_getgen(T, o, k, res, tokuV_rawgetint);
}


void tokuV_rawget(toku_State *T, const TValue *o, const TValue *k, SPtr res) {
    switch (ttypetag(o)) {
        case TOKU_VLIST:
            tokuA_get(T, listval(o), k, s2v(res));
            break;
        case TOKU_VTABLE: {
            TValue value;
            uint8_t tag = tokuH_get(tval(o), k, &value);
            finishTget(T, tag, &value, res);
            break;
        }
        case TOKU_VINSTANCE: {
            Instance *inst = insval(o);
            TValue value;
            uint8_t tag = tokuH_get(inst->fields, k, &value);
            if (tagisempty(tag)) {
                if (inst->oclass->methods) {
                    tag = tokuH_get(inst->oclass->methods, k, &value);
                    trynewboundmethod(T, &value, tag, inst, res);
                }
                /* fall through */
            } else {
                setobj2s(T, res, &value);
                break;
            }
            /* no such field or method */
            setnilval(s2v(res));
            break;
        }
        default: tokuD_typeerror(T, o, "index");
    }
}


void tokuV_get(toku_State *T, const TValue *o, const TValue *k, SPtr res) {
    tokuV_getgen(T, o, k, res, tokuV_rawget);
}


#define getsuper(T,inst,scls,k,res,f) { \
    if ((scls)->methods) { \
        TValue method; \
        uint8_t tag = f((scls)->methods, k, &method); \
        if (!tagisempty(tag)) { \
            newboundmethod(T, inst, &method, res); \
            vm_break; \
        } \
    } \
    setnilval(s2v(res)); }


t_sinline int32_t checksuper(toku_State *T, TValue *o, int32_t get, SPtr res) {
    OClass *scls;
    if (t_unlikely(!ttisinstance(o)))
        tokuD_runerror(T, "local 'self' is not an instance");
    scls = insval(o)->oclass->sclass;
    if (get) { /* get the superclass? */
        if (scls) { /* class instance has a superclass? */
            setclsval2s(T, res, scls);
        } else /* otherwise no superclass */
            setnilval(s2v(res));
    }
    return scls != NULL;
}


#define checksuperprop(T,obj,key,res,f) { \
    if (t_likely(checksuper(T, obj, 0, res))) { \
        Instance *inst = insval(obj); \
        OClass *scls = inst->oclass->sclass; \
        toku_assert(scls != NULL); \
        getsuper(T, inst, scls, key, res, f); \
    } else tokuD_runerror(T, "class instance has no superclass"); }


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
            delta = !!p->isvararg * (cf->t.nvarargs + p->arity + 1);
        }
        cf->func.p += delta; /* if vararg, back to virtual function */
        ftransfer = cast_u16(firstres - cf->func.p) - 1;
        toku_assert(ftransfer >= 0);
        tokuD_hook(T, TOKU_HOOK_RET, -1, ftransfer, nres); /* call it */
        cf->func.p -= delta; /* if vararg, back to original function */
    }
    if (isTokudae(cf = cf->prev))
        T->oldpc = relpc(cf->t.pc, cf_func(cf)->p); /* set 'oldpc' */
}


/* move call results and if needed close variables */
t_sinline void moveresults(toku_State *T, SPtr res, int32_t nres,
                                                    int32_t wanted) {
    int32_t i;
    SPtr firstresult;
    switch (wanted) {
        case TOKU_MULTRET: /* all values needed */
            wanted = nres;
            break;
        case 0: /* no values needed */
            T->sp.p = res;
            return; /* done */
        case 1: /* one value needed */
            if (nres == 0)
                setnilval(s2v(res));
            else
                setobjs2s(T, res, T->sp.p - nres);
            T->sp.p = res + 1;
            return; /* done */
        default: { /* two/more results and/or to-be-closed variables */
            if (hastocloseCfunc(wanted)) { /* to-be-closed variables? */
                res = tokuF_close(T, res, CLOSEKTOP); /* do the closing */
                if (T->hookmask) { /* if needed, call hook after '__close's */
                    ptrdiff_t savedres = savestack(T, res);
                    rethook(T, T->cf, nres);
                    res = restorestack(T, savedres); /* hook can move stack */
                }
                wanted = decodeNresults(wanted); /* decode nresults */
                if (wanted == TOKU_MULTRET)
                    wanted = nres; /* we want all results */
            }
        }
    }
    /* generic case (all values needed or 2 or more values needed) */
    firstresult = T->sp.p - nres;
    if (nres > wanted) /* have extra results? */
        nres = wanted; /* discard them */
    for (i = 0; i < nres; i++) /* move all the results */
        setobjs2s(T, res + i, firstresult + i);
    for (; i < wanted; i++)
        setnilval(s2v(res + i));
    T->sp.p = res + wanted;
}


/* get the next call frame or allocate a new one */
#define next_cf(T)   ((T)->cf->next ? (T)->cf->next : tokuT_newcf(T))

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


t_sinline CallFrame *prepcallframe(toku_State *T, SPtr func,
                                   uint32_t extra, int32_t nres, SPtr top) {
    CallFrame *cf = T->cf = next_cf(T);
    cf->func.p = func;
    cf->top.p = top;
    cf->nresults = nres;
    cf->extraargs = gccall(extra) + gcinit(extra) + gcmethod(extra);
    cf->status = cast_u8(gstatus(extra));
    return cf;
}


/* move the results into correct place and return to caller */
t_sinline void poscall(toku_State *T, CallFrame *cf, int32_t nres) {
    int32_t wanted = cf->nresults;
    if (t_unlikely(T->hookmask) && !hastocloseCfunc(wanted))
        rethook(T, cf, nres);
    /* move results to proper place */
    moveresults(T, cf->func.p, nres, wanted);
    /* function cannot be in any of these cases when returning */
    toku_assert(!(cf->status & (CFST_HOOKED | CFST_FIN)));
    T->cf = cf->prev; /* back to caller (after closing variables) */
}


t_sinline int32_t precallC(toku_State *T, SPtr func,
                           uint32_t extra, int32_t nres, toku_CFunction f) {
    int32_t n;
    CallFrame *cf;
    checkstackGCp(T, TOKU_MINSTACK, func); /* ensure minimum stack space */
    T->cf = cf = prepcallframe(T, func, extra | CFST_CCALL, nres,
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
    poscall(T, cf, n);
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

CallFrame *precall(toku_State *T, SPtr func, int32_t nres) {
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
            T->cf = cf = prepcallframe(T, func, extra, nres, func+1+fsize);
            cf->t.pc = cf->t.pcret = f->code; /* set starting point */
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
                moveresults(T, func, 1, nres); /* only instance is returned */
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
static int32_t pretailcall(toku_State *T, CallFrame *cf, SPtr func,
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
            cf->t.pc = cf->t.pcret = f->code; /* set starting point */
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
            /* return pretailcall(T, cf, func, narg1 + 2, delta); */
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
            /* return pretailcall(T, cf, func, narg1 + 1, delta); */
            narg1++;
            goto retry; /* try again */
    }
}


t_sinline void ccall(toku_State *T, SPtr func, int32_t nres, uint32_t inc) {
    CallFrame *cf;
    T->nCcalls += inc;
    if (t_unlikely(getCcalls(T) >= TOKUI_MAXCCALLS)) {
        checkstackp(T, 0, func); /* free any use of EXTRA_STACK */
        tokuT_checkCstack(T);
    }
    if ((cf = precall(T, func, nres)) != NULL) { /* Tokudae function? */
        cf->status = CFST_FRESH; /* mark it as a "fresh" execute */
        tokuV_execute(T, cf); /* call it */
    }
    T->nCcalls -= inc;
}


/* external interface for 'ccall' */
void tokuV_call(toku_State *T, SPtr func, int32_t nres) {
    ccall(T, func, nres, nyci);
}


#define isemptystr(v)   (ttisshrstring(v) && strval(v)->shrlen == 0)


static void copy2buff(SPtr top, int32_t n, char *buff) {
    size_t done = 0;
    do {
        OString *s = strval(s2v(top - n));
        size_t len = getstrlen(s);
        memcpy(&buff[done], getstr(s), len * sizeof(char));
        done += len;
    } while (--n > 0);
}


void tokuV_concat(toku_State *T, int32_t total) {
    if (total == 1)
        return; /* done */
    do {
        SPtr top = T->sp.p;
        int32_t n = 2; /* number of elements (minimum 2) */
        if (!(ttisstring(s2v(top - 2)) && ttisstring(s2v(top - 1))))
            tokuTM_tryconcat(T); /* at least one operand is not a string */
        else if (isemptystr(s2v(top - 1))) /*second operand is empty string?*/
            ; /* result already in the first operand */
        else if (isemptystr(s2v(top - 2))) { /*first operand is empty string?*/
            setobjs2s(T, top - 2, top - 1); /* result is second operand */
        } else { /* at least two non-empty strings */
            size_t ltotal = getstrlen(strval(s2v(top - 1)));
            /* collect total length and number of strings */
            for (n = 1; n < total && ttisstring(s2v(top - n - 1)); n++) {
                size_t len = getstrlen(strval(s2v(top - n - 1)));
                if (t_unlikely(len >= TOKU_MAXSIZE-sizeof(OString)-ltotal)) {
                    T->sp.p = top - total; /* pop strings */
                    tokuD_runerror(T, "string length overflow");
                }
                ltotal += len;
            }
            OString *s;
            if (ltotal <= TOKUI_MAXSHORTLEN) { /* fits in a short string? */
                char buff[TOKUI_MAXSHORTLEN];
                copy2buff(top, n, buff);
                s = tokuS_newl(T, buff, ltotal);
            } else { /* otherwise long string */
                s = tokuS_newlngstrobj(T, ltotal);
                copy2buff(top, n, getstr(s));
            }
            setstrval2s(T, top - n, s);
        }
        total -= n - 1; /* got 'n' strings to create one new */
        T->sp.p -= n - 1; /* popped 'n' strings and pushed one */
    } while (total > 1);
}


t_sinline OClass *checkinherit(toku_State *T, const TValue *scls) {
    if (t_unlikely(!ttisclass(scls)))
        tokuD_runerror(T, "inherit from %s value", typename(ttype(scls)));
    return classval(scls);
}


t_sinline void copytable(toku_State *T, Table **dest, Table *src) {
    toku_assert(src != NULL);
    if (!*dest) *dest = tokuH_new(T);
    tokuH_copy(T, *dest, src);
    invalidateTMcache(*dest);
}


static void doinherit(toku_State *T, OClass *cls, OClass *supcls) {
    if (supcls->methods) { /* superclass has methods? */
        copytable(T, &cls->methods, supcls->methods);
        tokuG_objbarrier(T, cls, cls->methods);
    }
    if (supcls->metatable) {
        copytable(T, &cls->metatable, supcls->metatable);
        tokuG_objbarrier(T, cls, cls->metatable);
    }
    cls->sclass = supcls; /* set the superclass */
}


#define log2size1(b)     ((b > 0) ? (1<<((b)-1)) : 0)


t_sinline void pushclass(toku_State *T, int32_t b) {
    OClass *cls = tokuTM_newclass(T);
    setclsval2s(T, T->sp.p++, cls);
    if (b & 0x80) { /* have metatable entries? */
        cls->metatable = tokuH_new(T);
        b &= 0x7F; /* remove flag */
    }
    if (b > 0) { /* have methods? */
        cls->methods = tokuH_new(T);
        tokuH_resize(T, cls->methods, cast_u32(log2size1(b)));
    }
}


t_sinline void pushlist(toku_State *T, int32_t b) {
    List *l = tokuA_new(T);
    setlistval2s(T, T->sp.p++, l);
    if (b > 0) /* list is not empty? */
        tokuA_ensure(T, l, log2size1(b));
}


t_sinline void pushtable(toku_State *T, int32_t b) {
    Table *t = tokuH_new(T);
    settval2s(T, T->sp.p++, t);
    if (b > 0) /* table is not empty? */
        tokuH_resize(T, t, cast_u32(log2size1(b)));
}


/* {======================================================================
** Macros for arithmetic/bitwise/comparison operations on numbers.
** ======================================================================= */

/* 'toku_Integer' arithmetic operations */
#define iadd(T,a,b)    (intop(+, a, b))
#define isub(T,a,b)    (intop(-, a, b))
#define imul(T,a,b)    (intop(*, a, b))

/* integer bitwise operations */
#define iband(a,b)      (intop(&, a, b))
#define ibor(a,b)       (intop(|, a, b))
#define ibxor(a,b)      (intop(^, a, b))

/* integer ordering operations */
#define ilt(a,b)        (a < b)
#define ile(a,b)        (a <= b)
#define igt(a,b)        (a > b)
#define ige(a,b)        (a >= b)


/* 
** Arithmetic operations
*/

#define op_arithKf_aux(T,v1,v2,fop) { \
    toku_Number n1 = 0, n2 = 0; /* to prevent warnings on MSVC */ \
    if (tonumber(v1, n1) && tonumber(v2, n2)) { \
        setfval(v1, fop(T, n1, n2)); \
    } else tokuD_aritherror(T, v1, v2); }


/* arithmetic operations with constant operand for floats */
#define op_arithKf(T,fop) { \
    TValue *v = peek(0); \
    TValue *lk; \
    savestate(T); \
    lk = K(fetch_l()); \
    op_arithKf_aux(T, v, lk, fop); }


/* arithmetic operations with number constant operand */
#define op_arithK(T,iop,fop) { \
    TValue *v = peek(0); \
    TValue *lk; \
    savestate(T); \
    lk = K(fetch_l()); toku_assert(ttisnum(lk)); \
    if (ttisint(v) && ttisint(lk)) { \
        toku_Integer i1 = ival(v); \
        toku_Integer i2 = ival(lk); \
        setival(v, iop(T, i1, i2)); \
    } else { \
        op_arithKf_aux(T, v, lk, fop); \
    }}


/* arithmetic operation error with immediate operand */
#define op_arithI_error(T,v,imm) \
    { TValue v2; setival(&v2, imm); tokuD_aritherror(T, v, &v2); }


/* arithmetic operations with immediate operand for floats */
#define op_arithIf(T,fop) { \
    TValue *v = peek(0); \
    toku_Number n = 0; /* to prevent warnings on MSVC */ \
    int32_t imm; \
    savestate(T); \
    imm = fetch_l(); \
    imm = IMML(imm); \
    if (tonumber(v, n)) { \
        toku_Number fimm = cast_num(imm); \
        setfval(v, fop(T, n, fimm)); \
    } else { \
        op_arithI_error(T, v, imm); \
    }}


/* arithmetic operations with immediate operand */
#define op_arithI(T,iop,fop) { \
    TValue *v = peek(0); \
    int32_t imm; \
    savestate(T); \
    imm = fetch_l(); \
    imm = IMML(imm); \
    if (ttisint(v)) { \
        toku_Integer i = ival(v); \
        setival(v, iop(T, i, imm)); \
    } else if (ttisflt(v)) { \
        toku_Number n = fval(v); \
        toku_Number fimm = cast_num(imm); \
        setfval(v, fop(T, n, fimm)); \
    } else { \
        op_arithI_error(T, v, imm); \
    }}


#define op_arithf_aux(T,res,v1,v2,fop) { \
    toku_Number n1 = 0, n2 = 0; /* to prevent warnings on MSVC */ \
    if (tonumber(v1, n1) && tonumber(v2, n2)) { \
        setfval(res, fop(T, n1, n2)); \
        sp--; /* v2 */ \
        pc += getopSize(OP_MBIN); \
    }/* else fall through to 'OP_MBIN' */}


/* arithmetic operations with stack operands for floats */
#define op_arithf(T,fop) { \
    TValue *res = peek(1); \
    TValue *v1 = res; \
    TValue *v2 = peek(0); \
    if (fetch_s()) t_swap(v1, v2); \
    op_arithf_aux(T, res, v1, v2, fop); }


/* arithmetic operations with stack operands */
#define op_arith(T,iop,fop) { \
    TValue *res = peek(1); \
    TValue *v1 = res; \
    TValue *v2 = peek(0); \
    if (fetch_s()) t_swap(v1, v2); \
    if (ttisint(v1) && ttisint(v2)) { \
        toku_Integer i1 = ival(v1); toku_Integer i2 = ival(v2); \
        setival(res, iop(T, i1, i2)); \
        sp--; /* v2 */ \
        pc += getopSize(OP_MBIN); \
    } else { \
        op_arithf_aux(T, res, v1, v2, fop); \
    }}



/*
** Bitwise operations
*/

/* bitwise operations with constant operand */
#define op_bitwiseK(T,op) { \
    TValue *v = peek(0); \
    TValue *lk; \
    toku_Integer i1, i2; \
    savestate(T); \
    lk = K(fetch_l()); \
    if (t_likely(tointeger(v, &i1) && tointeger(lk, &i2))) { \
        setival(v, op(i1, i2)); \
    } else tokuD_binoperror(T, v, lk, TM_BAND); }


/* bitwise operations with immediate operand */
#define op_bitwiseI(T,op) { \
    TValue *v = peek(0); \
    int32_t imm; \
    savestate(T); \
    imm = fetch_l(); \
    imm = IMML(imm); \
    toku_Integer i; \
    if (t_likely(tointeger(v, &i))) { \
        setival(v, op(i, imm)); \
    } else { \
        TValue vimm; setival(&vimm, imm); \
        tokuD_binoperror(T, v, &vimm, TM_BAND); \
    }}


/* bitwise operations with stack operands */
#define op_bitwise(T,op) { \
    TValue *res = peek(1); \
    TValue *v1 = res; \
    TValue *v2 = peek(0); \
    toku_Integer i1; toku_Integer i2; \
    savestate(T); \
    if (fetch_s()) t_swap(v1, v2); \
    if (tointeger(v1, &i1) && tointeger(v2, &i2)) { \
        setival(res, op(i1, i2)); \
        sp--; /* v2 */ \
        pc += getopSize(OP_MBIN); \
    }/* fall through to OP_MBIN */}



/*
** Ordering operations
*/

/* set ordering result */
#define setorderres(v,cond_,eq_) \
    { toku_assert(0 <= (cond_) && (cond_) <= 1); \
      settt(v, booleans[(cond_) == (eq_)]); }


/* order operations with stack operands */
#define op_order(T,iop,fop,other) { \
    TValue *v1 = peek(1); \
    TValue *v2 = peek(0); \
    int32_t cond; \
    savestate(T); \
    if (fetch_s()) t_swap(v1, v2); \
    if (ttisint(v1) && ttisint(v2)) { \
        toku_Integer i1 = ival(v1); \
        toku_Integer i2 = ival(v2); \
        cond = iop(i1, i2); \
    } else if (ttisnum(v1) && ttisnum(v2)) { \
        cond = fop(v1, v2); \
    } else { \
        cond = other(T, v1, v2); \
        updatetrap(cf); \
        updatestack(cf); \
    } \
    setorderres(peek(1), cond, 1); sp--; }


/* order operation error with immediate operand */
#define op_orderI_error(T,v,imm) \
    { TValue v2; setival(&v2, imm); tokuD_ordererror(T, v, &v2); }


/* order operations with immediate operand */
#define op_orderI(T,iop,fop) { \
    int32_t cond = 0; /* to prevent warnings */ \
    int32_t imm; \
    savestate(T); \
    imm = fetch_l(); \
    imm = IMML(imm); \
    TValue *v = peek(0); \
    if (ttisint(v)) { \
        cond = iop(ival(v), imm); \
    } else if (ttisflt(v)) { \
        toku_Number n1 = fval(v); \
        toku_Number n2 = cast_num(imm); \
        cond = fop(n1, n2); \
    } else op_orderI_error(T, v, imm); \
    setorderres(v, cond, 1); }

/* }====================================================================== */


/* {======================================================================
** Interpreter loop
** ======================================================================= */

/* get reference to constant value from 'k' at index 'idx' */
#define K(idx)          (k + (idx))

/* get stack slot at index 'i_' */
#define STK(i_)         (base+(i_))

/* get stack slot of value at 'i_' slots from the value on top */
#define stkpeek(i_)     (sp-1-(i_))

/* idem but this gets the actual value */
#define peek(n)         s2v(stkpeek(n))


/* update global 'trap' */
#define updatetrap(cf)      (trap = (cf)->t.trap)

/* update global 'base' */
#define updatebase(cf)      (base = (cf)->func.p + 1)


/*
** If 'trap' (maybe stack reallocation), then update global 'base'
** and global 'sp'.
*/
#define updatestackaux(cf,extra) \
        if (t_unlikely(trap)) { updatebase(cf); sp = T->sp.p; (extra); }

#define updatestack(cf)     updatestackaux(cf,(void)0)


/* store global 'pc' */
#define storepc(T)          (cf->t.pc = pc)

/* store global 'sp' */
#define storesp(T)          (T->sp.p = sp)


/*
** Store global 'pc' and 'sp', must be done before fetching the
** opcode arguments (via fetch_l/fetch_s) in order to properly fetch
** debug information for the error message (if error occurrs).
*/
#define savestate(T)        (storepc(T), storesp(T))


/* collector might of reallocated stack, so update global 'trap' */
#define checkGC(T)      tokuG_condGC(T, (void)0, updatetrap(cf))


/* fetch opcode */
#define fetch() { \
    if (t_unlikely(trap)) { /* stack reallocation or hooks? */ \
        ptrdiff_t sizestack = sp - base; \
        trap = tokuD_traceexec(T, pc, sizestack); /* handle hooks */ \
        updatebase(cf); /* correct stack */ \
        sp = base + sizestack; /* correct stack pointer */ \
    } \
    I = *(pc++); \
}

/* fetch short opcode argument */
#define fetch_s()       (*(pc++))

/* fetch long opcode argument */
#define fetch_l()       (pc += SIZE_ARG_L, get3bytes(pc - SIZE_ARG_L))


#define hookdelta()     (getopSize(*pc) + SIZE_OPCODE)


/* In cases where jump table is not available or prefered. */
#define vm_dispatch(x)      switch(x)
#define vm_case(l)          case l:
#define vm_break            break


/*
** Do a conditional jump: skip next opcode if 'cond_' is not what
** was expected, else do next opcode, which must be a jump.
*/
#define docondjumppre(pre) \
    { int32_t cond_ = fetch_s(); TValue *v = peek(0); pre; \
      if ((!t_isfalse(v)) != cond_) check_exp(getopSize(*pc)==4, pc += 4); \
      vm_break; }

#define docondjump()    docondjumppre(((void)0))


void tokuV_execute(toku_State *T, CallFrame *cf) {
    TClosure *cl;              /* active Tokudae function (closure) */
    TValue *k;                  /* constants */
    SPtr base;                  /* frame stack base */
    SPtr sp;                    /* local stack pointer (for performance) */
    const uint8_t *pc;      /* program counter */
    int32_t trap;                   /* true if 'base' reallocated */
#if TOKU_USE_JUMPTABLE
#include "tjmptable.h"
#endif
startfunc:
    trap = T->hookmask;
    toku_assert(cf->t.pcret == cf_func(cf)->p->code); /* must be at start */
returning: /* trap already set */
    cl = cf_func(cf);
    k = cl->p->k;
    pc = cf->t.pcret;
    if (t_unlikely(trap)) /* hooks? */
        trap = tokuD_tracecall(T, hookdelta());
    base = cf->func.p + 1;
    sp = T->sp.p;
    /* main loop of interpreter */
    for (;;) {
        uint8_t I; /* opcode being executed */
        fetch();
        debugline(cl->p, pc);
        toku_assert(base == cf->func.p + 1);
        toku_assert(base <= T->sp.p && T->sp.p <= T->stackend.p);
        vm_dispatch(I) {
            vm_case(OP_TRUE) {
                setbtval(s2v(sp));
                sp++;
                vm_break;
            }
            vm_case(OP_FALSE) {
                setbfval(s2v(sp));
                sp++;
                vm_break;
            }
            vm_case(OP_NIL) {
                int32_t n = fetch_l();
                while (n--) setnilval(s2v(sp++));
                vm_break;
            }
            vm_case(OP_SUPER) {
                savestate(T);
                checksuper(T, peek(0), 1, sp - 1);
                vm_break;
            }
            vm_case(OP_LOAD) {
                setobjs2s(T, sp, STK(fetch_l()));
                sp++;
                vm_break;
            }
            vm_case(OP_CONST) {
                setobj2s(T, sp, K(fetch_s()));
                sp++;
                vm_break;
            }
            vm_case(OP_CONSTL) {
                setobj2s(T, sp, K(fetch_l()));
                sp++;
                vm_break;
            }
            vm_case(OP_CONSTI) {
                int32_t imm = fetch_s();
                setival(s2v(sp), IMM(imm));
                sp++;
                vm_break;
            }
            vm_case(OP_CONSTIL) {
                int32_t imm = fetch_l();
                setival(s2v(sp), IMML(imm));
                sp++;
                vm_break;
            }
            vm_case(OP_CONSTF) {
                int32_t imm = fetch_s();
                setfval(s2v(sp), cast_num(IMM(imm)));
                sp++;
                vm_break;
            }
            vm_case(OP_CONSTFL) {
                int32_t imm = fetch_l();
                setfval(s2v(sp), cast_num(IMML(imm)));
                sp++;
                vm_break;
            }
            vm_case(OP_VARARGPREP) {
                savestate(T);
                /* 'tokuF_adjustvarargs' handles 'sp' */
                tokuF_adjustvarargs(T, fetch_l(), cf, &sp, cl->p);
                updatetrap(cf);
                if (t_unlikely(trap)) { /* (check for hooks) */
                    storepc(T);
                    tokuD_hookcall(T, cf, hookdelta());
                    /* next opcode will be seen as a "new" line */
                    T->oldpc = getopSize(OP_VARARGPREP); 
                    sp = T->sp.p; /* 'sp' must be in sync with 'base' */
                }
                updatebase(cf); /* function has new base after adjustment */
                vm_break;
            }
            vm_case(OP_VARARG) {
                savestate(T);
                /* 'tokuF_getvarargs' handles 'sp' */
                tokuF_getvarargs(T, cf, &sp, fetch_l() - 1);
                updatetrap(cf);
                updatebase(cf); /* make sure 'base' is up-to-date */
                vm_break;
            }
            vm_case(OP_CLOSURE) {
                savestate(T);
                pushclosure(T, cl->p->p[fetch_l()], cl->upvals, base);
                checkGC(T);
                sp++;
                vm_break;
            }
            vm_case(OP_NEWLIST) {
                savestate(T);
                pushlist(T, fetch_s());
                checkGC(T);
                sp++;
                vm_break;
            }
            vm_case(OP_NEWCLASS) {
                savestate(T);
                pushclass(T, fetch_s());
                checkGC(T);
                sp++;
                vm_break;
            }
            vm_case(OP_NEWTABLE) {
                savestate(T);
                pushtable(T, fetch_s());
                checkGC(T);
                sp++;
                vm_break;
            }
            vm_case(OP_METHOD) {
                int32_t hres;
                Table *t = classval(peek(1))->methods;
                TValue *f = peek(0);
                TValue *key;
                savestate(T);
                key = K(fetch_l());
                toku_assert(t && ttisstring(key));
                tokuV_fastset(t, strval(key), f, hres, tokuH_psetstr);
                if (hres == HOK)
                    tokuV_finishfastset(T, t, f);
                else {
                    tokuH_finishset(T, t, key, f, hres);
                    tokuG_barrierback(T, obj2gco(t), f);
                    invalidateTMcache(t);
                }
                sp -= 2; /* remove method and class copy */
                vm_break;
            }
            vm_case(OP_SETTM) {
                Table *mt = classval(peek(1))->metatable;
                TValue *v = peek(0);
                OString *key;
                int32_t hres;
                TM ev;
                savestate(T);
                toku_assert(mt != NULL);
                ev = asTM(fetch_s());
                key = eventstring(T, ev);
                toku_assert(cast_u32(ev) < TM_NUM);
                tokuV_fastset(mt, key, v, hres, tokuH_psetstr);
                if (t_likely(hres == HOK))
                    tokuV_finishfastset(T, mt, v);
                else {
                    TValue os;
                    setstrval(T, &os, key);
                    tokuH_finishset(T, mt, &os, v, hres);
                    tokuG_barrierback(T, obj2gco(mt), v);
                    invalidateTMcache(mt);
                }
                sp -= 2; /* remove metafield and class copy */
                vm_break;
            }
            vm_case(OP_SETMT) {
                Table *mt = classval(peek(1))->metatable;
                TValue *v = peek(0);
                TValue *key;
                int32_t hres;
                savestate(T);
                toku_assert(mt != NULL);
                key = K(fetch_l());
                tokuV_fastset(mt, strval(key), v, hres, tokuH_psetstr);
                if (hres == HOK)
                    tokuV_finishfastset(T, mt, v);
                else {
                    tokuH_finishset(T, mt, key, v, hres);
                    tokuG_barrierback(T, obj2gco(mt), v);
                    invalidateTMcache(mt);
                }
                sp -= 2; /* remove metafield and class copy */
                vm_break;
            }
            vm_case(OP_POP) {
                sp -= fetch_l();
                vm_break;
            }
            vm_case(OP_MBIN) {
                savestate(T);
                /* operands are already swapped */
                tokuTM_trybin(T, peek(1), peek(0), sp-2, asTM(fetch_s()));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_ADDK) {
                op_arithK(T, iadd, tokui_numadd);
                vm_break;
            }
            vm_case(OP_SUBK) {
                op_arithK(T, isub, tokui_numsub);
                vm_break;
            }
            vm_case(OP_MULK) {
                op_arithK(T, imul, tokui_nummul);
                vm_break;
            }
            vm_case(OP_DIVK) {
                op_arithKf(T, tokui_numdiv);
                vm_break;
            }
            vm_case(OP_IDIVK) {
                op_arithK(T, tokuV_divi, tokui_numidiv);
                vm_break;
            }
            vm_case(OP_MODK) {
                op_arithK(T, tokuV_modi, tokuV_modf);
                vm_break;
            }
            vm_case(OP_POWK) {
                op_arithKf(T, tokui_numpow);
                vm_break;
            }
            vm_case(OP_BSHLK) {
                op_bitwiseK(T, tokuO_shiftl);
                vm_break;
            }
            vm_case(OP_BSHRK) {
                op_bitwiseK(T, tokuO_shiftr);
                vm_break;
            }
            vm_case(OP_BANDK) {
                op_bitwiseK(T, iband);
                vm_break;
            }
            vm_case(OP_BORK) {
                op_bitwiseK(T, ibor);
                vm_break;
            }
            vm_case(OP_BXORK) {
                op_bitwiseK(T, ibxor);
                vm_break;
            }
            vm_case(OP_ADDI) {
                op_arithI(T, iadd, tokui_numadd);
                vm_break;
            }
            vm_case(OP_SUBI) {
                op_arithI(T, isub, tokui_numsub);
                vm_break;
            }
            vm_case(OP_MULI) {
                op_arithI(T, imul, tokui_nummul);
                vm_break;
            }
            vm_case(OP_DIVI) {
                op_arithIf(T, tokui_numdiv);
                vm_break;
            }
            vm_case(OP_IDIVI) {
                op_arithI(T, tokuV_divi, tokui_numidiv);
                vm_break;
            }
            vm_case(OP_MODI) {
                op_arithI(T, tokuV_modi, tokuV_modf);
                vm_break;
            }
            vm_case(OP_POWI) {
                op_arithIf(T, tokui_numpow);
                vm_break;
            }
            vm_case(OP_BSHLI) {
                op_bitwiseI(T, tokuO_shiftl);
                vm_break;
            }
            vm_case(OP_BSHRI) {
                op_bitwiseI(T, tokuO_shiftr);
                vm_break;
            }
            vm_case(OP_BANDI) {
                op_bitwiseI(T, iband);
                vm_break;
            }
            vm_case(OP_BORI) {
                op_bitwiseI(T, ibor);
                vm_break;
            }
            vm_case(OP_BXORI) {
                op_bitwiseI(T, ibxor);
                vm_break;
            }
            vm_case(OP_ADD) {
                op_arith(T, iadd, tokui_numadd);
                vm_break;
            }
            vm_case(OP_SUB) {
                op_arith(T, isub, tokui_numsub);
                vm_break;
            }
            vm_case(OP_MUL) {
                op_arith(T, imul, tokui_nummul);
                vm_break;
            }
            vm_case(OP_DIV) {
                op_arithf(T, tokui_numdiv);
                vm_break;
            }
            vm_case(OP_IDIV) {
                savestate(T);
                op_arith(T, tokuV_divi, tokui_numidiv);
                vm_break;
            }
            vm_case(OP_MOD) {
                savestate(T);
                op_arith(T, tokuV_modi, tokuV_modf);
                vm_break;
            }
            vm_case(OP_POW) {
                op_arithf(T, tokui_numpow);
                vm_break;
            }
            vm_case(OP_BSHL) {
                op_bitwise(T, tokuO_shiftl);
                vm_break;
            }
            vm_case(OP_BSHR) {
                op_bitwise(T, tokuO_shiftr);
                vm_break;
            }
            vm_case(OP_BAND) {
                op_bitwise(T, iband);
                vm_break;
            }
            vm_case(OP_BOR) {
                op_bitwise(T, ibor);
                vm_break;
            }
            vm_case(OP_BXOR) {
                op_bitwise(T, ibxor);
                vm_break;
            }
            vm_case(OP_CONCAT) {
                int32_t total;
                savestate(T);
                total = fetch_l();
                tokuV_concat(T, total);
                updatetrap(cf);
                checkGC(T);
                sp -= total - 1;
                vm_break;
            }
            vm_case(OP_EQK) {
                TValue *v1 = peek(0);
                const TValue *vk = K(fetch_l());
                int32_t eq = fetch_s();
                int32_t cond = tokuV_raweq(v1, vk);
                setorderres(v1, cond, eq);
                vm_break;
            }
            vm_case(OP_EQI) {
                TValue *v1 = peek(0);
                int32_t imm = fetch_l();
                int32_t eq = fetch_s();
                int32_t cond;
                if (ttisint(v1))
                    cond = (ival(v1) == IMML(imm));
                else if (ttisflt(v1))
                    cond = tokui_numeq(fval(v1), IMML(imm));
                else
                    cond = 0;
                setorderres(v1, cond, eq);
                vm_break;
            }
            vm_case(OP_LTI) {
                op_orderI(T, ilt, tokui_numlt);
                vm_break;
            }
            vm_case(OP_LEI) {
                op_orderI(T, ile, tokui_numle);
                vm_break;
            }
            vm_case(OP_GTI) {
                op_orderI(T, igt, tokui_numgt);
                vm_break;
            }
            vm_case(OP_GEI) {
                op_orderI(T, ige, tokui_numge);
                vm_break;
            }
            vm_case(OP_EQ) {
                int32_t condexp, cond;
                savestate(T);
                condexp = fetch_s();
                cond = tokuV_ordereq(T, peek(1), peek(0));
                updatetrap(cf);
                updatestack(cf);
                setorderres(peek(1), cond, condexp);
                sp--;
                vm_break;
            }
            vm_case(OP_LT) {
                op_order(T, ilt, LTnum, LTother);
                vm_break;
            }
            vm_case(OP_LE) {
                op_order(T, ile, LEnum, LEother);
                vm_break;
            }
            vm_case(OP_EQPRESERVE) {
                int32_t cond;
                savestate(T);
                cond = tokuV_ordereq(T, peek(1), peek(0));
                updatetrap(cf);
                updatestack(cf);
                setorderres(peek(0), cond, 1);
                vm_break;
            }
            vm_case(OP_NOT) {
                TValue *v = peek(0);
                if (t_isfalse(v))
                    setbtval(v);
                else
                    setbfval(v);
                vm_break;
            }
            vm_case(OP_UNM) {
                TValue *v = peek(0);
                if (ttisint(v)) {
                    toku_Integer ib = ival(v);
                    setival(v, intop(-, 0, ib));
                } else if (ttisflt(v)) {
                    toku_Number n = fval(v);
                    setfval(v, tokui_numunm(T, n));
                } else {
                    savestate(T);
                    tokuTM_tryunary(T, v, sp-1, TM_UNM);
                    updatetrap(cf);
                }
                vm_break;
            }
            vm_case(OP_BNOT) {
                TValue *v = peek(0);
                if (ttisint(v)) {
                    toku_Integer i = ival(v);
                    setival(v, intop(^, ~t_castS2U(0), i));
                } else {
                    savestate(T);
                    tokuTM_tryunary(T, v, sp-1, TM_BNOT);
                    updatetrap(cf);
                }
                vm_break;
            }
            vm_case(OP_JMP) {
                int32_t offset = fetch_l();
                pc += offset;
                updatetrap(cf); /* for interrupt in tight loops */
                vm_break;
            }
            vm_case(OP_JMPS) {
                int32_t offset = fetch_l();
                pc -= offset;
                updatetrap(cf); /* for interrupt in tight loops */
                vm_break;
            }
            vm_case(OP_TEST) {
                docondjump();
            }
            vm_case(OP_TESTPOP) {
                docondjumppre(sp--);
            }
            vm_case(OP_CALL) {
                CallFrame *newcf;
                SPtr func;
                int32_t nres;
                savestate(T);
                func = STK(fetch_l());
                nres = fetch_l() - 1;
                UNUSED(fetch_s());
                if ((newcf = precall(T, func, nres)) == NULL) /* C call? */
                    updatetrap(cf); /* done (C function already returned) */
                else { /* Tokudae call */
                    cf->t.pcret = pc; /* after return, continue at 'pc' */
                    cf = newcf; /* run function in this same C frame */
                    goto startfunc;
                }
                /* update 'sp' to properly compute stack size (if 'trap') */
                sp = base + (cast_i32(T->sp.p - cf->func.p) - 1);
                vm_break;
            }
            vm_case(OP_TAILCALL) {
                Proto *p = cl->p;
                int32_t nres, delta;
                SPtr func;
                savestate(T);
                func = STK(fetch_l());
                nres = fetch_l() - 1;
                delta = (p->isvararg) ? cf->t.nvarargs + p->arity + 1 : 0;
                if (fetch_s()) { /* close upvalues? */
                    tokuF_closeupval(T, base);
                    toku_assert(T->tbclist.p < base); /* no tbc variables */
                    toku_assert(base == cf->func.p + 1);
                }
                nres = pretailcall(T, cf, func, cast_i32(T->sp.p-func), delta);
                if (nres < 0) /* Tokudae function? */
                    goto startfunc; /* execute the callee */
                else { /* otherwise C function */
                    cf->func.p -= delta; /* restore 'func' (if vararg) */
                    poscall(T, cf, nres); /* finish caller */
                    updatetrap(cf); /* 'poscall' can change hooks */
                    goto ret; /* caller returns after the tail call */
                }
            }
            vm_case(OP_CLOSE) {
                savestate(T);
                tokuF_close(T, STK(fetch_l()), TOKU_STATUS_OK);
                updatetrap(cf);
                vm_break;
            }
            vm_case(OP_TBC) {
                savestate(T);
                tokuF_newtbcvar(T, STK(fetch_l()));
                vm_break;
            }
            vm_case(OP_CHECKADJ) {
                SPtr first = STK(fetch_l());
                int32_t nres = fetch_l() - 1;
                toku_assert(0 <= nres); /* must be fixed */
                sp = first + nres;
                vm_break;
            }
            vm_case(OP_GETLOCAL) {
                setobjs2s(T, sp, STK(fetch_l()));
                sp++;
                vm_break;
            }
            vm_case(OP_SETLOCAL) {
                setobjs2s(T, STK(fetch_l()), sp - 1);
                sp--;
                vm_break;
            }
            vm_case(OP_GETUVAL) {
                setobj2s(T, sp, cl->upvals[fetch_l()]->v.p);
                sp++;
                vm_break;
            }
            vm_case(OP_SETUVAL) {
                UpVal *uv = cl->upvals[fetch_l()];
                setobj(T, uv->v.p, s2v(sp - 1));
                tokuG_barrier(T, uv, s2v(sp - 1));
                sp--;
                vm_break;
            }
            vm_case(OP_SETLIST) {
                List *l;
                SPtr sl;
                int32_t len, n;
                savestate(T);
                sl = STK(fetch_l()); /* list stack slot */
                l = listval(s2v(sl)); /* 'sl' as list value */
                len = fetch_l(); /* num of elems. already in the list */
                n = fetch_s(); /* num of elements to store */
                if (l->len == len) { /* list has no prior holes? */
                    if (n == 0)
                        n = cast_i32(sp - sl - 1); /* get up to the top */
                    toku_assert(0 <= n);
                    tokuA_ensure(T, l, len + n);
                    for (int32_t i = 0; i < n; i++) { /* set the list */
                        TValue *v = s2v(sl + i + 1);
                        if (ttisnil(v)) break; /* value creates a hole? */
                        tokuA_fastset(T, l, len, v); 
                        len++;
                    }
                    l->len = len; /* update length */
                } /* otherwise list has hole(s), skip setting the list */
                sp = sl + 1; /* remove list elements off the stack (if any) */
                vm_break;
            }
            vm_case(OP_SETPROPERTY) {
                TValue *o;
                TValue *prop;
                savestate(T);
                o = peek(fetch_l());
                prop = K(fetch_l());
                toku_assert(ttisstring(prop));
                tokuV_setstr(T, o, prop, peek(0));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_GETPROPERTY) {
                TValue *prop;
                savestate(T);
                prop = K(fetch_l());
                toku_assert(ttisstring(prop));
                tokuV_getstr(T, peek(0), prop, sp - 1);
                updatetrap(cf);
                vm_break;
            }
            vm_case(OP_GETINDEX) {
                savestate(T);
                tokuV_get(T, peek(1), peek(0), sp - 2);
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_SETINDEX) {
                SPtr os;
                savestate(T);
                os = stkpeek(fetch_l());
                tokuV_set(T, s2v(os), s2v(os+1), peek(0));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_GETINDEXSTR) {
                TValue *i;
                savestate(T);
                i = K(fetch_l());
                toku_assert(ttisstring(i));
                tokuV_getstr(T, peek(0), i, sp - 1);
                updatetrap(cf);
                vm_break;
            }
            vm_case(OP_SETINDEXSTR) {
                TValue *o;
                TValue *idx;
                savestate(T);
                o = peek(fetch_l());
                idx = K(fetch_l());
                toku_assert(ttisstring(idx));
                tokuV_setstr(T, o, idx, peek(0));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_GETINDEXINT) {
                TValue i;
                int32_t imm;
                savestate(T);
                imm = fetch_s();
                setival(&i, IMM(imm));
                tokuV_getint(T, peek(0), &i, sp - 1);
                updatetrap(cf);
                vm_break;
            }
            vm_case(OP_GETINDEXINTL) {
                TValue i;
                int32_t imm;
                savestate(T);
                imm = fetch_l();
                setival(&i, IMML(imm));
                tokuV_getint(T, peek(0), &i, sp - 1);
                updatetrap(cf);
                vm_break;
            }
            vm_case(OP_SETINDEXINT) {
                TValue *o;
                toku_Integer imm;
                TValue index;
                savestate(T);
                o = peek(fetch_l());
                imm = fetch_s();
                setival(&index, IMM(imm));
                tokuV_setint(T, o, &index, peek(0));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_SETINDEXINTL) {
                TValue *o;
                toku_Integer imm;
                TValue index;
                savestate(T);
                o = peek(fetch_l());
                imm = fetch_l();
                setival(&index, IMML(imm));
                tokuV_setint(T, o, &index, peek(0));
                updatetrap(cf);
                sp--;
                vm_break;
            }
            vm_case(OP_GETSUP) {
                TValue *key;
                savestate(T);
                key = K(fetch_l());
                toku_assert(ttisstring(key));
                checksuperprop(T, peek(0), strval(key), sp-1, tokuH_getstr);
                vm_break;
            }
            vm_case(OP_GETSUPIDX) {
                savestate(T);
                checksuperprop(T, peek(1), peek(0), sp-2, tokuH_get);
                sp--;
                vm_break;
            }
            vm_case(OP_INHERIT) {
                OClass *supcls;
                OClass *cls = classval(peek(1));
                savestate(T);
                supcls = checkinherit(T, peek(0));
                toku_assert(cls != supcls);
                doinherit(T, cls, supcls);
                checkGC(T);
                sp--; /* remove superclass */
                vm_break;
            }
            vm_case(OP_FORPREP) {
                int32_t offset;
                savestate(T);
                /* create to-be-closed upvalue (if any) */
                tokuF_newtbcvar(T, STK(fetch_l()) + VAR_TBC);
                offset = fetch_l();
                pc += offset;
                /* go to the next opcode */
                I = *(pc++);
                toku_assert(I == OP_FORCALL);
                goto l_forcall;
            }
            vm_case(OP_FORCALL) {
            l_forcall: {
                /* 'stk' slot is iterator function, 'stk + 1' is the
                 * invariant state 'stk + 2' is the control variable, and
                 * 'stk + 3' is the to-be-closed variable. Call uses stack
                 * after these values (starting at 'stk + 4'). */
                SPtr stk;
                savestate(T);
                stk = STK(fetch_l());
                /* copy function, state and control variable */
                setobjs2s(T, stk + VAR_N + VAR_ITER, stk + VAR_ITER);
                setobjs2s(T, stk + VAR_N + VAR_STATE, stk + VAR_STATE);
                setobjs2s(T, stk + VAR_N + VAR_CNTL, stk + VAR_CNTL);
                T->sp.p = stk + VAR_N + VAR_CNTL + 1;
                tokuV_call(T, stk + VAR_N + VAR_ITER, fetch_l());
                updatetrap(cf);
                updatestack(cf); /* stack may have changed */
                sp = T->sp.p; /* correct sp for next opcode */
                toku_assert(sp - base == sp - cf->func.p - 1);
                /* go to the next opcode */
                I = *(pc++);
                toku_assert(I == OP_FORLOOP);
                goto l_forloop;
            }}
            vm_case(OP_FORLOOP) {
            l_forloop: {
                SPtr stk = STK(fetch_l());
                int32_t offset = fetch_l();
                int32_t nvars = fetch_l();
                if (!ttisnil(s2v(stk + VAR_N))) { /* continue loop? */
                    /* save control variable (first iterator result) */
                    setobjs2s(T, stk + VAR_CNTL, stk + VAR_N + VAR_ITER);
                    pc -= offset; /* jump back to loop body */
                } else /* otherwise leave the loop (fall through) */
                    sp -= nvars; /* remove leftover vars from previous call */
                vm_break;
            }}
            vm_case(OP_RETURN) {
                SPtr stk;
                int32_t nres; /* number of results */
                int32_t nslot;
                savestate(T);
                nslot = fetch_l();
                stk = STK(nslot);
                nres = fetch_l() - 1;
                if (nres < 0) /* not fixed ? */
                    nres = cast_i32(sp - stk);
                if (fetch_s()) { /* have open upvalues? */
                    tokuF_close(T, base, CLOSEKTOP);
                    updatetrap(cf);
                    updatestackaux(cf, stk = STK(nslot));
                }
                if (cl->p->isvararg) /* vararg function? */
                    cf->func.p -= cf->t.nvarargs + cl->p->arity + 1;
                T->sp.p = stk + nres; /* set stk ptr for 'poscall' */
                poscall(T, cf, nres);
                updatetrap(cf); /* 'poscall' can change hooks */
                /* return from Tokudae function */
            ret: /* return from Tokudae function */
                if (cf->status & CFST_FRESH) /* top-level function? */
                    return; /* end this frame */
                else {
                    cf = cf->prev; /* return to caller */
                    goto returning; /* continue running caller in this frame */
                }
            }
        }
    }
}

/* }====================================================================== */
