#include "Texture.h"
#include "lodepng.h"
#include "util.h"
#include "Renderer.h"

Texture::Texture( int width, int height, int format, void * data )
{
	this->format = this->iformat = format;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = (byte*)data;
	upload();
}

Texture::~Texture()
{
	glDeleteTextures(1, &id);
	delete [] data;
}

void Texture::upload()
{
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &id);

	glBindTexture(GL_TEXTURE_2D, id); // Binds this texture handle so we can load the data into it

	// Set up filters and wrap mode
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, farFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

	glTexImage2D(GL_TEXTURE_2D, 0, iformat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
}

void Texture::bind()
{
	glBindTexture(GL_TEXTURE_2D, id);
}
