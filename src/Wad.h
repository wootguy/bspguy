#pragma once
#include <string>

typedef unsigned char byte;
typedef unsigned int uint;

#define MAXTEXTURENAME 16
#define MIPLEVELS 4
#define MAXTEXELS 262144

struct COLOR3
{
	unsigned char r, g, b;
};

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

struct BSPMIPTEX
{
	char szName[MAXTEXTURENAME];  // Name of texture
	uint nWidth, nHeight;		  // Extends of the texture
	uint nOffsets[MIPLEVELS];	  // Offsets to texture mipmaps BSPMIPTEX;
};

struct WADTEX
{
	char szName[MAXTEXTURENAME];
	uint nWidth, nHeight;
	uint nOffsets[MIPLEVELS];
	byte * data; // all mip-maps and pallete
};

class Wad
{
public:
	std::string filename;
	WADHEADER header;
	WADDIRENTRY * dirEntries;
	BSPMIPTEX * miptex;
	WADTEX ** cache;
	int numTex;

	Wad(const std::string& file);
	Wad(void);
	~Wad(void);

	void loadCache();
	bool readInfo();
	bool hasTexture(std::string name);

	bool write(std::string filename, WADTEX ** textures, int numTex);


	WADTEX * readTexture(int dirIndex);
	WADTEX * readTexture(const std::string& texname);
};

