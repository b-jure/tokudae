/*
** tlstlib.c
** Standard library for list manipulation
** See Copyright Notice in tokudae.h
*/

#define tlstlib_c
#define TOKU_LIB

#include "tokudaeprefix.h"

#include "tokudae.h"

#include "tokudaeaux.h"
#include "tokudaelib.h"
#include "tokudaelimits.h"


/*
** Check if value at index 'n' is a list and return its length.
*/
#define checklist(T, n) \
        (tokuL_check_type(T, n, TOKU_T_LIST), t_castU2S(toku_len(T, n)))


static int32_t lst_insert(toku_State *T) {
    toku_Integer len = checklist(T, 0);
    toku_Integer pos = len;
    switch (toku_getntop(T)) {
        case 2: break; /* position is already set */
        case 3: { /* have position */
            pos = tokuL_check_integer(T, 1);
            tokuL_check_arg(T, 0 <= pos && pos <= len, 1,
                               "position out of bounds");
            /* memmove(&l[pos+1], &l[pos], len-pos) */
            for (toku_Integer i = len - 1; pos <= i; i--) {
                toku_get_index(T, 0, i);
                toku_set_index(T, 0, i+1);
            }
            break;
        }
        default: tokuL_error(T, "wrong number of arguments to 'insert'");
    }
    toku_set_index(T, 0, pos);
    return 0;
}


static int32_t lst_remove(toku_State *T) {
    toku_Integer len = checklist(T, 0);
    toku_Integer pos = tokuL_opt_integer(T, 1, len - (len > 0));
    toku_get_index(T, 0, pos); /* result = l[pos]; */
    if (len != 0) { /* the list is not empty? */
        tokuL_check_arg(T, 0 <= pos && pos < len, 1, "position out of bounds");
        /* memmove(&l[pos], &l[pos] + 1, len-pos-1) */
        for (; pos < len-1; pos++) {
            toku_get_index(T, 0, pos + 1);
            toku_set_index(T, 0, pos); /* l[pos] = l[pos + 1]; */
        }
        toku_push_nil(T);
        toku_set_index(T, 0, pos); /* remove slot l[len - 1] */
    }
    return 1;
}


/*
** Copy elements (0[f], ..., 0[e]) into (dl[d], dl[d+1], ...).
*/
static int32_t lst_move(toku_State *T) {
    toku_Integer len = checklist(T, 0); /* source list */
    toku_Integer f = tokuL_check_integer(T, 1); /* from */
    toku_Integer e = tokuL_check_integer(T, 2); /* end */
    toku_Integer d = tokuL_check_integer(T, 3); /* destination */
    int32_t dl = !toku_is_noneornil(T, 4) ? 4 : 0; /* destination list */
    tokuL_check_type(T, dl, TOKU_T_LIST);
    if (f <= e) { /* otherwise, nothing to move */
        toku_Integer dlen = (dl != 0) ? t_castU2S(toku_len(T, dl)) : len;
        toku_Integer n;
        tokuL_check_arg(T, 0 <= f, 1, "start index out of bounds");
        tokuL_check_arg(T, e < len, 2, "end index out of bounds");
        n = e - f + 1; /* number of elements to move */
        tokuL_check_arg(T, t_castS2U(d) <= t_castS2U(dlen), 3,
                          "destination index out of bounds");
        tokuL_check_arg(T, d <= TOKU_INTEGER_MAX - n, 3,
                           "destination wrap around");
        if (e < d || d <= f ||
                (dl != 0 && !toku_compare(T, 0, dl, TOKU_ORD_EQ))) {
            for (int32_t i = 0; i < n; i++) { /* lists are not overlapping */
                toku_get_index(T, 0, f + i);
                toku_set_index(T, dl, d + i);
            }
        } else { /* list are overlapping */
            toku_Integer i = d + n - 1;
            /* append missing indices (if any) */
            for (int32_t j = 0; dlen + j < i; j++) {
                toku_push_integer(T, 0); /* (garbage) */
                toku_set_index(T, dl, dlen + j);
            }
            for (i = n - 1; 0 <= i; i--) { /* do the move (from end) */
                toku_get_index(T, 0, f + i);
                toku_set_index(T, dl, d + i);
            }
        }
    }
    toku_push(T, dl); /* return destination list */
    return 1;
}


