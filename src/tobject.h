/*
** tobject.h
** Types definitions for Tokudae objects
** See Copyright Notice in tokudae.h
*/

#ifndef tobject_h
#define tobject_h


#include "tokudae.h"
#include "tokudaelimits.h"


/*
 * Additional types that are used only internally
 * or at markers.
 */
#define TOKU_TUPVALUE       TOKU_T_NUM        /* upvalue */
#define TOKU_TPROTO         (TOKU_T_NUM + 1)  /* function prototype */
#define TOKU_TDEADKEY       (TOKU_T_NUM + 2)  /* mark for dead table keys */


/* 
** Number of all types ('TOKU_T_*')
** (including 'TOKU_T_NONE' but excluding DEADKEY).
*/
#define TOKUI_TOTALTYPES    (TOKU_TPROTO + 2)


/*
** Tagged value types.
** Bits 0-3 are for value types (TOKU_T*).
** Bits 4-6 are for variant types (TOKU_V*).
** Bit 7 for collectable object tag.
*/

/* set variant bytes for type 't' */
#define makevariant(t, v)       ((t) | ((v) << 4))


/* Tokudae valuet */
typedef union Value {
    struct GCObject *gc; /* collectable value */
    void *p; /* light userdata */
    int32_t b; /* boolean */
    toku_Integer i; /* integer */
    toku_Number n; /* float */
    toku_CFunction cfn; /* T function */
} Value;


/* 'TValue' fields, defined for reuse and alignment purposes */
#define TValueFields    Value val; uint8_t tt


/* 'Value' with type */
typedef struct TValue {
    TValueFields;
} TValue;


#define val(o)      ((o)->val)


/* raw type tag of a TValue */
#define rawtt(o)            ((o)->tt)

/* tag with no variant (bitt 0-3) */
#define novariant(t)        ((t) & 0x0F)

/* type tag of a TValue (bitt 0-3 for tags + variant bits 4-6) */
#define withvariant(t)      ((t) & 0x7F)
#define ttypetag(o)         withvariant((o)->tt)

/* type of a TValue */
#define ttype(o)            novariant((o)->tt)


/* Macros to test type */
#define checktag(o,t)       (rawtt(o) == (t))
#define checktype(o,t)      (ttype(o) == (t))


/* Macros for internal tests */

/* collectable object hat the same tag as the original value */
#define righttt(obj)        (ttypetag(obj) == gcoval(obj)->tt_)

/*
** Any value being manipulated by the program either it non
** collectable, or the collectable object hat the right tag
** and it it not dead. The option 'T == NULL' allows other
** macros using this one to be used where T is not available.
*/
#define checkliveness(T,obj) \
        (UNUSED(T), toku_assert(!iscollectable(obj) || \
        (righttt(obj) && (T == NULL || !isdead(G(T), gcoval(obj))))))


/* Macros to set values */

/* set a value's tag */
#define settt(o,t)      (rawtt(o)=(t))

/* macro for copying valuet (from 'obj2' to 'obj1') */
#define setobj(T,obj1,obj2) \
    { TValue *o1_=(obj1); const TValue *o2_=(obj2); \
      o1_->val = o2_->val; settt(o1_, o2_->tt); \
      checkliveness(T,o1_); }

/* copy object from ttack to stack */
#define setobjs2s(T,o1,o2)      setobj(T,s2v(o1),s2v(o2))
/* copy object to ttack */
#define setobj2s(T,o1,o2)       setobj(T,s2v(o1),o2)


/*
** Entries in Tokudae stack.
** 'tbc' list contains 'delta' field which represents offset from the current
** stack value to the next value on the stack that needs to-be-closed.
** 'delta' being 0 indicates that the distance value doesn't fit in 'delta'
** and then it is assumed that the actual value is MAXDELTA. 
** This way we can represent larger distances without using larger data type.
** Note: On 8-byte alignment 'SValue' should be 16 
** bytes, while on 4-byte alignment 8 bytes.
*/
typedef union {
    TValue val_;
    struct {
        TValueFields;
        uint16_t delta;
    } tbc;
} SValue;


