#include "LightmapNode.h"
#include "util.h"

LightmapNode::LightmapNode(int offX, int offY, int mapW, int mapH)
{
	child[0] = child[1] = NULL;
	x = offX;
	y = offY;
	w = mapW;
	h = mapH;
	filled = false;
}


LightmapNode::~LightmapNode(void)
{
	delete child[0];
	delete child[1];
}

bool LightmapNode::insert(int iw, int ih, int& outX, int& outY)
{
	if (child[0] != NULL) // not a leaf, try putting into child nodes
	{
		if (child[0]->insert(iw, ih, outX, outY))
			return true;
		return child[1]->insert(iw, ih, outX, outY);
	}

	// must be in a leaf. Try adding the image here

	if (iw > w || ih > h || filled) // too big or we already have an image
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
		child[0] = new LightmapNode(x, y, iw, h);
		child[1] = new LightmapNode(x + iw, y, w - iw, h);
	}
	else // split horizontally
	{
		child[0] = new LightmapNode(x, y, w, ih);
		child[1] = new LightmapNode(x, y + ih, w, h - ih);
	}
	return child[0]->insert(iw, ih, outX, outY);
}
