/*
    Vibe coded parser for IPv4 address using ARM NEON instructions
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __ARM_NEON__
#include <arm_neon.h>
#else
typedef size_t uint8x16_t;
#endif

// Convert an 8-byte lane containing bytes 0x00 or 0x80 into an 8-bit mask.
static inline uint32_t byte80_lane_to_mask8(uint64_t lane_bytes)
{
    // bytes are either 0x00 or 0x80
    // Classic trick: collect MSBs into a bitmask
    // After multiplication, the top byte holds the packed bits.
    return (uint32_t)(((lane_bytes & 0x8080808080808080ULL) * 0x02040810204081ULL) >> 56);
}

// Convert 16-byte NEON vector compare result to a 16-bit bitmask.
// Assumes each byte is 0x00 or 0xFF (from vceqq / vcgeq / etc).
static inline uint32_t neon_cmp_to_mask16(uint8x16_t cmp_ff_or_00)
{
#ifdef __ARM_NEON__
    // Turn 0xFF/0x00 into 0x80/0x00 per byte (keep MSB).
    uint8x16_t msb = vshlq_n_u8(vshrq_n_u8(cmp_ff_or_00, 7), 7); // -> 0x80 or 0x00

    uint64x2_t lanes = vreinterpretq_u64_u8(msb);
    uint64_t lo = vgetq_lane_u64(lanes, 0);
    uint64_t hi = vgetq_lane_u64(lanes, 1);

    uint32_t mlo = byte80_lane_to_mask8(lo);
    uint32_t mhi = byte80_lane_to_mask8(hi);

    return mlo | (mhi << 8);
#else
    return 0;
#endif
}

static inline int ctz32(uint32_t x)
{
    // x must be nonzero
    return __builtin_ctz(x);
}

static inline int popcnt32(uint32_t x)
{
    return __builtin_popcount(x);
}

// Parse 1..3 digits from s[pos..pos+len) into 0..255, rejecting leading zeros (except "0").
static inline bool parse_octet_strict(const unsigned char *s, int pos, int len, unsigned *outv)
{
#ifdef __ARM_NEON__
    if ((unsigned)len - 1u > 2u) return false; // len must be 1..3

    unsigned v = 0;
    if (len == 1) {
        v = (unsigned)(s[pos] - '0');
    } else if (len == 2) {
        if (s[pos] == '0') return false; // leading zero
        v = (unsigned)(s[pos] - '0') * 10u + (unsigned)(s[pos + 1] - '0');
    } else { // len == 3
        if (s[pos] == '0') return false; // leading zero
        v = (unsigned)(s[pos] - '0') * 100u
          + (unsigned)(s[pos + 1] - '0') * 10u
          + (unsigned)(s[pos + 2] - '0');
    }
    if (v > 255u) return false;
    *outv = v;
    return true;
#else
    return false;
#endif
}

// Returns bytes consumed INCLUDING terminator (' ' or '\0'), or 0 on error.
// Writes IPv4 as 0xAABBCCDD (A=first octet).
size_t parse_ip_neon(const char *buf, size_t maxlen, uint32_t *out)
{
#ifdef __ARM_NEON__
    if (!buf || !out) return 0;

    // Minimal form: "0.0.0.0\0" => 8 bytes consumed (7 chars + NUL)
    if (maxlen < 8) return 0;

    const unsigned char *s = (const unsigned char *)buf;

    // We operate on the first 16 bytes; if token might be longer than 15 before terminator,
    // we reject (by requiring terminator within these 16).
    // For IPv4 dotted-quad, max is "255.255.255.255" (15 chars) + term, so we need term within 16 bytes.
    if (maxlen < 16) {
        // Safe scalar fallback for very short buffers (still strict).
        // You can replace this with your own scalar path if desired.
        return 0;
    }

    uint8x16_t v = vld1q_u8((const uint8_t *)s);

    // Find '.' positions
    uint8x16_t is_dot = vceqq_u8(v, vdupq_n_u8((uint8_t)'.'));
    uint32_t dot_mask = neon_cmp_to_mask16(is_dot);

    // Find terminator positions: ' ' or '\0'
    uint8x16_t is_sp  = vceqq_u8(v, vdupq_n_u8((uint8_t)' '));
    uint8x16_t is_nul = vceqq_u8(v, vdupq_n_u8((uint8_t)0));
    uint8x16_t is_term = vorrq_u8(is_sp, is_nul);
    uint32_t term_mask = neon_cmp_to_mask16(is_term);

    if (term_mask == 0) return 0; // must terminate within first 16 bytes

    int term_i = ctz32(term_mask); // earliest terminator index 0..15

    // terminator must be within maxlen (it is within first 16, but still guard)
    if ((size_t)term_i >= maxlen) return 0;

    // Only consider chars before the terminator.
    uint32_t pre_mask = (term_i == 0) ? 0u : ((1u << term_i) - 1u);
    uint32_t dots_before = dot_mask & pre_mask;

    // Must have exactly 3 dots before terminator.
    if (popcnt32(dots_before) != 3) return 0;

    // Validate that all non-dot bytes before terminator are digits.
    // digit: '0'..'9'
    uint8x16_t ge0 = vcgeq_u8(v, vdupq_n_u8((uint8_t)'0'));
    uint8x16_t le9 = vcleq_u8(v, vdupq_n_u8((uint8_t)'9'));
    uint8x16_t is_digit = vandq_u8(ge0, le9);

    // allowed = digit OR dot (before terminator)
    uint8x16_t allowed = vorrq_u8(is_digit, is_dot);
    uint32_t allowed_mask = neon_cmp_to_mask16(vceqq_u8(allowed, vdupq_n_u8(0xFF)));

    // Ensure every position before term is allowed.
    if ((allowed_mask & pre_mask) != pre_mask) return 0;

    // Extract dot indices (d1 < d2 < d3)
    uint32_t m = dots_before;
    int d1 = ctz32(m); m &= (m - 1u);
    int d2 = ctz32(m); m &= (m - 1u);
    int d3 = ctz32(m);

    // Segment lengths
    int len1 = d1;           // [0, d1)
    int len2 = d2 - d1 - 1;  // (d1, d2)
    int len3 = d3 - d2 - 1;  // (d2, d3)
    int len4 = term_i - d3 - 1; // (d3, term)

    // Must be 1..3 digits each
    if ((unsigned)len1 - 1u > 2u) return 0;
    if ((unsigned)len2 - 1u > 2u) return 0;
    if ((unsigned)len3 - 1u > 2u) return 0;
    if ((unsigned)len4 - 1u > 2u) return 0;

    // Parse octets (scalar but tiny)
    unsigned a, b, c, d;
    if (!parse_octet_strict(s, 0,        len1, &a)) return 0;
    if (!parse_octet_strict(s, d1 + 1,   len2, &b)) return 0;
    if (!parse_octet_strict(s, d2 + 1,   len3, &c)) return 0;
    if (!parse_octet_strict(s, d3 + 1,   len4, &d)) return 0;

    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;

    // bytes consumed includes the terminator
    return (size_t)term_i + 1u;
#else
    return 0;
#endif
}

