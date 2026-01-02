#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BENCH_VALID_CYCLES        = 1u << 0,
    BENCH_VALID_INSTRUCTIONS  = 1u << 1,
    BENCH_VALID_BRANCH_MISSES = 1u << 2,
    BENCH_VALID_L1D_MISSES    = 1u << 3,
    BENCH_VALID_BRANCHES      = 1u << 4,
    BENCH_VALID_TIME          = 1u << 5
};

typedef struct bench_result_t {
    uint64_t cycles;
    uint64_t instructions;
    uint64_t branch_misses;
    uint64_t l1d_misses;
    uint64_t branches;          /* NEW: total branch instructions */
    double   elapsed_seconds;
    uint32_t valid_mask;
    int32_t  backend_error;
} bench_result_t;

typedef struct bench_ctx bench_ctx;

bench_ctx*     bench_start(void);
bench_result_t bench_stop(bench_ctx* ctx);

#ifdef __cplusplus
}
#endif
#endif
