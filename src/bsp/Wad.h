#pragma once
#include <string>
#include "bsptypes.h"
#include "colors.h"

typedef unsigned char byte;
typedef unsigned int uint;

#define MAXTEXELS 262144

#define CLAMP(v, min, max) if (v < min) { v = min; } else if (v > max) { v = max; }

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
	WADTEX()
	{
		szName[0] = '\0';
		data = nullptr;
	}
	WADTEX(BSPMIPTEX* tex)
	{
		sprintf(szName, "%s", tex->szName);
		nWidth = tex->nWidth;
		nHeight = tex->nHeight;
		for (int i = 0; i < MIPLEVELS; i++)
			nOffsets[i] = tex->nOffsets[i];
		data = (byte * )(((byte*)tex) + tex->nOffsets[0]);
	}
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

	string getName();

	bool readInfo();
	bool hasTexture(std::string name);

	bool write(std::string filename, WADTEX** textures, int numTex);
	bool write(WADTEX** textures, int numTex);


	WADTEX * readTexture(int dirIndex);
	WADTEX * readTexture(const std::string& texname);
};