/* convert 'SValue' to a 'TValue' */
#define s2v(s)      (&(s)->val_)


/* pointer to the value on the stack */
typedef SValue *SPtr;


/*
** Represents index into the stack.
** Before reallocation occurs 'offset' is filled accordingly in
** case 'p' becomes invalid, and then after reallocation 'p' it restored.
*/
typedef struct {
    SPtr p; /* pointer to the value on the stack */
    ptrdiff_t offset; /* used when stack is being reallocated */
} SIndex;



/* -====================================================================
** Collectable Objects {
** ===================================================================== */

/* common header for objects */
#define ObjectHeader    struct GCObject* next; uint8_t tt_; uint8_t mark


/* common type for collectable objects */
typedef struct GCObject {
    ObjectHeader;
} GCObject;


#define HEADEROFFSET    (offsetof(GCObject, mark) + sizeof(uint8_t))

#define objzero(o,sz) \
        memset(cast_charp(o) + HEADEROFFSET, 0, sz - HEADEROFFSET)


/* bit for collectable types */
#define BIT_COLLECTABLE     (1 << 7)

#define iscollectable(o)    (rawtt(o) & BIT_COLLECTABLE)

/* mark a tag at collectable */
#define ctb(tt)             ((tt) | BIT_COLLECTABLE)

#define gcoval(o)           check_exp(iscollectable(o), val(o).gc)

#define setgcoval(T,obj,x) \
    { TValue *o_=(obj); GCObject *x_=(x); \
      val(o_).gc = x_; settt(o_, ctb(x_->tt_)); }

/* }==================================================================== */



/* ======================================================================
** Boolean {
** ====================================================================== */

#define TOKU_VFALSE         makevariant(TOKU_T_BOOL, 0) /* false bool */
#define TOKU_VTRUE          makevariant(TOKU_T_BOOL, 1) /* true bool */

#define ttisbool(o)         checktype(o, TOKU_T_BOOL)
#define ttistrue(o)         checktag(o, TOKU_VTRUE)
#define ttisfalse(o)        checktag(o, TOKU_VFALSE)

#define t_isfalse(o)        (ttisfalse(o) || ttisnil(o))

#define setbfval(o)         settt(o, TOKU_VFALSE)
#define setbtval(o)         settt(o, TOKU_VTRUE)

/* }==================================================================== */



/* =====================================================================
** Numbers {
** ===================================================================== */

#define TOKU_VNUMFLT    makevariant(TOKU_T_NUMBER, 0) /* float numberts */
#define TOKU_VNUMINT    makevariant(TOKU_T_NUMBER, 1) /* integer numbers */

#define ttisnum(o)      checktype(o, TOKU_T_NUMBER)
#define ttisflt(o)      checktag(o, TOKU_VNUMFLT)
#define ttisint(o)      checktag(o, TOKU_VNUMINT)

#define nval(o)         check_exp(ttisnum(o), \
                                  ttisint(o) ? cast_num(ival(o)) : fval(o))
#define ival(o)         check_exp(ttisint(o), val(o).i)
#define fval(o)         check_exp(ttisflt(o), val(o).n)

#define setival(obj,x) \
    { TValue *o_=(obj); val(o_).i = (x); settt(o_, TOKU_VNUMINT); }

#define setfval(obj,x) \
    { TValue *o_=(obj); val(o_).n = (x); settt(o_, TOKU_VNUMFLT); }

/* }==================================================================== */



/* ======================================================================
** List {
** ====================================================================== */

#define TOKU_VLIST        makevariant(TOKU_T_LIST, 0)

#define ttislist(o)     checktag((o), ctb(TOKU_VLIST))

#define listval(o)      gco2list(val(o).gc)

#define setlistval(T,obj,x) \
    { TValue *o_=(obj); const List *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VLIST)); \
      checkliveness(T, o_); }

#define setlistval2s(T,o,l)     setlistval(T,s2v(o),l)