static int32_t lst_new(toku_State *T) {
    toku_Unsigned size = t_castS2U(tokuL_check_integer(T, 0));
    tokuL_check_arg(T, size <= cast_u32(INT_MAX), 0, "out of range");
    toku_push_list(T, cast_i32(size));
    return 1;
}


static int32_t lst_flatten(toku_State *T) {
    toku_Integer len = checklist(T, 0);
    toku_Integer i = tokuL_opt_integer(T, 1, 0);
    toku_Integer e = tokuL_opt(T, tokuL_check_integer, 2, len - 1);
    toku_Unsigned n;
    if (e < i) return 0; /* empty range or empty list */
    tokuL_check_arg(T, 0 <= i, 1, "start index out of bounds");
    tokuL_check_arg(T, e < len, 1, "end index out of bounds");
    n = t_castS2U(e) - t_castS2U(i); /* number of elements minus 1 */
    if (t_unlikely(!toku_checkstack(T, cast_i32(++n))))
        return tokuL_error(T, "too many results");
    while (i <= e) /* push l[i..e] */
        toku_get_index(T, 0, i++);
    return cast_i32(n);
}


static void concatval(toku_State *T, tokuL_Buffer *b, toku_Integer i) {
    int32_t t;
    toku_get_index(T, 0, i);
    t = toku_type(T, -1);
    if (t == TOKU_T_NUMBER) {
        char numbuff[TOKU_N2SBUFFSZ];
        toku_numbertocstring(T, -1, numbuff); /* convert it to string */
        toku_push_string(T, numbuff); /* push it on stack */
        toku_replace(T, -2); /* and replace original value */
    } else if (t != TOKU_T_STRING) /* value is not a string? */
        tokuL_error(T, "cannot concat value (%s) at index %d",
                        toku_typename(T, t), i);
    tokuL_buff_push_stack(b);
}


static int32_t lst_concat(toku_State *T) {
    size_t lsep;
    toku_Integer len = checklist(T, 0);
    const char *sep = tokuL_opt_lstring(T, 1, "", &lsep);
    toku_Integer i = tokuL_opt_integer(T, 2, 0);
    toku_Integer e = tokuL_opt_integer(T, 3, len - 1);
    if (i <= e) { /* list and range are not empty */
        tokuL_Buffer b;
        tokuL_check_arg(T, 0 <= i, 2, "start index out of bounds");
        tokuL_check_arg(T, e < len, 3, "end index out of bounds");
        tokuL_buff_init(T, &b);
        while (i < e) { /* 0[i .. (e - 1)] */
            concatval(T, &b, i);
            tokuL_buff_push_lstring(&b, sep, lsep);
            i++;
        }
        concatval(T, &b, e); /* add last value */
        tokuL_buff_end(&b);
    } else /* otherwise nothing to concatenate */
        toku_push_literal(T, "");
    return 1;
}


/* {===================================================================
** Quicksort implementation (based on Lua's implementation of
** 'Algorithms in MODULA-3', Robert Sedgewick, Addison-Wesley, 1993.)
** ==================================================================== */

#define geti(T,idl,idx)     toku_get_index(T, idl, idx)
#define seti(T,idl,idx)     toku_set_index(T, idl, idx)


/*
** Lists larger than 'RANLIMIT' may use randomized pivots.
*/
#define RANLIMIT    100u


/*
** Error for invalid sorting function.
*/
#define ERRSORT     "invalid order function for sorting"


static void get2(toku_State *T, uint32_t idx1, uint32_t idx2) {
    geti(T, 0, idx1);
    geti(T, 0, idx2);
}


static void set2(toku_State *T, uint32_t idx1, uint32_t idx2) {
    seti(T, 0, idx1);
    seti(T, 0, idx2);
}


