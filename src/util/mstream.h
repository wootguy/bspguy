#pragma once
#include <stdint.h>

#undef read
#undef write

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

	// returns number of bytes that could be written into the buffer
	uint64_t write(void * src, uint64_t bytes);

	// write a 0 or 1
	uint64_t writeBit(uint8_t value);

	// returns the offset in the buffer
	uint64_t tell();

	// returns the size of the buffer
	uint64_t size();

	// returns pointer to the buffered memory
	char * getBuffer();

	// return pointer to the buffered memory at the current read/write offset
	char* get();

	// changes the read position in the buffer
	// resets eom flag if set to a valid position
	void seek(uint64_t to);

	// like seek but implements SEEK_CUR/SET/END functionality
	void seek(uint64_t to, int whence);

	// returns number of bytes skipped
	uint64_t skip(uint64_t bytes);

	bool eom();

	// deletes associated memory buffer
	void freeBuf();

private:
	uint8_t currentBit = 0;
	uint64_t start, end, pos;
	bool eomFlag; // end of memory buffer reached
};

