/*
** treader.c
** Buffered reader
** See Copyright Notice in tokudae.h
*/

#define treader_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include <string.h>

#include "treader.h"
#include "tokudaelimits.h"



void tokuR_init(toku_State *T, BuffReader *Z, toku_Reader freader, void *ud) {
    Z->n = 0;
    Z->p = NULL;
    Z->reader = freader;
    Z->userdata = ud;
    Z->T = T;
}


/* 
** Invoke reader returning the first character or TEOF (-1).
** 'reader' function should set the 'size' to the amount of bytes reader
** read and return the pointer to the start of that buffer. 
** In case there is no more data to be read, 'reader' should set 'size'
** to 0 or return NULL.
*/
int tokuR_fill(BuffReader *Z) {
    size_t size;
    toku_State *T = Z->T;
    const char *p;
    toku_unlock(T);
    p = Z->reader(T, Z->userdata, &size);
    toku_lock(T);
    if (p == NULL || size == 0)
        return TEOF;
    Z->n = size - 1; /* discount char being returned */
    Z->p = p;
    return cast_ubyte(*(Z->p++));
}


/* {===================================================================
** Load
** ==================================================================== */

static int checkbuffer(BuffReader *Z) {
    if (Z->n == 0) { /* no bytes in buffer? */
        if (tokuR_fill(Z) == TEOF) /* try to read more */
            return 0; /* no more input */
        else {
            Z->n++; /* tokuR_fill consumed first byte; put it back */
            Z->p--;
        }
    }
    return 1;  /* now buffer has something */
}


size_t tokuR_read(BuffReader *Z, void *b, size_t n) {
    while (n) {
        size_t m;
        if (!checkbuffer(Z))
            return n; /* no more input; return number of missing bytes */
        m = (n <= Z->n) ? n : Z->n; /* min. between n and Z->n */
        memcpy(b, Z->p, m);
        Z->n -= m;
        Z->p += m;
        b = cast_charp(b) + m;
        n -= m;
    }
    return 0;
}