static int32_t sort_cmp(toku_State *T, int32_t a, int32_t b) {
    if (toku_is_nil(T, 1)) /* no compare function? */
        return toku_compare(T, a, b, TOKU_ORD_LT);
    else { /* otherwise call compare function */
        int32_t res;
        toku_push(T, 1);          /* push function */
        toku_push(T, a-1);        /* -1 to compensate for function */
        toku_push(T, b-2);        /* -2 to compensate for function and 'a' */
        toku_call(T, 2, 1);       /* call function */
        res = toku_to_bool(T, -1);/* get result */
        toku_pop(T, 1);           /* pop result */
        return res;
    }
}


/*
** Does the partition: pivot P is at the top of the stack.
** precondition: l[lo] <= P == l[hi-1] <= l[hi],
** so it only needs to do the partition from lo + 1 to hi - 2.
** Pos-condition: l[lo .. i - 1] <= l[i] == P <= l[i + 1 .. hi]
** returns 'i'.
*/
static uint32_t partition(toku_State *T, uint32_t lo, uint32_t hi) {
    uint32_t i = lo; /* will be incremented before first use */
    uint32_t j = hi - 1; /* will be decremented before first use */
    /* loop invariant: l[lo .. i] <= P <= l[j .. hi] */
    for (;;) {
        /* next loop: repeat ++i while l[i] < P */
        while ((void)geti(T, 0, ++i), sort_cmp(T, -1, -2)) {
            if (t_unlikely(i == hi - 1)) /* l[hi - 1] < P == l[hi - 1] */
                tokuL_error(T, ERRSORT);
            toku_pop(T, 1); /* remove l[i] */
        }
        /* after the loop, l[i] >= P and l[lo .. i - 1] < P  (l) */
        /* next loop: repeat --j while P < l[j] */
        while ((void)geti(T, 0, --j), sort_cmp(T, -3, -1)) {
            if (t_unlikely(j < i)) /* j <= i-1 and l[j] > P, contradicts (l) */
                tokuL_error(T, ERRSORT);
            toku_pop(T, 1); /* remove l[j] */
        }
        /* after the loop, l[j] <= P and l[j + 1 .. hi] >= P */
        if (j < i) { /* no elements out of place? */
            /* l[lo .. i - 1] <= P <= l[j + 1 .. i .. hi] */
            toku_pop(T, 1); /* pop l[j] */
            /* swap pivot l[hi - 1] with l[i] to satisfy pos-condition */
            set2(T, hi - 1, i);
            return i;
        }
        /* otherwise, swap l[i] with l[j] to restore invariant and repeat */
        set2(T, i, j);
    }
}


/*
** Choose an element in the middle (2nd-3th quarters) of [lo,hi]
** "randomized" by 'rnd'.
*/
static uint32_t chose_pivot(uint32_t lo, uint32_t hi, uint32_t rnd) {
    uint32_t r4 = (hi - lo) / 4; /* range/4 */
    uint32_t p = (rnd ^ lo ^ hi) % (r4 * 2) + (lo + r4);
    toku_assert(lo + r4 <= p && p <= hi - r4);
    return p;
}


