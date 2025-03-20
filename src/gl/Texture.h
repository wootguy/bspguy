#pragma once

class Texture
{
public:	
	uint32_t id; // OpenGL texture ID
	uint32_t height, width;
	uint8_t * data; // RGB(A) data
	int nearFilter;
	int farFilter;
	uint32_t format; // format of the data
	uint32_t iformat; // format of the data when uploaded to GL
	bool uploaded = false;
	bool isLightmap; // always filtered

	Texture(int width, int height);
	Texture(int width, int height, void * data);
	~Texture();

	// upload the texture with the specified settings
	void upload(int format, bool lighmap=false);

	// use this texture for rendering
	void bind();
};