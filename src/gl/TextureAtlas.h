#pragma once
#include <stdint.h>

class TextureNode
{
public:
	TextureNode* child[2];
	uint16_t x, y, w, h;
	bool filled;

	TextureNode(int offX, int offY, int mapW, int mapH);
	~TextureNode(void);

	// places lightmap into the atlas, populating x/y coordinates
	// info width/height must be set before calling
	bool insert(int iw, int ih, uint16_t& outX, uint16_t& outY);
};

class TextureAtlas
{
public:
	TextureNode** zones; // sub-atlases to prevent node trees getting too deep
	int subdivisions; // zones per dimension
	int mapW, mapH;

	// idealZoneSize should be:
	// - not so big that it's slow to insert into
	// - not so small that it can't hold many textures and splits up the atlas too much
	// - greater or equal to the largest insertion
	TextureAtlas(int mapW, int mapH, int idealZoneSize);
	~TextureAtlas(void);

	// places lightmap into the atlas, populating x/y coordinates
	// info width/height must be set before calling
	// id is used to select a sub-atlas (use face idex)
	bool insert(int id, int iw, int ih, uint16_t& outX, uint16_t& outY);
};
