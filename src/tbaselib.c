/*
** tbaselib.c
** Basic library
** See Copyright Notice in tokudae.h
*/

#define tbaselib_c
#define TOKU_LIB

#include "tokudaeprefix.h"

#include <string.h>
#include <ctype.h>

#include "tokudae.h"

#include "tokudaeaux.h"
#include "tokudaelib.h"
#include "tokudaelimits.h"


/* {=====================================================================
** Clone
** ====================================================================== */

/*
** Index where cache table is stored.
*/
#define CACHEINDEX  1


/* repeated error message when cloning objects (for 'tokuL_check_stack') */
static const char *errclone = "too many values to clone";


/*
** Create a mapping 'ct[originalobject] = clonedobject' in the cache table.
** The value at 'index' is the original object, while the value on stack
** top is the clone of that value. This creates a graph of references,
** such that, each time we try to clone a new value, we first check if
** the value we are cloning is "repeated". If we have already "seen" this
** value, we will find it in cache table, and the value is the already
** cloned object of the original one. This way we just re-use the cloned
** object, mimicking the way original objects were stored.
*/
static void cacheobject(toku_State *T, int index) {
    toku_push(T, index); /* key */
    toku_push(T, -2); /* value */
    toku_set_field(T, CACHEINDEX);
}


/*
** Check if the original value at index has the mapping in the cache table.
** If so, re-use the already existing clone.
*/
static int checkcache(toku_State *T, int index) {
    int res = 1;
    toku_assert(index == -1 || CACHEINDEX < index);
    toku_push(T, index); /* copy of original value */
    if (toku_get_field(T, CACHEINDEX) == TOKU_T_NIL) { /* no cache hit? */
        toku_pop(T, 1); /* remove nil */
        res = 0; /* false; not found */
    } /* otherwise, the clone is on stack top */
    return res;
}


/*
** More often than not, the value to query in cache table is on
** stack top.
*/
#define checkcachetop(T)   checkcache(T, -1)


/* recursive clone functions */
static void cloneclass(toku_State *T, int index);
static void auxclone(toku_State *T, int t, int index);


static void clonelist(toku_State *T, int index) {
    int absi; /* absolute index of list value */
    toku_Integer len;
    toku_assert(CACHEINDEX < index);
    tokuL_check_stack(T, 4, errclone); /* list+value+cache */
    len = t_castU2S(toku_len(T, index));
    toku_push_list(T, cast_int(len));
    absi = toku_getntop(T);
    for (int i = 0; i < len; i++) {
        auxclone(T, toku_get_index(T, index, i), absi);
        toku_set_index(T, -2, i);
    }
    cacheobject(T, index);
}


static void clonetable(toku_State *T, int index) {
    int absi; /* absolute index for table key */
    toku_assert(CACHEINDEX < index);
    tokuL_check_stack(T, 6, errclone); /* table+2xkey+value+cache */
    toku_push_table(T, cast_int(t_castU2S(toku_len(T, index))));
    toku_push_nil(T); /* initial key for 'toku_nextfield' */
    absi = toku_gettop(T);
    while (toku_nextfield(T, index)) {
        toku_push(T, absi); /* key copy (for next iteration) */
        auxclone(T, toku_type(T, absi), absi); /* key clone */
        auxclone(T, toku_type(T, absi+1), absi+1); /* value clone */
        toku_push(T, absi); /* key copy */
        toku_push(T, absi+1); /* value copy */
        toku_set_field(T, absi-1); /* table_clone[key_copy] = value_copy */
        toku_pop(T, 2); /* remove key_clone + value_clone */
        /* key copy is on top */
    }
    cacheobject(T, index);
}


static void clonemetatable(toku_State *T, int index) {
    toku_assert(CACHEINDEX < index);
    if (toku_get_metatable(T, index)) {
        if (!checkcachetop(T))
            clonetable(T, toku_gettop(T));
        toku_replace(T, -2); /* replace original */
        toku_set_metatable(T, -2);
    }
}


static void clonemethods(toku_State *T, int index) {
    toku_assert(CACHEINDEX < index);
    if (toku_get_methodtable(T, index)) {
        if (!checkcachetop(T))
            clonetable(T, toku_gettop(T));
        toku_replace(T, -2); /* replace original */
        toku_set_methodtable(T, -2);
    }
}


static void clonesuperclass(toku_State *T, int index) {
    toku_assert(CACHEINDEX < index);
    if (toku_get_superclass(T, index)) {
        if (!checkcachetop(T))
            cloneclass(T, toku_gettop(T));
        toku_replace(T, -2); /* replace original */
        toku_set_superclass(T, -2);
    }
}


static void cloneclass(toku_State *T, int index) {
    toku_assert(CACHEINDEX < index);
    tokuL_check_stack(T, 4, errclone); /* class+(mt/table/superclass)+cache */
    toku_push_class(T);
    clonemetatable(T, index);
    clonemethods(T, index);
    clonesuperclass(T, index);
    cacheobject(T, index);
}


