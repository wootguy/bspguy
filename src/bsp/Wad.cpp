#include "Wad.h"
#include "util.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include <lodepng.h>
#include "quant.h"

#ifdef WIN32
	#define strcasecmp _stricmp
#endif

Wad::Wad(void)
{
	dirEntries = NULL;
}

Wad::Wad( const string& file )
{
	this->filename = file;
	numTex = -1;
	dirEntries = NULL;
}

Wad::~Wad(void)
{
	if (dirEntries)
		delete [] dirEntries;
}

string Wad::getName() {
	string wadname = filename;
	int lastSlash = wadname.find_last_of("/\\");
	if (lastSlash != -1) {
		wadname = wadname.substr(lastSlash + 1);
	}
	return wadname;
}

bool Wad::readInfo()
{
	string file = filename;

	if (!fileExists(file))
	{
		logf("%s does not exist!\n", filename.c_str());
		return false;
	}

	ifstream fin(file, ifstream::in|ios::binary);
	if (!fin.good())
		return false;

	uint begin = fin.tellg();
	fin.seekg (0, ios::end);
	uint end = fin.tellg();
	uint sz = end - begin;
	fin.seekg(0, ios::beg);

	if (sz < sizeof(WADHEADER))
	{
		fin.close();
		return false;
	}

	//
	// WAD HEADER
	//
	fin.read((char*)&header, sizeof(WADHEADER)); 

	if (string(header.szMagic).find("WAD3") != 0)
	{
		fin.close();
		return false;
	}

	if (header.nDirOffset >= sz)
	{
		fin.close();
		return false;
	}

	//
	// WAD DIRECTORY ENTRIES
	//
	fin.seekg(header.nDirOffset);
	numTex = header.nDir;
	dirEntries = new WADDIRENTRY[numTex];

	bool usableTextures = false;
	for (int i = 0; i < numTex; i++)
	{
		if (fin.eof()) { logf("Unexpected end of WAD\n"); return false; }
		fin.read((char*)&dirEntries[i], sizeof(WADDIRENTRY)); 
		if (dirEntries[i].nType == 0x43) usableTextures = true;
	}
	fin.close();

	if (!usableTextures)
	{
		delete [] dirEntries;
		dirEntries = NULL;
		header.nDir = 0;
		logf("%s contains no regular textures\n", filename.c_str());
		return false; // we can't use these types of textures (see fonts.wad as an example)
	}
	

	return true;
}

bool Wad::hasTexture(string name)
{
	for (int d = 0; d < header.nDir; d++)
		if (strcasecmp(name.c_str(), dirEntries[d].szName) == 0)
			return true;
	return false;
}

WADTEX * Wad::readTexture( int dirIndex )
{
	if (dirIndex < 0 || dirIndex >= numTex)
	{
		logf("invalid wad directory index\n");
		return NULL;
	}
	//if (cache != NULL)
		//return cache[dirIndex];
	string name = string(dirEntries[dirIndex].szName);
	return readTexture(name);
}

WADTEX* Wad::readTexture( const string& texname )
{
	string path = filename;
	const char * file = (path.c_str());

	ifstream fin(file, ifstream::in|ios::binary);
	if (!fin.good())
		return NULL;

	int idx = -1;
	for (int d = 0; d < header.nDir; d++)
	{
		if (strcasecmp(texname.c_str(), dirEntries[d].szName) == 0)
		{
			idx = d;
			break;
		}
	}

	if (idx < 0)
	{
		fin.close();
		return NULL;
	}
	if (dirEntries[idx].bCompression)
	{
		logf("OMG texture is compressed. I'm too scared to load it :<\n");
		return NULL;
	}
	fin.seekg(dirEntries[idx].nFilePos);

	BSPMIPTEX mtex;
	fin.read((char*)&mtex, sizeof(BSPMIPTEX));

	int w = mtex.nWidth;
	int h = mtex.nHeight;
	int sz = w*h;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int szAll = sz + sz2 + sz3 + sz4 + 2 + 256*3 + 2;

	byte * data = new byte[szAll];
	fin.read((char*)data, szAll);		
	
	fin.close();

	WADTEX * tex = new WADTEX;
	for (int i = 0; i < MAXTEXTURENAME; i++)
		tex->szName[i] = mtex.szName[i];
	for (int i = 0; i < MIPLEVELS; i++)
		tex->nOffsets[i] = mtex.nOffsets[i];
	tex->nWidth = mtex.nWidth;
	tex->nHeight = mtex.nHeight;
	tex->data = data;

	return tex;
}

