/*
** tokudaelib.h
** Tokudae standard libraries
** See Copyright Notice in tokudae.h
*/

#ifndef tokudaelib_h
#define tokudaelib_h


#include "tokudae.h"


/* version suffix for environment variable names */
#define TOKU_VERSUFFIX      "_" TOKU_VERSION_MAJOR "_" TOKU_VERSION_MINOR


#define TOKU_LIB_BASIC          1
#define TOKU_LIB_PACKAGE        (TOKU_LIB_BASIC << 1)
#define TOKU_LIB_STRING         (TOKU_LIB_PACKAGE << 1)
#define TOKU_LIB_MATH           (TOKU_LIB_STRING << 1)
#define TOKU_LIB_IO             (TOKU_LIB_MATH << 1)
#define TOKU_LIB_OS             (TOKU_LIB_IO << 1)
#define TOKU_LIB_REGEX          (TOKU_LIB_OS << 1)
#define TOKU_LIB_DEBUG          (TOKU_LIB_REGEX << 1)
#define TOKU_LIB_LIST           (TOKU_LIB_DEBUG << 1)
#define TOKU_LIB_UTF8           (TOKU_LIB_LIST << 1)


#define TOKU_LIBN_PACKAGE       "package"
#define TOKU_LIBN_STRING        "string"
#define TOKU_LIBN_MATH          "math"
#define TOKU_LIBN_IO            "io"
#define TOKU_LIBN_OS            "os"
#define TOKU_LIBN_REGEX         "reg"
#define TOKU_LIBN_DEBUG         "debug"
#define TOKU_LIBN_LIST          "list"
#define TOKU_LIBN_UTF8          "utf8"


/* open selected libraries */
TOKULIB_API void tokuL_openlibsx(toku_State *T, int libload, int libpreload);

/* open all libraries */
#define tokuL_openlibs(T)   tokuL_openlibsx(T, ~0, 0)


#endif