typedef struct List {
    ObjectHeader;
    GCObject *gclist;
    TValue *arr; /* memory */
    int32_t len; /* cached lenght of the list */
    int32_t size; /* size of the array 'arr' (capacity) */
} List;

/* }==================================================================== */



/* ======================================================================
** Nil {
** ====================================================================== */

/* standard nil */
#define TOKU_VNIL           makevariant(TOKU_T_NIL, 0)

/* empty table slot */
#define TOKU_VEMPTY         makevariant(TOKU_T_NIL, 1)

/* value returned for a key not found in a table (absent key) */
#define TOKU_VABSTKEY       makevariant(TOKU_T_NIL, 2)

#define setnilval(o)        settt(o, TOKU_VNIL)
#define setemptyval(o)      settt(o, TOKU_VEMPTY)

#define ttisnil(o)          checktype((o), TOKU_T_NIL)

/*
** Macro to test the result of a table access. Formally, it should
** distinguish between TOKU_VEMPTY/TOKU_VABSTKEY and other tags.
** As currently nil is equivalent to LUA_VEMPTY, it is simpler to
** just test whether the value is nil.
*/
#define tagisempty(tag)         (novariant(tag) == TOKU_T_NIL)

/*
** By default, entries with any kind of nil are considered empty.
** (In any definition, values associated with absent keys must also
** be accepted as empty.)
*/
#define isempty(o)          ttisnil(o)

#define isabstkey(v)        checktag((v), TOKU_VABSTKEY)

#define ABSTKEYCONSTANT     {NULL}, TOKU_VABSTKEY

/* }===================================================================== */



/* =======================================================================
** Thread (toku_State) {
** ======================================================================= */

#define TOKU_VTHREAD    makevariant(TOKU_T_THREAD, 0)

#define ttisthread(o)   checktag(o, ctb(TOKU_VTHREAD))

#define thval(o)        check_exp(ttisthread(o), gco2th(val(o).gc))

#define setthval(T,obj,x) \
    { TValue *o_=(obj); const toku_State *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VTHREAD)); \
      checkliveness(T, o_); }

#define setthval2s(T,o,th)      setthval(T,s2v(o),th)

/* }===================================================================== */



/* ======================================================================
** Hashtable {
** ====================================================================== */

#define TOKU_VTABLE         makevariant(TOKU_T_TABLE, 0)

#define ttistable(o)        checktag((o), ctb(TOKU_VTABLE))

#define tval(o)     check_exp(ttistable(o), gco2ht(val(o).gc))

#define settval(T,obj,x) \
    { TValue *o_=(obj); const Table *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VTABLE)); \
      checkliveness(T, o_); }

#define settval2s(T,o,ht)   settval(T,s2v(o),ht)


/*
** Nodes for hashtables; two TValue's for key-value fields.
** 'next' field is to link the colliding entries.
** Ordering of fields might seem weird but this is to ensure optimal
** alignment in both 4-byte and 8-byte alignments.
*/
typedef union Node {
    struct NodeKey {
        TValueFields; /* fields for value */
        uint8_t key_tt; /* key type tag */
        int32_t next; /* offset for next node */
        Value key_val; /* key value */
    } s;
    TValue i_val; /* direct node value access as a proper 'TValue' */
} Node;


/* copy a value into a key */
#define setnodekey(T,n,obj) \
    { Node *n_=(n); const TValue *obj_=(obj); \
      n_->s.key_val = obj_->val; n_->s.key_tt = obj_->tt; \
      checkliveness(T,obj_); }


/* copy a value from a key */
#define getnodekey(T,obj,n) \
    { TValue *obj_=(obj); const Node *n_=(n); \
      obj_->val = n_->s.key_val; obj_->tt = n_->s.key_tt; \
      checkliveness(T,obj_); }


typedef struct Table {
    ObjectHeader; /* internal only object */
    uint8_t flags; /* 1<<p means tagmethod(p) is not present */
    uint8_t size; /* log2 of array size */
    Node *node; /* memory block */
    Node *lastfree; /* any free position is before this position */
    GCObject *gclist;
} Table;


