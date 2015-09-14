#include "stdint.h"


// Functions
inline int ipow(int base, int exp)
{
    int result = 1;
    while (exp)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}


#define char_to_num(character, increment) switch(character) {\
        case 'C' : increment = 1; break;\
        case 'G' : increment = 2; break;\
        case 'T' : increment = 3; break;\
        default: increment = 0; break;\
    }



inline int get_binner(char *barcode, int length)
{
    int bin = 0;
    size_t count = 0;
    int inc_binner;
    for(int i = length; i; --i){
        char_to_num(barcode[i - 1], inc_binner);
        bin += ( ipow(4, count) * inc_binner);
        count++;
    }
    return bin;
}


inline uint64_t ulpow(uint64_t base, uint64_t exp)
{
    uint64_t result = 1;
    while (exp)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}


inline uint64_t get_binnerl(char *barcode, uint64_t length)
{
    uint64_t bin = 0;
    size_t count = 0;
    int inc_binner;
    for(int i = length; i; --i){
        char_to_num(barcode[i - 1], inc_binner);
        bin += ( ulpow(4, count) * inc_binner);
        count++;
    }
    return bin;
}
