#include "TextureAtlas.h"
#include <stdint.h>
#include <string.h>
#include "util.h"
#include <algorithm>

TextureNode::TextureNode( int offX, int offY, int mapW, int mapH )
{
	child[0] = child[1] = NULL;
	x = offX;
	y = offY;
	w = mapW;
	h = mapH;
	filled = false;
}


TextureNode::~TextureNode(void)
{
	delete child[0];
	delete child[1];
}

bool TextureNode::insert(int iw, int ih, int& outX, int& outY)
{
	if (child[0] != NULL) // not a leaf, try putting into child nodes
	{
		if (child[0]->insert(iw, ih, outX, outY))
			return true;
		return child[1]->insert(iw, ih, outX, outY);
	}

	// must be in a leaf. Try adding the image here

	if (filled || iw > w || ih > h) // too big or we already have an image
		return false;
	if (iw == w && ih == h) // just right
	{
		outX = x;
		outY = y;
		filled = true;
		return true;
	}

	// the image doesn't fit perfectly. Split up this space around the image and put into
	// a child node that will fit the image perfectly.

	if (w - iw > h - ih) // more horizontal space than vertical space (split vertically)
	{
		child[0] = new TextureNode(x, y, iw, h);
		child[1] = new TextureNode(x + iw, y, w - iw, h);
	}
	else // split horizontally
	{
		child[0] = new TextureNode(x, y, w, ih);
		child[1] = new TextureNode(x, y + ih, w, h - ih);
	}
	return child[0]->insert(iw, ih, outX, outY);
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
	
	zones = new TextureNode *[subdivisions * subdivisions];

	for (int x = 0; x < subdivisions; x++) {
		for (int y = 0; y < subdivisions; y++) {
			zones[y* subdivisions + x] = new TextureNode(x*zoneW, y*zoneH, zoneW, zoneH);
		}
	}
}


TextureAtlas::~TextureAtlas(void)
{
	for (int i = 0; i < subdivisions*subdivisions; i++) {
		delete zones[i];
	}
	delete[] zones;
}

bool TextureAtlas::insert(int id, int iw, int ih, int& outX, int& outY) {
	int subAtlas = id % (subdivisions * subdivisions);
	int subAtlasX = subAtlas % subdivisions;
	int subAtlasY = subAtlas / subdivisions;

	return zones[subAtlasY* subdivisions + subAtlasX]->insert(iw, ih, outX, outY);
}