#define keytt(n)                ((n)->s.key_tt)
#define keyval(n)               ((n)->s.key_val)

#define keyival(n)              (keyval(n).i)
#define keyfval(n)              (keyval(n).n)
#define keypval(n)              (keyval(n).p)
#define keycfval(n)             (keyval(n).cfn)
#define keygcoval(n)            (keyval(n).gc)
#define keystrval(n)            (gco2str(keyval(n).gc))

#define keyiscollectable(n)     (keytt(n) & BIT_COLLECTABLE)
#define keyisnil(n)             (keytt(n) == TOKU_T_NIL)
#define keyisshrstr(n)          (keytt(n) == ctb(TOKU_VSHRSTR))
#define keyisint(n)             (keytt(n) == TOKU_VNUMINT)

#define setnilkey(n)            (keytt(n) = TOKU_T_NIL)

#define setdeadkey(node)        (keytt(node) = TOKU_TDEADKEY)
#define keyisdead(n)            (keytt(n) == TOKU_TDEADKEY)

/* }===================================================================== */



/* =======================================================================
** Strings {
** ======================================================================= */

#define TOKU_VSHRSTR    makevariant(TOKU_T_STRING, 0) /* short string */
#define TOKU_VLNGSTR    makevariant(TOKU_T_STRING, 1) /* long string */

#define ttisstring(o)       checktype((o), TOKU_T_STRING)
#define ttisshrstring(o)    checktag((o), ctb(TOKU_VSHRSTR))
#define ttislngstring(o)    checktag((o), ctb(TOKU_VLNGSTR))

#define strval(o)   check_exp(ttisstring(o), gco2str(val(o).gc))

#define setstrval(T,obj,x) \
    { TValue *o_=(obj); const OString *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(x_->tt_)); \
      checkliveness((T), o_); }

#define setstrval2s(T,o,s)      setstrval(T,s2v(o),s)


typedef struct OString {
    ObjectHeader;
    /* reserved words or tag names index for short strings;
     * flag for long strings indicating that it has hash */
    uint8_t extra;
    uint8_t shrlen; /* length for short strings, 0xFF for longs strings */
    uint32_t hash;
    union {
        size_t lnglen; /* length for long strings */
        struct OString *next; /* linked list for 'strtab' (hash table) */
    } u;
    char bytes[1]; /* string contents */
} OString;


#define strisshr(ts)    ((ts)->shrlen < 0xFF)


/*
** Get string bytes from 'OString'. (Both generic version and specialized
** versions for long and short strings.)
*/
#define getstr(os)      ((os)->bytes)
#define getlngstr(os)   check_exp((os)->shrlen == 0xFF, (os)->bytes)
#define getshrstr(os)   check_exp((os)->shrlen != 0xFF, (os)->bytes)

/* get string length from 'OString *s' */
#define getstrlen(s)    ((s)->shrlen != 0xFF ? (s)->shrlen : (s)->u.lnglen)

/* }===================================================================== */



/* =======================================================================
** Class {
** ======================================================================= */

#define TOKU_VCLASS     makevariant(TOKU_T_CLASS, 0)

#define ttisclass(o)    checktag(o, ctb(TOKU_VCLASS))

#define classval(o)     check_exp(ttisclass(o), gco2cls(val(o).gc))

#define setclsval(T,obj,x) \
    { TValue *o_=(obj); const OClass *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VCLASS)); \
      checkliveness(T, o_); }

#define setclsval2s(T,o,cls)    setclsval(T,s2v(o),cls)

typedef struct OClass {
    ObjectHeader;
    struct OClass *sclass;
    Table *metatable;
    Table *methods;
} OClass;

/* }===================================================================== */



/* =======================================================================
** Function Prototypes {
** ======================================================================= */

#define TOKU_VPROTO         makevariant(TOKU_TPROTO, 0)

/* 
** Information of the upvalues for function prototypes
*/
typedef struct UpValInfo {
    OString *name; /* upvalue name (debug) */
    int32_t idx; /* index in stack or outer function local var list */
    uint8_t instack; /* is it on stack */
    uint8_t kind;
} UpValInfo;


