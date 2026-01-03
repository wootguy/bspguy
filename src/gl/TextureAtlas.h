#pragma once
#include <stdint.h>

class TextureAtlas;

class TextureNode
{
public:
	int child[2]; // index into node pool
	uint16_t x, y, w, h;
	bool filled;

	TextureNode() { child[0] = -1; child[1] = -1; filled = false; w = 0; h = 0; x = 0; y = 0; }
	TextureNode(int offX, int offY, int mapW, int mapH);	
};

class TextureAtlas
{
public:
	int subdivisions; // zones per dimension
	int mapW, mapH;

	TextureNode* nodePool; // flat array to reduce allocation count
	int nodePoolSz;
	int totalNodes;

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

	// places lightmap into the atlas, populating x/y coordinates
	// info width/height must be set before calling
	bool insertRecurse(int iNode, int iw, int ih, uint16_t& outX, uint16_t& outY);

	int allocNode();
};
