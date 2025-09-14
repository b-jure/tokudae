/*
** tstrlib.c
** Standard library for string operations
** See Copyright Notice in tokudae.h
*/

#define tstrlib_c
#define TOKU_LIB

#include "tokudaeprefix.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokudae.h"

#include "tstrlib.h"
#include "tokudaeaux.h"
#include "tokudaelib.h"
#include "tokudaelimits.h"


#if !defined(TOKU_BYTES)

/* uppercase ASCII letters */
#define TOKU_BYTES_UPPERCASE        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

/* lowercase ASCII letters */
#define TOKU_BYTES_LOWERCASE        "abcdefghijklmnopqrstuvwxyz"

/* (uppercase/lowercase) */
#define TOKU_BYTES_LETTERS          TOKU_BYTES_UPPERCASE TOKU_BYTES_LOWERCASE


/* octal digits */
#define TOKU_BYTES_OCTDIGITS        "01234567"

/* decimal digits */
#define TOKU_BYTES_DIGITS           TOKU_BYTES_OCTDIGITS "89"

/* hexadecimal digits */
#define TOKU_BYTES_HEXDIGITS        TOKU_BYTES_DIGITS "abcdef"


/* punctuation chars */
#define TOKU_BYTES_PUNCTUATION      "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"

/* whitespace chars */
#define TOKU_BYTES_WHITESPACE       " \t\n\r\v\f"

/* graphical chars */
#define TOKU_BYTES_PRINTABLE    TOKU_BYTES_DIGITS TOKU_BYTES_LETTERS \
                                TOKU_BYTES_PUNCTUATION TOKU_BYTES_WHITESPACE

#endif


/* common error message */
static const char *strtoolong = "string slice too long";


/*
** Translate relative end position to absolute slice index.
*/
static size_t posrelEnd(toku_Integer pos, size_t len) {
    toku_assert(-cast_sz2S(len) <= pos); /* should be handled already */
    if (cast_sz2S(len) <= pos) /* absolute that overflows? */
        return len - (len>0); /* clip to last index */
    else if (0 <= pos) /* absolute in-range 'pos'? */
        return cast_sizet(pos);
    else /* otherwise negative 'pos' */
        return len + cast_sizet(pos);
}


static const char *skipws(const char *s, size_t *pl, int rev) {
    const unsigned char *p;
    size_t l = *pl;
    if (l == 0)
        return NULL;
    else if (!rev) { /* from 's'? */
        p = (const t_ubyte *)s;
        while (l > 0 && isspace(*p))
            p++, l--;
    } else { /* reverse */
        p = ((const t_ubyte *)s + l) - 1;
        while (l > 0 && isspace(*p))
            p--, l--;
    }
    *pl = l;
    return (l > 0 ? (const char *)p : NULL);
}


static int split_into_list(toku_State *T, int rev) {
    size_t ls;
    const char *s = tokuL_check_lstring(T, 0, &ls); /* string */
    toku_Integer n = tokuL_opt_integer(T, 2, TOKU_INTEGER_MAX-1); /* maxsplit */
    int arr = toku_getntop(T);
    int i = 0;
    const char *aux;
    if (toku_is_noneornil(T, 1)) { /* split by whitespace? */
        toku_push_list(T, 1);
        if (!rev)
            while (ls > 0 && isspace(uchar(*s)))
                s++, ls--;
        else {
            aux = (s + ls) - 1;
            while (ls > 0 && isspace(uchar(*aux)))
                aux--, ls--;
        }
        while (n > 0 && (aux = skipws(s, &ls, rev)) != NULL) {
            size_t lw = 0;
            const char *p = aux;
            if (!rev)
                while (ls > 0 && !isspace(uchar(*aux)))
                    ls--, lw++, aux++;
            else { /* reverse */
                while (ls > 0 && !isspace(uchar(*aux)))
                    ls--, lw++, aux--;
                p = aux+1;
            }
            toku_assert(lw > 0);
            toku_push_lstring(T, p, lw);
            toku_set_index(T, arr, i);
            if (!rev) s = aux;
            n--; i++;
        }
        if (!rev)
            while (ls > 0 && isspace(uchar(*s)))
                s++, ls--;
        else {
            aux = (s + ls) - 1;
            while (ls > 0 && isspace(uchar(*aux)))
                aux--, ls--;
        }
        if (ls == 0)
            return 1; /* done */
    } else { /* else split by pattern */
        size_t lpat;
        const char *pat = tokuL_check_lstring(T, 1, &lpat);
        const char *e = s+ls;
        toku_push_list(T, 1);
        if (n < 1 || lpat == 0) goto pushs;
        while (n > 0 && (aux = findstr(s, ls, pat, lpat, rev)) != NULL) {
            if (!rev) { /* find from start? */
                toku_push_lstring(T, s, cast_diff2sz(aux - s));
                ls -= cast_diff2sz((aux + lpat) - s);
                s = aux + lpat;
            } else { /* reverse find */
                toku_push_lstring(T, aux+lpat, cast_diff2sz(e - (aux + lpat)));
                e = aux;
                ls = cast_diff2sz(aux - s);
            }
            toku_set_index(T, arr, i);
            n--; i++;
        }
    }
pushs:
    toku_push_lstring(T, s, ls); /* push last piece */
    toku_set_index(T, arr, i);
    return 1; /* return list */
}


