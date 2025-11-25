/*
** tokudae.h
** Tokudae - A Scripting Language derived from Lua and C
** See Copyright Notice at the end of this file
*/

#ifndef tokudae_h
#define tokudae_h

#include <stddef.h>
#include <stdarg.h>


#include "tokudaeconf.h"

/* {{Constants============================================================ */

#define TOKU_VERSION_MAJOR_N        1
#define TOKU_VERSION_MINOR_N        0
#define TOKU_VERSION_RELEASE_N      0

#define TOKU_VERSION_NUM  (TOKU_VERSION_MAJOR_N * 100 + TOKU_VERSION_MINOR_N)
#define TOKU_VERSION_RELEASE_NUM \
        (TOKU_VERSION_NUM * 100 + TOKU_VERSION_RELEASE_N)


/* mark for precompiled code ('<esc>Tokudae') */
#define TOKU_SIGNATURE      "\x1bTokudae"


/* option for multiple returns in 'toku_[p]call[k]' */
#define TOKU_MULTRET        (-1)


/*
** Pseudo-indices
** (-TOKU_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
#define TOKU_CLIST_INDEX        (-TOKU_MAXSTACK - 1000)
#define TOKU_CTABLE_INDEX       (TOKU_CLIST_INDEX - 1)
#define toku_upvalueindex(i)    (TOKU_CTABLE_INDEX - 1 - (i))


/* predefined values in the C list */
#define TOKU_CLIST_MAINTHREAD   0
#define TOKU_CLIST_GLOBALS      1
#define TOKU_CLIST_LAST         TOKU_CLIST_GLOBALS
    

/* thread status */
#define TOKU_STATUS_OK          0 /* ok */
#define TOKU_STATUS_YIELD       1 /* thread is suspended */
#define TOKU_STATUS_ERUN        2 /* runtime error */
#define TOKU_STATUS_ESYNTAX     3 /* syntax (compile-time) error */
#define TOKU_STATUS_EMEM        4 /* memory related error */
#define TOKU_STATUS_EERR        5 /* error while handling error */
#define TOKU_STATUS_NUM         6 /* total number of status codes */


/* types of values */
#define TOKU_T_NONE             (-1)
#define TOKU_T_NIL              0   /* nil */
#define TOKU_T_BOOL             1   /* boolean */
#define TOKU_T_NUMBER           2   /* number */
#define TOKU_T_USERDATA         3   /* userdata */
#define TOKU_T_LIGHTUSERDATA    4   /* light userdata */
#define TOKU_T_STRING           5   /* string */
#define TOKU_T_LIST             6   /* list */
#define TOKU_T_TABLE            7   /* table */
#define TOKU_T_FUNCTION         8   /* function */
#define TOKU_T_BMETHOD          9   /* bound method */
#define TOKU_T_CLASS            10  /* class */
#define TOKU_T_INSTANCE         11  /* instance */
#define TOKU_T_THREAD           12  /* thread */
#define TOKU_T_NUM              13  /* total number of types */


/* minimum stack space available to a C function */
#define TOKU_MINSTACK       20

/* (Other constants are defined right above the functions that use them.) */

/* }{Types================================================================ */

/* Tokudae thread state */
typedef struct toku_State toku_State;


/* type for integers */
typedef TOKU_INTEGER toku_Integer;

/* type for unsigned integers */
typedef TOKU_UNSIGNED toku_Unsigned;

/* type for floating point numbers */
typedef TOKU_NUMBER toku_Number;

/* type for continuation-function contexts */
typedef TOKU_KCONTEXT toku_KContext;


/* type of C function registered with Tokudae */
typedef int32_t (*toku_CFunction)(toku_State *T);

/* type of function for continuations */
typedef int32_t (*toku_KFunction)(toku_State *T, int status, toku_KContext cx);

/* type of function that de/allocates memory */
typedef void *(*toku_Alloc)(void *ptr, void *ud, size_t osz, size_t nsz);

/* type of warning function */
typedef void (*toku_WarnFunction)(void *ud, const char *msg, int32_t tocont);

/* type of function that reads or Tokudae (compiled) chunks */
typedef const char *(*toku_Reader)(toku_State *T, void *data, size_t *szread);

/* type of function for writing precompiled chunks */
typedef int32_t (*toku_Writer)(toku_State *T, const void *b, size_t sz,
                                                             void *data);