static void cloneuservalues(toku_State *T, int index, t_ushort nuv) {
    int absi = toku_getntop(T);
    toku_assert(CACHEINDEX < index);
    for (t_ushort i = 0; i < nuv; i++) {
        toku_get_uservalue(T, index, i);
        auxclone(T, toku_type(T, absi), absi);
        toku_set_uservalue(T, absi - 1, i);
    }
    toku_assert(toku_getntop(T) == absi);
}


static void cloneuserdata(toku_State *T, int index) {
    void *p;
    t_ushort nuv = toku_numuservalues(T, check_exp(CACHEINDEX < index, index));
    size_t size = toku_lenudata(T, index);
    tokuL_check_stack(T, 5, errclone); /* udata+(mt/methods/2xudval)+cache */
    p = toku_push_userdata(T, size, nuv);
    memcpy(p, toku_to_userdata(T, index), size);
    if (toku_get_metatable(T, index))
        toku_set_metatable(T, -2); /* keep metatable the same for userdata */
    cloneuservalues(T, index, nuv);
    cacheobject(T, index);
}


static void cloneinstance(toku_State *T, int index) {
    toku_assert(CACHEINDEX < index);
    tokuL_check_stack(T, 4, errclone); /* ins+class+cache */
    toku_get_class(T, index);
    if (!checkcachetop(T))
        cloneclass(T, toku_gettop(T));
    toku_replace(T, -2); /* replace original */
    toku_push_instance(T, -1);
    toku_remove(T, -2); /* remove class */
    toku_get_fieldtable(T, index);
    if (!checkcachetop(T))
        clonetable(T, toku_gettop(T));
    toku_replace(T, -2); /* replace original */
    toku_set_fieldtable(T, -2);
    cacheobject(T, index);
}


static void clonebmethod(toku_State *T, int index) {
    int type;
    toku_assert(CACHEINDEX < index);
    tokuL_check_stack(T, 5, errclone); /* bmethod+self+method+cache */
    type = toku_get_self(T, index);
    if (!checkcachetop(T)) {
        if (type == TOKU_T_USERDATA) /* self is userdata? */
            cloneuserdata(T, toku_gettop(T));
        else /* otherwise self is instance */
            cloneinstance(T, toku_gettop(T));
    }
    toku_replace(T, -2); /* replace original */
    toku_get_method(T, index);
    if (!checkcachetop(T)) /* not in cache table? */
        auxclone(T, toku_type(T, -1), toku_gettop(T));
    else /* otherwise cached value is on stack top */
        toku_replace(T, -2); /* replace original */
    toku_push_boundmethod(T, -2);
    toku_remove(T, -2); /* remove self */
    cacheobject(T, index);
}


static int trycopy(toku_State *T, int t, int index) {
    switch (t) {
        case TOKU_T_NIL: case TOKU_T_BOOL: case TOKU_T_NUMBER:
        case TOKU_T_LIGHTUSERDATA: case TOKU_T_FUNCTION:
        case TOKU_T_STRING: case TOKU_T_THREAD: {
            toku_push(T, index);
            return 1;
        }
        default: return 0;
    }
}


static void auxclone(toku_State *T, int t, int index) {
    toku_assert(CACHEINDEX < index); /* index is absolute */
    if (!trycopy(T, t, index) && !checkcache(T, index)) {
        /* value is object not in cache */
        switch (t) { /* clone it */
            case TOKU_T_LIST: clonelist(T, index); break;
            case TOKU_T_TABLE: clonetable(T, index); break;
            case TOKU_T_CLASS: cloneclass(T, index); break;
            case TOKU_T_INSTANCE: cloneinstance(T, index); break;
            case TOKU_T_USERDATA: cloneuserdata(T, index); break;
            case TOKU_T_BMETHOD: clonebmethod(T, index); break;
            default: toku_assert(0); /* unreachable */
        }
    }
    toku_replace(T, index); /* replace original */
}


static int b_clone(toku_State *T) {
    tokuL_check_any(T, 0);
    toku_setntop(T, 1); /* leave only object to copy on top */
    toku_push_table(T, 0); /* push cache table */
    toku_push(T, 0); /* copy of value to clone */
    auxclone(T, toku_type(T, 2), 2);
    return 1;
}

/* }===================================================================== */


static int b_error(toku_State *T) {
    int level = cast_int(tokuL_opt_integer(T, 1, 1));
    toku_setntop(T, 1); /* leave only message on top */
    if (toku_type(T, 0) == TOKU_T_STRING && level >= 0) {
        tokuL_where(T, level); /* push extra information */
        toku_push(T, 0); /* push original error message... */
        toku_concat(T, 2); /* ...and concatenate it with extra information */
        /* error message with extra information is now on top */
    }
    return toku_error(T);
}