vector<WADTEX> Wad::readAllTextures() {
	string path = filename;
	const char* file = (path.c_str());
	vector<WADTEX> allTextures;

	if (!dirEntries) {
		return allTextures;
	}

	ifstream fin(file, ifstream::in | ios::binary);
	if (!fin.good())
		return allTextures;

	for (int i = 0; i < numTex; i++) {
		if (dirEntries[i].bCompression) {
			errorf("OMG texture %s is compressed. I'm too scared to load it :<\n", dirEntries[i].szName);
			continue;
		}
		fin.seekg(dirEntries[i].nFilePos);

		WADTEX tex;
		fin.read((char*)&tex, sizeof(BSPMIPTEX));

		int w = tex.nWidth;
		int h = tex.nHeight;
		int sz = w * h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;

		tex.data = new byte[szAll];
		fin.read((char*)tex.data, szAll);
		allTextures.push_back(tex);
	}

	fin.close();

	return allTextures;
}

bool Wad::write(WADTEX* textures, int numTex)
{
	return write(filename, textures, numTex);
}

bool Wad::write( std::string filename, WADTEX* textures, int numTex )
{
	ofstream myFile(filename, ios::out | ios::binary | ios::trunc);

	header.szMagic[0] = 'W';
	header.szMagic[1] = 'A';
	header.szMagic[2] = 'D';
	header.szMagic[3] = '3';
	header.nDir = numTex;

	int tSize = sizeof(BSPMIPTEX)*numTex;
	for (int i = 0; i < numTex; i++)
	{
		int w = textures[i].nWidth;
		int h = textures[i].nHeight;
		int sz = w*h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + 2 + 256*3 + 2;
		tSize += szAll;
	}

	header.nDirOffset = 12 + tSize;
	myFile.write ((char*)&header, sizeof(WADHEADER));

	for (int i = 0; i < numTex; i++)
	{
		BSPMIPTEX miptex;
		for (int k = 0; k < MAXTEXTURENAME; k++)
			miptex.szName[k] = textures[i].szName[k];

		int w = textures[i].nWidth;
		int h = textures[i].nHeight;
		int sz = w*h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + 2 + 256*3 + 2;
		miptex.nWidth = w;
		miptex.nHeight = h;
		miptex.nOffsets[0] = sizeof(BSPMIPTEX);
		miptex.nOffsets[1] = sizeof(BSPMIPTEX) + sz;
		miptex.nOffsets[2] = sizeof(BSPMIPTEX) + sz + sz2;
		miptex.nOffsets[3] = sizeof(BSPMIPTEX) + sz + sz2 + sz3;

		myFile.write ((char*)&miptex, sizeof(BSPMIPTEX));
		myFile.write ((char*)textures[i].data, szAll);
	}

	int offset = 12;
	for (int i = 0; i < numTex; i++)
	{
		WADDIRENTRY entry;
		entry.nFilePos = offset;
		int w = textures[i].nWidth;
		int h = textures[i].nHeight;
		int sz = w*h;	   // miptex 0
		int sz2 = sz / 4;  // miptex 1
		int sz3 = sz2 / 4; // miptex 2
		int sz4 = sz3 / 4; // miptex 3
		int szAll = sz + sz2 + sz3 + sz4 + 2 + 256*3 + 2;
		entry.nDiskSize = szAll + sizeof(BSPMIPTEX);
		entry.nSize = szAll + sizeof(BSPMIPTEX);
		entry.nType = 0x43; // Texture
		entry.bCompression = false;
		entry.nDummy = 0;
		for (int k = 0; k < MAXTEXTURENAME; k++)
			entry.szName[k] = textures[i].szName[k];
		offset += szAll + sizeof(BSPMIPTEX);

		myFile.write ((char*)&entry, sizeof(WADDIRENTRY));
	}
	
	//myFile.write ((char*)textures[0]->data, szAll);
	myFile.close();

	return true;
}

