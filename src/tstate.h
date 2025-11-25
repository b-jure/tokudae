/*
** tstate.h
** Global and Thread state
** See Copyright Notice in tokudae.h
*/

#ifndef tstate_h
#define tstate_h


#include "tobject.h"
#include "tlist.h"
#include "tmeta.h"

#include <setjmp.h>


/*
** Some notes about garbage-collected objects: All objects in Tokudae must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'ObjectHeader' for the link:
**
** 'objects': all objects not marked for finalization;
** 'fin': all objects marked for finalization;
** 'tobefin': all objects ready to be finalized;
** 'fixed': all objects that are not to be collected
**          (currently only small strings, such as reserved words).
**
** There is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray (with two exceptions explained below):
**
** 'graylist': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all threads.
**
** The exception to that "gray rule" is that open upvalues are kept gray to
** avoid barriers, but they stay out of gray lists.
** (They don't even have a 'gclist' field.)
*/


/*
** About 'nCcalls':  This count has two parts: the lower 16 bits counts
** the number of recursive invocations in the C stack; the higher
** 16 bits counts the number of non-yieldable calls in the stack.
** (They are together so that we can change and save both with one
** instruction.)
*/

/* true if this thread does not have non-yieldable calls in the stack */
#define yieldable(T)	(((T)->nCcalls & 0xffff0000) == 0)

/* real number of C calls */
#define getCcalls(T)	((T)->nCcalls & 0xffff)

/* increment the number of non-yieldable calls */
#define incnny(T)       ((T)->nCcalls += 0x10000)

/* decrement the number of non-yieldable calls */
#define decnny(T)       ((T)->nCcalls -= 0x10000)

/* non-yieldable call increment */
#define nyci    (0x10000 | 1)


typedef struct toku_longjmp toku_longjmp; /* defined in 'tprotected.c' */


/* atomic type */
#if !defined(t_signal)
#include <signal.h>
#define t_signal        sig_atomic_t
#endif


/*
** Extra stack space to handle TM calls and some other extras. This
** space is not included in 'stackend'. It is used only to avoid stack
** checks, either because the element will be promptly popped or because
** there will be a stack check soon after the push. Function frames
** never use this extra space, so it does not need to be kept clean.
*/
#define EXTRA_STACK     5


#define INIT_STACKSIZE  (TOKU_MINSTACK * 2)


#define stacksize(th)   cast_i32((th)->stackend.p - (th)->stack.p)


/* {{CallFrame============================================================ */

/* bits in CallFrame status */
#define CFST_C          bitmask(0) /* call is running a C function */
#define CFST_FRESH      bitmask(1) /* call is on fresh "tokuV_execute" */
#define CFST_CLSRET     bitmask(2) /* function is closing tbc variables */
#define CFST_TBC        bitmask(3) /* function has tbc vars. to close */
#define CFST_OAH        bitmask(4) /* original value of 'allowhook' */
#define CFST_HOOKED     bitmask(5) /* call is running a debug hook */
#define CFST_YPCALL     bitmask(6) /* doing a yieldable protected call */
#define CFST_TAIL       bitmask(7) /* call was tail called */
#define CFST_HOOKYIELD  bitmask(8) /* last hook that was called yielded */
#define CFST_FIN        bitmask(9) /* function "called" a finalizer */
/* bits 10-12 are used for CFST_RECST (see below) */
#define CFST_RECST      10u /* the offset, not the mask */
/* bits 13-15 are unused */


/*
** Field CFST_RECST stores the "recover status", used to keep the error
** status while closing to-be-closed variables in coroutines, so that
** Tokudae can correctly resume after an yield from a __close method
** called because of an error.
*/
#define getcfstrecst(cf)    (((cf)->status >> CFST_RECST) & 7)
#define setcfstrecst(cf,st) \
        check_exp(((st) & 7) == (st), /* must fit in three bits */ \
                  ((cf)->status = cast_u16(((cf)->status & ~(7u<<CFST_RECST)) \
                                  | cast_u16(cast_u16(st) << CFST_RECST))))


/* active function is Tokudae function */
#define isTokudae(cf)       (!((cf)->status & CFST_C))

/* call is running Tokudae code (not a hook) */
#define isTokudaecode(cf)   (!((cf)->status & (CFST_C | CFST_HOOKED)))


/* set/get original 'allowhook' from status */
#define setoah(cf,v)  \
        ((cf)->status = cast_u16((v) ? (cf)->status|CFST_OAH  \
                                     : (cf)->status & ~CFST_OAH))
#define getoah(cf)  (((cf)->status & CFST_OAH) ? 1 : 0)


