#define TOKU_LIB

#include "tokudae.h"


int32_t tokuopen_lib2(toku_State *T);

TOKUMOD_API int32_t tokuopen_lib21(toku_State *T) {
    return tokuopen_lib2(T);
}