static int b_assert(toku_State *T) {
    if (t_likely(toku_to_bool(T, 0))) { /* true? */
        return toku_getntop(T); /* get all arguments */
    } else { /* failed assert (error) */
        tokuL_check_any(T, 0); /* must have a condition */
        toku_remove(T, 0); /* remove condition */
        toku_push_literal(T, "assertion failed!"); /* push default error msg */
        toku_setntop(T, 1); /* leave only one message on top */
        return b_error(T);
    }
}


/*
** Auxiliary macro for 'b_gc' that checks if GC option 'optnum' is valid.
*/
#define checkres(v)   { if (v == -1) break; }


static int b_gc(toku_State *T) {
    static const char *const opts[] = {"stop", "restart", "collect",
        "check", "count", "step", "param", "isrunning", NULL};
    static const int numopts[] = {TOKU_GC_STOP, TOKU_GC_RESTART,
        TOKU_GC_COLLECT, TOKU_GC_CHECK, TOKU_GC_COUNT,  TOKU_GC_STEP,
        TOKU_GC_PARAM, TOKU_GC_ISRUNNING};
    int opt = numopts[tokuL_check_option(T, 0, "collect", opts)];
    switch (opt) {
        case TOKU_GC_CHECK: {
            int hadcollection = toku_gc(T, opt);
            checkres(hadcollection);
            toku_push_bool(T, hadcollection);
            return 1;
        }
        case TOKU_GC_COUNT: {
            int kb = toku_gc(T, opt); /* Kbytes */
            int b = toku_gc(T, TOKU_GC_COUNTBYTES); /* leftover bytes */
            checkres(kb);
            toku_push_number(T, cast_num(kb) + (cast_num(b)/1024));
            return 1;
        }
        case TOKU_GC_STEP: {
            int nstep = cast_int(tokuL_opt_integer(T, 1, 0));
            int completecycle = toku_gc(T, opt, nstep);
            checkres(completecycle);
            toku_push_bool(T, completecycle);
            return 1;
        }
        case TOKU_GC_PARAM: {
            static const char *const params[] = {
                "pause", "stepmul", "stepsize", NULL};
            int param = tokuL_check_option(T, 1, NULL, params);
            int value = cast_int(tokuL_opt_integer(T, 2, -1));
            toku_push_integer(T, toku_gc(T, opt, param, value));
            return 1;
        }
        case TOKU_GC_ISRUNNING: {
            int running = toku_gc(T, opt);
            checkres(running);
            toku_push_bool(T, running);
            return 1;
        }
        default: {
            int res = toku_gc(T, opt);
            checkres(res);
            toku_push_integer(T, res);
            return 1;
        }
    }
    tokuL_push_fail(T); /* invalid call (inside a finalizer) */
    return 1;
}


/* default mode for 'load' and 'loadfile' is "bt" (binary/text) */
#define getmode(T,idx)      tokuL_opt_string(T, idx, "bt")


/*
** Reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has 4
** optional arguments (chunk, chunkname, mode and environment).
*/
#define RESERVEDSLOT    4


static const char *genericreader(toku_State *T, void *ud, size_t *sz) {
    (void)ud; /* unused */
    tokuL_check_stack(T, 2, "too many nested functions");
    toku_push(T, 0); /* push func... */
    toku_call(T, 0, 1); /* ...and call it */
    if (toku_is_nil(T, -1)) { /* nothing else to read? */
        toku_pop(T, 1); /* pop result */
        *sz = 0;
        return NULL;
    } else if (t_unlikely(!toku_is_string(T, -1))) /* top is not a string? */
        tokuL_error(T, "reader function must return a string");
    toku_replace(T, RESERVEDSLOT); /* move string into reserved slot */
    return toku_to_lstring(T, RESERVEDSLOT, sz);
}


static int auxload(toku_State *T, int status, int envidx) {
    if (t_likely(status == TOKU_STATUS_OK)) {
        if (envidx != 0) { /* 'env' parameter? */
            toku_push(T, envidx); /* environment for loaded function */
            if (!toku_setupvalue(T, -2, 0)) /* set it as 1st upvalue */
                toku_pop(T, 1); /* remove 'env' if not used by previous call */
        }
        return 1; /* return function */
    } else { /* error (message is on top of the stack) */
        tokuL_push_fail(T); /* push fail */
        toku_insert(T, -2); /* and put it before error message */
        return 2; /* return fail + error message */
    }
}


