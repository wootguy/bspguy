#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "TextureArray.h"
#include "util.h"
#include "globals.h"
#include "colors.h"
#include "Editor.h"

TextureArray::TextureArray() {
	memset(buckets, 0, sizeof(TextureBucket) * TEXARRAY_BUCKET_COUNT);

	uint32_t* ids = new uint32_t[TEXARRAY_BUCKET_COUNT];
	glGenTextures(TEXARRAY_BUCKET_COUNT, ids);

	for (int i = 0; i < TEXARRAY_BUCKET_COUNT; i++) {
		buckets[i].glArrayId = ids[i];
	}

	delete[] ids;

	maxBucketDepth = g_max_texture_array_layers;
	if (!g_opengl_texture_array_support) {
		maxBucketDepth = 4096; // virtually no limit besides memory
	}
}

TextureArray::~TextureArray() {
	for (int i = 0; i < TEXARRAY_BUCKET_COUNT; i++) {
		if (buckets[i].count) {
			glDeleteTextures(1, &buckets[i].glArrayId);
			delete[] buckets[i].textures;
		}
	}
	g_renderStats.texMem -= uploadedDataSize;
	uploadedDataSize = 0;
}

void TextureArray::getBucketDimensions(int& width, int& height) {
	const bool aggressiveScaling = false; // reduce quality for more speed

	if (width != height) {
		// try to move this into a square bucket
		if (width % height == 0) {
			height = width;
		}
		else if (height % width == 0) {
			width = height;
		}
	}

	if (width == height && width < 64 && 64 % width == 0) {
		width = height = 64;
	}

	if (aggressiveScaling) {
		if (width < 128 || height < 128) {
			width = 128;
			height = 128;
		}
	}

	return;
}

TexArrayOffset TextureArray::tally(int width, int height) {
	TexArrayOffset offset;
	offset.arrayIdx = 0;
	offset.layer = 0;

	if ((width % 16) != 0 || (height % 16) != 0) {
		logf("Texture array got dimensions not divisible by 16\n");
		return offset;
	}
	if (width > 1024 || height > 1024 || width < 16 || height < 16) {
		logf("Texture array got invalid texture size\n");
		return offset;
	}
	if (!g_opengl_texture_array_support) {
		return offset;
	}

	getBucketDimensions(width, height);

	int bucketX = (width / 16) - 1;
	int bucketY = (height / 16) - 1;
	offset.arrayIdx = bucketY * TEXARRAY_BUCKET_DIM + bucketX;

	TextureBucket& bucket = buckets[offset.arrayIdx];

	if (bucket.count >= maxBucketDepth) {
		return offset;
	}

	bucket.count++;

	offset.layer = bucket.count - 1;
	return offset;
}

void TextureArray::add(Texture* tex) {
	if ((tex->width % 16) != 0 || (tex->height % 16) != 0) {
		logf("Texture array got dimensions not divisible by 16\n");
		return;
	}
	if (tex->width > 1024 || tex->height > 1024 || tex->width < 16 || tex->height < 16) {
		logf("Texture array got invalid texture size\n");
		return;
	}
	if (!g_opengl_texture_array_support) {
		return;
	}

	int width = tex->width;
	int height = tex->height;
	getBucketDimensions(width, height);

	int bucketX = (width / 16) - 1;
	int bucketY = (height / 16) - 1;

	TextureBucket& bucket = buckets[bucketY * TEXARRAY_BUCKET_DIM + bucketX];

	if (bucket.count >= maxBucketDepth) {
		errorf("ERROR: Texture array buffer overflowed! Incorrect textures will be displayed.\n");
		return;
	}

	if (width != tex->width || height != tex->height) {
		COLOR4* newData = new COLOR4[width * height];

		float xScale = width / (float)tex->width;
		float yScale = height / (float)tex->height;

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int srcX = x / xScale;
				int srcY = y / yScale;
				newData[y * width + x] = ((COLOR4*)tex->data)[srcY * tex->width + srcX];
			}
		}

		tex->width = width;
		tex->height = height;
		delete[] tex->data;
		tex->data = (uint8_t*)newData;

		for (int i = 1; i <= tex->numMipMaps; i++) {
			int w = width >> i;
			int h = height >> i;
			MipTexture& mip = tex->mipmaps[i - 1];
			COLOR4* newData = new COLOR4[w * h];
			COLOR4* oldData = mip.data;

			float xScale = w / (float)mip.width;
			float yScale = h / (float)mip.height;

			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					int srcX = x / xScale;
					int srcY = y / yScale;
					newData[y * w + x] = oldData[srcY * mip.width + srcX];
				}
			}

			mip.width = w;
			mip.height = h;
			delete[] mip.data;
			mip.data = newData;
		}

		numResize++;
	}	

	if (!bucket.textures) {
		bucket.textures = new Texture*[1];
		bucket.count = 0; // reset after tally() calls
	}
	else {
		Texture** oldTextures = bucket.textures;
		bucket.textures = new Texture*[bucket.count + 1];
		memcpy(bucket.textures, oldTextures, sizeof(Texture*) * bucket.count);
		delete[] oldTextures;
	}

	bucket.textures[bucket.count] = tex;

	if (g_opengl_texture_array_support) {
		tex->arrayId = bucket.glArrayId;
		tex->layer = bucket.count;
	}
	
	bucket.count += 1;
}

