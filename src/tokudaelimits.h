/*
** tokudaelimits.h
** Limits, basic types and some other definitions
** See Copyright Notice in tokudae.h
*/


#ifndef tokudaelimits_h
#define tokudaelimits_h

#include "tokudae.h"


typedef size_t          t_umem;
typedef ptrdiff_t       t_mem;

#define TOKU_MAXUMEM    cast_umem(~cast_umem(0))
#define TOKU_MAXMEM     cast_mem(TOKU_MAXUMEM >> 1)


/* maximum value for 'size_t' */
#define TOKU_MAXSIZET   cast_sizet(~cast_sizet(0))


/*
** Maximum size for strings and userdata visible for Tokudae;
** should be representable as a toku_Integer and as a size_t.
*/
#define TOKU_MAXSIZE \
        (sizeof(size_t) < sizeof(toku_Integer) \
            ? TOKU_MAXSIZET : cast_sizet(TOKU_INTEGER_MAX))


/* convert pointer 'p' to uint32_t */
#if defined(UINTPTR_MAX)
#define T_P2I   uintptr_t
#else
#define T_P2I   uintmax_t
#endif

#define pointer2u32(p)     cast_u32(cast(T_P2I, (p)) & UINT32_MAX)


/* inline functions */
#if defined(__GNUC__)
#define t_inline        __inline__
#else
#define t_inline        inline
#endif

/* static inline */
#define t_sinline       static t_inline


/* non-return type */
#if defined(__GNUC__)
#define t_noret         void __attribute__((noreturn))
#elif defined(_MST_VER) && _MST_VER >= 1200
#define t_noret         void __declspec(noreturn)
#else
#define t_noret         void
#endif


/*
** @UNUSED - marks variable unused to avoid compiler warnings.
*/
#if !defined(UNUSED)
#define UNUSED(x)           ((void)(x))
#endif


/*
** Allow threaded code by default on GNU C compilers.
** What this allows is usage of jump table, meaning the use of
** local labels inside arrays, making opcode dispatch O(1)
** inside the interpreter loop.
*/
#if defined(__GNUC__)
#define PRECOMPUTED_GOTO
#endif


/* internal assertions for debugging */
#if defined(TOKUI_ASSERT)
#undef NDEBUG
#include <assert.h>
#define toku_assert(e)      assert(e)
#endif

#if defined(toku_assert)
#define check_exp(c,e)      (toku_assert(c),(e))
#else
#define toku_assert(e)      UNUSED(0)
#define check_exp(c,e)      (e)
#endif

/* C API assertions */
#if !defined(tokui_checkapi)
#define tokui_checkapi(T,e)     (UNUSED(T), toku_assert(e))
#endif

#define api_check(T,e,err)      tokui_checkapi(T, (e) && err)


/*
** Maximum length for short strings, that is, strings that are
** internalized. (Cannot be smaller than reserved words or keys for
** metamethods, as these strings must be internalized;
** strlen("continue") = 8, strlen("__getidx") = 8.)
*/
#if !defined(TOKUI_MAXSHORTLEN)
#define TOKUI_MAXSHORTLEN       40
#endif


/*
** Minimum size for string buffer during lexing, this buffer memory
** will be freed after compilation.
*/
#if !defined(TOKUI_MINBUFFER)
#define TOKUI_MINBUFFER         32
#endif


/*
** Runs each time program enters ('toku_lock') and leaves ('toku_unlock')
** Tokudae core (C API).
*/
#if !defined(toku_lock)
#define toku_lock(T)        UNUSED(T)
#define toku_unlock(T)      UNUSED(T)
#endif



/*
** Casting via macros.
*/


/* type casts (a macro highlights casts in the code) */
#define cast(t, e)          ((t)(e))

#define cast_void(e)        cast(void,(e))
#define cast_voidp(e)       cast(void *,(e))
#define cast_cvoidp(e)      cast(const void *,(e))

#define cast_num(e)         cast(toku_Number,(e))
#define cast_Integer(e)     cast(toku_Integer,(e))
#define cast_Unsigned(e)    cast(toku_Unsigned,(e))

#define cast_charp(e)       cast(char *,(e))
#define cast_char(e)        cast(char,(e))

#define cast_i8(e)          cast(int8_t,(e))
#define cast_u8(e)          cast(uint8_t,(e))
#define cast_u8p(e)         cast(uint8_t *,(e))
#define cast_i16(e)         cast(int16_t,(e))
#define cast_u16(e)         cast(uint16_t,(e))
#define cast_i32(e)         cast(int32_t,(e))
#define cast_u32(e)         cast(uint32_t,(e))

#define cast_mem(e)         cast(t_mem,(e))
#define cast_umem(e)        cast(t_umem,(e))
#define cast_sizet(e)       cast(size_t,(e))


/* cast a signed toku_Integer to toku_Unsigned */
#if !defined(t_castS2U)
#define t_castS2U(i)        cast_Unsigned(i)
#endif

/*
** Cast a toku_Unsigned to a signed toku_Integer; this cast is
** not strict ISO C, but two-complement architectures should
** work fine.
*/
#if !defined(t_castU2S)
#define t_castU2S(i)        cast_Integer(i)
#endif

/* 
** Cast a size_t to toku_Integer: These casts are always valid for
** sizes of Tokudae objects (see TOKU_MAXSIZE).
*/
#define cast_sz2S(sz)       cast_Integer(sz)