static int b_load(toku_State *T) {
    int status;
    size_t l;
    const char *s = toku_to_lstring(T, 0, &l);
    const char *mode = getmode(T, 2);
    int env = (!toku_is_none(T, 3) ? 3 : 0);
    if (s != NULL) { /* loading a string? */
        const char *chunkname = tokuL_opt_string(T, 1, s);
        status = tokuL_loadbufferx(T, s, l, chunkname, mode);
    } else { /* loading from a reader function */
        const char *chunkname = tokuL_opt_string(T, 1, "=(load)");
        tokuL_check_type(T, 0, TOKU_T_FUNCTION); /* 'chunk' must be a function */
        toku_setntop(T, RESERVEDSLOT+1); /* create reserved slot */
        status = toku_load(T, genericreader, NULL, chunkname, mode);
    }
    return auxload(T, status, env);
}


static int b_loadfile(toku_State *T) {
    const char *filename = tokuL_opt_string(T, 0, NULL);
    const char *mode = getmode(T, 1);
    int env = (!toku_is_none(T, 2) ? 2 : 0);
    int status = tokuL_loadfilex(T, filename, mode);
    return auxload(T, status, env);
}


static int b_runfile(toku_State *T) {
    const char *filename = tokuL_opt_string(T, 0, NULL);
    toku_setntop(T, 1);
    if (t_unlikely(tokuL_loadfile(T, filename) != TOKU_STATUS_OK))
        return toku_error(T);
    toku_call(T, 0, TOKU_MULTRET);
    return toku_gettop(T); /* all except the 'filename' */
}


static int b_getmetatable(toku_State *T) {
    tokuL_check_any(T, 0);
    if (!toku_get_metatable(T, 0)) {
        toku_push_nil(T);
        return 1; /* no metatable */
    }
    tokuL_get_metafield(T, 0, "__metatable");
    return 1; /* return either __metatable value (if present) or metatable */
}


static int b_setmetatable(toku_State *T) {
    int t;
    tokuL_check_type(T, 0, TOKU_T_CLASS);
    t = toku_type(T, 1);
    tokuL_expect_arg(T, t == TOKU_T_NIL || t == TOKU_T_TABLE, 1, "nil/table");
    if (t_unlikely(tokuL_get_metafield(T, 0, "__metatable") != TOKU_T_NIL))
        return tokuL_error(T, "cannot change a protected metatable");
    toku_setntop(T, 2);
    toku_set_metatable(T, 0);
    return 1;
}


static int b_unwrapmethod(toku_State *T) {
    tokuL_check_type(T, 0, TOKU_T_BMETHOD);
    toku_get_self(T, 0);
    toku_get_method(T, 0);
    return 2; /* return self and method value */
}


static int auxgetmethods(toku_State *T, int idx, int chkfields) {
    if (tokuL_get_metafield(T, idx, "__methodtable") == TOKU_T_NIL) {
        if (!toku_get_methodtable(T, idx)) { /* no method table? */
            toku_push_nil(T);
            return 0;
        }
    } else if (chkfields) { /* __methodtable value must have fields? */
        int t = toku_type(T, -1);
        if (t != TOKU_T_TABLE && t != TOKU_T_INSTANCE) {
            toku_push_nil(T);
            return 0;
        }
    }
    return 1; /* have either a __methodtable metafield or method table */
}


static int b_getmethods(toku_State *T) {
    tokuL_check_type(T, 0, TOKU_T_CLASS);
    toku_setntop(T, 1);
    auxgetmethods(T, 0, 0);
    return 1; /* returns __methodtable metafield or method table or nil) */
}


static int b_setmethods(toku_State *T) {
    tokuL_check_type(T, 0, TOKU_T_CLASS);
    toku_setntop(T, 2);
    if (t_unlikely(tokuL_get_metafield(T, 0, "__methodtable") != TOKU_T_NIL))
        return tokuL_error(T, "cannot change a protected method table");
    if (!toku_is_nil(T, 1))
        tokuL_check_type(T, 1, TOKU_T_TABLE); /* must be a table */
    toku_set_methodtable(T, 0);
    return 1;
}


static int b_nextfield(toku_State *T) {
    int t = toku_type(T, 0);
    tokuL_expect_arg(T, t == TOKU_T_INSTANCE || t == TOKU_T_TABLE, 0,
                        "instance/table");
    toku_setntop(T, 2); /* if 2nd argument is missing create it */
    if (toku_nextfield(T, 0)) /* found field? */
        return 2; /* key (index) + value */
    else {
        toku_push_nil(T);
        return 1;
    }
}


static void metamethoditer(toku_State *T) {
    toku_push(T, 0); /* argument 'self' to metamethod */
    toku_call(T, 1, 3); /* get 3 values from the metamethod */
}


static int b_fields(toku_State *T) {
    tokuL_check_any(T, 0);
    if (tokuL_get_metafield(T, 0, "__fields") == TOKU_T_NIL) {
        toku_push_cfunction(T, b_nextfield);  /* will return generator, */
        toku_push(T, 0);                      /* state, */
        toku_push_nil(T);                     /* and initial value */
    } else /* otherwise have __fields */
        metamethoditer(T);
    return 3;
}