typedef struct CallFrame {
    SIndex func; /* function stack index */
    SIndex top; /* top for this function */
    struct CallFrame *prev, *next; /* dynamic call link */
    union {
        struct { /* only for Tokudae functions */
            const uint8_t *savedpc; /* current pc (1 byte after the opcode) */
            volatile t_signal trap; /* tracing lines/count or stack changed */
            int32_t nvarargs; /* number of extra args. in vararg function */
        } t;
        struct { /* only for C functions */
            toku_KFunction k; /* continuation in case of yields */
            toku_KContext cx; /* context information in case of yields */
            ptrdiff_t old_errfunc; /* offset of message handler */
        } c;
    } u;
    union {
        int32_t funcidx; /* called-function index */
        int32_t nyield; /* number of values yielded */
        int32_t nres; /* number of values returned */
    } u2;
    uint32_t nresults; /* number of wanted results from this function + 1 */
    uint32_t extraargs; /* number of call, init and/or bound (meta)methods */
    uint16_t status; /* call status */
} CallFrame;


/* 'nresults' in CallFrame are biased with 1 because of TOKU_MULTRET */
#define cfnres(n)       (cast_u32(n) + 1u)

/* maximum number of calls through init/call/bound (meta)methods */
#define CALLCHAIN_MAX   UINT8_MAX


/* }{Global state========================================================= */

/*
** Size of cache for strings in the API. 'N' is the number of
** sets (better be a prime) and 'M' is the size of each set.
** (M == 1 makes a direct cache.)
*/
#if !defined(TOKUI_STRCACHE_N)
#define TOKUI_STRCACHE_N        53 /* cache lines */
#define TOKUI_STRCACHE_M        2  /* cache line size * sizeof(OString*) */
#endif


/* 
** Table for interned strings.
** Collision resolution is resolved by chain.
*/
typedef struct StringTable {
    OString **hash; /* array of buckets (linked lists of strings) */
    int32_t nuse;   /* number of elements */
    int32_t size;   /* number of buckets */
} StringTable;


typedef struct GState {
    toku_Alloc falloc;   /* allocator */
    void *ud_alloc;      /* userdata for 'falloc' */
    t_mem totalbytes;    /* number of bytes allocated - gcdebt */
    t_mem gcdebt;        /* number of bytes not yet compensated by collector */
    t_umem gcestimate;   /* gcestimate of non-garbage memory in use */
    StringTable strtab;  /* interned strings */
    TValue c_list;       /* API list */
    TValue c_table;      /* API table */
    TValue nil;          /* special nil value (also init flag) */
    uint32_t seed;       /* initial seed for hashing */
    uint8_t whitebit;    /* current white bit (WHITEBIT0 or WHITEBIT1) */
    uint8_t gcstate;     /* GC state bits */
    uint8_t gcstopem;    /* stops emergency collections */
    uint8_t gcstop;      /* control whether GC is running */
    uint8_t gcemergency; /* true if this is emergency collection */
    uint8_t gcparams[TOKU_GCP_NUM]; /* GC options */
    uint8_t gccheck;     /* true if collection triggered since last check */
    GCObject *objects;   /* list of all collectable objects */
    GCObject **sweeppos; /* current position of sweep in list */
    GCObject *fin;       /* list of objects that have finalizer */
    GCObject *graylist;  /* list of gray objects */
    GCObject *grayagain; /* list of objects to be traversed atomically */
    GCObject *tobefin;   /* list of objects to be finalized (pending) */
    GCObject *fixed;     /* list of fixed objects (not to be collected) */
    struct toku_State *twups; /* list of threads with open upvalues */
    toku_CFunction fpanic;    /* panic handler (runs in unprotected calls) */
    struct toku_State *mainthread; /* thread that also created global state */
    OString *listfields[LFNUM];    /* array with names of list fields */
    OString *memerror;             /* preallocated message for memory errors */
    OString *tmnames[TM_NUM];      /* array with tag method names */
    OString *strcache[TOKUI_STRCACHE_N][TOKUI_STRCACHE_M]; /* string cache */
    toku_WarnFunction fwarn; /* warning function */
    void *ud_warn;           /* userdata for 'fwarn' */
} GState;


/* }{Thread (per-thread-state)============================================ */