static int s_split(toku_State *T) {
    return split_into_list(T, 0);
}


static int s_rsplit(toku_State *T) {
    return split_into_list(T, 1);
}


static int s_startswith(toku_State *T) {
    size_t l1, l2, posi, posj;
    const char *s1 = tokuL_check_lstring(T, 0, &l1);
    const char *s2 = tokuL_check_lstring(T, 1, &l2);
    toku_Integer i = tokuL_opt_integer(T, 2, 0);
    toku_Integer j = tokuL_opt_integer(T, 3, -1);
    if (j < -cast_Integer(l1)) /* 'j' would be less than 0? */
        goto l_fail; /* empty interval */
    else { /* convert to positions */
        posi = posrelStart(i, l1);
        posj = posrelEnd(j, l1);
    }
    if (posi <= posj && l2 <= (posj-posi) + 1) {
        size_t k = 0;
        while (posi <= posj && k < l2 && s1[posi] == s2[k])
            posi++, k++;
        toku_push_integer(T, cast_sz2S(posi));
        if (k == l2)
            return 1; /* return index after 's2' in 's1' */
        else {
            tokuL_push_fail(T);
            toku_insert(T, -2);
        }
    } else {
    l_fail:
        tokuL_push_fail(T);
        toku_push_integer(T, -1); /* invalid range */
    }
    return 2; /* return fail and invalid or non-matching index */
}


static int s_reverse(toku_State *T) {
    size_t l, i;
    tokuL_Buffer b;
    const char *s = tokuL_check_lstring(T, 0, &l);
    if (l > 1) { /* non trivial string? */
        char *p = tokuL_buff_initsz(T, &b, l);
        char *end = p + l - 1;
        for (i = 0; p < end; i++) {
            *p++ = s[l-i-1];
            *end-- = s[i];
        }
        tokuL_buff_endsz(&b, l);
    }
    return 1; /* return string */
}


static int s_repeat(toku_State *T) {
    size_t l, lsep;
    const char *s = tokuL_check_lstring(T, 0, &l);
    toku_Integer n = tokuL_check_integer(T, 1);
    const char *sep = tokuL_opt_lstring(T, 2, "", &lsep);
    if (t_unlikely(n <= 0))
        toku_push_literal(T, "");
    else if (l + lsep < l || TOKU_MAXSIZE / cast_sizet(n) < l + lsep)
        tokuL_error(T, "resulting string too large");
    else {
        tokuL_Buffer b;
        size_t totalsize = (cast_sizet(n) * (l + lsep)) - lsep;
        char *p = tokuL_buff_initsz(T, &b, totalsize);
        while (n-- > 1) {
            memcpy(p, s, l * sizeof(char)); p += l;
            if (lsep > 0) { /* branch costs less than empty 'memcpy' copium */
                memcpy(p, sep, lsep * sizeof(char));
                p += lsep;
            }
        }
        memcpy(p, s, l * sizeof(char)); /* last copy without separator */
        tokuL_buff_endsz(&b, totalsize);
    }
    return 1; /* return final string */
}


static void auxjoinstr(tokuL_Buffer *b, const char *s, size_t l,
                                      const char *sep, size_t lsep) {
    tokuL_buff_push_lstring(b, s, l);
    if (lsep > 0) /* non empty separator? */
        tokuL_buff_push_lstring(b, sep, lsep);
}


static void joinfromtable(toku_State *T, tokuL_Buffer *b,
                          const char *sep, size_t lsep) {
    toku_push_nil(T);
    while (toku_nextfield(T, 1) != 0) {
        size_t l;
        const char *s = toku_to_lstring(T, -1, &l);
        int pop = 1; /* value */
        if (s && l > 0) {
            toku_push(T, -3); /* push buffer */
            auxjoinstr(b, s, l, sep, lsep);
            pop++; /* buffer */
        }
        toku_pop(T, pop);
    }
}


static void joinfromlist(toku_State *T, tokuL_Buffer *b,
                         const char *sep, size_t lsep, int len) {
    for (int i = 0; i < len; i++) {
        size_t l;
        const char *s;
        toku_get_index(T, 1, i);
        s = toku_to_lstring(T, -1, &l);
        toku_pop(T, 1);
        if (s && l > 0)
            auxjoinstr(b, s, l, sep, lsep);
    }
}


