/*
    Vibe coded "state machine parser" for IPv4 addresses.
 
    This is a typical way of using state-machines to parse
    things.
 
    It could easily be optimized to be better.
 */
#include <stddef.h>
#include <stdint.h>

static int is_digit_ascii(unsigned char c)
{
    return (c >= '0' && c <= '9');
}

size_t parse_ip_fsm(const char *buf, size_t len, uint32_t *out)
{
    enum {
        S_OCTET_START = 0,
        S_OCTET_DIGITS,
        S_AFTER_OCTET,
        S_DONE,
        S_ERR
    } st = S_OCTET_START;

    uint32_t acc = 0;
    unsigned octet = 0;      // 0..3
    unsigned value = 0;      // current octet value
    unsigned ndigits = 0;
    unsigned first_digit = 0;
    size_t i = 0;

    while (i < len && st != S_DONE && st != S_ERR) {
        unsigned char c = (unsigned char)buf[i];

        switch (st) {
        case S_OCTET_START:
            if (!is_digit_ascii(c)) {
                st = S_ERR;
                break;
            }
            first_digit = (unsigned)(c - '0');
            value = first_digit;
            ndigits = 1;
            st = S_OCTET_DIGITS;
            i++;
            break;

        case S_OCTET_DIGITS:
            if (is_digit_ascii(c)) {
                /* forbid leading zeroes */
                if (ndigits == 1 && first_digit == 0) {
                    st = S_ERR;
                    break;
                }
                if (ndigits == 3) { st = S_ERR; break; }
                value = value * 10u + (unsigned)(c - '0');
                if (value > 255u) { st = S_ERR; break; }
                ndigits++;
                i++;
            } else {
                st = S_AFTER_OCTET;
            }
            break;

        case S_AFTER_OCTET:
            /* commit octet in host-order packing */
            acc <<= 8;
            acc |= value;

            if (octet < 3) {
                if (i >= len || buf[i] != '.') {
                    st = S_ERR;
                    break;
                }
                i++;          // consume '.'
                octet++;
                st = S_OCTET_START;
            } else {
                /* last octet: must terminate */
                if (i == len ||
                    buf[i] == ' ' ||
                    buf[i] == '\0') {
                    st = S_DONE;
                } else {
                    st = S_ERR;
                }
            }
            break;

        default:
            st = S_ERR;
            break;
        }
    }

    /* handle buffer end immediately after digits */
    if (st == S_OCTET_DIGITS) {
        acc <<= 8;
        acc |= value;
        st = (octet == 3) ? S_DONE : S_ERR;
    }

    if (st != S_DONE) return 0;

    *out = acc;
    return i;   /* bytes consumed (terminator not consumed) */
}

