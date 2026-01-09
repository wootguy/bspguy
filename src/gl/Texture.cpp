#include <GL/glew.h>
#include "colors.h"
#include "Texture.h"
#include "globals.h"
#include <string.h>
#include <cmath>
#include "resample.h"
#include "quant.h"
#include <unordered_set>
#include "util.h"
#include "Editor.h"

Texture::Texture(int width, int height) {
	this->width = width;
	this->height = height;
	this->depth = 1;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = new uint8_t[width*height*sizeof(COLOR4)];
	this->format = GL_RGBA;
	arrayId = -1;
	layer = 0;
	is3d = false;
}

Texture::Texture(int width, int height, int depth) {
	this->width = width;
	this->height = height;
	this->depth = depth;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = new uint8_t[width * height * depth * sizeof(COLOR4)];
	this->format = GL_RGBA;
	arrayId = -1;
	layer = 0;
	is3d = true;
}

Texture::Texture( int width, int height, void * data )
{
	this->width = width;
	this->height = height;
	this->depth = 1;
	this->nearFilter = GL_LINEAR;
	this->farFilter = GL_LINEAR_MIPMAP_LINEAR;
	this->data = (uint8_t*)data;
	arrayId = -1;
	layer = 0;
	is3d = false;
}

Texture::~Texture()
{
	if (uploaded) {
		glDeleteTextures(1, &id);
		g_renderStats.texMem -= uploadedDataSize;
	}
	if (data)
		delete[] data;
	for (int i = 0; i < numMipMaps; i++) {
		delete[] mipmaps[i].data;
	}
	numMipMaps = 0;
}

vector<COLOR3> Texture::resample(COLOR3* srcData, int srcW, int srcH, COLOR3* dstData,
	int dstW, int dstH, int mode, int outputMode, COLOR3 maskColor) {
	
	vector<COLOR3> palette;

	if (outputMode == RESAMP_PAL_MASKED && mode != KernelTypeNearest) {
		COLOR3* maskedData = new COLOR3[srcW * srcH];
		memcpy(maskedData, srcData, srcW * srcH * sizeof(COLOR3));

		// replace the mask color with black. Better to fade edges into black than bright blue/pink.
		for (int i = 0; i < srcW * srcH; i++) {
			if (maskedData[i] == maskColor) {
				maskedData[i] = COLOR3(0, 0, 0);
			}
		}
		resample24((byte*)maskedData, srcW, srcH, (byte*)dstData, dstW, dstH, mode);
		delete[] maskedData;

		// quantize the image, saving one palette entry for the mask color
		palette = median_cut_quantize(dstData, dstW * dstH, 255);

		// apply the mask color using nearest neighbor sampling
		COLOR3* nearestResamp = new COLOR3[dstW * dstH];
		resample24((byte*)srcData, srcW, srcH, (byte*)nearestResamp, dstW, dstH, KernelTypeNearest);
		for (int i = 0; i < dstW * dstH; i++) {
			if (nearestResamp[i] == maskColor) {
				dstData[i] = maskColor;
			}
		}
		delete[] nearestResamp;

		while (palette.size() < 255) {
			palette.push_back(COLOR3());
		}
		palette.push_back(maskColor);
	}
	else {
		resample24((byte*)srcData, srcW, srcH, (byte*)dstData, dstW, dstH, mode);
		
		if (outputMode == RESAMP_PAL) {
			if (mode != KernelTypeNearest) {
				palette = median_cut_quantize(dstData, dstW * dstH, 256);
			}
			else {
				unordered_set<COLOR3> uniqueColors;
				for (int i = 0; i < dstW * dstH; i++) {
					uniqueColors.insert(dstData[i]);
				}
				palette = vector<COLOR3>(uniqueColors.begin(), uniqueColors.end());
			}
		}
	}

	return palette;
}

void Texture::generateMipMaps(int mipLevels, COLOR3 maskColor) {
	for (int i = 0; i < numMipMaps; i++) {
		delete[] mipmaps[i].data;
	}

	numMipMaps = 0;
	COLOR4* texdata = (COLOR4*)data;

	if (mipLevels == 0)
		return;

	// convert to 24bit for resample lib
	COLOR3* data24 = new COLOR3[width * height];
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			COLOR4& src = texdata[y * width + x];
			COLOR3& dst = data24[y * width + x];
			dst.r = src.r;
			dst.g = src.g;
			dst.b = src.b;
			if (src.a == 0) {
				dst = maskColor;
			}
		}
	}

	for (int m = 1; m <= mipLevels; m++) {
		int mipWidth = width >> m;
		int mipHeight = height >> m;
		int scale = 1 << m;

		COLOR3* mipData24 = new COLOR3[mipWidth * mipHeight];
		resample24((byte*)data24, width, height, (byte*)mipData24, mipWidth, mipHeight,
			//KernelTypeNearest); // for masked textures
			KernelTypeAverage); // checkerboards look less flickery with this
			//KernelTypeBilinear);
			//KernelTypeLanczos3);

		// use nearest sampling to fill the alpha channel
		COLOR4* mipData32 = new COLOR4[mipWidth * mipHeight];
		for (int y = 0; y < mipHeight; y++) {
			for (int x = 0; x < mipWidth; x++) {
				COLOR3& src = mipData24[y * mipWidth + x];
				COLOR4& dst = mipData32[y * mipWidth + x];
				COLOR4& originalSrc = texdata[y * width * scale + x * scale];
				dst.r = src.r;
				dst.g = src.g;
				dst.b = src.b;
				dst.a = originalSrc.a;
			}
		}
		delete[] mipData24;

		MipTexture mip;
		mip.width = mipWidth;
		mip.height = mipHeight;
		mip.data = mipData32;
		mip.level = m;
		mipmaps[numMipMaps++] = mip;
	}

	delete[] data24;
}