/* 
** Casts a ptrdiff_t to size_t when it is known that the minuend
** comes from the subtrahend (the base).
*/
#define cast_diff2sz(df)    cast_sizet(df)

/* cast a ptrdiff_t to toku_Integer */
#define cast_diff2S(df)     cast_sz2S(cast_diff2sz(df))

/*
** Special type equivalent to '(void*)' for functions (to suppress some
** warnings when converting function pointers)
*/
typedef void (*voidf)(void);

/*
** Macro to convert pointer-to-void* to pointer-to-function. This cast
** is undefined according to ISO C, but POSIX assumes that it works.
** (The '__extension__' in gnu compilers is only to avoid warnings.)
*/
#if defined(__GNUC__)
#define cast_func(p)        (__extension__ cast(voidf, p))
#else
#define cast_func(p)        cast(voidf, p)
#endif



/*
** Check if 'x' is a power of 2.
*/
#if !defined(t_ispow2)
#define t_ispow2(x)         (((x) & ((x)-1)) == 0)
#endif


#if !defined(t_fastmod)
#define t_fastmod(x,y)      ((x) & ((y) - 1))
#endif


/*
** Number of bits in a given type.
*/
#if !defined(t_nbits)
#define t_nbits(x)          cast_i32(sizeof(x) * CHAR_BIT)
#endif


/*
** Size of fixed array.
*/
#if !defined(t_arraysize)
#define t_arraysize(x)      (sizeof(x)/sizeof((x)[0]))
#endif


/*
** Length of string literal, not including the null terminator '\0'.
*/
#if !defined(LL)
#define LL(sl)          (sizeof(sl) - 1)
#endif


#if defined(TOKU_CORE) || defined(TOKU_LIB)
/* shorter names for internal use */
#define t_likely(cond)      tokui_likely(cond)
#define t_unlikely(cond)    tokui_unlikely(cond)
#endif



/*
** The tokui_num* macros define the primitive operations over numbers.
*/

/* floor division (defined as 'floor(a/b)') */
#if !defined(tokui_numidiv)
#define tokui_numidiv(T,a,b)    (UNUSED(T), t_floor(tokui_numdiv(T,a,b)))
#endif

/* float division */
#if !defined(tokui_numdiv)
#define tokui_numdiv(T,a,b)     (UNUSED(T), (a)/(b))
#endif

/*
** modulo: defined as 'a - floor(a/b)*b'; the direct computation
** using this definition has several problems with rounding errors,
** so it is better to use 'fmod'. 'fmod' gives the result of
** 'a - trunc(a/b)*b', and therefore must be corrected when
** 'trunc(a/b) ~= floor(a/b)'. That happens when the division has a
** non-integer negative result: non-integer result is equivalent to
** a non-zero remainder 'm'; negative result is equivalent to 'a' and
** 'b' with different signs, or 'm' and 'b' with different signs
** (as the result 'm' of 'fmod' has the same sign of 'a').
*/
#if !defined(tokui_nummod)
#define tokui_nummod(T,a,b,m) \
    { UNUSED(T); (m) = t_mathop(fmod)(a, b); \
      if (((m) > 0) ? (b) < 0 : ((m) < 0 && (b) > 0)) (m) += (b); }
#endif

/* exponentiation */
#if !defined(tokui_numpow)
#define tokui_numpow(T,a,b) \
        (UNUSED(T), (b) == 2 ? (a)*(a) : t_mathop(pow)(a, b))
#endif

#if !defined(tokui_numadd)
#define tokui_numadd(T, a, b)   (UNUSED(T), (a) + (b))
#define tokui_numsub(T, a, b)   (UNUSED(T), (a) - (b))
#define tokui_nummul(T, a, b)   (UNUSED(T), ((a) * (b)))
#define tokui_numunm(T, a)      (UNUSED(T), -(a))
#define tokui_numeq(a, b)       ((a) == (b))
#define tokui_numne(a, b)       (!tokui_numeq(a, b))
#define tokui_numlt(a, b)       ((a) < (b))
#define tokui_numle(a, b)       ((a) <= (b))
#define tokui_numgt(a, b)       ((a) > (b))
#define tokui_numge(a, b)       ((a) >= (b))
#define tokui_numisnan(a)       (!tokui_numeq(a, a))
#endif



/*
** Macro to control inclusion of some hard tests on stack reallocation.
*/
#if !defined(TOKUI_HARDSTACKTESTS)
#define condmovestack(T,pre,pos)    UNUSED(0)
#else
/* realloc stack keeping its size */
#define condmovestack(T,pre,pos)  \
    { int sz_ = stacksize(T); pre; tokuPR_reallocstack((T), sz_, 0); pos; }
#endif


#if !defined(TOKUI_HARDMEMTESTS)
#define condchangemem(T,pre,pos,emg)    UNUSED(0)
#else
#define condchangemem(T,pre,pos,emg)  \
    { if (gcrunning(G(T))) { pre; tokuG_fullinc(T, emg); pos; } }
#endif



/*
** Basic report of messages and errors.
*/
#if !defined(t_writestring)
#define t_writestring(s,l)      fwrite((s), sizeof(char), (l), stdout)
#define t_writeline()           (t_writestring("\n", 1), fflush(stdout))
#define t_writestringerr(s,p)   (fprintf(stderr, (s), (p)), fflush(stdout))
#endif

#endif
