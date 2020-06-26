#pragma once
#include "util.h"

class Texture
{
public:	
	uint id; // OpenGL texture ID
	uint height, width;
	byte * data; // RGB(A) data
	int nearFilter;
	int farFilter;
	uint format; // format of the data
	uint iformat; // format of the data when uploaded to GL
	bool uploaded = false;

	Texture(int width, int height);
	Texture(int width, int height, void * data);
	~Texture();

	// upload the texture with the specified settings
	void upload();

	// use this texture for rendering
	void bind();
};