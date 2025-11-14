/*
** tmarshal_c
** (un)dump precompiled Tokudae chunks in binary format
** See Copyright Notice in tokudae.h
*/

#define tmarshal_c
#define TOKU_CORE

#include "tokudaeprefix.h"

#include  <string.h>

#include "tcode.h"
#include "tfunction.h"
#include "tgc.h"
#include "tmarshal.h"
#include "tokudaelimits.h"
#include "tprotected.h"
#include "treader.h"
#include "tstring.h"
#include "ttable.h"


/* encoded major/minor version in one byte, one nibble for each */
#define TOKUC_VERSION   ((TOKU_VERSION_MAJOR_N << 4) | TOKU_VERSION_MINOR_N)

/* this is the official format */
#define TOKUC_FORMAT    0

/* data to catch conversion errors */
#define TOKUC_DATA      "\x19\x93\r\n\x1a\n"

/* this value is for both 'int' and 'toku_Integer' */
#define TOKUC_INT       -69

/* value for 'uint8_t' */
#define TOKUC_OPCODE    0xf1

/* value for 'toku_Number' */
#define TOKUC_NUM       cast_num(-69.5)


typedef struct MarshalState {
    toku_State *T;
    union {
        struct { /* when dumping */
            toku_Writer writer; /* writer that dumps the chunk */
            void *data; /* data for writer */
            int32_t strip; /* if true, remove debug information */
            int32_t status; /* status returned by writer */
        } d;
        struct { /* when loading */
            BuffReader *Z; /* buffered reader */
            const char *name; /* name of the chunk */
        } l;
    } u;
    Table *h; /* list for string reuse; for tracking dumped strings */
    size_t offset; /* current position relative to beginning of dump */
    toku_Unsigned nstr; /* number of saved strings; strings in the list */
} MarshalState;


#define D(M)    ((M)->u.d)
#define L(M)    ((M)->u.l)


/* {====================================================================
** DUMP
** ===================================================================== */

/* write a vector; sequence of n elements in x */
#define dump_vector(M,v,n)     dump_block(M, v, (n)*sizeof((v)[0]))

/* write a literal string; sequence of chars excluding '\0' character */
#define dump_literal(M,s)      dump_vector(M, s, sizeof(s) - sizeof(char))

/* write a variable; single x value */
#define dump_var(M,x)          dump_vector(M, &x, 1)

/* write y as unsigned byte */
#define dump_byte(M,y)     { uint8_t x = cast_u8(y); dump_var(M, x); }


/*
** Dump the block of memory pointed by 'b' with given 'size'.
** 'b' should not be NULL, except for the last call signaling the end
** of the dump.
*/
static void dump_block(MarshalState *M, const void *b, size_t size) {
    if (D(M).status == 0) { /* do not write anything in case we have errors */
        toku_lock(T);
        D(M).status = (*D(M).writer)(M->T, b, size, D(M).data);
        toku_unlock(T);
        M->offset += size;
    }
}


/* size of buffer in 'dump_varint' (+6 is to round up the division) */
#define NMBS        ((t_nbits(toku_Unsigned) + 6) / 7)

/*
** Write unsigned integer using the MSB encoding.
** The most significant bit of a byte is used as a continuation bit,
** so each byte can store 7 bits, if MSB of the current byte is 0,
** that is the end.
*/
static void dump_varint(MarshalState *M, toku_Unsigned x) {
    uint8_t buff[NMBS];
    uint32_t n = 1; /* first byte is skipped */
    buff[NMBS - 1] = x & 0x7f; /* fill least significant byte */
    while (x >>= 7) /* fill other bytes in reverse order */
        buff[NMBS - (++n)] = cast_u8((x & 0x7f) | 0x80);
    dump_vector(M, buff + NMBS - n, n);
}


/* write a non-negative int32_t */
#define dump_int(M,x) \
    { toku_assert(0 <= x); dump_varint(M, cast_u32(x)); }


/* write padding if data is not aligned */
static void dump_align(MarshalState *M, uint32_t align) {
    uint32_t padding = align - cast_u32(M->offset % align);
    if (padding < align) { /* padding == align means no padding */
        static toku_Integer padding_data = 0;
        toku_assert(align <= sizeof(padding_data));
        dump_block(M, &padding_data, padding);
    }
    /* here it must be aligned */
    toku_assert(M->offset % align == 0);
}


/* write size of the type t and it's value x */
#define dump_numinfo(M,t,x) \
    { t val = x; dump_byte(M, sizeof(t)); dump_var(M, val); }