static int s_join(toku_State *T) {
    tokuL_Buffer b;
    size_t lsep;
    const char *sep = tokuL_check_lstring(T, 0, &lsep);
    int t = toku_type(T, 1);
    tokuL_expect_arg(T, t == TOKU_T_LIST || t == TOKU_T_TABLE, 1, "list/table");
    tokuL_buff_init(T, &b);
    if (t == TOKU_T_LIST) {
        int len = cast_int(t_castU2S(toku_len(T, 1)));
        if (len > 0)
            joinfromlist(T, &b, sep, lsep, len);
    } else
        joinfromtable(T, &b, sep, lsep);
    if (tokuL_bufflen(&b) > 0 && lsep > 0) /* buffer has separator? */
        tokuL_buffsub(&b, lsep); /* remove it */
    tokuL_buff_end(&b);
    return 1; /* return final string */
}


/* {======================================================
** STRING FORMAT
** ======================================================= */

/*
** Maximum size for items formatted with '%f'. This size is produced
** by format('%.99f', -maxfloat), and is equal to 99 + 3 ('-', '.',
** and '\0') + number of decimal digits to represent maxfloat (which
** is maximum exponent + 1). (99+3+1, adding some extra, 110)
*/
#define MAX_ITEMF	(110 + t_floatatt(MAX_10_EXP))


/*
** All formats except '%f' do not need that large limit.  The other
** float formats use exponents, so that they fit in the 99 limit for
** significant digits; 's' for large strings and 'q' add items directly
** to the buffer; all integer formats also fit in the 99 limit.  The
** worst case are floats: they may need 99 significant digits, plus
** '0x', '-', '.', 'e+XXXX', and '\0'. Adding some extra, 120.
*/
#define MAX_ITEM    120


#if !defined(T_FMTC)

/* conversion specification introducer */
#define T_FMTC          '%'

/* valid flags for a, A, e, E, f, F, g, and G conversions */
#define T_FMTFLAGSF     "-+#0 "

/* valid flags for o, x, and X conversions */
#define T_FMTFLAGSX	"-#0"

/* valid flags for d and i conversions */
#define T_FMTFLAGSI	"-+0 "

/* valid flags for u conversions */
#define T_FMTFLAGSU	"-0"

/* valid flags for c, p, and s conversions */
#define T_FMTFLAGSC	"-"

#endif


/*
** Maximum size of each format specification (such as "%-099.99d"):
** Initial '%', flags (up to 5), width (2), period, precision (2),
** length modifier (8), conversion specifier, and final '\0', plus some
** extra.
*/
#define MAX_FORMAT	32


static void addquoted(tokuL_Buffer *b, const char *s, size_t len) {
    tokuL_buff_push(b, '"');
    while (len--) {
        if (*s == '"' || *s == '\\' || *s == '\n') {
            tokuL_buff_push(b, '\\');
            tokuL_buff_push(b, *s);
        }
        else if (iscntrl(uchar(*s))) {
            char buff[10];
            if (!isdigit(uchar(*(s+1))))
                t_snprintf(buff, sizeof(buff), "\\%d", cast_int(uchar(*s)));
            else
                t_snprintf(buff, sizeof(buff), "\\%03d", cast_int(uchar(*s)));
            tokuL_buff_push_string(b, buff);
        }
        else
            tokuL_buff_push(b, *s);
        s++;
    }
    tokuL_buff_push(b, '"');
}


/*
** Serialize a floating-point number in such a way that it can be
** scanned back by Tokudae. Use hexadecimal format for "common" numbers
** (to preserve precision); inf, -inf, and NaN are handled separately.
** (NaN cannot be expressed as a numeral, so we write '(0/0)' for it.)
*/
static int quotefloat(toku_State *T, char *buff, toku_Number n) {
    const char *s; /* for the fixed representations */
    if (n == cast_num(HUGE_VAL)) /* inf? */
        s = "1e9999";
    else if (n == -cast_num(HUGE_VAL)) /* -inf? */
        s = "-1e9999";
    else if (n != n) /* NaN? */
        s = "(0/0)";
    else { /* format number as hexadecimal */
        int nb = toku_number2strx(T, buff, MAX_ITEM, "%"TOKU_NUMBER_FMTLEN"a", n);
        /* ensures that 'buff' string uses a dot as the radix character */
        if (memchr(buff, '.', cast_uint(nb)) == NULL) { /* no dot? */
            char point = toku_getlocaledecpoint(); /* try locale point */
            char *ppoint = cast_charp(memchr(buff, point, cast_uint(nb)));
            if (ppoint) *ppoint = '.'; /* change it to a dot */
        }
        return nb;
    }
    /* for the fixed representations */
    return t_snprintf(buff, MAX_ITEM, "%s", s);
}