WADTEX loadTextureFromPng(const std::string& filename) {
	WADTEX out;
	memset(&out, 0, sizeof(WADTEX));
	
	uint8_t* pngpixels;
	unsigned int w, h;

	if (lodepng_decode32_file(&pngpixels, &w, &h, filename.c_str())) {
		return out;
	}

	if (w % 16 != 0 || h % 16 != 0) {
		logf("Image dimensions must be divisible by 16\n");
		return out;
	}

	COLOR4* srcData = (COLOR4*)pngpixels;

	// find a color to use as the invisible color
	bool isMasked = false;
	const int numMaskColors = 5;
	COLOR3 maskColors[numMaskColors] = {
		COLOR3(0, 0, 255),
		COLOR3(255, 0, 255),
		COLOR3(0, 255, 0),
		COLOR3(0, 255, 255),
		COLOR3(255, 255, 0),
	};
	bool validMaskColors[numMaskColors];
	memset(validMaskColors, 1, sizeof(bool) * numMaskColors);

	for (int i = 0; i < w * h; i++) {
		COLOR4& c = srcData[i];
		for (int k = 0; k < numMaskColors; k++) {
			if (c.a < 128) {
				isMasked = true;
			} else if (c.rgb() == maskColors[k]) {
				validMaskColors[k] = false;
			}
		}
	}

	COLOR3 maskColor = COLOR3(1, 2, 3);
	for (int k = 0; k < numMaskColors; k++) {
		if (validMaskColors[k]) {
			maskColor = maskColors[k];
			break;
		}
	}

	vector<COLOR3> colors;
	COLOR3* rgbData = new COLOR3[w * h];
	bool* maskData = new bool[w * h];
	memset(maskData, 0, sizeof(bool) * w * h);

	for (int i = 0; i < w * h; i++) {
		COLOR4& c = srcData[i];
		COLOR3& dst = rgbData[i];
		if (c.a < 128) {
			dst = maskColor;
			maskData[w * h] = true;
		}
		else {
			dst = c.rgb();
			maskData[w * h] = false;
		}

		bool uniqueColor = true;
		for (int i = 0; i < colors.size(); i++) {
			if (dst == colors[i]) {
				uniqueColor = false;
				break;
			}
		}

		if (uniqueColor) {
			colors.push_back(dst);
		}
	}

	if (colors.size() > 256) {
		logf("Quantized %d colors to 256 colors\n", colors.size());
		if (isMasked) {
			// convert mask color to black for quantizing
			for (int i = 0; i < w * h; i++) {
				if (maskData[i]) {
					rgbData[i] = COLOR3();
				}
			}
			colors = median_cut_quantize(rgbData, w * h, 255);
			for (int i = 0; i < w * h; i++) {
				if (maskData[i]) {
					rgbData[i] = maskColor;
				}
			}
			while (colors.size() < 255) {
				colors.push_back(COLOR3());
			}
			colors.push_back(maskColor);
		}
		else {
			colors = median_cut_quantize(rgbData, w * h, 256);
			while (colors.size() < 256) {
				colors.push_back(COLOR3());
			}
		}
	}
	else {
		if (isMasked) {
			// don't include the mask color twice
			for (int i = 0; i < colors.size(); i++) {
				if (colors[i] == maskColor) {
					colors.erase(colors.begin() + i);
					break;
				}
			}
		}
		while (colors.size() < 255) {
			colors.push_back(COLOR3());
		}
		colors.push_back(isMasked ? maskColor : COLOR3());
	}

	out.loadRGB(rgbData, &colors[0], w, h);

	delete[] rgbData;
	delete[] maskData;
	delete[] pngpixels;

	return out;
}

int Wad::calcMemoryUsage() {
	return sizeof(Wad) + filename.size() * numTex * sizeof(WADDIRENTRY);
}