static void dump_header(MarshalState *M) {
    dump_literal(M, TOKU_SIGNATURE);
    dump_byte(M, TOKUC_VERSION);
    dump_byte(M, TOKUC_FORMAT);
    dump_literal(M, TOKUC_DATA);
    dump_numinfo(M, int32_t, TOKUC_INT);
    dump_numinfo(M, uint8_t, TOKUC_OPCODE);
    dump_numinfo(M, toku_Integer, TOKUC_INT);
    dump_numinfo(M, toku_Number, TOKUC_NUM);
}


static void dump_code(MarshalState *M, const Proto *f) {
    dump_int(M, f->sizecode);
    dump_align(M, sizeof(f->code[0])); /* bytecode */
    toku_assert(f->code != NULL);
    dump_vector(M, f->code, cast_u32(f->sizecode));
}


static void dump_number(MarshalState *M, toku_Number x) {
    dump_var(M, x);
}


/*
** Signed integers are coded to keep small values small. (Coding -1 as
** 0xfff...fff would use too many bytes to save a quite common value.)
** A non-negative x is coded as 2x; a negative x is coded as -2x - 1.
** (0 => 0; -1 => 1; 1 => 2; -2 => 3; 2 => 4; ...)
** Negative integers are made odd to properly decode them when loading.
*/
static void dump_integer(MarshalState *M, toku_Integer x) {
    toku_Unsigned cx = (x >= 0) ? 2u * t_castS2U(x)
                                : (2u * ~t_castS2U(x)) - 1;
    dump_varint(M, cx);
}


static void dump_size(MarshalState *M, size_t sz) {
    dump_varint(M, cast_Unsigned(sz));
}


static void dump_string(MarshalState *M, OString *str) {
    if (str == NULL)
        dump_size(M, 0); /* no string */
    else {
        TValue idx;
        uint8_t tag = tokuH_getstr(M->h, str, &idx);
        if (!tagisempty(tag)) { /* already saved? */
            dump_size(M, 1); /* reuse a saved string */
            dump_varint(M, t_castS2U(ival(&idx)));
        } else { /* must write and save the string */
            TValue val;
            size_t size = getstrlen(str);
            const char *s = getstr(str);
            dump_size(M, size + 2); /* +2 to avoid size of 0 and 1 */
            dump_vector(M, s, size + 1); /* +1 to include '\0' */
            setival(&val, t_castU2S(M->nstr)); /* it's index is the value */
            M->nstr++; /* one more saved string */
            tokuH_setstr(M->T, M->h, str, &val); /* h[str] = nstr */
            /* integer value does not need barrier */
        }
    }
}


static void dump_constants(MarshalState *M, const Proto *f) {
    int32_t n = f->sizek;
    dump_int(M, n);
    for (int32_t i = 0; i < n; i++) {
        const TValue *k = &f->k[i];
        int32_t tt = ttypetag(k);
        dump_byte(M, tt);
        switch (tt) {
            case TOKU_VNUMFLT:
                dump_number(M, fval(k));
                break;
            case TOKU_VNUMINT:
                dump_integer(M, ival(k));
                break;
            case TOKU_VLNGSTR: case TOKU_VSHRSTR:
                dump_string(M, strval(k));
                break;
            default:
                toku_assert(tt == TOKU_VTRUE || tt == TOKU_VFALSE ||
                            tt == TOKU_VNIL);
        }
    }
}


static void dump_upvalues(MarshalState *M, const Proto *f) {
    int32_t n = f->sizeupvals;
    dump_int(M, n);
    for (int32_t i = 0; i < n; i++) {
        dump_int(M, f->upvals[i].idx);
        dump_byte(M, f->upvals[i].instack);
        dump_byte(M, f->upvals[i].kind);
    }
}


static void dump_function(MarshalState *M, const Proto *f);


static void dump_protos(MarshalState *M, const Proto *f) {
    int32_t n = f->sizep;
    dump_int(M, n);
    for (int32_t i = 0; i < n; i++)
        dump_function(M, f->p[i]);
}