static int auxindices(toku_State *T) {
    toku_Integer i;
    tokuL_check_type(T, 0, TOKU_T_LIST);
    i = tokuL_check_integer(T, 1);
    i = tokuL_intop(+, i, 1);
    toku_push_integer(T, i);
    return (toku_get_index(T, 0, i) == TOKU_T_NIL) ? 1 : 2;
}


static int b_indices(toku_State *T) {
    tokuL_check_any(T, 0);
    if (tokuL_get_metafield(T, 0, "__indices") == TOKU_T_NIL) {
        tokuL_check_type(T, 0, TOKU_T_LIST); /* must be a list */
        toku_push_cfunction(T, auxindices); /* iteration function */
        toku_push(T, 0);                    /* state */
        toku_push_integer(T, -1);           /* initial value */
    } else /* otherwise have __indices */
        metamethoditer(T);
    return 3;
}


static int finishpcall(toku_State *T, int status, int extra) {
    if (t_unlikely(status != TOKU_STATUS_OK)) {
        toku_push_bool(T, 0);     /* false */
        toku_push(T, -2);         /* error message */
        return 2;               /* return false, message */
    } else
        return toku_getntop(T) - extra; /* return all */
}


static int b_pcall(toku_State *T) {
    int status;
    tokuL_check_any(T, 0);
    toku_push_bool(T, 1); /* first result if no errors */
    toku_insert(T, 0); /* insert it before the object being called */
    status = toku_pcall(T, toku_getntop(T) - 2, TOKU_MULTRET, -1);
    return finishpcall(T, status, 0);
}


static int b_xpcall(toku_State *T) {
    int status;
    int nargs = toku_getntop(T) - 2;
    tokuL_check_type(T, 1, TOKU_T_FUNCTION); /* check error function */
    toku_push_bool(T, 1); /* first result */
    toku_push(T, 0); /* function */
    toku_rotate(T, 2, 2); /* move them below function's arguments */
    status = toku_pcall(T, nargs, TOKU_MULTRET, 1);
    return finishpcall(T, status, 2);
}


static int b_print(toku_State *T) {
    int n = toku_getntop(T);
    for (int i = 0; i < n; i++) {
        size_t len;
        const char *str = tokuL_to_lstring(T, i, &len);
        if (i > 0)
            toku_writelen(stdout, "\t", 1);
        toku_writelen(stdout, str, len);
        toku_pop(T, 1); /* pop result from 'tokuL_to_string' */
    }
    toku_writeline(stdout);
    return 0;
}


static int b_printf(toku_State *T) {
    size_t lfmt;
    const char *fmt = tokuL_check_lstring(T, 0, &lfmt);
    if (0 < lfmt) { /* format string is not empty? */
        int arg = 0;
        int top = toku_gettop(T);
        const char *efmt = fmt + lfmt;
        tokuL_Buffer b;
        tokuL_buff_initsz(T, &b, lfmt);
        while (fmt < efmt) {
            if (*fmt != '%') /* regular character? */
                tokuL_buff_push(&b, *fmt++);
            else if (*++fmt == '%') /* '%%'? */
                tokuL_buff_push(&b, *fmt++);
            else { /* otherwise '%' */
                if (t_unlikely(++arg > top)) /* too many format specifiers? */
                    return tokuL_error(T, "unmatched '%%' at position #%d", arg);
                tokuL_to_lstring(T, arg, NULL);
                tokuL_buff_push_stack(&b);
            }
        }
        tokuL_buff_end(&b);
        fmt = toku_to_lstring(T, -1, &lfmt);
        toku_writelen(stdout, fmt, lfmt);
    }
    toku_writeline(stdout);
    return 0;
}


static int b_warn(toku_State *T) {
    int n = toku_getntop(T);
    int i;
    tokuL_check_string(T, 0); /* at least one string */
    for (i = 1; i < n; i++)
        tokuL_check_string(T, i);
    for (i = 0; i < n - 1; i++)
        toku_warning(T, toku_to_string(T, i), 1);
    toku_warning(T, toku_to_string(T, n - 1), 0);
    return 0;
}


static int b_len(toku_State *T) {
    int t = toku_type(T, 0);
    tokuL_expect_arg(T, t == TOKU_T_LIST || t == TOKU_T_TABLE ||
                        t == TOKU_T_INSTANCE || t == TOKU_T_CLASS ||
                        t == TOKU_T_STRING, 0,
                        "list/table/class/instance/string");
    toku_push_integer(T, t_castU2S(toku_len(T, 0)));
    return 1;
}


static int b_rawequal(toku_State *T) {
    tokuL_check_any(T, 0); /* lhs */
    tokuL_check_any(T, 1); /* rhs */
    toku_push_bool(T, toku_rawequal(T, 0, 1));
    return 1;
}