/* 
** Information of the local variable for function prototypes
** (used for debug information).
*/
typedef struct LVarInfo {
    OString *name; /* local name */
    int32_t startpc; /* point where variable is in scope */
    int32_t endpc; /* point where variable it out of scope */
} LVarInfo;


/*
** Associates the absolute line source for a given opcode ('pc').
** The array 'lineinfo' gives, for each opcode, the difference in
** lines from the previous opcode. When that difference does not
** fit into a byte, Tokudae saves the absolute line for that opcode.
** (Tokudae also saves the absolute line periodically, to speed up the
** computation of a line number: we can use binary search in the
** absolute-line array, but we must traverse the 'lineinfo' array
** linearly to compute a line.)
*/
typedef struct AbsLineInfo {
    int32_t pc;
    int32_t line;
} AbsLineInfo;


/*
** Function Prototypes.
*/
typedef struct Proto {
    ObjectHeader;
    uint8_t isvararg;       /* true if this function accepts extra params */
    int32_t defline;        /* function definition line (debug) */
    int32_t deflastline;    /* function definition last line (debug) */
    int32_t arity;          /* number of fixed (named) function parameters */
    int32_t maxstack;       /* max stack size for this function */
    int32_t sizecode;       /* size of 'code' */
    int32_t sizek;          /* size of 'k' */
    int32_t sizeupvals;     /* size of 'upvals' */
    int32_t sizep;          /* size of 'p' */
    int32_t sizelineinfo;   /* size of 'lineinfo' */
    int32_t sizeabslineinfo;/* size of 'abslineinfo' */
    int32_t sizeopcodepc;   /* size of 'opcodepc' */
    int32_t sizelocals;     /* size of 'locals' */
    uint8_t *code;          /* bytecode */
    TValue *k;              /* constant values */
    UpValInfo *upvals;      /* debug information for upvalues */
    struct Proto **p;       /* list of funcs defined inside of this function */
    /* debug information (can be stripped away when dumping) */
    OString *source;            /* source name */
    int8_t *lineinfo;           /* information about source lines */
    AbsLineInfo *abslineinfo;   /* idem */
    int32_t *opcodepc;          /* list of pc's for each opcode */
    LVarInfo *locals;           /* information about local variables */
    /* (for garbage collector) */
    GCObject *gclist;
} Proto;

/* }====================================================================== */



/* =======================================================================
**  Instance {
** ======================================================================= */

#define TOKU_VINSTANCE      makevariant(TOKU_T_INSTANCE, 0)

#define ttisinstance(o)     checktag(o, ctb(TOKU_VINSTANCE))

#define insval(o)       check_exp(ttisinstance(o), gco2ins(val(o).gc))

#define setinsval(T,obj,x) \
    { TValue *o_=(obj); const Instance *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VINSTANCE)); \
      checkliveness(T, o_); }

#define setinsval2s(T,o,ins)    setinsval(T,s2v(o),ins)

typedef struct Instance {
    ObjectHeader;
    OClass *oclass;
    Table *fields;
} Instance;

/* }===================================================================== */



/* =======================================================================
** Functions {
** ======================================================================= */

#define TOKU_VUPVALUE   makevariant(TOKU_TUPVALUE, 0)

#define TOKU_VTCL       makevariant(TOKU_T_FUNCTION, 0) /* Tokudae closure */
#define TOKU_VLCF       makevariant(TOKU_T_FUNCTION, 1) /* light C function */
#define TOKU_VCCL       makevariant(TOKU_T_FUNCTION, 2) /* C closure */

#define ttisfunction(o)     checktype(o, TOKU_T_FUNCTION)
#define ttisTclosure(o)     checktag(o, ctb(TOKU_VTCL))
#define ttislcf(o)          checktag(o, TOKU_VLCF)
#define ttisCclosure(o)     checktag(o, ctb(TOKU_VCCL))
#define ttisclosure(o)      (ttisTclosure(o) || ttisCclosure(o))

