#pragma once
#include "util.h"

class Texture
{
public:
	unsigned int id; // OpenGL texture ID
	unsigned int height, width;
	int nearFilter;
	int farFilter;
	unsigned int format; // format of the data
	unsigned int iformat; // format of the data when uploaded to GL

	Texture(int width, int height);
	Texture(int width, int height, unsigned char* data);
	~Texture();

	// upload the texture with the specified settings
	void upload(int format, bool lightmap = false);

	// use this texture for rendering
	void bind();

	unsigned char* data; // RGB(A) data

	bool uploaded = false;
};