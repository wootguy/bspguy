#include <GL/glew.h>
#include "Wad.h"
#include "Texture.h"
#include "lodepng.h"
#include "util.h"

Texture::Texture(int width, int height) {
	this->width = width;
	this->height = height;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = new byte[width*height*sizeof(COLOR3)];
}

Texture::Texture( int width, int height, byte * data )
{
	this->width = width;
	this->height = height;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = data;
}

Texture::~Texture()
{
	if (uploaded)
		glDeleteTextures(1, &id);
	delete[] data;
}

void Texture::upload(int format, bool lightmap)
{
	if (uploaded) {
		glDeleteTextures(1, &id);
	}
	glGenTextures(1, &id);

	glBindTexture(GL_TEXTURE_2D, id); // Binds this texture handle so we can load the data into it

	// Set up filters and wrap mode
	if (lightmap)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	if (format == GL_RGB)
	{
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	}


	//glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

	// TODO: load mipmaps from BSP/WAD

	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

	uploaded = true;
}

void Texture::bind()
{
	glBindTexture(GL_TEXTURE_2D, id);
}