static void addliteral(toku_State *T, tokuL_Buffer *b, int arg) {
    switch (toku_type(T, arg)) {
        case TOKU_T_STRING: {
            size_t len;
            const char *s = toku_to_lstring(T, arg, &len);
            addquoted(b, s, len);
            break;
        }
        case TOKU_T_NUMBER: {
            char *buff = tokuL_buff_ensure(b, MAX_ITEM);
            int nb;
            if (!toku_is_integer(T, arg)) /* float? */
                nb = quotefloat(T, buff, toku_to_number(T, arg));
            else { /* integers */
                toku_Integer n = toku_to_integer(T, arg);
                const char *format = (n == TOKU_INTEGER_MIN) /* corner case? */
                    ? "0x%" TOKU_INTEGER_FMTLEN "x" /* use hex */
                    : TOKU_INTEGER_FMT; /* else use default format */
                nb = t_snprintf(buff, MAX_ITEM, format, n);
            }
            tokuL_buffadd(b, cast_uint(nb));
            break;
        }
        case TOKU_T_NIL: case TOKU_T_BOOL: {
            tokuL_to_lstring(T, arg, NULL);
            tokuL_buff_push_stack(b);
            break;
        }
        default:
            tokuL_error_arg(T, arg, "value has no literal form");
    }
}


static const char *get2digits (const char *s) {
    if (isdigit(uchar(*s))) {
        s++;
        if (isdigit(uchar(*s))) s++; /* (2 digits at most) */
    }
    return s;
}


/*
** Check whether a conversion specification is valid. When called,
** first character in 'form' must be '%' and last character must
** be a valid conversion specifier. 'flags' are the accepted flags;
** 'precision' signals whether to accept a precision.
*/
static void checkformat(toku_State *T, const char *form, const char *flags,
                        int precision) {
    const char *spec = form + 1; /* skip '%' */
    spec += strspn(spec, flags); /* skip flags */
    if (*spec != '0') { /* a width cannot start with '0' */
        spec = get2digits(spec); /* skip width */
        if (*spec == '.' && precision) {
            spec++;
            spec = get2digits(spec); /* skip precision */
        }
    }
    if (!isalpha(uchar(*spec))) /* did not go to the end? */
        tokuL_error(T, "invalid conversion specification: '%s'", form);
}


/*
** Get a conversion specification and copy it to 'form'.
** Return the address of its last character.
*/
static const char *getformat(toku_State *T, const char *strfrmt, char *form) {
    /* spans flags, width, and precision ('0' is included as a flag) */
    size_t len = strspn(strfrmt, T_FMTFLAGSF "123456789.");
    len++;  /* adds following character (should be the specifier) */
    /* still needs space for '%', '\0', plus a length modifier */
    if (len >= MAX_FORMAT - 10)
        tokuL_error(T, "invalid format (too long)");
    *(form++) = '%';
    memcpy(form, strfrmt, len * sizeof(char));
    *(form + len) = '\0';
    return strfrmt + len - 1;
}


/*
** Add length modifier into formats.
*/
static void addlenmod(char *form, const char *lenmod) {
    size_t l = strlen(form);
    size_t lm = strlen(lenmod);
    char spec = form[l - 1];
    strcpy(form + l - 1, lenmod);
    form[l + lm - 1] = spec;
    form[l + lm] = '\0';
}