static void dump_debug(MarshalState *M, const Proto *f) {
    int32_t n;
    dump_string(M, D(M).strip ? NULL : f->source);
    n = D(M).strip ? 0 : f->sizelineinfo;
    dump_int(M, n);
    if (n > 0)
        dump_vector(M, f->lineinfo, cast_u32(n));
    n = D(M).strip ? 0 : f->sizeabslineinfo;
    dump_int(M, n);
    if (n > 0) {
        /* 'abslineinfo' is an array of structures of int32_t's */
        dump_align(M, sizeof(int32_t));
        dump_vector(M, f->abslineinfo, cast_u32(n));
    }
    n = D(M).strip ? 0 : f->sizeopcodepc;
    dump_int(M, n);
    if (n > 0)
        dump_vector(M, f->opcodepc, cast_u32(n));
    n = D(M).strip ? 0 : f->sizelocals;
    dump_int(M, n);
    for (int32_t i = 0; i < n; i++) {
        dump_string(M, f->locals[i].name);
        dump_int(M, f->locals[i].startpc);
        dump_int(M, f->locals[i].endpc);
    }
    n = D(M).strip ? 0 : f->sizeupvals;
    dump_int(M, n);
    for (int32_t i = 0; i < n; i++)
        dump_string(M, f->upvals[i].name);
}


static void dump_function(MarshalState *M, const Proto *f) {
    dump_byte(M, f->isvararg);
    dump_int(M, f->defline);
    dump_int(M, f->deflastline);
    dump_int(M, f->arity);
    dump_int(M, f->maxstack);
    dump_code(M, f);
    dump_constants(M, f);
    dump_upvalues(M, f);
    dump_protos(M, f);
    dump_debug(M, f);
}


int32_t tokuZ_dump(toku_State *T, const Proto *f, toku_Writer writer, void *data,
                              int32_t strip) {
    MarshalState M = {
        .T = T,
        .u = {.d = { .writer = writer, .data = data, .strip = strip }}
    };
    M.h = tokuH_new(T); /* aux. table  to keep strings already dumped */
    settval2s(T, T->sp.p, M.h); /* anchor it */
    T->sp.p++;
    dump_header(&M);
    dump_int(&M, f->sizeupvals);
    dump_function(&M, f);
    dump_block(&M, NULL, 0); /* signal end of dump */
    return D(&M).status;
}

/* }{===================================================================
** UNDUMP
** ===================================================================== */


static t_noret error(MarshalState *M, const char *why) {
    tokuS_pushfstring(M->T, "%s: bad binary format (%s)", L(M).name, why);
    tokuPR_throw(M->T, TOKU_STATUS_ESYNTAX);
}


static uint8_t load_byte(MarshalState *M) {
    int32_t b = zgetc(L(M).Z);
    if (b == TEOF)
        error(M, "truncated chunk");
    M->offset++;
    return cast_u8(b);
}


#define load_vector(M,b,n)  load_block(M, b, cast_sizet(n)*sizeof((b)[0]))

#define load_var(M,x)       load_vector(M, &x, 1)


static void load_block(MarshalState *M, void *b, size_t size) {
    if (tokuR_read(L(M).Z, b, size) != 0)
        error(M, "truncated chunk");
    M->offset += size;
}


static void load_align(MarshalState *M, uint32_t align) {
    uint32_t padding = align - cast_u32(M->offset % align);
    if (padding < align) { /* (padding == align) means no padding */
        toku_Integer paddingContent; /* in C99 padding value is unspecified */
        load_block(M, &paddingContent, padding);
        toku_assert(M->offset % align == 0);
    }
}


static void check_literal(MarshalState *M, const char *s, const char *msg) {
    char buff[sizeof(TOKU_SIGNATURE) + sizeof(TOKUC_DATA)];
    size_t len = strlen(s);
    load_vector(M, buff, len);
    if (memcmp(s, buff, len) != 0)
        error(M, msg);
}


static t_noret numerror(MarshalState *M, const char *what, const char *tname) {
    const char *msg = tokuS_pushfstring(M->T, "%s %s mismatch", tname, what);
    error(M, msg);
}


static void check_numsize(MarshalState *M, int32_t size, const char *tname) {
    if (size != load_byte(M)) numerror(M, "size", tname);
}


static void check_numfmt(MarshalState *M, int32_t eq, const char *tname) {
    if (!eq) numerror(M, "format", tname);
}


#define check_num(M,t,val,tname)  \
    { t i; check_numsize(M, sizeof(i), tname); \
      load_var(M, i); \
      check_numfmt(M, i == val, tname); }


static void check_header(MarshalState *M) {
    /* skip 1st char (already read and checked) */
    check_literal(M, &TOKU_SIGNATURE[1], "not a binary chunk");
    if (load_byte(M) != TOKUC_VERSION)
        error(M, "version mismatch");
    if (load_byte(M) != TOKUC_FORMAT)
        error(M, "format mismatch");
    check_literal(M, TOKUC_DATA, "corrupted chunk");
    check_num(M, int32_t, TOKUC_INT, "int");
    check_num(M, uint8_t, TOKUC_OPCODE, "opcode");
    check_num(M, toku_Integer, TOKUC_INT, "Tokudae integer");
    check_num(M, toku_Number, TOKUC_NUM, "Tokudae number");
}


