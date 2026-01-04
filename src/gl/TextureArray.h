#include "Texture.h"

#define TEXARRAY_BUCKET_DIM 64
#define TEXARRAY_BUCKET_COUNT (TEXARRAY_BUCKET_DIM*TEXARRAY_BUCKET_DIM)

struct TextureBucket {
	Texture** textures;
	uint32_t count;
	uint32_t glArrayId;

	int calcMemoryUsage();
};

struct TexArrayOffset {
	uint16_t arrayIdx; // index into array buckets
	uint16_t layer; // layer wihin a bucket
};

class TextureArray {
public:
	// each index increase represents 16px increase in size, from 16 -> 1024
	TextureBucket buckets[TEXARRAY_BUCKET_COUNT];
	int numResize = 0;
	int maxBucketDepth;
	int uploadedDataSize = 0;

	TextureArray();
	~TextureArray();

	// returns the layer in a bucket a texture of this size would be added to
	TexArrayOffset tally(int width, int height);

	void getBucketDimensions(int& width, int& height);

	void add(Texture* tex);

	void upload();

	// removes textures from bucket arrays but does not delete the array textures
	void clear();

	int calcMemoryUsage();
};