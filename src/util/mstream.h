#pragma once
#include "vectors.h"
#include <stdint.h>

class mstream
{
public:	

	mstream();

	// creates a new buffer on the heap
	// and uses it as the stream source.
	// Be sure to free() after use!
	mstream(uint64_t len);

	// stream an existing buffer
	mstream(char * buf, uint64_t len);

	~mstream(void);

	// copy data from buffer into the destination
	// returns the number of bytes read
	uint64_t read(void * dest, uint64_t bytes);

	// returns 0-1 for bit that was read, or -1 for EOM 
	uint32_t readBit();

	uint64_t readBits(uint8_t bitCount);

	vec3 readBitVec3Coord();
	
	float readBitCoord();

	// increment to the next byte if current bit is not 0
	// true if position was incremented
	bool endBitReading();

	// returns number of bytes that could be written into the buffer
	uint64_t write(void * src, uint64_t bytes);

	// write a 0 or 1 bit, partially filling a byte. Returns true on success.
	bool writeBit(bool value);

	// write bitCount bits from value. Returns bits written
	uint8_t writeBits(uint64_t value, uint8_t bitCount);

	// write zeroes into the remaining bits at the current byte, then increment the position to the next byte
	// true if position was incremented
	bool endBitWriting();

	// write Half-Life vector as bits (used in sound message)
	bool writeBitVec3Coord(const float* fa);

	// write Half-Life coordinate as bits (used in sound message)
	bool writeBitCoord(const float f);

	// returns the offset in the buffer
	uint64_t tell();

	// returns the offset in the buffer, accurate to the bit
	uint64_t tellBits();

	// returns the size of the buffer
	uint64_t size();

	// returns pointer to the buffered memory
	char * getBuffer();

	// return pointer to the buffered memory at the current read/write offset
	char* getOffsetBuffer();

	// changes the read position in the buffer
	// resets eom flag if set to a valid position
	void seek(uint64_t to);

	// position is given in bits
	void seekBits(uint64_t to);

	// like seek but implements SEEK_CUR/SET/END functionality
	void seek(uint64_t to, int whence);

	// returns number of bytes skipped
	uint64_t skip(uint64_t bytes);

	bool eom();

	// deletes associated memory buffer
	void freeBuf();

private:
	uint64_t start, end, pos;
	uint8_t currentBit = 0;
	bool eomFlag; // end of memory buffer reached
};