/* type for general debug API */
typedef struct toku_Debug toku_Debug;

/* type of function to be called by the debugger in specific events */
typedef void (*toku_Hook)(toku_State *T, toku_Debug *ar);

/* type for storing opcode information */
typedef struct toku_Opcode toku_Opcode;

/* type for storing description of the opcode */
typedef struct toku_Opdesc toku_Opdesc;

/* type for storing pre-compiled Tokudae function information */
typedef struct toku_Cinfo toku_Cinfo;

/* }{State manipulation=================================================== */

TOKU_API toku_State *toku_newstate(toku_Alloc alloc, void *ud, uint32_t seed); 
TOKU_API void        toku_close(toku_State *T);
TOKU_API toku_State *toku_newthread(toku_State *T);
TOKU_API int32_t     toku_closethread(toku_State *T, toku_State *from);
TOKU_API toku_CFunction toku_atpanic(toku_State *T, toku_CFunction fn);
TOKU_API void        toku_setallocf(toku_State *T, toku_Alloc alloc, void *ud); 
TOKU_API toku_Alloc  toku_getallocf(toku_State *T, void **ud); 

/* }{Stack manipulation=================================================== */

TOKU_API void    toku_setntop(toku_State *T, int32_t n); 
TOKU_API int32_t toku_gettop(const toku_State *T); 
TOKU_API int32_t toku_absindex(toku_State *T, int32_t idx); 
TOKU_API void    toku_rotate(toku_State *T, int32_t idx, int32_t n); 
TOKU_API void    toku_copy(toku_State *T, int32_t src, int32_t dest); 
TOKU_API int32_t toku_checkstack(toku_State *T, int32_t n); 
TOKU_API void    toku_push(toku_State *T, int32_t idx); 
TOKU_API void    toku_xmove(toku_State *src, toku_State *dest, int32_t n); 

/* }{Access functions (Stack -> C)======================================== */

