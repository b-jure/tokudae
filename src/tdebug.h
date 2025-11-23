/*
** tdebug.h
** Debug and error reporting functiont
** See Copyright Notice in tokudae.h
*/

#ifndef tdebug_h
#define tdebug_h


#include "tobject.h"
#include "tstate.h"


/*
** This assumes 'pc' points one byte after the opcode.
** (See 'savestate' in 'tokuV_execute'.)
*/
#define relpc(pc, p)    cast_i32((pc) - (p)->code - 1)


/* active Tokudae function (given call frame) */
#define cf_func(cf)     (clTval(s2v((cf)->func.p)))


#define resethookcount(C)       (C->hookcount = C->basehookcount)


/*
** Mark for entries in 'lineinfo' array that has absolute information in
** 'abslineinfo' array, or for opcode arguments.
*/
#define ABSLINEINFO     (-0x80)


/*
** MAXimum number of successive Opcodes WiTHout ABSolute line
** information. (A power of two allows fast divisions.)
*/
#if !defined(MAXOWTHABS)
#define MAXOWTHABS     128
#endif


#define tokuD_aritherror(C,v1,v2) \
        tokuD_opinterror(C, v1, v2, "perform arithmetic on")

#define tokuD_bitwerror(C,v1,v2) \
        tokuD_opinterror(C, v1, v2, "perform bitwise operation on")

TOKUI_FUNC int32_t tokuD_getfuncline(const Proto *fn, int32_t pc);
TOKUI_FUNC const char *tokuD_findlocal(toku_State *T, CallFrame *cf,
                                       int32_t n, SPtr *pos);
TOKUI_FUNC const char *tokuD_addinfo(toku_State *T, const char *msg,
                                     OString *src, int32_t line);
TOKUI_FUNC t_noret tokuD_runerror(toku_State *T, const char *fmt, ...);
TOKUI_FUNC t_noret tokuD_typeerror(toku_State *T, const TValue *o,
                                                  const char *op);
TOKUI_FUNC t_noret tokuD_classerror(toku_State *T, TM event);
TOKUI_FUNC t_noret tokuD_binoperror(toku_State *T, const TValue *v1,
                                    const TValue *v2, TM event);
TOKUI_FUNC t_noret tokuD_ordererror(toku_State *T, const TValue *v1,
                                                   const TValue *v2);
TOKUI_FUNC t_noret tokuD_opinterror(toku_State *T, const TValue *v1,
                                    const TValue *v2, const char *msg);
TOKUI_FUNC t_noret tokuD_tointerror(toku_State *T, const TValue *v1,
                                                   const TValue *v2);
TOKUI_FUNC t_noret tokuD_callerror(toku_State *T, const TValue *obj);
TOKUI_FUNC t_noret tokuD_concaterror(toku_State *T, const TValue *v1,
                                                    const TValue *v2);
TOKUI_FUNC t_noret tokuD_unknownlf(toku_State *T, const TValue *field);
TOKUI_FUNC t_noret tokuD_lfseterror(toku_State *T, int32_t lf);
TOKUI_FUNC t_noret tokuD_indexboundserror(toku_State *T, List *l,
                                          const TValue *k);
TOKUI_FUNC t_noret tokuD_invindexerror(toku_State *T, const TValue *k);
TOKUI_FUNC t_noret tokuD_errormsg(toku_State *T);
TOKUI_FUNC void tokuD_hook(toku_State *T, int32_t event, int32_t line,
                                          int32_t ftransfer,
                                          int32_t ntransfer);
TOKUI_FUNC void tokuD_hookcall(toku_State *T, CallFrame *cf);
TOKUI_FUNC int32_t tokuD_tracecall(toku_State *T);
TOKUI_FUNC int32_t tokuD_traceexec(toku_State *T, const uint8_t *pc,
                                              ptrdiff_t stacksize);

#endif
