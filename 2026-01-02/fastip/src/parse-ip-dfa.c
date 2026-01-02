/*
    Funky IPv4 address parser using the "dfa" driven concept.
 
    This is sort of what you'd get from using a `regex` with
    `capture groups` to parse an IPv4 address.
 */
#include <stddef.h>

enum {
    START=0,
    NUM1_1, NUM1_2, NUM1_3, DOT1,
    NUM2_1, NUM2_2, NUM2_3, DOT2,
    NUM3_1, NUM3_2, NUM3_3, DOT3,
    NUM4_1, NUM4_2, NUM4_3,
    DONE,
    ERROR
};

static int table[100][256];
static int indexes[] = {0, 1, 1, 1, 0, 2, 2, 2, 0, 3, 3, 3, 0, 4, 4, 4, 0, 0};

void parse_ip_dfa_init(void) {
    int c;
    int i;
    
    for (i=0; i<=ERROR; i++) {
        int j;
        for (j=0; j<256; j++)
            table[i][j] = ERROR;
    }
    
    for (c='0'; c<='9'; c++) {
        table[START][c] = NUM1_1;
        table[NUM1_1][c] = NUM1_2;
        table[NUM1_2][c] = NUM1_3;
        
        table[DOT1][c] = NUM2_1;
        table[NUM2_1][c] = NUM2_2;
        table[NUM2_2][c] = NUM2_3;
        
        table[DOT2][c] = NUM3_1;
        table[NUM3_1][c] = NUM3_2;
        table[NUM3_2][c] = NUM3_3;

        table[DOT3][c] = NUM4_1;
        table[NUM4_1][c] = NUM4_2;
        table[NUM4_2][c] = NUM4_3;
    }
    
    table[NUM1_1]['.'] = DOT1;
    table[NUM1_2]['.'] = DOT1;
    table[NUM1_3]['.'] = DOT1;

    table[NUM2_1]['.'] = DOT2;
    table[NUM2_2]['.'] = DOT2;
    table[NUM2_3]['.'] = DOT2;

    table[NUM3_1]['.'] = DOT3;
    table[NUM3_2]['.'] = DOT3;
    table[NUM3_3]['.'] = DOT3;

    for (i=0; i<5; i++) {
        c = " \t\r\n\0"[i];
        table[NUM4_3][c] = DONE;
        table[NUM4_2][c] = DONE;
        table[NUM4_1][c] = DONE;
    }
}

size_t parse_ip_dfa(const char *buf, size_t length, unsigned *ip_address) {
    size_t offset = 0;
    unsigned state = 0;
    unsigned short nums[5] = {0, 0, 0, 0, 0};
    int is_error;
    
    while ((length - offset) && (DONE - state)) {
        unsigned c = buf[offset++];
        state = table[state][c];
        nums[indexes[state]] *= 10;
        nums[indexes[state]] += c - '0';
    }
    is_error = (nums[1]>255) + (nums[2]>255) + (nums[3]>255) + (nums[4]>255);
    if (is_error || state == ERROR)
        return 0;
    
    *ip_address = nums[1]<<24 | nums[2]<<16 | nums[3]<<8 | nums[4];
    
    return offset;
}
