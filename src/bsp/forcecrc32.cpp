/*
 * CRC-32 forcer (C++)
 *
 * Copyright (c) 2021 Project Nayuki
 * https://www.nayuki.io/page/forcing-a-files-crc-to-any-value
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING.txt).
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "forcecrc32.h"

 // Public library function. Returns NULL if successful, a string starting with "I/O error: "
 // if an I/O error occurred (please see perror()), or a string if some other error occurred.

unsigned int ReplaceCrc32InMemory(unsigned char* data, unsigned int len, unsigned int offset, unsigned int newcrc, unsigned int oldcrc) {
	unsigned int crc = GetCrc32InMemory(data, len, oldcrc);
	PathCrc32InMemory(data, len, offset, crc, newcrc);
	return GetCrc32InMemory(data, len);
}
/*---- Utilities ----*/

// Generator polynomial. Do not modify, because there are many dependencies
const uint64_t POLYNOMIAL = UINT64_C(0x104C11DB7);

void PathCrc32InMemory(unsigned char* data, unsigned int len, unsigned int offset, unsigned int oldcrc, unsigned int newcrc)
{
	unsigned int delta = oldcrc ^ newcrc;
	delta = (unsigned int)multiply_mod(reciprocal_mod(pow_mod(2, (len - offset) * 8)), delta);

	for (int i = 0; i < 4; i++) {
		int b = data[offset + i];
		b ^= (int)((reverse_bits(delta) >> (i * 8)) & 0xFF);
		data[offset + i] = b;
	}
}

unsigned int GetCrc32InMemory(unsigned char* f, unsigned int length, unsigned int oldcrc) {
	unsigned int crc = oldcrc;
	for (size_t i = 0; i < length; i++) {
		for (int j = 0; j < 8; j++) {
			unsigned int bit = ((unsigned char)f[i] >> j) & 1;
			crc ^= bit << 31;
			bool xor = (crc >> 31) != 0;
			crc = (crc & UINT32_C(0x7FFFFFFF)) << 1;
			if (xor)
				crc ^= (unsigned int)POLYNOMIAL;
		}
	}
	return crc;
}

unsigned int reverse_bits(unsigned int x) {
	unsigned int result = 0;
	for (int i = 0; i < 32; i++, x >>= 1)
		result = (result << 1) | (x & 1U);
	return result;
}


/*---- Polynomial arithmetic ----*/

// Returns polynomial x multiplied by polynomial y modulo the generator polynomial.
uint64_t multiply_mod(uint64_t x, uint64_t y) {
	// Russian peasant multiplication algorithm
	uint64_t z = 0;
	while (y != 0) {
		z ^= x * (y & 1);
		y >>= 1;
		x <<= 1;
		if (((x >> 32) & 1) != 0)
			x ^= POLYNOMIAL;
	}
	return z;
}


// Returns polynomial x to the power of natural number y modulo the generator polynomial.
uint64_t pow_mod(uint64_t x, uint64_t y) {
	// Exponentiation by squaring
	uint64_t z = 1;
	while (y != 0) {
		if ((y & 1) != 0)
			z = multiply_mod(z, x);
		x = multiply_mod(x, x);
		y >>= 1;
	}
	return z;
}


// Computes polynomial x divided by polynomial y, returning the quotient and remainder.
void divide_and_remainder(uint64_t x, uint64_t y, uint64_t* q, uint64_t* r) {
	if (y == 0) {
		return;
	}
	if (x == 0) {
		*q = 0;
		*r = 0;
		return;
	}

	int ydeg = get_degree(y);
	uint64_t z = 0;
	for (int i = get_degree(x) - ydeg; i >= 0; i--) {
		if (((x >> (i + ydeg)) & 1) != 0) {
			x ^= y << i;
			z |= (uint64_t)1 << i;
		}
	}
	*q = z;
	*r = x;
}

// Returns the reciprocal of polynomial x with respect to the generator polynomial.
uint64_t reciprocal_mod(uint64_t x) {
	// Based on a simplification of the extended Euclidean algorithm
	uint64_t y = x;
	x = POLYNOMIAL;
	uint64_t a = 0;
	uint64_t b = 1;
	while (y != 0) {
		uint64_t q, r;
		divide_and_remainder(x, y, &q, &r);
		uint64_t c = a ^ multiply_mod(q, b);
		x = y;
		y = r;
		a = b;
		b = c;
	}
	if (x == 1)
		return a;
	else {
		return 0;
	}
}


int get_degree(uint64_t x) {
	int result = -1;
	for (; x != 0; x >>= 1)
		result++;
	return result;
}

