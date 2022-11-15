#include "mstream.h"
#include "stdio.h"
#include "string.h"

mstream::mstream()
{
	currentBit = start = end = pos = 0;
	eomFlag = true;
}

mstream::mstream(char * buf, uint64_t len)
{
	start = (uint64_t)buf;
	end = start + len;
	pos = start;
	eomFlag = false;
}

mstream::mstream( uint64_t len )
{
	start = (uint64_t)new char[len];
	end = start + len;
	pos = start;
	eomFlag = false;
}

uint64_t mstream::read( void * dest, uint64_t bytes )
{
	if (eomFlag)
		return 0;
	uint64_t newpos = pos + bytes;
	if (newpos > end || newpos < start)
	{
		eomFlag = true;
		bytes = end - pos;
	}
	memcpy(dest, (void*)pos, bytes);
	pos = newpos;

	return bytes;
}

uint64_t mstream::write( void * src, uint64_t bytes )
{
	if (eomFlag)
		return 0;
	uint64_t newpos = pos + bytes;
	if (newpos > end || newpos < start)
	{
		eomFlag = true;
		bytes = end - pos;
	}
	memcpy((void*)pos, src, bytes);
	pos = newpos;

	return bytes;
}

uint64_t mstream::writeBit(uint8_t value) {
	if (eomFlag)
		return 0;

	if (currentBit >= 8) {
		if (pos + 1 >= end) {
			eomFlag = true;
			return 0;
		}
		
		pos++;
		currentBit = 0;
	}

	*((uint8_t*)pos) |= (value & 1) << currentBit;

	currentBit++;
	return 1;
}

void mstream::seek( uint64_t to )
{
	pos = start + to;
	currentBit = 0;
	eomFlag = false;
	if (pos >= end || pos < start)
		eomFlag = true;
}

void mstream::seek( uint64_t to, int whence )
{
	switch(whence)
	{
	case (SEEK_SET):
		pos = start + to;
		break;
	case (SEEK_CUR):
		pos += to;
		break;
	case (SEEK_END):
		pos = end + to;
		break;
	}
	currentBit = 0;
	eomFlag = false;
	if (pos >= end || pos < start)
		eomFlag = true;
}

uint64_t mstream::skip( uint64_t bytes )
{
	if (eomFlag)
		return 0;
	uint64_t newpos = pos + bytes;
	if (newpos >= end || newpos < start)
	{
		bytes = end - pos;
		eomFlag = true;
	}
	pos = newpos;
	currentBit = 0;
	return bytes;
}

uint64_t mstream::tell()
{
	return pos - start;
}

char * mstream::getBuffer()
{
	return (char*)start;
}

char* mstream::getOffsetBuffer() {
	return (char*)start + pos;
}

bool mstream::eom()
{
	return eomFlag;
}

void mstream::freeBuf()
{
	delete [] (char*)start;
}

mstream::~mstream( void )
{

}

uint64_t mstream::size()
{
	return end - start;
}
