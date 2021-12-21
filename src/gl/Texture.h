#pragma once
#include "util.h"

class Texture
{
public:
	uint id; // OpenGL texture ID
	uint height, width;
	int nearFilter;
	int farFilter;
	uint format; // format of the data
	uint iformat; // format of the data when uploaded to GL

	Texture(int width, int height);
	Texture(int width, int height, byte* data);
	~Texture();

	// upload the texture with the specified settings
	void upload(int format, bool lightmap = false);

	// use this texture for rendering
	void bind();

	byte* data; // RGB(A) data

	bool uploaded = false;
};