#define clval(o)        check_exp(ttisclosure(o), gco2cl(val(o).gc))
#define clTval(o)       check_exp(ttisTclosure(o), gco2clt(val(o).gc))
#define clCval(o)       check_exp(ttisCclosure(o), gco2clc(val(o).gc))
#define lcfval(o)       check_exp(ttislcf(o), val(o).cfn)

#define setclTval(T,obj,x) \
    { TValue *o_=(obj); const TClosure *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VTCL)); \
      checkliveness(T, o_); }

#define setclTval2s(T,o,cl)     setclTval(T,s2v(o),cl)

#define setcfval(T,obj,x) \
    { TValue *o_ = (obj); val(o_).cfn=(x); settt(o_, TOKU_VLCF); }

#define setclCval(T,obj,x) \
    { TValue *o_=(obj); const CClosure *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VCCL)); \
      checkliveness(T, o_); }

#define setclCval2s(T,o,cl)     setclCval(T,s2v(o),cl)


/* upvalues for Tokudae closures */
typedef struct UpVal {
    ObjectHeader;
    union {
        TValue *p; /* on stack or in 'u.value' */
        ptrdiff_t offset; /* when reallocating stack */
    } v;
    union {
        struct { /* valid when open */
            struct UpVal *next; /* linked list */
            struct UpVal **prev; /* (optimization) */
        } open;
        TValue value; /* value stored here when closed */
    } u;
} UpVal;


/* common closure header */
#define ClosureHeader   ObjectHeader; int32_t nupvals; GCObject *gclist


typedef struct TClosure {
    ClosureHeader;
    Proto *p;
    UpVal *upvals[1];
} TClosure;


typedef struct CClosure {
    ClosureHeader;
    toku_CFunction fn;
    TValue upvals[1];
} CClosure;


typedef union Closure {
    CClosure c;
    TClosure t;
} Closure;


#define getproto(o)     (clTval(o)->p)

/* }===================================================================== */



/* =======================================================================
** Bound methods {
** ======================================================================= */

#define TOKU_VIMETHOD   makevariant(TOKU_T_BMETHOD, 0) /* instance method */
#define TOKU_VUMETHOD   makevariant(TOKU_T_BMETHOD, 1) /* userdata method */

#define ttisinstancemethod(o)   checktag(o, ctb(TOKU_VIMETHOD))
#define ttisusermethod(o)       checktag(o, ctb(TOKU_VUMETHOD))

#define imval(o)    check_exp(ttisinstancemethod(o), gco2im(val(o).gc))
#define umval(o)    check_exp(ttisusermethod(o), gco2um(val(o).gc))

#define setimval(T,obj,x) \
    { TValue *o_=(obj); const IMethod *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VIMETHOD)); \
      checkliveness(T, o_); }

#define setimval2s(T,o,im)      setimval(T,s2v(o),im)

#define setumval(T,obj,x) \
    { TValue *o_=(obj); const UMethod *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VUMETHOD)); \
      checkliveness(T, o_); }

#define setumval2s(T,o,um)      setumval(T,s2v(o),um)


/* common bound method header */
#define MethodHeader    ObjectHeader; TValue method   


/* method bound to Instance */
typedef struct IMethod {
    MethodHeader;
    Instance *ins;
} IMethod;


/* method bound to UserData */
typedef struct UMethod {
    MethodHeader;
    struct UserData *ud;
} UMethod;

/* }==================================================================== */


/* ======================================================================
** Userdata {
** ====================================================================== */

#define TOKU_VUSERDATA          makevariant(TOKU_T_USERDATA, 0)
#define TOKU_VLIGHTUSERDATA     makevariant(TOKU_T_LIGHTUSERDATA, 0)

#define ttisfulluserdata(o)     checktag(o, ctb(TOKU_VUSERDATA))
#define ttislightuserdata(o)    checktag(o, TOKU_VLIGHTUSERDATA)

#define udval(o)    check_exp(ttisfulluserdata(o), gco2u(val(o).gc))
#define pval(o)     check_exp(ttislightuserdata(o), val(o).p)