/* Tokudae thread state */
struct toku_State {
    ObjectHeader;
    uint8_t status;             /* status of the thread */
    uint8_t allowhook;          /* true if hooks are allowed to run */
    uint16_t ncf;               /* number of call frames in 'cf' list */
    GCObject *gclist;           /* (for GC) */
    struct toku_State *twups;   /* next thread with open upvalues */
    GState *gstate;             /* global state of the thread */
    toku_longjmp *errjmp;       /* error recovery */
    SIndex stack;               /* stack base */
    SIndex sp;                  /* first free slot in the 'stack' */
    SIndex stackend;            /* end of 'stack' + 1 */
    CallFrame basecf;           /* base frame, C's entry to Tokudae */
    CallFrame *cf;              /* active frame */
    volatile toku_Hook hook;    /* hook function */
    UpVal *openupval;           /* list of open upvalues */
    SIndex tbclist;             /* list of to-be-closed variables */
    ptrdiff_t errfunc;          /* error handling function (on stack) */
    uint32_t nCcalls;           /* number of C calls */
    int32_t oldpc;              /* last pc that was traced */
    int32_t basehookcount;      /* original value set for 'hookcount' */
    int32_t hookcount;          /* current instruction count until hook */
    volatile t_signal hookmask; /* mask of events when the hook should run */
    struct { /* info about transferred values (for call/return hooks) */
        int32_t ftransfer; /* offset of first value transferred */
        int32_t ntransfer; /* number of values transferred */
    } transferinfo;
};


/* check if global state is fully built */
#define statefullybuilt(gs)     ttisnil(&(gs)->nil)


/* get global state/C list/C table */
#define G(T)        ((T)->gstate)
#define CL(T)       (&G(T)->c_list)
#define CT(T)       (&G(T)->c_table)


/*
** Get the global table in the clist. Since all predefined
** indices in the C list were inserted right when the C list
** was created and were never removed, they must always be present.
*/
#define GT(T)       (&listval(CL(T))->arr[TOKU_CLIST_GLOBALS])


/* eXtra space + main thread State */
typedef struct XS {
    uint8_t extra_[TOKU_EXTRASPACE];
    toku_State t;
} XS;


/* extra space(X) + main thread state(S) + global state(G) */
typedef struct XSG {
    XS xs;
    GState gs;
} XSG;


/* cast 'toku_State' back to start of 'XS' */
#define fromstate(T)    cast(XS *, cast(uint8_t *, (T)) - offsetof(XS, t))

/* }}===================================================================== */


/* union for conversions (casting) */
union GCUnion {
    struct GCObject gc;  /* object header */
    struct Table tab;    /* table */
    struct List l;       /* list */
    struct OString str;  /* string */
    struct UpVal uv;     /* upvalue */
    struct Proto p;      /* function prototype */
    union Closure cl;    /* closure (C or Tokudae) */
    struct OClass cls;   /* class */
    struct Instance ins; /* instance */
    struct IMethod im;   /* instance method */
    struct UMethod um;   /* userdata method */
    struct UserData u;   /* userdata */
    struct toku_State T; /* thread */
};

#define cast_gcu(o)     cast(union GCUnion *, (o))

#define gco2ht(o)       (&(cast_gcu(o)->tab))
#define gco2list(o)     (&(cast_gcu(o)->l))
#define gco2str(o)      (&(cast_gcu(o)->str))
#define gco2uv(o)       (&(cast_gcu(o)->uv))
#define gco2proto(o)    (&(cast_gcu(o)->p))
#define gco2clc(o)      (&((cast_gcu(o)->cl).c))
#define gco2clt(o)      (&((cast_gcu(o)->cl).t))
#define gco2cl(o)       (&(cast_gcu(o)->cl))
#define gco2cls(o)      (&(cast_gcu(o)->cls))
#define gco2ins(o)      (&(cast_gcu(o)->ins))
#define gco2im(o)       (&(cast_gcu(o)->im))
#define gco2um(o)       (&(cast_gcu(o)->um))
#define gco2u(o)        (&(cast_gcu(o)->u))
#define gco2th(o)       (&(cast_gcu(o)->T))

#define obj2gco(o)      (&(cast_gcu(o)->gc))


TOKUI_FUNC CallFrame *tokuT_newcf(toku_State *T);
TOKUI_FUNC void tokuT_shrinkstack(toku_State *T);
TOKUI_FUNC void tokuT_incCstack(toku_State *T);
TOKUI_FUNC void tokuT_checkCstack(toku_State *T);
TOKUI_FUNC int32_t tokuT_resetthread(toku_State *T, int32_t status);
TOKUI_FUNC void tokuT_warning(toku_State *T, const char *msg, int32_t cont);
TOKUI_FUNC void tokuT_warnerror(toku_State *T, const char *where);
TOKUI_FUNC void tokuT_free(toku_State *T, toku_State *thread);

#endif