void Texture::loadRgbFromIndexed(uint8_t* srcData, COLOR3* pal) {
	int sz = width * height;
	data = (uint8_t*)new COLOR3[sz];

	for (int i = 0; i < sz; i++) {
		((COLOR3*)data)[i] = pal[srcData[i]];
	}
}

void Texture::addMipMap(int mipLevel, uint8_t* srcData, COLOR3* pal) {
	if (mipLevel == 0) {
		errorf("Can't replace base mip");
		return;
	}
	if (mipLevel > numMipMaps + 1) {
		errorf("Load mip maps in order!\n");
		return;
	}

	int w = width >> mipLevel;
	int h = height >> mipLevel;
	MipTexture& tex = mipmaps[mipLevel - 1];
	tex.width = w;
	tex.height = h;
	tex.level = mipLevel;
	tex.data = new COLOR4[w*h];
	int idx = 0;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t palIdx = srcData[y * w + x];
			tex.data[idx++] = COLOR4(pal[palIdx], palIdx == 255 ? 0 : 255);
		}
	}

	numMipMaps++;
}

void Texture::addMipMap(int mipLevel, uint8_t* srcData) {
	if (mipLevel == 0) {
		errorf("Can't replace base mip");
		return;
	}
	if (mipLevel > numMipMaps + 1) {
		errorf("Load mip maps in order!\n");
		return;
	}

	int w = width >> mipLevel;
	int h = height >> mipLevel;
	MipTexture& tex = mipmaps[mipLevel - 1];
	tex.width = w;
	tex.height = h;
	tex.level = mipLevel;
	tex.data = (COLOR4*)(new uint8_t[w * h]);

	memcpy(tex.data, srcData, w * h);

	numMipMaps++;
}

int Texture::getPixelBytes(int format) {
	switch (format) {
	default:
		return 4;
	case GL_LUMINANCE: return 1;
	case GL_RGB: return 3;
	case GL_RGBA: return 4;
	case GL_RGBA32F: return 16;
	}
}

void Texture::upload(int format, bool lightmap, bool deleteData)
{
	if (!data) {
		return;
	}

	this->format = format;
	this->isLightmap = lightmap;
	if (uploaded) {
		g_renderStats.texMem -= uploadedDataSize;
		glDeleteTextures(1, &id);
	}
	glGenTextures(1, &id);

	uploadedDataSize = 0;

	if (is3d) {
		int glParam3d = g_use_texture_arrays ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_3D;

		glBindTexture(glParam3d, id); // Binds this texture handle so we can load the data into it

		glTexParameteri(glParam3d, GL_TEXTURE_WRAP_S, GL_REPEAT); // Note: GL_CLAMP is significantly slower
		glTexParameteri(glParam3d, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// Set up filters
		glTexParameteri(glParam3d, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(glParam3d, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		if (format == GL_RGB)
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		}

		glTexImage3D(glParam3d, 0, format, width, height, depth, 0, format, GL_UNSIGNED_BYTE, data);
		uploadedDataSize += width * height * depth * getPixelBytes(format);
	}
	else {
		glBindTexture(GL_TEXTURE_2D, id); // Binds this texture handle so we can load the data into it

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // Note: GL_CLAMP is significantly slower
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// Set up filters
		if (lightmap)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
		else
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}

		if (format == GL_RGB)
		{
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		}

		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		uploadedDataSize += width * height * getPixelBytes(format);

		if (!lightmap && width % 16 == 0 && height % 16 == 0 && format == GL_RGBA) {
			const int mipLevels = numMipMaps;
			COLOR4* texdata = (COLOR4*)data;

			for (int i = 0; i < numMipMaps; i++) {
				MipTexture& mip = mipmaps[i];
				glTexImage2D(GL_TEXTURE_2D, mip.level, format, mip.width, mip.height, 0, format, GL_UNSIGNED_BYTE, mip.data);
				uploadedDataSize += mip.width * mip.height * getPixelBytes(format);
			}

			if (numMipMaps) {
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, g_app->tex_lod_bias);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels);
			}
			else {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			}
		}
		else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}
	}

	if (deleteData) {
		// don't keep duplicate data in cpu mem
		delete[] data;
		for (int i = 0; i < numMipMaps; i++) {
			delete[] mipmaps[i].data;
			mipmaps[i].data = NULL;
		}
		numMipMaps = 0;
		data = NULL;
	}

	uploaded = true;
	g_renderStats.texMem += uploadedDataSize;
}

void Texture::bind()
{	
	g_renderStats.numTextureBinds++;
	int filterMin = g_settings.texture_filtering ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
	int filterMax = g_settings.texture_filtering ? GL_LINEAR : GL_NEAREST;

	if (arrayId != -1) {
		glBindTexture(GL_TEXTURE_2D_ARRAY, arrayId);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, filterMin);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, filterMax);
		return;
	}

	if (is3d) {
		// 3D textures break when using any interpolation or mip-mapping.
		// A new shader is needed for bilinear filtering without blending the Z axis.
		glBindTexture(GL_TEXTURE_2D_ARRAY, id);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		return;
	}

	glBindTexture(GL_TEXTURE_2D, id);

	if (isLightmap) {
		// always linear for smooth lighting
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMin);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMax);
	}
}


int Texture::calcMemoryUsage() {
	int bytes = sizeof(Texture);
	int bpp = getPixelBytes(format);
	bytes += data ? width * height * depth * bpp : 0;

	for (int i = 0; i < numMipMaps; i++) {
		MipTexture& mip = mipmaps[i];
		bytes += sizeof(MipTexture);
		bytes += mip.data ? mip.width * mip.height * bpp : 0;
	}

	return bytes;
}