#define setudval(T,obj,x) \
    { TValue *o_=(obj); const UserData *x_=(x); \
      val(o_).gc = obj2gco(x_); settt(o_, ctb(TOKU_VUSERDATA)); \
      checkliveness(T,o_); }

#define setudval2s(T,o,uv)      setudval(T, s2v(o), uv)

#define setpval(obj,x) \
    { TValue *o_=(obj); val(o_).p = (x); settt(o_, TOKU_VLIGHTUSERDATA); }


/*
** 'TValue' that ensures that addresses after this type are
** always fully aligned.
*/
typedef union UValue {
    TValue val;
    TOKUI_MAXALIGN; /* ensures maximum alignment for binary data */
} UValue;


typedef struct UserData {
    ObjectHeader;
    uint16_t nuv; /* number of 'uservalues' */
    size_t size; /* size of 'UserData' memory in bytes */
    Table *metatable;
    GCObject *gclist;
    UValue uv[1]; /* user values */
    /* 'UserData' memory starts here */
} UserData;


/*
** 'UserData' without user values, meaning 'uv' is empty ('nuv' == 0).
** This is used when allocating 'UserData' to properly calculate offset
** of user memory because 'uv' is a flexible array member.
** Alto this kind of userdata is never gray so it doesn't need 'gclist'.
** Internally Tokudae only uses 'UserData' to access fields and it takes
** care to avoid using 'uv' and 'gclist' fields when 'nuv' is 0.
*/
typedef struct EmptyUserData {
    ObjectHeader;
    uint16_t nuv;
    size_t size;
    Table *metatable;
    union {TOKUI_MAXALIGN;} bin;
    /* 'UserData' memory starts here */
} EmptyUserData;


/* offset in 'UserData' where user memory begins */
#define udmemoffset(nuv) \
        ((nuv) == 0 ? offsetof(EmptyUserData, bin) \
                    : offsetof(UserData, uv) + ((nuv)*sizeof(UValue)))

/* get the address of the memory block inside 'UserData' */
#define getuserdatamem(u)       (cast_charp(u) + udmemoffset((u)->nuv))

/* size of 'UserData' */
#define sizeofuserdata(nuv, size)   (udmemoffset(nuv) + (size))

/* }==================================================================== */



/*
** Conversion modes when converting 'toku_Integer'
** into 'toku_Number'.
*/
typedef enum N2IMode {
    N2IFLOOR,
    N2ICEIL,
    N2IEQ,
} N2IMode;


#define intop(op,x,y)   t_castU2S(t_castS2U(x) op t_castS2U(y))


/* convert value to 'toku_Integer' */
#define tointeger(v,i) \
        (t_likely(ttisint(v)) ? (*(i) = ival(v), 1) \
                              : tokuO_tointeger(v, i, N2IEQ))


/* convert value to 'toku_Number' */
#define tonumber(v,n) \
        (ttisflt(v) ? ((n) = fval(v), 1) : \
        (ttisint(v) ? ((n) = cast_num(ival(v)), 1) : 0))


/* same as left shift but indicate right by making 'y' negative */
#define tokuO_shiftr(x,y)     tokuO_shiftl(x, intop(-, 0, y))


/* fast 'module' operation for hashing (sz is always power of 2) */
#define tmod(h,sz) \
        (check_exp(t_ispow2(sz), (cast_i32((h) & ((sz)-1)))))


TOKUI_FUNC int32_t tokuO_ceillog2(uint32_t x);
TOKUI_FUNC int32_t tokuO_n2i(toku_Number n, toku_Integer *i, N2IMode mode);
TOKUI_FUNC int32_t tokuO_tointeger(const TValue *v, toku_Integer *i,
                                                    int32_t mode);
TOKUI_FUNC toku_Integer tokuO_shiftl(toku_Integer x, toku_Integer y);
TOKUI_FUNC int32_t tokuO_arithmraw(toku_State *T, const TValue *a,
                                                  const TValue *b,
                                                  TValue *res,
                                                  int32_t op);

#endif
