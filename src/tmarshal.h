/*
** tmarshal_h
** (un)dump precompiled Tokudae chunks in binary format
** See Copyright Notice in tokudae.h
*/

#ifndef tmarshal_h
#define tmarshal_h

#include "tokudae.h"
#include "tstate.h"
#include "tobject.h"


TOKUI_FUNC int tokuZ_dump(toku_State *T, const Proto *f, toku_Writer w,
                                         void *data, int strip);

TOKUI_FUNC TClosure *tokuZ_undump(toku_State *T, BuffReader *Z,
                                                 const char *name);


#endif