static int formatstr(toku_State *T, const char *fmt, size_t lfmt) {
    int top = toku_gettop(T);
    int arg = 0;
    const char *efmt = fmt + lfmt;
    const char *flags;
    tokuL_Buffer b;
    tokuL_buff_init(T, &b);
    while (fmt < efmt) {
        if (*fmt != T_FMTC) { /* not % */
            tokuL_buff_push(&b, *fmt++);
            continue;
        } else if (*++fmt == T_FMTC) { /* %% */
            tokuL_buff_push(&b, *fmt++);
            continue;
        } /* else '%' */
        char form[MAX_FORMAT]; /* to store the format ('%...') */
        t_uint maxitem = MAX_ITEM; /* maximum length for the result */
        char *buff = tokuL_buff_ensure(&b, maxitem); /* to put result */
        int nb = 0; /* number of bytes in result */
        if (++arg > top) /* too many format specifiers? */
            return tokuL_error_arg(T, arg, "missing format value");
        fmt = getformat(T, fmt, form);
        switch (*fmt++) {
            case 'c': {
                checkformat(T, form, T_FMTFLAGSC, 0);
                nb = t_snprintf(buff, maxitem, form,
                                cast_int(tokuL_check_integer(T, arg)));
                break;
            }
            case 'd': case 'i':
                flags = T_FMTFLAGSI;
                goto intcase;
            case 'u':
                flags = T_FMTFLAGSU;
                goto intcase;
            case 'o': case 'x': case 'X':
                flags = T_FMTFLAGSX;
            intcase: {
                toku_Integer n = tokuL_check_integer(T, arg);
                checkformat(T, form, flags, 1);
                addlenmod(form, TOKU_INTEGER_FMTLEN);
                nb = t_snprintf(buff, maxitem, form, n);
                break;
            }
            case 'a': case 'A':
                checkformat(T, form, T_FMTFLAGSF, 1);
                addlenmod(form, TOKU_NUMBER_FMTLEN);
                nb = toku_number2strx(T, buff, maxitem, form,
                                         tokuL_check_number(T, arg));
                break;
            case 'f':
                     maxitem = MAX_ITEMF; /* extra space for '%f' */
                     buff = tokuL_buff_ensure(&b, maxitem);
                     /* fall through */
            case 'e': case 'E': case 'g': case 'G': {
                toku_Number n = tokuL_check_number(T, arg);
                checkformat(T, form, T_FMTFLAGSF, 1);
                addlenmod(form, TOKU_NUMBER_FMTLEN);
                nb = t_snprintf(buff, maxitem, form, n);
                break;
            }
            case 'p': {
                const void *p = toku_to_pointer(T, arg);
                checkformat(T, form, T_FMTFLAGSC, 0);
                if (p == NULL) { /* avoid calling 'printf' with NULL */
                    p = "(null)"; /* result */
                    form[strlen(form) - 1] = 's'; /* format it as a string */
                }
                nb = t_snprintf(buff, maxitem, form, p);
                break;
            }
            case 'q': {
                if (form[2] == '\0') { /* no modifiers? */
                    addliteral(T, &b, arg);
                    break;
                } /* otherwise have modifiers */
                return tokuL_error(T, "specifier '%%q' cannot have modifiers");
            }
            case 's': {
                size_t l;
                const char *s = tokuL_to_lstring(T, arg, &l);
                if (form[2] == '\0') /* no modifiers? */
                    tokuL_buff_push_stack(&b); /* keep entire string */
                else {
                    tokuL_check_arg(T, l == strlen(s), arg, "string contains zeros");
                    checkformat(T, form, T_FMTFLAGSC, 1);
                    if (strchr(form, '.') == NULL && l >= 100) {
                        /* no precision and string is too long to be formatted */
                        tokuL_buff_push_stack(&b); /* keep entire string */
                    }
                    else { /* format the string into 'buff' */
                        nb = t_snprintf(buff, maxitem, form, s);
                        toku_pop(T, 1); /* remove result from 'tokuL_tolstring' */
                    }
                }
                break;
            }
            default: { /* also treat cases 'pnLlh' */
                return tokuL_error(T, "invalid conversion '%s' to 'format'", form);
            }
        }
        toku_assert(cast_uint(nb) < maxitem);
        tokuL_buffadd(&b, cast_uint(nb));
    }
    tokuL_buff_end(&b);
    return 1; /* return formatted string */
}


static int s_fmt(toku_State *T) {
    size_t lfmt;
    const char *fmt = tokuL_check_lstring(T, 0, &lfmt);
    if (lfmt == 0) {
        toku_push_literal(T, "");
        return 1;
    }
    return formatstr(T, fmt, lfmt);
}

/* }====================================================== */


static int auxtocase(toku_State *T, int (*f)(int c)) {
    size_t l, posi, posj, endpos;
    const char *s = tokuL_check_lstring(T, 0, &l);
    toku_Integer i = tokuL_opt_integer(T, 1, 0);
    toku_Integer j = tokuL_opt_integer(T, 2, -1);
    if (j < -cast_sz2S(l)) /* 'j' would be less than 0? */
        goto l_done; /* empty interval */
    else { /* convert to positions */
        posi = posrelStart(i, l);
        posj = posrelEnd(j, l);
        endpos = posj + 1; /* save end position */
    }
    if (l == 0 || posj < posi) {
        l_done: toku_push(T, 0);
    } else {
        tokuL_Buffer b;
        char *p = tokuL_buff_initsz(T, &b, l);
        memcpy(p, s, posi);
        while (posi < posj) {
            p[posi] = cast_char(f(s[posi])); posi++;
            p[posj] = cast_char(f(s[posj])); posj--;
        }
        p[posi] = cast_char(f(s[posi]));
        memcpy(p + endpos, s + endpos, l - endpos);
        tokuL_buff_endsz(&b, l);
    }
    return 1; /* return final string */
}


static int s_toupper(toku_State *T) {
    return auxtocase(T, toupper);
}


static int s_tolower(toku_State *T) {
    return auxtocase(T, tolower);
}


