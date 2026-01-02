/*
    IPv4 address parser from this blogpost:
    
 https://lemire.me/blog/2025/12/27/parsing-ip-addresses-quickly-portably-without-simd-magic/
 
    The original was in C++, I translated it to C.
 */
#include <stddef.h>
#include <stdint.h>

size_t parse_ip_ai(const char *p, size_t length, unsigned *out) {
    const char *pend = p + length;
    uint32_t ip = 0;
    int octets = 0;
    while (p < pend && octets < 4) {
        uint32_t val = 0;
        if (p < pend && *p >= '0' && *p <= '9') {
            val = (*p++ - '0');
            if (p < pend && *p >= '0' && *p <= '9') {
                if (val == 0) {
                  return 0;
                }
                val = val * 10 + (*p++ - '0');
                if (p < pend && *p >= '0' && *p <= '9') {
                    val = val * 10 + (*p++ - '0');
                    if (val > 255) {
                      return 0;
                    }
                }
            }
        } else {
            return 0;
        }
        ip = (ip << 8) | val;
        octets++;
        if (octets < 4) {
            if (p == pend || *p != '.') {
              return 0;
            }
            p++; // Skip the dot
        }
    }
    if (octets == 4) {
        *out = ip;
        return 1;
    } else {
        return 0;
    }
}