static int b_rawget(toku_State *T) {
    int t = toku_type(T, 0);
    int field = !toku_to_bool(T, 2);
    tokuL_check_arg(T, t == TOKU_T_INSTANCE || t == TOKU_T_TABLE, 0,
                       "table/instance");
    tokuL_check_any(T, 1); /* key */
    toku_setntop(T, 2);
    if (t == TOKU_T_TABLE || field) /* get only the field? */
        toku_get_field(T, 0);
    else /* otherwise allow bound method */
        toku_get_raw(T, 0);
    return 1;
}


static int b_rawset(toku_State *T) {
    int t = toku_type(T, 0);
    tokuL_expect_arg(T, t == TOKU_T_INSTANCE || t == TOKU_T_TABLE, 0,
                        "table/instance");
    tokuL_check_any(T, 1); /* key */
    tokuL_check_any(T, 2); /* value */
    toku_setntop(T, 3);
    toku_set_field(T, 0);
    return 1; /* return object */
}


static int b_getargs(toku_State *T) {
    toku_Integer i;
    int nres = toku_getntop(T) - 1;
    if (toku_type(T, 0) == TOKU_T_STRING) {
        const char *what = toku_to_string(T, 0);
        if (strcmp(what, "list") == 0) { /* list? */
            toku_push_list(T, nres); /* push the list */
            toku_replace(T, 0); /* move in place of string */
            for (int i = 0; i < nres; i++) { /* set the list indices */
                toku_push(T, 1 + i);
                toku_set_index(T, 0, i);
            }
            toku_setntop(T, 1); /* leave only list on top */
        } else if (strcmp(what, "table") == 0) { /* hashset? */
            toku_push_table(T, nres); /* push the table (hashset) */
            toku_replace(T, 0); /* move in place of string */
            while (nres--) { /* set the table fields */
                toku_push_bool(T, 1);
                toku_set_field(T, 0);
            }
        } else if (strcmp(what, "last") == 0) { /* last? */
            i = (nres > 0) ? nres-1 : 0; /* get last argument */
            goto l_getargs;
        } else if (strcmp(what, "len") == 0) /* len? */
            toku_push_integer(T, nres); 
        else
            tokuL_error_arg(T, 0,
            "invalid mode, expected \"list\"/\"table\"/\"len\"/\"last\"");
        return 1; /* return (list|table|len) */
    } else {
        i = tokuL_check_integer(T, 0);
    l_getargs:
        if (i < 0) i = nres + i;
        else if (i > nres) i = nres - 1;
        tokuL_check_arg(T, 0 <= i, 0, "index out of range");
        return nres - cast_int(i); /* return 'nres-i' results */
    }
}


/* space characters to skip */
#define SPACECHARS      " \f\n\r\t\v"