static int auxfind(toku_State *T, int rev) {
    size_t l, lpat, posi, posj;
    const char *s = tokuL_check_lstring(T, 0, &l);
    const char *pat = tokuL_check_lstring(T, 1, &lpat);
    toku_Integer i = tokuL_opt_integer(T, 2, 0);
    toku_Integer j = tokuL_opt_integer(T, 3, -1);
    if (j < -cast_sz2S(l)) /* 'j' would be less than 0? */
        goto l_fail; /* empty interval */
    else { /* convert to positions */
        posi = posrelStart(i, l);
        posj = posrelEnd(j, l);
    }
    const char *p;
    if (posi <= posj && (p = findstr(s+posi, (posj-posi)+1, pat, lpat, rev)))
        toku_push_integer(T, p-s); /* start index */
    else {
        l_fail: tokuL_push_fail(T); /* nothing was found */
    }
    return 1;
}


static int s_find(toku_State *T) {
    return auxfind(T, 0);
}


static int s_rfind(toku_State *T) {
    return auxfind(T, 1);
}


// TODO: optimize with bitmask
static int aux_span(toku_State *T, int complement) {
    size_t l, lb, posi, posj, startpos;
    const char *s = tokuL_check_lstring(T, 0, &l);
    const char *b = tokuL_check_lstring(T, 1, &lb);
    toku_Integer i = tokuL_opt_integer(T, 2, 0);
    toku_Integer j = tokuL_opt_integer(T, 3, -1);
    if (j < -cast_sz2S(l)) /* 'j' would be less than 0? */
        goto l_fail; /* empty interval */
    posi = posrelStart(i, l);
    posj = posrelEnd(j, l);
    startpos = posi; /* save starting position */
    if (posj < posi) { /* empty interval? */
    l_fail:
        tokuL_push_fail(T);
        return 1;
    } else if (complement) { /* strcspn */
        if (lb == 0) /* 'b' is empty string? */
            posi = l - posi; /* whole segment is valid */
        else {
            while (posi <= posj) {
                for (size_t k = 0; k < lb; k++)
                    if (s[posi] == b[k]) goto done;
                posi++;
            }
        }
    } else { /* strspn */
        if (lb == 0) /* 'b' is empty string? */
            posi = startpos; /* 0 */
        else {
            while (posi <= posj) {
                for (size_t k = 0; k < lb; k++)
                    if (s[posi] == b[k]) goto nextc;
                break; /* push segment len */
            nextc:
                posi++;
            }
        }
    }
done:
    /* return computed segment length (span) */
    toku_push_integer(T, cast_sz2S(posi - startpos));
    return 1;
}


static int s_span(toku_State *T) {
    return aux_span(T, 0);
}


static int s_cspan(toku_State *T) {
    return aux_span(T, 1);
}


static int s_replace(toku_State *T) {
    size_t l, lpat, lv;
    const char *s = tokuL_check_lstring(T, 0, &l);
    const char *pat = tokuL_check_lstring(T, 1, &lpat);
    const char *v = tokuL_check_lstring(T, 2, &lv);
    toku_Integer n = tokuL_opt_integer(T, 3, TOKU_INTEGER_MAX);
    if (n <= 0) /* no replacements? */
        toku_setntop(T, 1); /* remove all except the original string */
    else if (lpat == 0) /* pattern is empty string? */
        toku_push_lstring(T, v, lv); /* return replacement string */
    else {
        tokuL_Buffer b;
        const char *p;
        tokuL_buff_init(T, &b);
        while (0 < n && (p = findstr(s, l, pat, lpat, 0))) {
            size_t sz = cast_diff2sz(p - s);
            tokuL_buff_push_lstring(&b, s, sz); /* push prefix */
            tokuL_buff_push_lstring(&b, v, lv); /* push replacement text */
            l -= sz + lpat; /* subtract prefix and pattern length */
            s = p + lpat; /* go after the pattern */
            n--; /* one less replacement to do */
        }
        tokuL_buff_push_lstring(&b, s, l); /* push remaining string */
        tokuL_buff_end(&b);
    }
    return 1; /* return final string */
}


static int s_substr(toku_State *T) {
    size_t l, posi, posj;
    const char *s = tokuL_check_lstring(T, 0, &l);
    toku_Integer i = tokuL_opt_integer(T, 1, 0);
    toku_Integer j = tokuL_opt_integer(T, 2, -1);
    if (toku_to_bool(T, 3)) { /* positions must be absolute? */
        if (!(i < 0 || j < 0 || j < i)) {
            posi = cast_sizet(t_castS2U(i));
            posj = cast_sizet(t_castS2U(j));
        } else { /* otherwise force to push empty string */
            posi = 1;
            posj = 0;
        }
    } else if (-cast_sz2S(l) <= j) {
        posi = posrelStart(i, l),
        posj = posrelEnd(j, l);
    } else
        goto pushempty;
    if (posi <= posj)
        toku_push_lstring(T, s + posi, (posj - posi) + 1);
    else {
    pushempty:
        toku_push_literal(T, "");
    }
    return 1;
}