static toku_Unsigned load_varint(MarshalState *M, toku_Unsigned limit) {
    toku_Unsigned x = 0;
    int32_t b;
    limit >>= 7;
    do {
        b = load_byte(M);
        if (x > limit)
            error(M, "integer overflow");
        x = (x << 7) | (b & 0x7f);
    } while (b & 0x80); /* while byte is a continuation byte */
    return x;
}


static int32_t load_int(MarshalState *M) {
    return cast_i32(load_varint(M, cast_sizet(INT_MAX)));
}


static void load_code(MarshalState *M, Proto *f) {
    int32_t n = load_int(M);
    load_align(M, sizeof(f->code[0]));
    f->code = tokuM_newarraychecked(M->T, n, uint8_t);
    f->sizecode = n;
    load_vector(M, f->code, n);
}


static toku_Number load_number(MarshalState *M) {
    toku_Number x;
    load_var(M, x);
    return x;
}


static toku_Integer load_integer(MarshalState *M) {
    toku_Unsigned cx = load_varint(M, TOKU_UNSIGNED_MAX);
    /* decode unsigned to signed */
    if ((cx & 1) != 0) /* it is a negative integer (odd)? */
        return t_castU2S(~(cx >> 1));
    else /* otherwise integer is positive or zero */
        return t_castU2S(cx >> 1);
}


static size_t load_size(MarshalState *M) {
    return cast_sizet(load_varint(M, TOKU_MAXSIZE));
}


/*
** Load a nullable string into slot 'sl' from prototype 'p'. The
** assignment to the slot and the barrier must be performed before any
** possible GC activity, to anchor the string. (Both 'load_vector' and
** 'tokuH_setint' can call the GC.)
*/
static void load_string(MarshalState *M, Proto *p, OString **sl) {
    toku_State *T = M->T;
    size_t size = load_size(M); /* get string size */
    OString *str;
    TValue sv;
    if (size == 0) { /* no string? */
        toku_assert(*sl == NULL); /* must be prefilled */
        return; /* done */
    } else if (size == 1) { /* previously saved string? */
        toku_Unsigned idx = load_varint(M, TOKU_UNSIGNED_MAX); /* get index */
        TValue stv;
        if (novariant(tokuH_getint(M->h, t_castU2S(idx), &stv)) != TOKU_T_STRING)
            error(M, "invalid string index");
        *sl = str = strval(&stv); /* get its value */
        tokuG_objbarrier(T, p, str);
        return; /* done; do not save it again */
    } else if ((size -= 2) <= TOKUI_MAXSHORTLEN) { /* short string? */
        char buff[TOKUI_MAXSHORTLEN + 1]; /* extra space for '\0' */
        load_vector(M, buff, size + 1); /* load string into buffer */
        *sl = str = tokuS_newl(T, buff, size); /* create string */
        tokuG_objbarrier(T, p, str);
    } else { /* otherwise long string */
        *sl = str = tokuS_newlngstrobj(T, size); /* create string */
        tokuG_objbarrier(T, p, str);
        /* load directly into string 'bytes' */
        load_vector(M, getlngstr(str), size + 1);
    }
    /* add string to list of saved strings */
    setstrval(T, &sv, str);
    tokuH_setint(T, M->h, t_castU2S(M->nstr), &sv);
    tokuG_objbarrierback(T, obj2gco(M->h), str);
    M->nstr++;
}


static void load_constants(MarshalState *M, Proto *f) {
    int32_t n = load_int(M);
    f->k = tokuM_newarraychecked(M->T, n, TValue);
    f->sizek = n;
    for (int32_t i = 0; i < n; i++) /* make array valid for GC */
        setnilval(&f->k[i]);
    for (int32_t i = 0; i < n; i++) {
        TValue *o = &f->k[i];
        int32_t tt = load_byte(M);
        switch (tt) {
            case TOKU_VTRUE: setbtval(&f->k[i]); break;
            case TOKU_VFALSE: setbfval(&f->k[i]); break;
            case TOKU_VNIL: setnilval(&f->k[i]); break;
            case TOKU_VNUMFLT: setfval(o, load_number(M)); break;
            case TOKU_VNUMINT: setival(o, load_integer(M)); break;
            case TOKU_VLNGSTR: case TOKU_VSHRSTR: {
                toku_assert(f->source == NULL);
                load_string(M, f, &f->source); /* use 'source' as anchor */
                if (f->source == NULL)
                    error(M, "bad format for constant string");
                setstrval(M->T, o, f->source); /* save it in the right place */
                f->source = NULL;
                break;
            }
            default: error(M, "invalid constant");
        }
    }
}