/* Lookup table for digit values. -1==255>=36 -> invalid */
static const t_ubyte table[] = { 255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,255,255,255,255,255,255,
255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

/*
** Converts string to 'toku_Integer', skips leading and trailing whitespace,
** checks for overflows and underflows and checks if 's' is valid numeral
** string. Conversion works for bases [2,36] and hexadecimal and octal
** literal strings.
*/
static const char *strtoint(const char *s, toku_Unsigned base,
                                           toku_Integer *pn, int *of) {
    const t_ubyte *val = table+1;
    toku_Unsigned lim = t_castS2U(TOKU_INTEGER_MIN);
    toku_Unsigned n = 0;
    int sign = 1;
    int c;
    s += strspn(s, SPACECHARS); /* skip leading spaces */
    c = *s++;
    if (c == '-' || c == '+') { /* handle sign */
        sign -= 2*(c == '-');
        c = *s++;
    }
    /* skip optional prefix (if any) */
    if (c == '0' && (base == 2 || base == 8 || base == 16)) {
        c = *s++; /* skip 0 */
        if ((c|32) == 'b') { /* binary prefix? */
            if (base != 2) return NULL;
            c = *s++; /* skip 'b|B' */
        } else if ((c|32) == 'x') { /* hexadecimal prefix? */
            if (base != 16) return NULL;
            c = *s++; /* skip x|X */
        }
    }
    if (!isalnum(cast_ubyte(c))) /* no digit? */
        return NULL;
    do {
        if (base <= val[c]) return NULL; /* invalid numeral */
        if (TOKU_UNSIGNED_MAX/base < n || TOKU_UNSIGNED_MAX-val[c] < base*n)
            break; /* (under)overflow */
        n = n * base + val[c];
        c = *s++;
    } while (isalnum(cast_ubyte(c)));
    if (isalnum(cast_ubyte(c))) { /* (under)overflow? */
        while (val[c] < base) c = *s++; /* skip rest of valid digits */
        if (isalnum(cast_ubyte(c))) return NULL; /* invalid numeral */
        n = lim - (sign > 0); /* if positive, upper limit is one less */
        *of = sign;
    } else if (lim <= n) { /* greater or touching the signed limit? */
        if (sign > 0) { /* positive? */
            *of = 1; /* it is overflow by at least one */
            n = lim-1;
        } else if (lim < n) { /* overflow of negative number? */
            *of = -1;
            n = lim;
        } /* otherwise we gucci */
    }
    s--; /* go back once (lookahead char) */
    s += strspn(s, SPACECHARS); /* skip trailing spaces */
    *pn = t_castU2S((sign < 0) ? (0u - n) : n);
    return s;
}


static int b_tonum(toku_State *T) {
    int of = 0; /* overflow flag */
    if (toku_is_noneornil(T, 1)) { /* no base? */
        if (toku_type(T, 0) == TOKU_T_NUMBER) { /* number ? */
            toku_setntop(T, 1); /* set it as top */
            return 1; /* return it */
        } else { /* must be string */
            size_t l;
            const char *s = toku_to_lstring(T, 0, &l);
            if (s != NULL && toku_stringtonumber(T, s, &of) == l + 1)
                goto done;
            /* else not a number */
            tokuL_check_any(T, 0); /* (but there must be some parameter) */
        }
    } else { /* have base */
        size_t l;
        toku_Integer i = tokuL_check_integer(T, 1); /* base */
        const char *s = tokuL_check_lstring(T, 0, &l); /* numeral to convert */
        toku_Integer n;
        tokuL_check_arg(T, 2 <= i && i <= 36, 1, "base out of range [2,36]");
        if (strtoint(s, t_castS2U(i), &n, &of) == s + l) { /* conversion ok? */
            toku_push_integer(T, n); /* push the conversion number */
            goto done;
        }
    }
    of = 0; /* skip 'if' */
    tokuL_push_fail(T); /* conversion failed */
done:
    if (of) {
        toku_push_integer(T, of);
        return 2; /* return number + over(under)flow flag (-1|1) */
    }
    return 1; /* return fail or number */ 
}


static int b_tostr(toku_State *T) {
    tokuL_check_any(T, 0);
    tokuL_to_lstring(T, 0, NULL);
    return 1;
}


static int b_typeof(toku_State *T) {
    tokuL_check_any(T, 0);
    toku_push_string(T, tokuL_typename(T, 0));
    return 1;
}


static void getclassfield(toku_State *T, int t) {
    if (auxgetmethods(T, 2, 1)) { /* __methodtable or method table? */
        toku_push(T, 1); /* key copy */
        if (toku_get_field(T, -2) != TOKU_T_NIL && t == TOKU_T_INSTANCE)
            toku_push_boundmethod(T, 0);
    } /* otherwise nil is on stack top */
}


static int b_getclass(toku_State *T) {
    int t = toku_type(T, 0);
    tokuL_expect_arg(T, t == TOKU_T_CLASS || t == TOKU_T_INSTANCE, 0,
                        "class/instance");
    toku_setntop(T, 2);
    if (t == TOKU_T_INSTANCE)
        toku_get_class(T, 0);
    else
        toku_push(T, 0); /* keep class on stack top */
    if (!toku_is_noneornil(T, 1))
        getclassfield(T, t);
    return 1;
}


static int b_getsuper(toku_State *T) {
    int t = toku_type(T, 0);
    tokuL_expect_arg(T, t == TOKU_T_CLASS || t == TOKU_T_INSTANCE, 0,
                        "class/instance");
    toku_setntop(T, 2);
    if (!toku_get_superclass(T, 0))
        toku_push_nil(T); /* no superclass */
    else if (!toku_is_noneornil(T, 1))
        getclassfield(T, t);
    return 1;
}


/* {=====================================================================
** RANGE
** ====================================================================== */

#define RANGE_VALUES(T) \
    toku_Integer start = toku_to_integer(T, toku_upvalueindex(0)); \
    toku_Integer stop = toku_to_integer(T, toku_upvalueindex(1)); \
    toku_Integer step = toku_to_integer(T, toku_upvalueindex(2));


static void pushrangeres(toku_State *T, toku_Integer start, toku_Integer next) {
    toku_push_integer(T, start); /* current range value */
    toku_push_integer(T, next); /* next range value */
    toku_replace(T, toku_upvalueindex(0)); /* update upvalue */
}


/* get next range value */
#define getnext(start,step,stop,op,lim) \
        (((start) op (lim)-(step)) ? (start)+(step) : (stop))


static int aux_revrange(toku_State *T) {
    RANGE_VALUES(T);
    toku_assert(step < 0);
    if (start > stop) {
        toku_Integer next = getnext(start, step, stop, >=, TOKU_INTEGER_MIN);
        pushrangeres(T, start, next);
    } else
        toku_push_nil(T);
    return 1; /* return 'next' or nil */
}


static int aux_range(toku_State *T) {
    RANGE_VALUES(T);
    toku_assert(step > 0);
    if (start < stop) {
        toku_Integer next = getnext(start, step, stop, <=, TOKU_INTEGER_MAX);
        pushrangeres(T, start, next);
    } else
        toku_push_nil(T);
    return 1; /* return 'next' or nil */
}


static int b_range(toku_State *T) {
    toku_Integer stop, step;
    toku_Integer start = tokuL_check_integer(T, 0);
    if (toku_is_noneornil(T, 1)) { /* no stop? */
        stop = start; /* range stops at start... */
        start = 0; /* ...and starts at 0 */
    } else /* have stop value */
        stop = tokuL_check_integer(T, 1);
    step = tokuL_opt_integer(T, 2, 1);
    if (t_unlikely(step == 0)) /* invalid range? */
        return tokuL_error(T, "invalid 'step', expected non-zero integer");
    else { /* push iterator */
        toku_push_integer(T, start);
        toku_push_integer(T, stop);
        toku_push_integer(T, step);
        toku_push_cclosure(T, (step < 0) ? aux_revrange : aux_range, 3);
        return 1; /* return iterator */
    }
}

/* ====================================================================} */


static const tokuL_Entry basic_funcs[] = {
    {"clone", b_clone},
    {"error", b_error},
    {"assert", b_assert},
    {"gc", b_gc},
    {"load", b_load},
    {"loadfile", b_loadfile},
    {"runfile", b_runfile},
    {"getmetatable", b_getmetatable},
    {"setmetatable", b_setmetatable},
    {"unwrapmethod", b_unwrapmethod},
    {"getmethods", b_getmethods},
    {"setmethods", b_setmethods},
    {"nextfield", b_nextfield},
    {"fields", b_fields},
    {"indices", b_indices},
    {"pcall", b_pcall},
    {"xpcall", b_xpcall},
    {"print", b_print},
    {"printf", b_printf},
    {"warn", b_warn},
    {"len", b_len},
    {"rawequal", b_rawequal},
    {"rawget", b_rawget},
    {"rawset", b_rawset},
    {"getargs", b_getargs},
    {"tonum", b_tonum},
    {"tostr", b_tostr},
    {"typeof", b_typeof},
    {"getclass", b_getclass},
    {"getsuper", b_getsuper},
    {"range", b_range},
    /* placeholders */
    {TOKU_GNAME, NULL},
    {"__VERSION", NULL},
    /* compatibility flags */
    {"__POSIX", NULL},
    {"__WINDOWS", NULL},
    /* internal test flags table */
#if (defined(TOKUI_EMERGENCYGCTESTS) || defined(TOKUI_HARDMEMTESTS) || \
        defined(TOKUI_HARDSTACKTESTS))
    {"__TESTS", NULL},
#endif
    {NULL, NULL},
};