#define swapcase(c)         cast_char(isalpha(c) ? (c)^32 : (c))
#define swaptolower(c)      cast_char(isupper(c) ? tolower(c) : (c))
#define swaptoupper(c)      cast_char(islower(c) ? toupper(c) : (c))

static void auxswapcase(char *d, const char *s, size_t posi, size_t posj) {
    t_ubyte c;
    while (posi < posj) {
        c = uchar(s[posi]), d[posi++] = swapcase(c);
        c = uchar(s[posj]), d[posj--] = swapcase(c);
    }
    if (posi == posj)
        c = uchar(s[posi]), d[posi] = swapcase(c);
}


static void auxuppercase(char *d, const char *s, size_t posi, size_t posj) {
    t_ubyte c;
    while (posi < posj) {
        c = uchar(s[posi]), d[posi++] = swaptolower(c);
        c = uchar(s[posj]), d[posj--] = swaptolower(c);
    }
    c = uchar(s[posi]), d[posi] = swaptolower(c);
}


static void auxlowercase(char *d, const char *s, size_t posi, size_t posj) {
    t_ubyte c;
    while (posi < posj) {
        c = uchar(s[posi]), d[posi++] = swaptoupper(c);
        c = uchar(s[posj]), d[posj--] = swaptoupper(c);
    }
    c = uchar(s[posi]), d[posi] = swaptoupper(c);
}


static int auxcase(toku_State *T, void (*f)(char*,const char*,size_t,size_t)) {
    size_t l;
    const char *s = tokuL_check_lstring(T, 0, &l);
    toku_Integer i = tokuL_opt_integer(T, 1, 0);
    toku_Integer j = tokuL_opt_integer(T, 2, -1);
    if (j >= -cast_sz2S(l)) { /* 'j' would be greater than 0? */
        size_t posi = posrelStart(i, l);
        size_t posj = posrelEnd(j, l);
        if (posj < posi || l == 0) /* empty interval or empty string */
            goto l_empty;
        else { /* otherwise build the new string */
            tokuL_Buffer b;
            char *p = tokuL_buff_initsz(T, &b, l);
            memcpy(p, s, posi);
            f(p, s, posi, posj);
            posj++; /* go past the last index that was swapped */
            memcpy(p+posj, s+posj, l-posj);
            tokuL_buff_endsz(&b, l);
        }
    } else { /* otherwise 'j' would be less than 0 */
        l_empty: toku_push(T, 0); /* get original string */
    }
    return 1;
}


static int s_swapcase(toku_State *T) {
    return auxcase(T, auxswapcase);
}


static int s_swapupper(toku_State *T) {
    return auxcase(T, auxuppercase);
}


static int s_swaplower(toku_State *T) {
    return auxcase(T, auxlowercase);
}


#define pushbyte(T,s,i,k)   toku_push_integer(T, uchar((s)[(i) + cast_uint(k)]))

static int getbytes_list(toku_State *T, const char *s, size_t posi, size_t posj) {
    int n = cast_int(posj - posi) + 1;
    toku_push_list(T, n);
    for (int k = 0; k < n; k++) {
        pushbyte(T, s, posi, k);
        toku_set_index(T, -2, k);
    }
    return 1; /* return list */
}


static int getbytes_bytes(toku_State *T, const char *s, size_t posi, size_t posj) {
    int n = cast_int(posj - posi) + 1;
    tokuL_check_stack(T, n, strtoolong);
    for (int k = 0; k < n; k++) pushbyte(T, s, posi, k);
    return n; /* return 'n' bytes */
}


static int auxgetbytes(toku_State *T, int pack) {
    size_t l;
    const char *s = tokuL_check_lstring(T, 0, &l);
    toku_Integer i = tokuL_opt_integer(T, 1, 0);
    toku_Integer j;
    if (!toku_is_noneornil(T, 2)) /* have ending position? */
        j = tokuL_check_integer(T, 2); /* get it */
    else { /* no ending position, set default one */
        if (!pack) { /* not packing bytes into list? */
            if (i < -cast_sz2S(l)) /* 'i' would be less than 0? */
                j = 0; /* clip end position to first index */
            else /* otherwise 'j' will be equal or above 0 */
                j = i;
        } else /* otherwise pack bytes into a list */
            j = -1; /* end position will be the last index */
        goto skipjcheck;
    }
    if (-cast_sz2S(l) <= j) { /* end position will be >=0? */
    skipjcheck:
        if (0 < l) {
            size_t posi = posrelStart(i, l);
            size_t posj = posrelEnd(j, l);
            if (posj >= posi) { /* non-empty interval? */
                if (t_unlikely((posj-posi)+1 <= (posj-posi) ||
                            cast_sizet(INT_MAX) <= (posj-posi)+1))
                    return tokuL_error(T, strtoolong);
                else if (pack) /* pack bytes into a list? */
                    return getbytes_list(T, s, posi, posj);
                else /* otherwise get bytes by pushing them on stack */
                    return getbytes_bytes(T, s, posi, posj);
            }
        }
    }
    return 0; /* nothing to return */
}