static void auxsort(toku_State *T, uint32_t lo, uint32_t hi, uint32_t rnd) {
    while (lo < hi) {
        uint32_t n; /* to be used later */
        uint32_t p; /* pivot index */
        get2(T, lo, hi);
        if (sort_cmp(T, -1, -2)) /* l[hi] < l[lo]? */
            set2(T, lo, hi); /* swap l[hi] with l[lo] */
        else
            toku_pop(T, 2);
        if (hi - lo == 1) /* only 2 elements? */
            return; /* done */
        if (hi - lo < RANLIMIT || !rnd) /* small interval or no randomize */
            p = (lo + hi)/2; /* use middle element as pivot */
        else
            p = chose_pivot(lo, hi, rnd);
        get2(T, p, lo);
        if (sort_cmp(T, -2, -1)) /* l[p] < l[lo]? */
            set2(T, p, lo); /* swap l[p] with l[lo] */
        else {
            toku_pop(T, 1); /* remove l[lo] */
            geti(T, 0, hi);
            if (sort_cmp(T, -1, -2)) /* l[hi] < l[p] */ 
                set2(T, p, hi); /* swap l[hi] with l[p] */
            else
                toku_pop(T, 2);
        }
        if (hi - lo == 2) /* only 3 elements? */
            return; /* done */
        geti(T, 0, p); /* get pivot */
        toku_push(T, -1); /* push pivot */
        geti(T, 0, hi - 1); /* push l[hi - 1] */
        set2(T, p, hi - 1); /* swap pivot l[p] with l[hi - 1] */
        p = partition(T, lo, hi);
        /* l[lo .. p - 1] <= l[p] == P <= l[p + 1 .. hi] */
        if (p - lo < hi - p) { /* lower interval is smaller? */
            auxsort(T, lo, p-1, rnd); /* call recursively for lower interval */
            n = p - lo; /* size of smaller interval */
            lo = p + 1; /* tail call for [p + 1 .. hi] (upper interval) */
        } else {
            auxsort(T, p+1, hi, rnd); /* call recursively for upper interval */
            n = hi - p; /* size of smaller interval */
            hi = p - 1; /* tail call for [lo .. p - 1]  (lower interval) */
        }
        if ((hi - lo) / 128 > n) /* partition too imbalanced? */
            rnd = tokuL_makeseed(T); /* try a new randomization */
    } /* tail call auxsort(T, lo, hi, rnd) */
}


static int32_t lst_sort(toku_State *T) {
    toku_Integer len = checklist(T, 0);
    if (1 < len) { /* non trivial? */
        tokuL_check_arg(T, len < INT_MAX, 0, "list too big");
        if (!toku_is_noneornil(T, 1)) /* is there a 2nd argument? */
            tokuL_check_type(T, 1, TOKU_T_FUNCTION); /* sort function */
        toku_setntop(T, 2); /* make sure there are two arguments */
        auxsort(T, 0, cast(uint32_t, len-1u), 0);
    }
    return 0;
}


static int32_t lst_isordered(toku_State *T) {
    toku_Integer len = checklist(T, 0);
    int32_t ord = 1;
    toku_Integer i, e;
    if (!toku_is_noneornil(T, 1)) /* is there a 2nd argument? */
        tokuL_check_type(T, 1, TOKU_T_FUNCTION); /* must be a function */
    i = tokuL_opt_integer(T, 2, 0); /* start index */
    e = tokuL_opt_integer(T, 3, len - 1); /* end index */
    if (i < e) { /* list and range have at least 2 values? */
        tokuL_check_arg(T, 0 <= i, 2, "start index out of bounds");
        tokuL_check_arg(T, e < len, 3, "end index out of bounds");
        toku_setntop(T, 2); /* make sure there are two arguments */
        while (ord && i < e) {
            geti(T, 0, i);
            geti(T, 0, i + 1);
            ord = sort_cmp(T, -2, -1);
            toku_pop(T, 2);
            i++;
        }
    }
    toku_push_bool(T, ord);
    if (!ord) {
        toku_push_integer(T, i);
        return 2; /* return false and first index which breaks ordering */
    }
    return 1; /* return true */
}

/* }=================================================================== */


static int32_t lst_shrink(toku_State *T) {
    tokuL_check_type(T, 0, TOKU_T_LIST);
    toku_push_bool(T, toku_shrinklist(T, 0));
    return 1;
}


static tokuL_Entry lstlib[] = {
    {"insert", lst_insert},
    {"remove", lst_remove},
    {"move", lst_move},
    {"new", lst_new},
    {"flatten", lst_flatten},
    {"concat", lst_concat},
    {"sort", lst_sort},
    {"isordered", lst_isordered},
    {"shrink", lst_shrink},
    {NULL, NULL}
};


int32_t tokuopen_list(toku_State *T) {
    tokuL_push_lib(T, lstlib);
    return 1;
}
