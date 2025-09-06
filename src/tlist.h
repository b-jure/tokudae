/*
** tlist.h
** List manipulation functions
** See Copyright Notice in tokudae.h
*/

#ifndef tlist_h
#define tlist_h

#include "tobject.h"


/*
** List is a sequence that can contain any Tokudae value.
** Length of the list is defined as the number of non-nil values in a
** sequence starting from index 0 (with few exceptions).
** For example, given the list [5, 2, nil, 7], the length of this list would
** be 2. The list tracks its own length, at all times. Inserting elements
** past the list length is an error. Appending to the list (inserting at
** the current length) is allowed, if appending nil value, this is a no-op,
** otherwise the value is appended and length is incremented by 1.
** Inserting 'nil' in bounds of the list [0, len), truncates the list and
** the length is set to the index which was assigned the nil value.
** All of the indices past the len-1 will not be marked by GC and therefore
** are subject to collection. In cases where append of non-nil value would
** fill a gap that separated two sequences, the length would still be
** incremented only by 1, meaning the two sequences are not merged.
*/


/* value of 'extra' for first list field name */
#define FIRST_LF        (NUM_KEYWORDS + TM_NUM + 1)

/* indices into 'C->gs->listfields' */
#define LFLEN       0
#define LFSIZE      1
#define LFLAST      2
#define LFX         3
#define LFY         4
#define LFZ         5
#define LFNUM       6


/* get list field index */
#define gLF(s)      ((s)->extra - FIRST_LF)


/* test whether a string is a valid list field */
#define islistfield(s) \
        ((s)->tt_ == TOKU_VSHRSTR && FIRST_LF <= (s)->extra && \
         (s)->extra < FIRST_LF + LFNUM)


#define tokuA_fastset(T,l,i,v) \
    { setobj(T, &(l)->arr[(i)], v); tokuG_barrierback(T, obj2gco(l), (v)); }


#define tokuA_ensureindex(T,l,i)    tokuA_ensure(T, l, (i) + 1)


TOKUI_FUNC void tokuA_setindex(toku_State *T, List *l, const TValue *k,
                                                       const TValue *v);
TOKUI_FUNC void tokuA_setstr(toku_State *T, List *l, const TValue *k,
                                                     const TValue *v);
TOKUI_FUNC void tokuA_set(toku_State *T, List *l, const TValue *k,
                                                  const TValue *v);

TOKUI_FUNC void tokuA_getindex(List *l, toku_Integer i, TValue *out);
TOKUI_FUNC void tokuA_get(toku_State *T, List *l, const TValue *k, TValue *r);
TOKUI_FUNC void tokuA_getstr(toku_State *T, List *l, const TValue *k,
                                                     TValue *r);
TOKUI_FUNC void tokuA_init(toku_State *T);
TOKUI_FUNC List *tokuA_new(toku_State *T);
TOKUI_FUNC int tokuA_shrink(toku_State *T, List *l);
TOKUI_FUNC void tokuA_ensure(toku_State *T, List *l, int n);
TOKUI_FUNC void tokuA_free(toku_State *T, List *l);

#endif
