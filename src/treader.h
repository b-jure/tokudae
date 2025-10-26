/*
** treader.h
** Buffered reader
** See Copyright Notice in tokudae.h
*/

#ifndef treader_h
#define treader_h


#include "tokudae.h"
#include "tmem.h"


/* end of file */
#define TEOF        (-1)


/* Return next char and progress the buffer or try fill the buffer. */
#define zgetc(Z)    ((Z)->n-- > 0 ? cast_u8(*(Z)->p++) : tokuR_fill(Z))


typedef struct {
    size_t n; /* unread bytes */
    const char* p; /* position in buffer */
    toku_Reader reader; /* reader function */
    void* userdata; /* user data for 'reader' */
    toku_State* T; /* 'toku_State' for 'reader' */
} BuffReader;


TOKUI_FUNC void tokuR_init(toku_State* T, BuffReader* Z, toku_Reader freader,
                                          void* userdata);
TOKUI_FUNC int tokuR_fill(BuffReader* Z);
TOKUI_FUNC size_t tokuR_read(BuffReader *Z, void *b, size_t n);



#define tokuR_buff(b)       ((b)->str)
#define tokuR_bufflen(b)    ((b)->len)
#define tokuR_buffsize(b)   ((b)->size)

#define tokuR_buffpop(b)        ((b)->len -= 1)
#define tokuR_buffreset(b)      ((b)->len = 0)
#define tokuR_buffpopn(b,n)     ((b)->len -= cast_sizet(n))

#define tokuR_buffresize(T,b,s) \
    { (b)->str = cast_charp(tokuM_saferealloc(T, (b)->str, (b)->size, s)); \
      (b)->size = s; }

#define tokuR_freebuffer(T,b)   tokuR_buffresize(T, b, 0)


/* string buffer for lexer */
typedef struct Buffer {
  char *str;
  size_t len;
  size_t size;
} Buffer;

#endif