void TextureArray::upload() {
	int bucketCount = 0;
	int textureCount = 0;
	int texDataSz = 0;

	if (!g_opengl_texture_array_support) {
		return;
	}

	uploadedDataSize = 0;

	for (int i = 0; i < TEXARRAY_BUCKET_COUNT; i++) {
		int sizeX = ((i % TEXARRAY_BUCKET_DIM) + 1)*16;
		int sizeY = ((i / TEXARRAY_BUCKET_DIM) + 1)*16;

		if (buckets[i].count) {
			bucketCount++;
			textureCount += buckets[i].count;
			//debugf("%d textures in bucket %dx%d\n", buckets[i].count, sizeX, sizeY);

			int format = g_settings.pal_textures ? GL_RED : GL_RGBA;
			int bpp = g_settings.pal_textures ? 1 : 4;

			glBindTexture(GL_TEXTURE_2D_ARRAY, buckets[i].glArrayId);

			glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, sizeX, sizeY, buckets[i].count, 0, format, GL_UNSIGNED_BYTE, NULL);
			uploadedDataSize += sizeX * sizeY * buckets[i].count * bpp;

			// allocate mipmaps
			glTexImage3D(GL_TEXTURE_2D_ARRAY, 1, format, sizeX >> 1, sizeY >> 1, buckets[i].count, 0, format, GL_UNSIGNED_BYTE, NULL);
			glTexImage3D(GL_TEXTURE_2D_ARRAY, 2, format, sizeX >> 2, sizeY >> 2, buckets[i].count, 0, format, GL_UNSIGNED_BYTE, NULL);
			glTexImage3D(GL_TEXTURE_2D_ARRAY, 3, format, sizeX >> 3, sizeY >> 3, buckets[i].count, 0, format, GL_UNSIGNED_BYTE, NULL);
			uploadedDataSize += (sizeX >> 1) * (sizeY >> 1) * buckets[i].count * bpp;
			uploadedDataSize += (sizeX >> 2) * (sizeY >> 2) * buckets[i].count * bpp;
			uploadedDataSize += (sizeX >> 3) * (sizeY >> 3) * buckets[i].count * bpp;

			texDataSz += buckets[i].count * sizeX * sizeY * 3;

			if (buckets[i].textures) {
				for (int k = 0; k < buckets[i].count; k++) {
					Texture* tex = buckets[i].textures[k];
					COLOR4* texdata = (COLOR4*)tex->data;

					glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, k, sizeX, sizeY, 1, format, GL_UNSIGNED_BYTE, texdata);

					for (int i = 0; i < tex->numMipMaps; i++) {
						MipTexture& mip = tex->mipmaps[i];

						glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip.level, 0, 0, k,
							mip.width, mip.height, 1, format, GL_UNSIGNED_BYTE, mip.data);
					}
				}
			}

			if (g_settings.pal_textures) {
				glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			}
			else {
				glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
			}
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_LOD_BIAS, g_app->tex_lod_bias);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, buckets[i].textures[0]->numMipMaps);
		}
	}

	g_renderStats.texMem += uploadedDataSize;

	debugf("uploaded %d textures as %d arrays (%d resized, %.2f MB)\n",
		textureCount, bucketCount, numResize, texDataSz / (1024.0f*1024.0f));
}

int TextureBucket::calcMemoryUsage() {
	return sizeof(TextureBucket) + count * sizeof(Texture*);
}

int TextureArray::calcMemoryUsage() {
	int bytes = sizeof(TextureArray);
	for (int i = 0; i < TEXARRAY_BUCKET_COUNT; i++) {
		bytes += buckets[i].calcMemoryUsage();
	}
	return bytes;
}