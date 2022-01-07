#pragma once
#include <string>
#include "bsplimits.h"
#include "bsptypes.h"

#define MAXTEXELS 262144

#define CLAMP(v, min, max) if (v < min) { v = min; } else if (v > max) { v = max; }

#pragma pack(push, 1)
struct COLOR3
{
	unsigned char r, g, b;

	COLOR3() = default;
	COLOR3(unsigned char r, unsigned char g, unsigned char b) : r(r), g(g), b(b) {}
};
#pragma pack(pop)
struct COLOR4
{
	unsigned char r, g, b, a;
	COLOR4() = default;
	COLOR4(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a) {}
	COLOR4(COLOR3 c, unsigned char a) : r(c.r), g(c.g), b(c.b), a(a) {}
};

COLOR3 operator*(COLOR3 v, float f);
bool operator==(COLOR3 c1, COLOR3 c2);

COLOR4 operator*(COLOR4 v, float f);
bool operator==(COLOR4 c1, COLOR4 c2);

struct WADHEADER
{
	char szMagic[4];    // should be WAD2/WAD3
	int nDir;			// number of directory entries
	int nDirOffset;		// offset into directories
};

struct WADDIRENTRY
{
	int nFilePos;				 // offset in WAD
	int nDiskSize;				 // size in file
	int nSize;					 // uncompressed size
	char nType;					 // type of entry
	bool bCompression;           // 0 if none
	short nDummy;				 // not used
	char szName[MAXTEXTURENAME]; // must be null terminated
};

struct WADTEX
{
	char szName[MAXTEXTURENAME];
	unsigned int nWidth, nHeight;
	unsigned int nOffsets[MIPLEVELS];
	unsigned char* data; // all mip-maps and pallete
	WADTEX()
	{
		szName[0] = '\0';
		data = NULL;
	}
	WADTEX(BSPMIPTEX* tex)
	{
#ifdef WIN32
		sprintf_s(szName, MAXTEXTURENAME, "%s", tex->szName);
#else 
		snprintf(szName, "%s", tex->szName);
#endif

		nWidth = tex->nWidth;
		nHeight = tex->nHeight;
		for (int i = 0; i < MIPLEVELS; i++)
			nOffsets[i] = tex->nOffsets[i];
		data = (unsigned char*)(((unsigned char*)tex) + tex->nOffsets[0]);
	}
};

class Wad
{
public:
	std::string filename;
	WADHEADER header = WADHEADER();
	WADDIRENTRY* dirEntries;
	int numTex;

	Wad(const std::string& file);
	Wad(void);
	~Wad(void);

	bool readInfo();
	bool hasTexture(std::string name);

	bool write(std::string filename, WADTEX** textures, int numTex);
	bool write(WADTEX** textures, int numTex);


	WADTEX* readTexture(int dirIndex);
	WADTEX* readTexture(const std::string& texname);
};