static void set_compat(toku_State *T, const char *have, const char *missing) {
    if (have) toku_set_field_str(T, -2, have);
    toku_push_nil(T);
    toku_set_field_str(T, -2, missing);
}


static void set_compat_flags(toku_State *T) {
    #if (defined(TOKU_USE_POSIX)) /* POSIX */
        toku_push_integer(T, TOKU_POSIX_REV);
        set_compat(T, "__POSIX", "__WINDOWS");
    #elif (defined(TOKU_USE_WINDOWS)) /* WINDOWS */
        toku_push_integer(T, _MSC_VER);
        set_compat(T, "__WINDOWS", "__POSIX");
    #else /* ISO C */
        set_compat(T, NULL, "__POSIX");
        set_compat(T, NULL, "__WINDOWS");
    #endif
}


static void set_internal_test_flags(toku_State *T) {
    toku_push_table(T, 0);
    toku_push(T, -1); /* table copy */
    toku_set_field_str(T, -3, "__TESTS");
    #if (defined(TOKUI_EMERGENCYGCTESTS))
        toku_push_bool(T, 1);
        toku_set_field_str(T, -2, "gc");
    #endif
    #if (defined(TOKUI_HARDMEMTESTS))
        toku_push_bool(T, 1);
        toku_set_field_str(T, -2, "memory");
    #endif
    #if (defined(TOKUI_HARDSTACKTESTS))
        toku_push_bool(T, 1);
        toku_set_field_str(T, -2, "stack");
    #endif
    toku_pop(T, 1); /* remove __TESTS table */
}


int tokuopen_basic(toku_State *T) {
    /* open lib into global instance */
    toku_push_globaltable(T);
    tokuL_set_funcs(T, basic_funcs, 0);
    /* set global __G */
    toku_push(T, -1); /* copy of global table */
    toku_set_field_str(T, -2, TOKU_GNAME);
    /* set global __VERSION */
    toku_push_literal(T, TOKU_VERSION);
    toku_set_field_str(T, -2, "__VERSION");
    /* set compatibility flags */
    set_compat_flags(T);
    set_internal_test_flags(T);
    return 1;
}
