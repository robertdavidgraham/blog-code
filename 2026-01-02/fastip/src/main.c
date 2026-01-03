/*
    Benchmarking various IPv4 parsing algorithms
 
 The point isn't to speed up this algorithm, but to look at various
 parsing styles in general, and how they work on different compilers,
 CPUs, optimizations, and so forth.
 */
#include "bench.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#endif


/*
 * These are the prototypes/declarations for the various parser
 * algorithms held in other files.
 */
size_t parse_ip_ai(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_fromchars(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_swar(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_neon(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_fsm(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_fsm2(const char *buf, size_t maxlen, uint32_t *out);
size_t parse_ip_dfa(const char *buf, size_t maxlen, uint32_t *out);
void parse_ip_dfa_init(void);

/**
 * This is a traditional LCG random number generator. I want
 * it to return a full 32-bits of randomnous so one call generates
 * an address (instead of building addresses from multiple calls).
 * The point is to use a DETERMINISTIC random number generator
 * so the same seed always produces the same sequence of
 * numbers. This makes test reproducibility easier.
 */
uint32_t lcg32(uint64_t *state) {
    uint64_t product = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    *state = product;  /* Full 64-bit state for better quality */
    return product >> 32;
}



/*
 * All the parser functions must conform to this prototype.
 * @returns
 *  >0 : number of bytes consumed, not including delimeter
 *   0 : parse failure
 */
typedef size_t (*PARSER)(const char *buf, size_t maxlen, uint32_t *out);

/**
 * This function benchmarks a single parser algorithm. It's called multiple
 *  times, for different algorithms, and different sized test buffers.
 */
static void
run_benchmark(const char *test, size_t N, size_t C, const char *name, PARSER parser, unsigned in_sum) {
    unsigned checksum;
    size_t repeat;
    size_t i;
    const uint64_t iterations = N * C;
    
    /*
     * This variable does two things. First, it'll act as a
     * 'sink' to prevent optimizers removing code that
     * doens't contribute to a result. Second, it verifies
     * that the parsers are working correctly: if there
     * is a subtle bug in parsing addresses, this will
     * detect it.
     */
    checksum = 0;
    
    /*
     * Run the benchmarked code `C` times.
     * Each run parses `N` addresses.
     */
    bench_ctx *ctx = bench_start();
    for (repeat=0; repeat<C; repeat++) {
        for (i=0; i<N; i++) {
            unsigned ip_address;
            parser(test + i*16, 16, &ip_address);
            checksum += ip_address;
        }
    }
#if defined(__APPLE__)
    usleep(100);
#endif
    bench_result_t counters = bench_stop(ctx);

    printf("[%6s] %5.1f-GHz %5.1f-ns %4llu %4llu %4.1f %4llu %4.1f %4.1f    [0x%08x]\n", name,
           counters.cycles/counters.elapsed_seconds/1000000000.0,
           1000000000.0 * counters.elapsed_seconds/iterations,
           counters.cycles/iterations,
           counters.instructions/iterations,
           1.0 * counters.instructions/counters.cycles,
           counters.branches/iterations,
           1.0 * counters.branch_misses/iterations,
           1.0 * counters.l1d_misses/iterations,
           checksum - in_sum
           );
}

/**
 * Creates a test-case buffer printing random IPv4 addresses into a
 * buffer separated by one or more spaces.
 *
 * Kludge: we are padding every address to 16-bytes, so they
 * are located at 16-byte boundaries.
 */
static char *
create_test_case(size_t *length, size_t N, uint64_t seed) {
    size_t i;
    size_t offset = *length;
    char* test = malloc(1);
    *test = '\0';

    /*
     * Create `N` addresses
     */
    for (i=0; i<N; i++) {
        unsigned ip_address;
        char buf[64];
        size_t ip_length;
     
        /* Generate a random IPv4 address, 32-bits in size */
        ip_address = lcg32(&seed);
        
        /* Print to a temporary string */
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u         ",
                 (ip_address>>24)&0xFF,
                 (ip_address>>16)&0xFF,
                 (ip_address>> 8)&0xFF,
                 (ip_address>> 0)&0xFF);
        buf[16] = '\0'; /* truncate to only 16 bytes */
        
        ip_length = strlen(buf); /* should be 16 */
        
        /* Make sure we have enough memory, otherwise, expand
         * the buffer */
        while (offset + ip_length + 1 >= *length) {
            *length = *length * 2 + 1;
            test = realloc(test, *length);
        }
        
        /* Append */
        memcpy(test + offset, buf, ip_length+1);

        offset += ip_length;
    }

    *length = offset;
    return test;
}


int main(void) {
    static const int N = 1500; /* size of test case */
    static const int C = 100; /* count of test cases to run */
    char *test;
    size_t test_length = 0;
    
    /*
     * We need to initialize the tables for this algorithm.
     */
    parse_ip_dfa_init();
    

    /*
     * This is the test case string, which consists of a large
     * number of IPv4 addresses separated by spaces.
     */
    test = create_test_case(&test_length, N*100, 1);

    /*
     * Sets a higher priority thread. On macOS, this likely
     * causes the current thread to be moved to a P-core,
     * the performance CPU.
     */
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 1);
    usleep(1);
#endif
    
    /*
     * Run the benchmarks for the performance cores. Do a
     * throway run to warm things up.
     */
    run_benchmark(test, N*100, C, "warmup", parse_ip_ai, 0xfa929ccc);
    printf("==[p-cores]============\n");
    printf("[%6s] %5s     %5s    %4s %4s %4s %4s %4s %4s    %10s\n", "",
           "freq", "time", "cycl", "inst", "ipc", "brch", "miss", "l1d", "checksum");
    run_benchmark(test, N, C*100, "   ai ", parse_ip_ai, 0x26f598c0);
    run_benchmark(test, N*100, C, "   ai+", parse_ip_ai, 0xfa929ccc);
    run_benchmark(test, N, C*100, " swar ", parse_ip_swar, 0x26f598c0);
    run_benchmark(test, N*100, C, " swar+", parse_ip_swar, 0xfa929ccc);
    run_benchmark(test, N, C*100, " from ", parse_ip_fromchars, 0x26f598c0);
    run_benchmark(test, N*100, C, " from+", parse_ip_fromchars, 0xfa929ccc);
#ifndef FASTAI
    run_benchmark(test, N, C*100, "  dfa ", parse_ip_dfa, 0x26f598c0);
    run_benchmark(test, N*100, C, "  dfa+", parse_ip_dfa, 0xfa929ccc);
    run_benchmark(test, N, C*100, "  fsm ", parse_ip_fsm, 0x26f598c0);
    run_benchmark(test, N*100, C, "  fsm+", parse_ip_fsm, 0xfa929ccc);
    run_benchmark(test, N, C*100, " fsm2 ", parse_ip_fsm2, 0x26f598c0);
    run_benchmark(test, N*100, C, " fsm2+", parse_ip_fsm2, 0xfa929ccc);
    run_benchmark(test, N, C*100, " neon ", parse_ip_neon, 0x26f598c0);
    run_benchmark(test, N*100, C, " neon+", parse_ip_neon, 0xfa929ccc);
#endif
    printf("\n");

    /*
     * On macOS, this moves the thread to an efficiency core.
     */
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#endif

    /*
     * Run the tests for the efficiency cores. This should be
     * a lot slower.
     */
    run_benchmark(test, N*100, C, "warmup", parse_ip_ai, 0xfa929ccc);
    printf("**[e-cores]************\n");
    printf("[%6s] %5s     %5s    %4s %4s %4s %4s %4s %4s \n", "",
           "freq", "time", "cycl", "inst", "ipc", "brch", "miss", "l1d");
    run_benchmark(test, N, C*100, "   ai ", parse_ip_ai, 0x26f598c0);
    run_benchmark(test, N*100, C, "   ai+", parse_ip_ai, 0xfa929ccc);
    run_benchmark(test, N, C*100, " swar ", parse_ip_swar, 0x26f598c0);
    run_benchmark(test, N*100, C, " swar+", parse_ip_swar, 0xfa929ccc);
    run_benchmark(test, N, C*100, " from ", parse_ip_fromchars, 0x26f598c0);
    run_benchmark(test, N*100, C, " from+", parse_ip_fromchars, 0xfa929ccc);
#ifndef FASTAI
    run_benchmark(test, N, C*100, "  dfa ", parse_ip_dfa, 0x26f598c0);
    run_benchmark(test, N*100, C, "  dfa+", parse_ip_dfa, 0xfa929ccc);
    run_benchmark(test, N, C*100, "  fsm ", parse_ip_fsm, 0x26f598c0);
    run_benchmark(test, N*100, C, "  fsm+", parse_ip_fsm, 0xfa929ccc);
    run_benchmark(test, N, C*100, " fsm2 ", parse_ip_fsm2, 0x26f598c0);
    run_benchmark(test, N*100, C, " fsm2+", parse_ip_fsm2, 0xfa929ccc);
    run_benchmark(test, N, C*100, " neon ", parse_ip_neon, 0x26f598c0);
    run_benchmark(test, N*100, C, " neon+", parse_ip_neon, 0xfa929ccc);
#endif
    printf("\n");


    return 0;
}

