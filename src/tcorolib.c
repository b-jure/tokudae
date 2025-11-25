/*
** tcorolib.c
** Standard library for coroutines
** See Copyright Notice in tokudae.h
*/

#define tcorolib_c
#define TOKU_LIB

#include "tokudaeprefix.h"

#include "tokudae.h"

#include "tokudaeaux.h"
#include "tokudaelib.h"
#include "tokudaelimits.h"


static toku_State *getco(toku_State *T) {
    toku_State *co = toku_to_thread(T, 0);
    tokuL_expect_arg(T, co, 0, "thread");
    return co;
}


/*
** Resumes a coroutine. Returns the number of results for non-error
** cases or -1 for errors.
*/
static int32_t auxresume(toku_State *T, toku_State *co, int32_t narg) {
    int32_t status, nres;
    if (t_unlikely(!toku_checkstack(co, narg))) {
        toku_push_literal(T, "too many arguments to resume");
        return -1; /* error */
    }
    toku_xmove(T, co, narg);
    status = toku_resume(co, T, narg, &nres);
    if (t_likely(status == TOKU_STATUS_OK || status == TOKU_STATUS_YIELD)) {
        if (t_unlikely(!toku_checkstack(T, nres + 1))) {
            toku_pop(co, nres); /* remove results anyway */
            toku_push_literal(T, "too many results to resume");
            return -1; /* error */
        }
        toku_xmove(co, T, nres); /* move yielded values */
        return nres;
    } else {
        toku_xmove(co, T, 1); /* move error message */
        return -1; /* error */
    }
}


static int32_t tokuCO_resume(toku_State *T) {
    toku_State *co = getco(T);
    int32_t r;
    r = auxresume(T, co, toku_getntop(T) - 1);
    if (t_unlikely(r < 0)) { /* error? */
        toku_push_bool(T, 0);
        toku_insert(T, -2);
        return 2; /* return false + error message */
    } else {
        toku_push_bool(T, 1);
        toku_insert(T, -(r + 1));
        return r + 1; /* return true + 'resume' returns */
    }
}


static int32_t auxwrap(toku_State *T) {
    toku_State *co = toku_to_thread(T, toku_upvalueindex(0));
    int32_t r = auxresume(T, co, toku_getntop(T));
    if (t_unlikely(r < 0)) { /* error? */
        int32_t status = toku_status(co);
        if (status != TOKU_STATUS_OK && status != TOKU_STATUS_YIELD) {
            /* error in the coroutine */
            status = toku_closethread(co, T); /* close its tbc variables */
            toku_assert(status != TOKU_STATUS_OK);
            toku_xmove(co, T, 1); /* move error message to the caller */
        }
        if (status != TOKU_STATUS_EMEM && /* not a memory error and... */
            toku_type(T, -1) == TOKU_T_STRING) { /* err. object is a string? */
            tokuL_where(T, 1); /* add extra info (if available) */
            toku_insert(T, -2); /* move it before the error message */
            toku_concat(T, 2); /* concat them */
        }
        return toku_error(T); /* propagate error */
    }
    return r; /* return number of returned/yielded results */
}


static int32_t tokuCO_new(toku_State *T) {
    toku_State *NT;
    tokuL_check_type(T, 0, TOKU_T_FUNCTION);
    NT = toku_newthread(T);
    toku_push(T, 0); /* move function to top */
    toku_xmove(T, NT, 1); /* move function from T to NT */
    return 1; /* return the thread */
}


static int32_t tokuCO_wrap(toku_State *T) {
    tokuCO_new(T);
    toku_push_cclosure(T, auxwrap, 1);
    return 1;
}


static int32_t tokuCO_yield(toku_State *T) {
    return toku_yield(T, toku_getntop(T));
}


/* 'auxstatus' */
#define COST_RUN        0
#define COST_DEAD       1
#define COST_YIELD      2
#define COST_NORM       3


static int32_t auxstatus(toku_State *T, toku_State *co) {
    if (T == co)
        return COST_RUN;
    else {
        switch(toku_status(co)) {
            case TOKU_STATUS_YIELD:
                return COST_YIELD;
            case TOKU_STATUS_OK: {
                toku_Debug ar;
                if (toku_getstack(co, 0, &ar)) /* does it have frames? */
                    return COST_NORM; /* it is running */
                else if (toku_getntop(co) == 0) /* no frame and empty stack? */
                    return COST_DEAD;
                else
                    return COST_YIELD; /* in initial state */
            }
            default: /* some error occurred */
                return COST_DEAD;
        }
    }
}


static int32_t tokuCO_status(toku_State *T) {
    static const char *const name[] = {"running","dead","suspended","normal"};
    toku_State *co = getco(T);
    toku_push_string(T, name[auxstatus(T, co)]);
    return 1;
}


static toku_State *getoptco(toku_State *T) {
    return(toku_is_none(T, 0) ? T : getco(T));
}


static int32_t tokuCO_isyieldable(toku_State *T) {
    toku_State *co = getoptco(T);
    toku_push_bool(T, toku_isyieldable(co));
    return 1;
}


static int32_t tokuCO_running(toku_State *T) {
    int32_t ismain = toku_push_thread(T);
    toku_push_bool(T, ismain);
    return 2;
}


static int32_t tokuCO_close(toku_State *T) {
    toku_State *co = getoptco(T);
    int32_t status = auxstatus(T, co);
    switch(status) {
        case COST_DEAD: case COST_YIELD: {
            status = toku_closethread(co, T);
            if (status == TOKU_STATUS_OK) { /* no errors? */
                toku_push_bool(T, 1);
                return 1;
            } else { /* otherwise error (can't yield in 'toku_closethread') */
                toku_push_bool(T, 0);
                toku_xmove(co, T, 1); /* move error message */
                return 2;
            }
        }
        case COST_RUN: /* running coroutine? */
            toku_get_cindex(T, TOKU_CLIST_MAINTHREAD); /* get mainthread */
            if (toku_to_thread(T, -1) == co) /* closing mainthread? */
                return tokuL_error(T, "cannot close main thread");
            toku_assert(co == T); /* implied by COST_RUN */
            toku_closethread(co, T); /* close itself */
            toku_assert(0); /* previous call does not return */
            return 0;
        default: /* normal coroutine */
            return tokuL_error(T, "cannot close a normal coroutine");
    }
}


static const tokuL_Entry co_funcs[] = {
    {"new", tokuCO_new},
    {"resume", tokuCO_resume},
    {"running", tokuCO_running},
    {"status", tokuCO_status},
    {"wrap", tokuCO_wrap},
    {"yield", tokuCO_yield},
    {"isyieldable", tokuCO_isyieldable},
    {"close", tokuCO_close},
    {NULL, NULL}
};


TOKUMOD_API int32_t tokuopen_co(toku_State *T) {
    tokuL_push_lib(T, co_funcs);
    return 1;
}
