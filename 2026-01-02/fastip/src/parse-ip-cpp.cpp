#include <stdint.h>
#include <charconv>
#include <string>

enum class parse_error { invalid_format };


extern "C" size_t parse_ip_fromchars(const char *p, size_t maxlen, unsigned *result) {
  const char *current = p;
    const char *pend = p + maxlen;
  uint32_t ip = 0;
  for (int i = 0; i < 4; ++i) {
    uint8_t value;
    auto r = std::from_chars(current, pend, value);
    if (r.ec != std::errc()) {
      return 0;
    }
    current = r.ptr;
    ip = (ip << 8) | value;
    if (i < 3) {
      if (current == pend || *current++ != '.') {
        return 0;
      }
    }
  }
    *result = ip;
  return current - p;
}
