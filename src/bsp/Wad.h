#pragma once
#include <string>
#include "bsplimits.h"

typedef unsigned char byte;
typedef unsigned int uint;

#define MAXTEXELS 262144

#define CLAMP(v, min, max) if (v < min) { v = min; } else if (v > max) { v = max; }

#pragma pack(push, 1)
struct COLOR3
{
	byte r, g, b;

	COLOR3() {}
	COLOR3(byte r, byte g, byte b) : r(r), g(g), b(b) {}
};
#pragma pack(pop)
struct COLOR4
{
	byte r, g, b, a;

	COLOR4() {}
	COLOR4(byte r, byte g, byte b, byte a) : r(r), g(g), b(b), a(a) {}
	COLOR4(COLOR3 c, byte a) : r(c.r), g(c.g), b(c.b), a(a) {}
};

COLOR3 operator*(COLOR3 v, float f);
bool operator==(COLOR3 c1, COLOR3 c2);

COLOR4 operator*(COLOR4 v, float f);
bool operator==(COLOR4 c1, COLOR4 c2);

struct WADHEADER
{
	char szMagic[4];    // should be WAD2/WAD3
	int32_t nDir;			// number of directory entries
	int32_t nDirOffset;		// offset into directories
};

struct WADDIRENTRY
{
	int32_t nFilePos;				 // offset in WAD
	int32_t nDiskSize;				 // size in file
	int32_t nSize;					 // uncompressed size
	char nType;					 // type of entry
	bool bCompression;           // 0 if none
	int16_t nDummy;				 // not used
	char szName[MAXTEXTURENAME]; // must be null terminated
};

struct WADTEX
{
	char szName[MAXTEXTURENAME];
	uint32_t nWidth, nHeight;
	uint32_t nOffsets[MIPLEVELS];
	byte * data; // all mip-maps and pallete
};

class Wad
{
public:
	std::string filename;
	WADHEADER header = WADHEADER();
	WADDIRENTRY * dirEntries;
	int numTex;

	Wad(const std::string& file);
	Wad(void);
	~Wad(void);

	bool readInfo();
	bool hasTexture(std::string name);

	bool write(std::string filename, WADTEX ** textures, int numTex);


	WADTEX * readTexture(int dirIndex);
	WADTEX * readTexture(const std::string& texname);
};

