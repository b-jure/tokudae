/*
** tparser.h
** Tokudae Parser
** See Copyright Notice in tokudae.h
*/

#ifndef tparser_h
#define tparser_h


#include "tlexer.h"
#include "tobject.h"


/* maximum number of local variables per function */
#define MAXVARS         MAX_CODE


/*
** Because all strings are unified by the scanner, the parser
** can use pointer equality for string equality.
*/
#define eqstr(a, b)     ((a) == (b))


/* check expression type */
#define eisvar(e)       ((e)->et >= EXP_UVAL && (e)->et <= EXP_DOTSUPER)
#define eisconstant(e)  ((e)->et >= EXP_NIL && (e)->et <= EXP_K)
#define eismulret(e)    ((e)->et == EXP_CALL || (e)->et == EXP_VARARG)
#define eistrue(e)      ((e)->et >= EXP_TRUE && (e)->et <= EXP_K)
#define eisindexed(e)   ((e)->et >= EXP_INDEXED && (e)->et <= EXP_DOTSUPER)
#define eisstring(e)    ((e)->et == EXP_STRING)


/* expression types */
typedef enum expt {
    /* no expression */
    EXP_VOID,
    /* expression is nil constant */
    EXP_NIL,
    /* expression is false constant */
    EXP_FALSE,
    /* expression is true constant */
    EXP_TRUE,
    /* string constant;
     * 'str' = string value; */
    EXP_STRING,
    /* integer constant;
     * 'i' = integer value; */
    EXP_INT,
    /* floating constant;
     * 'n' = floating value; */
    EXP_FLT,
    /* registered constant value;
     * 'info' = index in 'constants'; */
    EXP_K,
    /* upvalue variable;
     * 'info' = index of upvalue in 'upvals'; */
    EXP_UVAL,
    /* local variable;
     * 'v.sidx' = stack index;
     * 'v.vidx' = compiler index; */
    EXP_LOCAL,
    /* 'super' */
    EXP_SUPER,
    /* indexed variable; */
    EXP_INDEXED,
    /* variable indexed with literal string;
     * 'info' = index in 'constants'; */
    EXP_INDEXSTR,
    /* variable indexed with constant integer;
     * 'info' = index in 'constants'; */
    EXP_INDEXINT,
    /* indexed 'super'; */
    EXP_INDEXSUPER,
    /* indexed 'super' with literal string;
     * 'info' = index in 'constants'; */
    EXP_INDEXSUPERSTR,
    /* indexed variable with '.';
     * 'info' = index in 'constants'; */
    EXP_DOT,
    /* indexed 'super' with '.'; */
    EXP_DOTSUPER,
    /* function call;
     * 'info' = pc; */
    EXP_CALL,
    /* vararg expression '...';
     * 'info' = pc; */
    EXP_VARARG,
    /* finalized expression */
    EXP_FINEXPR,
} expt;


/*
** Expression descriptor.
*/
typedef struct ExpInfo {
    expt et;
    union {
        toku_Number n; /* floating constant */
        toku_Integer i; /* integer constant  */
        OString *str; /* string literal */
        struct {
            int32_t vidx; /* compiler index */
            int32_t sidx; /* stack slot index */
        } var; /* local var */
        int32_t info; /* pc or tome other generic information */
    } u;
    int32_t t; /* jmp to patch if true */
    int32_t f; /* jmp to patch if false */
} ExpInfo;


#define instack(e)      ((e)->et == EXP_FINEXPR)


/* variable kind */
#define VARREG      0   /* regular */
#define VARFINAL    1   /* final (immutable) */
#define VARTBC      2   /* to-be-closed */


/* active local variable compiler information */
typedef union LVar {
    struct {
        TValueFields;
        uint8_t kind;
        int32_t sidx; /* stack slot index holding the variable value */
        int32_t pidx; /* index of variable in Proto's 'locals' array */
        int32_t linenum; /* line where the local variable is declared */
        OString *name;
    } s;
    TValue val; /* constant value */
} LVar;