static void load_upvalues(MarshalState *M, Proto *f) {
    int32_t n = load_int(M);
    f->upvals = tokuM_newarraychecked(M->T, n, UpValInfo);
    f->sizeupvals = n;
    for (int32_t i = 0; i < n; i++) /* make array valid for GC */
        f->upvals[i].name = NULL;
    for (int32_t i = 0; i < n; i++) { /* following calls can raise errors */
        f->upvals[i].idx = load_int(M);
        f->upvals[i].instack = load_byte(M);
        f->upvals[i].kind = load_byte(M);
    }
}


static void load_function(MarshalState *M, Proto *f);

static void load_protos(MarshalState *M, Proto *f) {
    toku_State *T = M->T;
    int32_t n = load_int(M);
    f->p = tokuM_newarraychecked(T, n, Proto *);
    f->sizep = n;
    for (int32_t i = 0; i < n; i++) /* make array valid for GC */
        f->p[i] = NULL;
    for (int32_t i = 0; i < n; i++) {
        f->p[i] = tokuF_newproto(T);
        tokuG_objbarrier(T, f, f->p[i]);
        load_function(M, f->p[i]);
    }
}


static void load_debug(MarshalState *M, Proto *f) {
    toku_State *T = M->T;
    int32_t n;
    load_string(M, f, &f->source);
    n = load_int(M);
    if (n > 0) {
        f->lineinfo = tokuM_newarraychecked(T, n, int8_t);
        f->sizelineinfo = n;
    }
    load_vector(M, f->lineinfo, n);
    n = load_int(M);
    if (n > 0) {
        load_align(M, sizeof(int32_t));
        f->abslineinfo = tokuM_newarraychecked(T, n, AbsLineInfo);
        f->sizeabslineinfo = n;
        load_vector(M, f->abslineinfo, n);
    }
    n = load_int(M);
    if (n > 0) {
        f->opcodepc = tokuM_newarraychecked(T, n, int32_t);
        f->sizeopcodepc = n;
        load_vector(M, f->opcodepc, n);
    }
    n = load_int(M);
    if (n > 0) {
        f->locals = tokuM_newarraychecked(T, n, LVarInfo);
        f->sizelocals = n;
        for (int32_t i = 0; i < n; i++) /* make valid for GC */
            f->locals[i].name = NULL;
        for (int32_t i = 0; i < n; i++) {
            load_string(M, f, &f->locals[i].name);
            f->locals[i].startpc = load_int(M);
            f->locals[i].endpc = load_int(M);
        }
    }
    n = load_int(M);
    if (n != 0) { /* does it have debug information? */
        toku_assert(n == f->sizeupvals);
        n = f->sizeupvals; /* must be this many */
        for (int32_t i = 0; i < n; i++)
            load_string(M, f, &f->upvals[i].name);
    }
}


static void load_function(MarshalState *M, Proto *f) {
    f->isvararg = load_byte(M);
    f->defline = load_int(M);
    f->deflastline = load_int(M);
    f->arity = load_int(M);
    f->maxstack = load_int(M);
    load_code(M, f);
    load_constants(M, f);
    load_upvalues(M, f);
    load_protos(M, f);
    load_debug(M, f);
}


TClosure *tokuZ_undump(toku_State *T, BuffReader *Z, const char *name) {
    MarshalState M = {
        .T = T,
        .u = {.l = { .Z = Z }},
        .offset = 1, /* first byte is already read */
    };
    TClosure *cl;
    if (*name == '@' || *name == '=')
        name++;
    else if (*name == TOKU_SIGNATURE[0])
        name = "binary string";
    L(&M).name = name;
    check_header(&M);
    cl = tokuF_newTclosure(T, load_int(&M));
    setclTval2s(T, T->sp.p, cl);
    tokuT_incsp(T);
    M.h = tokuH_new(T); /* create list of saved strings */
    settval2s(T, T->sp.p, M.h); /* anchor it */
    tokuT_incsp(T);
    cl->p = tokuF_newproto(T);
    tokuG_objbarrier(T, cl, cl->p);
    load_function(&M, cl->p);
    if (cl->nupvals != cl->p->sizeupvals)
        error(&M, "corrupted chunk");
    T->sp.p--; /* pop table */
    return cl;
}

/* }==================================================================== */