static int s_byte(toku_State *T) {
    return auxgetbytes(T, 0);
}


static int s_bytes(toku_State *T) {
    return auxgetbytes(T, 1);
}


static int s_char(toku_State *T) {
    int n = toku_getntop(T); /* number of arguments */
    if (n > 0) { /* have at least 1 argument? */
        tokuL_Buffer b;
        char *p = tokuL_buff_initsz(T, &b, cast_uint(n));
        for (int i=0; i<n; i++) {
            toku_Unsigned c = t_castS2U(tokuL_check_integer(T, i));
            tokuL_check_arg(T, c <= cast_uint(UCHAR_MAX), i,
                               "value out of range");
            p[i] = cast_char(uchar(c));
        }
        tokuL_buff_endsz(&b, cast_uint(n));
    } else /* otherwise no arguments were provided */
        toku_push_literal(T, ""); /* push empty string (a bit faster) */
    return 1;
}


static int s_cmp(toku_State *T) {
    int res;
    size_t l1, l2;
    const char *s1 = tokuL_check_lstring(T, 0, &l1);
    const char *s2 = tokuL_check_lstring(T, 1, &l2);
    size_t i = 0;
    if (l2 > l1) l1 = l2;
    while (l1-- && uchar(s1[i]) == uchar(s2[i])) i++;
    res = s1[i] - s2[i];
    toku_push_integer(T, res);
    if (res) /* strings are not equal? */
        toku_push_integer(T, cast_sz2S(i)); /* push "that" position */
    else /* otherwise strings are equal */
        toku_push_nil(T); /* "that" position doesn't exist */
    return 2; /* return comparison result and position */
}


static const tokuL_Entry strlib[] = {
    {"split", s_split},
    {"rsplit", s_rsplit},
    {"startswith", s_startswith},
    {"reverse", s_reverse},
    {"repeat", s_repeat},
    {"join", s_join},
    {"fmt", s_fmt},
    {"toupper", s_toupper},
    {"tolower", s_tolower},
    {"find", s_find},
    {"rfind", s_rfind},
    {"span", s_span},
    {"cspan", s_cspan},
    {"replace", s_replace},
    {"substr", s_substr},
    {"swapcase", s_swapcase},
    {"swapupper", s_swapupper},
    {"swaplower", s_swaplower},
    {"byte", s_byte},
    {"bytes", s_bytes},
    {"char", s_char},
    {"cmp", s_cmp},
    {"ascii_uppercase", NULL},
    {"ascii_lowercase", NULL},
    {"ascii_letters", NULL},
    {"digits", NULL},
    {"hexdigits", NULL},
    {"octdigits", NULL},
    {"punctuation", NULL},
    {"whitespace", NULL},
    {"printable", NULL},
    {NULL, NULL}
};


static void set_string_bytes(toku_State *T) {
    /* letter bytes */
    toku_push_string(T, TOKU_BYTES_UPPERCASE);
    toku_set_field_str(T, -2, "ascii_uppercase");
    toku_push_string(T, TOKU_BYTES_LOWERCASE);
    toku_set_field_str(T, -2, "ascii_lowercase");
    toku_push_string(T, TOKU_BYTES_LETTERS);
    toku_set_field_str(T, -2, "ascii_letters");
    /* digit bytes */
    toku_push_string(T, TOKU_BYTES_OCTDIGITS);
    toku_set_field_str(T, -2, "octdigits");
    toku_push_string(T, TOKU_BYTES_DIGITS);
    toku_set_field_str(T, -2, "digits");
    toku_push_string(T, TOKU_BYTES_HEXDIGITS);
    toku_set_field_str(T, -2, "hexdigits");
    /* punctuation bytes */
    toku_push_string(T, TOKU_BYTES_PUNCTUATION);
    toku_set_field_str(T, -2, "punctuation");
    /* whitespace bytes */
    toku_push_string(T, TOKU_BYTES_WHITESPACE);
    toku_set_field_str(T, -2, "whitespace");
    /* printable bytes */
    toku_push_string(T, TOKU_BYTES_PRINTABLE);
    toku_set_field_str(T, -2, "printable");
}


int tokuopen_string(toku_State *T) {
    tokuL_push_lib(T, strlib);
    set_string_bytes(T);
    return 1;
}
