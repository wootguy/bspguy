#include "TextureAtlas.h"
#include <stdint.h>
#include <string.h>
#include "util.h"
#include <algorithm>

TextureNode::TextureNode( int offX, int offY, int mapW, int mapH )
{
	child[0] = child[1] = -1;
	x = offX;
	y = offY;
	w = mapW;
	h = mapH;
	filled = false;
}


TextureAtlas::TextureAtlas(int mapW, int mapH, int idealZoneSize)
{
	this->mapW = mapW;
	this->mapH = mapH;
	subdivisions = min(mapW, mapH) / idealZoneSize;

	if (mapW % idealZoneSize != 0 || mapH % idealZoneSize != 0) {
		logf("Lightmap atlas size should be divisible by the ideal zone size");
	}

	int zoneW = mapW / subdivisions;
	int zoneH = mapH / subdivisions;
	
	nodePool = new TextureNode[64];
	nodePoolSz = 64;
	totalNodes = 0;

	for (int x = 0; x < subdivisions; x++) {
		for (int y = 0; y < subdivisions; y++) {
			int headNode = allocNode();
			nodePool[headNode] = TextureNode(x*zoneW, y*zoneH, zoneW, zoneH);
		}
	}
}

TextureAtlas::~TextureAtlas(void)
{
	delete[] nodePool;
}

bool TextureAtlas::insert(int id, int iw, int ih, uint16_t& outX, uint16_t& outY) {
	int subAtlas = id % (subdivisions * subdivisions);
	int subAtlasX = subAtlas % subdivisions;
	int subAtlasY = subAtlas / subdivisions;

	return insertRecurse(subAtlasY * subdivisions + subAtlasX, iw, ih, outX, outY);
}

bool TextureAtlas::insertRecurse(int iNode, int iw, int ih, uint16_t& outX, uint16_t& outY)
{
	TextureNode* node = &nodePool[iNode];

	if (node->child[0] >= 0) // not a leaf, try putting into child nodes
	{
		if (insertRecurse(node->child[0], iw, ih, outX, outY))
			return true;
		return insertRecurse(node->child[1], iw, ih, outX, outY);
	}

	// must be in a leaf. Try adding the image here

	if (node->filled || iw > node->w || ih > node->h) // too big or we already have an image
		return false;
	if (iw == node->w && ih == node->h) // just right
	{
		outX = node->x;
		outY = node->y;
		node->filled = true;
		return true;
	}

	// the image doesn't fit perfectly. Split up this space around the image and put into
	// a child node that will fit the image perfectly.

	int new0 = allocNode();
	int new1 = allocNode();
	node = &nodePool[iNode]; // in case pool was resized and pointer was invalidated

	node->child[0] = new0;
	node->child[1] = new1;

	if (node->w - iw > node->h - ih) // more horizontal space than vertical space (split vertically)
	{
		nodePool[node->child[0]] = TextureNode(node->x, node->y, iw, node->h);
		nodePool[node->child[1]] = TextureNode(node->x + iw, node->y, node->w - iw, node->h);
	}
	else // split horizontally
	{
		nodePool[node->child[0]] = TextureNode(node->x, node->y, node->w, ih);
		nodePool[node->child[1]] = TextureNode(node->x, node->y + ih, node->w, node->h - ih);
	}

	return insertRecurse(node->child[0], iw, ih, outX, outY);
}

int TextureAtlas::allocNode() {
	if (totalNodes >= nodePoolSz) {
		nodePoolSz *= 2;
		TextureNode* newNodes = new TextureNode[nodePoolSz];
		memcpy(newNodes, nodePool, sizeof(TextureNode) * totalNodes);
		delete[] nodePool;
		nodePool = newNodes;
	}

	return totalNodes++;
}