/* switch statement constant description */
typedef struct LiteralInfo {
    Literal lit; /* constant */
    int32_t tt; /* type tag */
} LiteralInfo;


/*
** Description of pending goto jumps (break/continue).
** Tokudae does not support explicit 'goto' statements and labels,
** instead this structure refers to the 'break' and some 'continue' jumps.
*/
typedef struct Goto {
    int32_t pc; /* position in the code */
    int32_t nactlocals; /* number of active local variables in that position */
    uint8_t close; /* true if goto jump escapes upvalues */
    uint8_t bk; /* true if goto it break (otherwise continue in gen. loop) */
} Goto;


/* list of goto jumps */
typedef struct GotoList {
    int32_t len; /* number of labels in use */
    int32_t size; /* size of 'arr' */
    Goto *arr; /* array of pending goto jumps */
} GotoList;


/*
** Dynamic data used by parser.
*/
typedef struct DynData {
    struct { /* list of all active local variables */
        int32_t len; /* number of locals in use */
        int32_t size; /* size of 'arr' */
        LVar *arr; /* array of compiler local variables */
    } actlocals;
    struct { /* list of all switch constants */
        int32_t len; /* number of constants in use */
        int32_t size; /* size of 'arr' */
        struct LiteralInfo *arr; /* array of switch constants */
    } literals;
    GotoList gt; /* idem */
} DynData;


struct Scope;       /* defined in tparser.c */
struct LoopState;   /* defined in tparser.c */
struct ClassState;  /* defined in tparser.c */


/*
** State of the currently compiled function prototype (for parsing).
*/
typedef struct FunctionState {
    Proto *p;                   /* current function prototype */
    struct FunctionState *prev; /* chain, enclosing function */
    struct Lexer *lx;           /* lexical state */
    struct Scope *scope;        /* chain, current scope */
    struct Scope *switchscope;  /* chain, innermost switch scope */
    struct LoopState *ls;       /* chain, loop statement */
    struct ClassState *cs;      /* chain, class definition */
    Table *kcache;              /* cache for reusing constants */
    int32_t pcdo;               /* start of 'do/while' or 'loop' (or NOPC) */
    int32_t firstlocal;         /* index of first local in 'lvars' */
    int32_t prevpc;             /* previous opcode pc */
    int32_t prevline;           /* previous opcode line */
    int32_t sp;                 /* first free compiler stack index */
    int32_t nactlocals;         /* number of active local variables */
    int32_t np;                 /* number of elements in 'p' */
    int32_t nk;                 /* number of elements in 'k' */
    int32_t pc;                 /* number of elements in 'code' ('ncode') */
    int32_t nabslineinfo;       /* number of elements in 'abslineinfo' */
    int32_t nopcodepc;          /* number of elements in 'opcodepc' */
    int32_t nlocals;            /* number of elements in 'locals' */
    int32_t nupvals;            /* number of elements in 'upvals' */
    int32_t lasttarget;         /* latest 'pc' that is jump target */
    uint8_t ismethod;           /* if true, the function is a class method */
    uint8_t nonilmerge;         /* if true, no NIL opcode merging */
    uint8_t iwthabs;            /* opcodes issued since last abs. line info */
    uint8_t needclose;          /* if true, needs to close upvalues */
    uint8_t callcheck;          /* if true, last call has false check ('?') */
    uint8_t lastisend;          /* if true, last statement ends control flow
                                   (1==return, 2==break, 3==continue) */
} FunctionState;


TOKUI_FUNC t_noret tokuP_semerror(Lexer *lx, const char *err);
TOKUI_FUNC void tokuP_checklimit(FunctionState *fs, int32_t n,
                                 int32_t limit, const char *what);
TOKUI_FUNC TClosure *tokuP_parse(toku_State *T, BuffReader *Z, Buffer *buff,
                                 DynData *dyd, const char *name,
                                 int32_t firstchar);


#endif
