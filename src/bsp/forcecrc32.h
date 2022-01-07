#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <string.h>


void PathCrc32InMemory(unsigned char* data, unsigned int len, unsigned int offset, unsigned int oldcrc, unsigned int newcrc);
unsigned int GetCrc32InMemory(unsigned char* f, unsigned int length, unsigned int oldcrc = UINT32_C(0xFFFFFFFF));
unsigned int ReplaceCrc32InMemory(unsigned char* data, unsigned int len, unsigned int offset, unsigned int newcrc, unsigned int oldcrc = UINT32_C(0xFFFFFFFF));
unsigned int reverse_bits(unsigned int x);

uint64_t multiply_mod(uint64_t x, uint64_t y);
uint64_t pow_mod(uint64_t x, uint64_t y);
void divide_and_remainder(uint64_t x, uint64_t y, uint64_t* q, uint64_t* r);
uint64_t reciprocal_mod(uint64_t x);
int get_degree(uint64_t x);