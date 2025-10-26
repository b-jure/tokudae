/*
** tbits.h
** Bit manipulation functions
** See Copyright Notice in tokudae.h
*/

#ifndef tbits_h
#define tbits_h


/* raise 2 to the power of 'x' */
#define twoto(x)        (1u<<(x))


/* bit manipulation */
#define resetbits(x,m)          ((x) &= cast_u8(~(m)))
#define setbits(x,m)            ((x) |= (m))
#define testbits(x,m)           ((x) & (m))
#define togglebits(x,m,t)       ((x) ^ (((x) ^ -((t) != 0)) & (m)))
#define bitmask(b)              (1 << (b))
#define bit2mask(b1,b2)         (bitmask(b1) | bitmask(b2)) 
#define resetbit(x,b)           resetbits(x, bitmask(b))
#define setbit(x,b)             setbits(x, bitmask(b))
#define clearbit(x,b)           resetbits(x, bitmask(b))
#define testbit(x,b)            testbits(x, bitmask(b))
#define togglebit(x,b,t)        togglebits(x, bitmask(b), t)


/* get byte at offset 'o' from 'v' */
#define getbyte(v,o)        (((v) >> ((o) * 8)) & 0xFF)


/* set 'src' byte at offset 'o' to 'v' */
#define setbyte(src,o,v)    (*(cast_u8p(src) + (o)) = cast_u8(v))


/* 
** Get first 3 bytes (LE byte order) from 'p' casted to 'uint32_t'.
*/
#define get3bytes(p) \
        cast_i32(cast_u32(0) | \
        cast_u32((*(cast_u8p(p) + 2)) << 16) | \
        cast_u32((*(cast_u8p(p) + 1)) << 8) | \
        (*cast_u8p(p)))


/* 
** Set first 3 (LE byte order) bytes from 'src'
** (integer type) into 'dest'.
*/
#define set3bytes(dest,src) \
    { uint8_t *dest_=cast_u8p(dest); int src_=cast_i32(src); \
      setbyte(dest_, 0, getbyte(src_, 0)); \
      setbyte(dest_, 1, getbyte(src_, 1)); \
      setbyte(dest_, 2, getbyte(src_, 2)); }

#endif
