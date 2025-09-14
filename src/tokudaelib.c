/*
** tokudaelib.c
** Tokudae standard libraries
** See Copyright Notice in tokudae.h
*/

#define tokudaelib_c
#define TOKU_LIB

#include "tokudaeprefix.h"

#include "tokudaelib.h"
#include "tokudaeaux.h"


TOKUI_FUNC int tokuopen_basic(toku_State *T);
TOKUI_FUNC int tokuopen_package(toku_State *T);
TOKUI_FUNC int tokuopen_string(toku_State *T);
TOKUI_FUNC int tokuopen_math(toku_State *T);
TOKUI_FUNC int tokuopen_io(toku_State *T);
TOKUI_FUNC int tokuopen_os(toku_State *T);
TOKUI_FUNC int tokuopen_reg(toku_State *T);
TOKUI_FUNC int tokuopen_debug(toku_State *T);
TOKUI_FUNC int tokuopen_list(toku_State *T);
TOKUI_FUNC int tokuopen_utf8(toku_State *T);


/*
** Standard Libraries. (Must be listed in the same ORDER of
** their respective constants TOKU_LIB_<libname>.)
*/
static const tokuL_Entry stdlibs[] = {
    {TOKU_GNAME, tokuopen_basic},
    {TOKU_LIBN_PACKAGE, tokuopen_package},
    {TOKU_LIBN_STRING, tokuopen_string},
    {TOKU_LIBN_MATH, tokuopen_math},
    {TOKU_LIBN_IO, tokuopen_io},
    {TOKU_LIBN_OS, tokuopen_os},
    {TOKU_LIBN_REGEX, tokuopen_reg},
    {TOKU_LIBN_DEBUG, tokuopen_debug},
    {TOKU_LIBN_LIST, tokuopen_list},
    {TOKU_LIBN_UTF8, tokuopen_utf8},
    {NULL, NULL}
};


TOKULIB_API void tokuL_openlibsx(toku_State *T, int libload, int libpreload) {
    int mask;
    const tokuL_Entry *lib;
    tokuL_get_subtable(T, TOKU_CTABLE_INDEX, TOKU_PRELOAD_TABLE);
    for (lib = stdlibs, mask = 1; lib->name != NULL; lib++, mask <<= 1) {
        if (libload & mask) { /* selected? */
            tokuL_importf(T, lib->name, lib->func, 1); /* import library */
            toku_pop(T, 1); /* remove result from the stack */
        } else if (libpreload & mask) { /* selected? */
            toku_push_cfunction(T, lib->func);
            toku_set_field_str(T, -2, lib->name); /* __PRELOAD[name] = libf */
        }
    }
    toku_assert((mask >> 1) == TOKU_LIB_UTF8);
    toku_pop(T, 1); /* remove preload table */
}