TOKU_API int32_t     toku_is_number(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_is_integer(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_is_string(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_is_cfunction(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_is_udatamethod(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_is_userdata(toku_State *T, int32_t idx); 
TOKU_API int32_t     toku_type(toku_State *T, int32_t idx); 
TOKU_API const char *toku_typename(toku_State *T, int32_t type); 

TOKU_API toku_Number  toku_to_numberx(toku_State *T, int32_t idx,
                                                     int32_t *isnum); 
TOKU_API toku_Integer toku_to_integerx(toku_State *T, int32_t idx,
                                                      int32_t *isnum); 
TOKU_API int32_t     toku_to_bool(toku_State *T, int32_t idx); 
TOKU_API const char *toku_to_lstring(toku_State *T, int32_t idx, size_t *len); 
TOKU_API toku_CFunction toku_to_cfunction(toku_State *T, int32_t idx); 
TOKU_API void       *toku_to_userdata(toku_State *T, int32_t idx); 
TOKU_API const void *toku_to_pointer(toku_State *T, int32_t idx); 
TOKU_API toku_State *toku_to_thread(toku_State *T, int32_t idx); 

/* }{Ordering & Arithmetic functions====================================== */

/* Arithmetic operations */
#define TOKU_OP_ADD         0
#define TOKU_OP_SUB         1
#define TOKU_OP_MUL         2
#define TOKU_OP_DIV         3
#define TOKU_OP_IDIV        4
#define TOKU_OP_MOD         5
#define TOKU_OP_POW         6
#define TOKU_OP_BSHL        7
#define TOKU_OP_BSHR        8
#define TOKU_OP_BAND        9
#define TOKU_OP_BOR         10
#define TOKU_OP_BXOR        11
#define TOKU_OP_UNM         12
#define TOKU_OP_BNOT        13
#define TOKU_OP_NUM         14

TOKU_API void toku_arith(toku_State *T, int32_t op); 

/* order operations */
#define TOKU_ORD_EQ         0
#define TOKU_ORD_LT         1
#define TOKU_ORD_LE         2
#define TOKU_ORD_NUM        3

TOKU_API int32_t toku_compare(toku_State *T, int32_t idx1, int32_t idx2,
                              int32_t op); 
TOKU_API int32_t toku_rawequal(toku_State *T, int32_t idx1, int32_t idx2); 

/* }{Push functions (C -> Stack)========================================== */

TOKU_API void        toku_push_nil(toku_State *T); 
TOKU_API void        toku_push_number(toku_State *T, toku_Number n); 
TOKU_API void        toku_push_integer(toku_State *T, toku_Integer n); 
TOKU_API const char *toku_push_lstring(toku_State *T, const char *s, size_t l); 
TOKU_API const char *toku_push_string(toku_State *T, const char *s); 
TOKU_API const char *toku_push_fstring(toku_State *T, const char *f, ...); 
TOKU_API const char *toku_push_vfstring(toku_State *T, const char *f,
                                                       va_list ap); 
TOKU_API void toku_push_cclosure(toku_State *T, toku_CFunction f, int32_t nup); 
TOKU_API void    toku_push_bool(toku_State *T, int32_t b); 
TOKU_API void    toku_push_lightuserdata(toku_State *T, void *p); 
TOKU_API void   *toku_push_userdata(toku_State *T, size_t sz, uint16_t nuv); 
TOKU_API void    toku_push_list(toku_State *T, int32_t sz);
TOKU_API void    toku_push_table(toku_State *T, int32_t sz);
TOKU_API int32_t toku_push_thread(toku_State *T); 
TOKU_API void    toku_push_class(toku_State *T);
TOKU_API void    toku_push_instance(toku_State *T, int32_t idx);
TOKU_API void    toku_push_boundmethod(toku_State *T, int32_t idx);

/* }{Get functions (Tokudae -> Stack)===================================== */

TOKU_API int32_t toku_get(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_raw(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_global_str(toku_State *T, const char *name); 
TOKU_API int32_t toku_get_index(toku_State *T, int32_t idx, toku_Integer i);
TOKU_API int32_t toku_get_cindex(toku_State *T, toku_Integer i); 
TOKU_API int32_t toku_get_field(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_field_str(toku_State *T, int32_t idx, const char *s); 
TOKU_API int32_t toku_get_field_int(toku_State *T, int32_t idx, toku_Integer i);
TOKU_API int32_t toku_get_cfield_str(toku_State *T, const char *s); 
TOKU_API int32_t toku_get_class(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_superclass(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_method(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_self(toku_State *T, int32_t idx);
TOKU_API int32_t toku_get_uservalue(toku_State *T, int32_t idx, uint16_t n); 
TOKU_API int32_t toku_get_methodtable(toku_State *T, int32_t idx); 
TOKU_API int32_t toku_get_metatable(toku_State *T, int32_t idx);
TOKU_API void    toku_get_fieldtable(toku_State *T, int32_t idx); 

/* }{Set functions (Stack -> Tokudae)===================================== */

TOKU_API void toku_set(toku_State *T, int32_t idx); 
TOKU_API void toku_set_raw(toku_State *T, int32_t idx); 
TOKU_API void toku_set_global_str(toku_State *T, const char *name); 
TOKU_API void toku_set_index(toku_State *T, int32_t idx, toku_Integer i);
TOKU_API void toku_set_cindex(toku_State *T, toku_Integer i);
TOKU_API void toku_set_field(toku_State *T, int32_t idx); 
TOKU_API void toku_set_field_str(toku_State *T, int32_t idx, const char *s); 
TOKU_API void toku_set_field_int(toku_State *T, int32_t idx, toku_Integer i); 
TOKU_API void toku_set_cfield_str(toku_State *T, const char *s);
TOKU_API void toku_set_superclass(toku_State *T, int32_t idx); 
TOKU_API void toku_set_metatable(toku_State *T, int32_t idx);
TOKU_API int32_t toku_set_uservalue(toku_State *T, int32_t idx, uint16_t n); 
TOKU_API void toku_set_methodtable(toku_State *T, int32_t idx); 
TOKU_API void toku_set_fieldtable(toku_State *T, int32_t idx);

/* }{Call/Load/Combine/Dump Tokudae chunks================================ */

// TODO: add docs
TOKU_API void toku_callk(toku_State *T, int32_t nargs, int32_t nresults,
                         toku_KContext cx, toku_KFunction k); 
#define toku_call(T,n,r)        toku_callk(T, (n), (r), 0, NULL)  

// TODO: add docs
TOKU_API int32_t toku_pcallk(toku_State *T, int32_t nargs, int32_t nresults,
                             int32_t msgh, toku_KContext cx, toku_KFunction k);
#define toku_pcall(T,n,r,f)     toku_pcallk(T, (n), (r), (f), 0, NULL)

TOKU_API int32_t toku_load(toku_State *T, toku_Reader freader, void *userdata,
                           const char *chunkname, const char *mode); 
TOKU_API int32_t toku_combine(toku_State *T, const char *chunkname, int32_t n);
TOKU_API int32_t toku_dump(toku_State *T, toku_Writer fw, void *data,
                                                          int32_t strip);

/* }{Coroutine functions================================================== */

// TODO: add docs + implement all of these (except 'toku_status')
TOKU_API int32_t toku_yieldk(toku_State *T, int32_t nresults,
                             toku_KContext cx, toku_KFunction k);
TOKU_API int32_t toku_resume(toku_State *T, toku_State *from, int32_t narg,
                             int32_t *nres);
// TODO: update docs
TOKU_API int32_t toku_status(toku_State *T);
TOKU_API int32_t toku_isyieldable(toku_State *T);

#define toku_yield(T,n)	    toku_yieldk(T, (n), 0, NULL)

/* }{Garbage collector API================================================ */

/* GC options (what) */
#define TOKU_GC_STOP            0 /* stop GC */
#define TOKU_GC_RESTART         1 /* restart GC (start if stopped) */
#define TOKU_GC_CHECK           2 /* check and clear GC collection flag */
#define TOKU_GC_COLLECT         3 /* perform full GC cycle */
#define TOKU_GC_COUNT           4 /* get number of bytes_allocated/1024 */
#define TOKU_GC_COUNTBYTES      5 /* get remainder of bytes_allocated/1024 */
#define TOKU_GC_STEP            6 /* performs incremental GC step */
#define TOKU_GC_PARAM           7 /* set or get GC parameter */
#define TOKU_GC_ISRUNNING       8 /* test whether GC is running */

/* incremental GC parameters */
#define TOKU_GCP_PAUSE          0 /* size of GC "pause" */
#define TOKU_GCP_STEPMUL        1 /* GC "speed" */
#define TOKU_GCP_STEPSIZE       2 /* GC "granularity" */
#define TOKU_GCP_NUM            3 /* number of parameters */

TOKU_API int32_t toku_gc(toku_State *T, int32_t what, ...); 

/* }{Warning-related functions============================================ */

TOKU_API void toku_setwarnf(toku_State *T, toku_WarnFunction fwarn, void *ud); 
TOKU_API void toku_warning(toku_State *T, const char *msg, int32_t cont); 

/* }{Miscellaneous functions and useful macros============================ */

#define TOKU_N2SBUFFSZ     64
TOKU_API uint32_t toku_numbertocstring(toku_State *T, int32_t idx, char *buff); 

TOKU_API size_t toku_stringtonumber(toku_State *T, const char *s, int32_t *f); 

TOKU_API toku_Number    toku_version(toku_State *T);
TOKU_API int32_t        toku_error(toku_State *T); 
TOKU_API toku_Unsigned  toku_len(toku_State *T, int32_t idx); 
TOKU_API size_t         toku_lenudata(toku_State *T, int32_t idx);
TOKU_API int32_t        toku_nextfield(toku_State *T, int32_t idx); 
TOKU_API void           toku_concat(toku_State *T, int32_t n); 
TOKU_API void           toku_toclose(toku_State *T, int32_t idx); 
TOKU_API void           toku_closeslot(toku_State *T, int32_t idx); 
TOKU_API int32_t        toku_shrinklist(toku_State *T, int32_t idx);
TOKU_API uint16_t       toku_numuservalues(toku_State *T, int32_t idx);

#define toku_is_function(T, n)      (toku_type(T, (n)) == TOKU_T_FUNCTION)
#define toku_is_boundmethod(T, n)   (toku_type(T, (n)) == TOKU_T_BMETHOD)
#define toku_is_list(T, n)          (toku_type(T, (n)) == TOKU_T_LIST)
#define toku_is_table(T, n)         (toku_type(T, (n)) == TOKU_T_TABLE)
#define toku_is_class(T, n)         (toku_type(T, (n)) == TOKU_T_CLASS)
#define toku_is_instance(T, n)      (toku_type(T, (n)) == TOKU_T_INSTANCE)
#define toku_is_lightuserdata(T, n) (toku_type(T, (n)) == TOKU_T_LIGHTUSERDATA)
#define toku_is_fulluserdata(T, n)  (toku_type(T, (n)) == TOKU_T_USERDATA)
#define toku_is_nil(T, n)           (toku_type(T, (n)) == TOKU_T_NIL)
#define toku_is_bool(T, n)          (toku_type(T, (n)) == TOKU_T_BOOL)
#define toku_is_thread(T, n)        (toku_type(T, (n)) == TOKU_T_THREAD)
#define toku_is_none(T, n)          (toku_type(T, (n)) == TOKU_T_NONE)
#define toku_is_noneornil(T, n)     (toku_type(T, (n)) <= 0)

#define toku_to_string(T, i)        toku_to_lstring(T, i, NULL)
#define toku_to_number(T,i)         toku_to_numberx(T,(i),NULL)
#define toku_to_integer(T,i)        toku_to_integerx(T,(i),NULL)

#define toku_push_mainthread(T) \
        ((void)toku_get_cindex(T, TOKU_CLIST_MAINTHREAD))

#define toku_push_globaltable(T) \
        ((void)toku_get_cindex(T, TOKU_CLIST_GLOBALS))

#define toku_push_clist(T)        toku_push(T, TOKU_CLIST_INDEX)
#define toku_push_ctable(T)       toku_push(T, TOKU_CTABLE_INDEX)

#define toku_push_literal(T, s)   toku_push_string(T, "" s)
#define toku_push_cfunction(T,f)  toku_push_cclosure(T,f,0)

#define toku_register(T,n,f) \
        (toku_push_cfunction(T,(f)), toku_set_global_str(T,(n)))

#define toku_insert(T,idx)      toku_rotate(T, (idx), 1)
#define toku_pop(T,n)           toku_setntop(T, -(n)-1)
#define toku_remove(T,idx)      (toku_rotate(T, (idx), -1), toku_pop(T, 1))
#define toku_replace(T,idx)     (toku_copy(T, -1, (idx)), toku_pop(T, 1))

#define toku_getextraspace(T)   ((void *)((char *)(T) - TOKU_EXTRASPACE))

#define toku_getntop(T)         (toku_gettop(T) + 1)

/* }{Debug API============================================================ */

/* Event codes */
#define TOKU_HOOK_CALL      0
#define TOKU_HOOK_RET       1
#define TOKU_HOOK_LINE      2
#define TOKU_HOOK_COUNT     3
#define TOKU_HOOK_TAILCALL  4

/* Event masks */
#define TOKU_MASK_CALL      (1 << TOKU_HOOK_CALL)
#define TOKU_MASK_RET       (1 << TOKU_HOOK_RET)
#define TOKU_MASK_LINE      (1 << TOKU_HOOK_LINE)
#define TOKU_MASK_COUNT     (1 << TOKU_HOOK_COUNT)
/* TOKU_MASK_CALL is also for tail calls */

TOKU_API int32_t toku_getstack(toku_State *T, int32_t level, toku_Debug *ar); 
TOKU_API int32_t toku_getinfo(toku_State *T, const char *what, toku_Debug *ar); 
TOKU_API const char *toku_getlocal(toku_State *T, const toku_Debug *ar,
                                                  int32_t n); 
TOKU_API const char *toku_setlocal(toku_State *T, const toku_Debug *ar,
                                                  int32_t n); 
TOKU_API const char *toku_getupvalue(toku_State *T, int32_t idx, int32_t n); 
TOKU_API const char *toku_setupvalue(toku_State *T, int32_t idx, int32_t n); 
TOKU_API void       *toku_upvalueid(toku_State *T, int32_t idx, int32_t n);
TOKU_API void        toku_upvaluejoin(toku_State *T, int32_t i1, int32_t n1,
                                                     int32_t i2, int32_t n2);
TOKU_API void        toku_sethook(toku_State *T, toku_Hook fh, int32_t mask,
                                                               int32_t count);
TOKU_API toku_Hook   toku_gethook(toku_State *T);
TOKU_API int32_t     toku_gethookmask(toku_State *T);
TOKU_API int32_t     toku_gethookcount(toku_State *T);
TOKU_API int32_t     toku_stackinuse(toku_State *T);

struct toku_Debug {
    int32_t event;
    const char *name;           /* (n) */
    const char *namewhat;       /* (n) */
    const char *what;           /* (s) */
    const char *source;         /* (s) */
    size_t srclen;              /* (s) */
    int32_t defline;            /* (s) */
    int32_t lastdefline;        /* (s) */
    int32_t currline;           /* (l) */
    int32_t ftransfer;          /* (r) */
    int32_t ntransfer;          /* (r) */
    int32_t nupvals;            /* (u) */
    int32_t nparams;            /* (u) */
    int32_t extraargs;          /* (t) */
    uint8_t istailcall;         /* (t) */
    uint8_t isvararg;           /* (u) */
    char shortsrc[TOKU_IDSIZE]; /* (s) */
    /* private part */
    struct CallFrame *cf;
};

TOKU_API int32_t toku_getopcode(toku_State *T, const toku_Cinfo *ci,
                                int32_t n, toku_Opcode *opc);
TOKU_API void    toku_getopcodeoff(toku_State *T, const toku_Cinfo *ci,
                                   int32_t offset, toku_Opcode *opc);
TOKU_API int32_t toku_getopcode_next(toku_State *T, toku_Opcode *opc);

struct toku_Opcode {
    int32_t args[TOKU_OPARGS];
    int32_t line;
    int32_t offset;
    int32_t op;
    char name[TOKU_OPCNAMESIZE];
    /* private part */
    struct Proto *f;
};

TOKU_API void toku_getopdesc(toku_State *T, toku_Opdesc *opd,
                                            const toku_Opcode *opc);

struct toku_Opdesc {
    struct {
        int32_t type;
        union {
            int32_t b;
            toku_Number n;
            const char *s;
        } value;
    } extra;
    char desc[TOKU_OPCDESCSIZE];
};

TOKU_API int32_t toku_getcompinfo(toku_State *T, int32_t idx, toku_Cinfo *ci);
TOKU_API int32_t toku_getconstant(toku_State *T, const toku_Cinfo *ci,
                                                 int32_t n);
TOKU_API void toku_getshortsrc(toku_State *T, const toku_Cinfo *ci, char *out);
TOKU_API const char *toku_getlocalinfo(toku_State *T, toku_Cinfo *ci,
                                                      int32_t n);
TOKU_API const char *toku_getupvalueinfo(toku_State *T, toku_Cinfo *ci,
                                                        int32_t n);
TOKU_API int32_t toku_getfunction(toku_State *T, const toku_Cinfo *src,
                                  toku_Cinfo *dest, int32_t n);

struct toku_Cinfo {
    const void *func;
    const void *constants;
    const void *functions;
    const void *code;
    const char *source;
    size_t srclen;
    union {
        struct {
            int32_t startoff;
            int32_t endoff;
        } l;
        struct {
            int32_t instack;
            int32_t idx;
        } u;
    } info;
    int32_t defline;
    int32_t lastdefline;
    int32_t nupvals;
    int32_t nlocals;
    int32_t nparams;
    int32_t nconstants;
    int32_t nfunctions;
    int32_t ncode;
    int32_t nslots;
    uint8_t isvararg;
    /* private part */
    struct Proto *f;
};

/* }{Copyright============================================================ */

#define TOKUI_TOSTR_AUX(x)      #x
#define TOKUI_TOSTR(x)          TOKUI_TOSTR_AUX(x)

#define TOKU_VERSION_MAJOR      TOKUI_TOSTR(TOKU_VERSION_MAJOR_N)
#define TOKU_VERSION_MINOR      TOKUI_TOSTR(TOKU_VERSION_MINOR_N)
#define TOKU_VERSION_RELEASE    TOKUI_TOSTR(TOKU_VERSION_RELEASE_N)

#define TOKU_VERSION    "Tokudae " TOKU_VERSION_MAJOR "." TOKU_VERSION_MINOR
#define TOKU_RELEASE    TOKU_VERSION "." TOKU_VERSION_RELEASE

#define TOKU_COPYRIGHT "Copyright (C) 1994-2024 Lua.org, PUC-Rio\n" \
        TOKU_RELEASE " Copyright (C) 2024-2025 Jure Bagić"


 /*--------------------------------,
| Big Thank You to Lua Developers! |
\________________________________*/

/* =======================================================================
** Copyright (C) 1994-2024 Lua.org, PUC-Rio.
** Copyright (C) 2024-2025 Jure Bagić
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
** ======================================================================= */

/* }}EOF================================================================== */

#endif
