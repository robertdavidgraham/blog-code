/*
    Parse IPv4 address - SWAR or "SIMD within a register"
 
 This parses an address using some techniques inspired
 by SIMD, but not using SIMD instructions.
 
 Specifically, it removes branches in the code and adds
 "lanes" whose results are masked off, depending on conditionals.
 
 Thus, this code has no `if` statements or `for` or `while`
 loops.
 
 This executes more instructions over all, essentially parsing
 each number three times, once each for the number of digits
 it might have. It then masks off the correct results at the end.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * Form an IPv4 address from each of the parsed integers.
 */
static inline uint32_t pack_ipv4_u32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/**
 *  Convert byte to digit 0..9; returns 0..255 with (digit<=9) check done separately.
 */
static inline uint32_t decval_u8(uint8_t ch) {
    return (uint32_t)(ch - (uint8_t)'0');
}

/**
 * Branch-light octet parser for "ddd<dot>" where dot must appear at b1/b2/b3.
 * Consumes exactly (ndigits + 1) bytes on success.
 * On failure, sets *err nonzero.
 */
static inline void
parse_octet_dot(const uint8_t *p, uint32_t *val, uint32_t *ndigits, uint32_t *err) {
    uint8_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];

    /* Where is the required '.'? */
    uint32_t m1 = !(b1 ^ (uint8_t)'.');                 /* 1 digit */
    uint32_t m2 = (!m1) & (b2 == (uint8_t)'.');         /* 2 digits */
    uint32_t m3 = (!m1) & (!m2) & (b3 == (uint8_t)'.'); /* term after 3 digits */

    uint32_t sel1 = m1;
    uint32_t sel2 = m2;
    uint32_t sel3 = m3;
    uint32_t any  = sel1 | sel2 | sel3;

    /* Digit checks (computed as flags, not branches) */
    uint32_t d0 = decval_u8(b0);
    uint32_t d1 = decval_u8(b1);
    uint32_t d2 = decval_u8(b2);

    uint32_t isdig0 = (d0 <= 9);
    uint32_t isdig1 = (d1 <= 9);
    uint32_t isdig2 = (d2 <= 9);

    /* Leading zero forbidden for multi-digit: if first char
     * is '0' and ndigits>1. */
    uint32_t multi = sel2 | sel3;
    uint32_t leading_zero_bad = multi & (b0 == (uint8_t)'0');

    /* For 2 digits: need digit1; for 3 digits: need digit1 and digit2 */
    uint32_t need1_bad = multi & (1u ^ isdig1);
    uint32_t need2_bad = sel3  & (1u ^ isdig2);

    /* Compute value candidates */
    uint32_t v1 = d0;
    uint32_t v2 = d0 * 10u + d1;
    uint32_t v3 = (d0 * 10u + d1) * 10u + d2;

    uint32_t v = sel1 * v1 + sel2 * v2 + sel3 * v3;
    uint32_t nd = sel1 * 1u + sel2 * 2u + sel3 * 3u;

    /* Range check: only meaningful if any==1, but safe either way */
    uint32_t range_bad = (v > 255u);

    /* Accumulate errors (no early returns) */
    *err |= (1u ^ any);          /* missing '.' within next 3 bytes */
    *err |= (1u ^ isdig0);       /* first char must be digit */
    *err |= leading_zero_bad;
    *err |= need1_bad;
    *err |= need2_bad;
    *err |= range_bad;

    *val = v;
    *ndigits = nd;
}

/**
 * Branch-light last octet parser for "ddd<space-or-nul>".
 * Same as the other parser, but assumes a terminator character
 * rather than a dot.
 * Also, does NOT consume the terminator (caller uses ndigits to
 * compute consumed length).
 */
static inline void
parse_octet_last(const uint8_t *p, uint32_t *val, uint32_t *ndigits, uint8_t *term, uint32_t *err) {
    uint8_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];

    uint32_t t1 = (b1 == (uint8_t)' ') | (b1 == (uint8_t)'\0');
    uint32_t t2 = (!t1) & ((b2 == (uint8_t)' ') | (b2 == (uint8_t)'\0'));
    uint32_t t3 = (!t1) & (!t2) & ((b3 == (uint8_t)' ') | (b3 == (uint8_t)'\0'));

    uint32_t sel1 = t1;
    uint32_t sel2 = t2;
    uint32_t sel3 = t3;
    uint32_t any  = sel1 | sel2 | sel3;

    uint32_t d0 = decval_u8(b0);
    uint32_t d1 = decval_u8(b1);
    uint32_t d2 = decval_u8(b2);

    uint32_t isdig0 = (d0 <= 9);
    uint32_t isdig1 = (d1 <= 9);
    uint32_t isdig2 = (d2 <= 9);

    uint32_t multi = sel2 | sel3;
    uint32_t leading_zero_bad = multi & (b0 == (uint8_t)'0');

    uint32_t need1_bad = multi & (1u ^ isdig1);
    uint32_t need2_bad = sel3  & (1u ^ isdig2);

    uint32_t v1 = d0;
    uint32_t v2 = d0 * 10u + d1;
    uint32_t v3 = (d0 * 10u + d1) * 10u + d2;

    uint32_t v = sel1 * v1 + sel2 * v2 + sel3 * v3;
    uint32_t nd = sel1 * 1u + sel2 * 2u + sel3 * 3u;

    uint32_t range_bad = (v > 255u);

    *err |= (1u ^ any);
    *err |= (1u ^ isdig0);
    *err |= leading_zero_bad;
    *err |= need1_bad;
    *err |= need2_bad;
    *err |= range_bad;

    *val = v;
    *ndigits = nd;

    /* Pick the actual terminator byte (space or NUL) without branching
     * (sel1/sel2/sel3 are 0/1 and mutually exclusive by construction) */
    uint8_t tt = (uint8_t)(sel1 * b1 + sel2 * b2 + sel3 * b3);
    *term = tt;
}


size_t
parse_ip_swar(const char *s, size_t , uint32_t *out) {

    const uint8_t *p = (uint8_t*)s;
    uint32_t err = 0;
    uint32_t a, b, c, d;
    uint32_t n1, n2, n3, n4;
    uint8_t term4;

    parse_octet_dot (p, &a, &n1, &err); p += (size_t)n1 + 1u;
    parse_octet_dot (p, &b, &n2, &err); p += (size_t)n2 + 1u;
    parse_octet_dot (p, &c, &n3, &err); p += (size_t)n3 + 1u;
    parse_octet_last(p, &d, &n4, &term4, &err);

    /* bytes consumed (exclude terminator) */
    size_t consumed = (size_t)(p - (uint8_t*)s) + (size_t)n4;

    *out = pack_ipv4_u32(a, b, c, d);
    return consumed * (err == 0);
}

