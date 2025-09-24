/*
** tlexer.h
** Scanner
** See Copyright Notice in tokudae.h
*/

#ifndef tlexer_h
#define tlexer_h

#include "treader.h"
#include "tobject.h"



/* multi-char tokens start at this numeric value */
#define FIRSTTK		(UCHAR_MAX + 1)


#if !defined(TOKU_ENV)
#define TOKU_ENV      "__ENV"
#endif


/*
** WARNING: if you change the order of this enumeration, grep
** "ORDER TK".
*/
enum TK {
    /* keyword tokens */
    TK_AND = FIRSTTK, TK_BREAK, TK_CASE, TK_CONTINUE, TK_CLASS,
    TK_DEFAULT, TK_ELSE, TK_FALSE, TK_FOR, TK_FOREACH, TK_FN, TK_IF,
    TK_IN, TK_INHERITS, TK_NIL, TK_OR, TK_RETURN, TK_SUPER, TK_DO,
    TK_SWITCH, TK_TRUE, TK_WHILE, TK_LOOP, TK_LOCAL, TK_INF, TK_INFINITY,
    /* other multi-char tokens */
    TK_IDIV, TK_NE, TK_EQ, TK_GE, TK_LE, TK_SHL, TK_SHR, TK_POW,
    TK_CONCAT, TK_DOTS, TK_DBCOLON,
    TK_EOS,
    /* literal tokens */
    TK_FLT, TK_INT, TK_STRING, TK_NAME,
};

/* number of reserved keywords */
#define NUM_KEYWORDS	((TK_INFINITY - (FIRSTTK)) + 1)



/* scanner literals */
typedef union {
    toku_Integer i;
    toku_Number n;
    OString *str;
} Literal;


typedef struct {
    int tk;
    Literal lit;
} Token;


typedef struct Lexer {
    int c; /* current char */
    int lastline; /* line of previous token */
    int line; /* current line number */
    Token t; /* current token */
    Token tahead; /* lookahead token */
    Table *tab; /* scanner table */
    struct toku_State *T;
    struct FunctionState *fs;
    BuffReader *Z; /* buffered reader */
    Buffer *buff; /* string buffer */
    struct DynData *dyd; /* dynamic data used by parser */
    OString *src; /* current source name */
    OString *envn; /* environment variable */
} Lexer;


#define tokuY_newliteral(lx, l)   tokuY_newstring(lx, "" (l), LL(l))

TOKUI_FUNC void tokuY_setinput(toku_State *T, Lexer *lx, BuffReader *Z,
                               OString *source, int firstchar);
TOKUI_FUNC void tokuY_init(toku_State *T);
TOKUI_FUNC const char *tokuY_tok2str(Lexer *lx, int t);
TOKUI_FUNC OString *tokuY_newstring(Lexer *lx, const char *str, size_t len);
TOKUI_FUNC t_noret tokuY_syntaxerror(Lexer *lx, const char *err);
TOKUI_FUNC void tokuY_scan(Lexer *lx);
TOKUI_FUNC int tokuY_scanahead(Lexer *lx);

#endif
