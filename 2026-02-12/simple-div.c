#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef union {
    double d;
    uint64_t u;
} double_bits;

void long_division(uint64_t num, uint64_t den, int max_digits) {
    printf("%llu.", num / den);
    uint64_t remainder = num % den;
    for (int i = 0; i < max_digits && remainder != 0; i++) {
        remainder *= 10;
        printf("%llu", remainder / den);
        remainder %= den;
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <double> [max_digits]\n", argv[0]);
        return 1;
    }
    
    double value = atof(argv[1]);
    int max_digits = argc > 2 ? atoi(argv[2]) : 80;
    
    double_bits db = {.d = value};
    uint64_t sign = (db.u >> 63) & 1;
    int64_t exponent = ((db.u >> 52) & 0x7FF) - 1023;
    uint64_t mantissa = db.u & 0xFFFFFFFFFFFFFULL;
    uint64_t significand = (1ULL << 52) | mantissa;
    
    printf("Sign: %llu, Exponent: %lld, Mantissa: 0x%013llx\n", sign, exponent, mantissa);
    
    uint64_t numerator, denominator;
    if (exponent >= 52) {
        numerator = significand << (exponent - 52);
        denominator = 1;
    } else {
        numerator = significand;
        denominator = 1ULL << (52 - exponent);
    }
    
    printf("Fraction: %llu / %llu\n", numerator, denominator);
    long_division(numerator, denominator, max_digits);
    
    return 0;
}
