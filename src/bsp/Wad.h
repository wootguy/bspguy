#pragma once
#include <string>
#include "bsptypes.h"
#include "colors.h"
#include <cstring>

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
	int getPaletteOffset() const {
		int w = nWidth;
		int h = nHeight;
		int sz = w * h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		return sz + sz2 + sz3 + sz4;
	}
	int getDataSize() const {
		return getPaletteOffset() + 2 + 256 * 3;
	}
	uint8_t* getMip(int mipLevel) const {
		int w = nWidth;
		int h = nHeight;
		int sz = w * h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2

		switch (mipLevel) {
		default:
		case 0:
			return data;
		case 1:
			return data + sz;
		case 2:
			return data + sz + sz2;
		case 3:
			return data + sz + sz2 + sz3;
		}
	}
	COLOR3* getPalette() const {
		if (!data)
			return NULL;

		return (COLOR3*)(data + getPaletteOffset() + 2);
	}
	void loadRGB(COLOR3* pixels, COLOR3* palette, int w, int h) {
		nWidth = w;
		nHeight = h;
		data = new byte[getDataSize()];

		COLOR3* pal = getPalette();
		for (int i = 0; i < 256; i++) {
			pal[i] = palette[i];
		}

		uint8_t* mip0 = getMip(0);
		memset(mip0, 0, w * h);
		for (int i = 0; i < w * h; i++) {
			for (int k = 0; k < 256; k++) {
				if (pixels[i] == pal[k]) {
					mip0[i] = k;
				}
			}
		}

		uint16_t* palCount = (uint16_t*)((uint8_t*)pal - 2);
		*palCount = 256;

		generateMips();
	}
	void generateMips() {
		if (!data)
			return;
		nOffsets[0] = sizeof(BSPMIPTEX);
		for (int i = 1; i < 4; i++) {
			uint8_t* dstData = getMip(i);
			int mipWidth = nWidth >> i;
			int mipHeight = nHeight >> i;
			int mipScale = 1 << i;
			nOffsets[i] = nOffsets[i - 1] + (nWidth >> (i - 1)) * (nHeight >> (i - 1));

			for (int y = 0; y < mipHeight; y++) {
				for (int x = 0; x < mipWidth; x++) {
					dstData[y * mipWidth + x] = data[y * mipScale * nWidth + x * mipScale];
				}
			}
		}
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

	bool write(std::string filename, WADTEX* textures, int numTex);
	bool write(WADTEX* textures, int numTex);


	WADTEX * readTexture(int dirIndex);
	WADTEX * readTexture(const std::string& texname);
	vector<WADTEX> readAllTextures();

	int calcMemoryUsage();
};

WADTEX loadTextureFromPng(const std::string& filename);

