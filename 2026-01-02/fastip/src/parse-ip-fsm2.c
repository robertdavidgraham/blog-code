#include <stddef.h>
#include <stdint.h>

static int is_digit_ascii(unsigned char c)
{
    return (c >= '0' && c <= '9');
}

size_t parse_ip_fsm2(const char *buf, size_t len, uint32_t *out)
{
    enum {
        START=0,
        NUM1_1, NUM1_2, NUM1_3, DOT1,
        NUM2_1, NUM2_2, NUM2_3, DOT2,
        NUM3_1, NUM3_2, NUM3_3, DOT3,
        NUM4_1, NUM4_2, NUM4_3,
        DONE,
        ERROR
    } state = 0;
    
    uint32_t ip_address = 0;
    unsigned value = 0;      // current octet value
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)buf[i++];

        switch (state) {
        case START:
            state++;
        case NUM1_1:
        case NUM2_1:
        case NUM3_1:
        case NUM4_1:
            if (!is_digit_ascii(c))
                return 0;
            value = (unsigned)(c - '0');;
            state++;
            break;
        case NUM1_2:
        case NUM2_2:
        case NUM3_2:
            if (c == '.') {
                ip_address = ip_address<<8 | value;
                value = 0;
                state += 3;
                continue;
            }
            if (!is_digit_ascii(c))
                return 0;
            if (value == 0)
                return 0; /* no leading zeroes */
            value = value * 10 + (c - '0');
            state++;
            break;
        case NUM4_2:
            if (c == ' ' || c == '\0') {
                ip_address = ip_address<<8 | value;
                value = 0;
                goto done;
            }
            if (!is_digit_ascii(c))
                return 0;
            if (value == 0)
                return 0; /* no leading zeroes */
            value = value * 10 + (c - '0');
            state++;
            break;
        case NUM1_3:
        case NUM2_3:
        case NUM3_3:
            if (c == '.') {
                ip_address = ip_address<<8 | value;
                value = 0;
                state += 2;
                continue;
            }
            if (!is_digit_ascii(c))
                return 0;
            value = value * 10 + (c - '0');
            state++;
            break;
        case NUM4_3:
            if (c == ' ' || c == '\0') {
                ip_address = ip_address<<8 | value;
                value = 0;
                goto done;
            }
            if (!is_digit_ascii(c))
                return 0;
            value = value * 10 + (c - '0');
            state++;
            break;
        case DOT1:
        case DOT2:
        case DOT3:
            if (c != '.')
                return 0;
            ip_address = ip_address<<8 | value;
            value = 0;
            state++;
            break;
        case DONE:
            if (c == ' ' || c == '\0') {
                ip_address = ip_address<<8 | value;
                value = 0;
                goto done;
            }
            return 0;
        default:
            return 0;
        }
    }
    return 0;
done:
    *out = ip_address;
    return i - 1;   /* bytes consumed (terminator not consumed) */
}

