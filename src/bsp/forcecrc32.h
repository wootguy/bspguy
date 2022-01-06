#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <string.h>


void PathCrc32InMemory(unsigned char* data, uint32_t len, uint32_t offset, uint32_t oldcrc, uint32_t newcrc);
uint32_t GetCrc32InMemory(unsigned char* f, uint32_t length, uint32_t oldcrc = UINT32_C(0xFFFFFFFF));
uint32_t ReplaceCrc32InMemory(unsigned char* data, uint32_t len, uint32_t offset, uint32_t newcrc, uint32_t oldcrc = UINT32_C(0xFFFFFFFF));
uint32_t reverse_bits(uint32_t x);

uint64_t multiply_mod(uint64_t x, uint64_t y);
uint64_t pow_mod(uint64_t x, uint64_t y);
void divide_and_remainder(uint64_t x, uint64_t y, uint64_t* q, uint64_t* r);
uint64_t reciprocal_mod(uint64_t x);
int get_degree(uint64_t x);