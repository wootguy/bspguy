#pragma once
#include <stdint.h>
#include <vector>
#include "colors.h"
#include "resample.h"

struct MipTexture {
	uint16_t width;
	uint16_t height;
	uint16_t level;
	COLOR4* data;
};

enum resample_output_modes {
	RESAMP_PAL,
	RESAMP_PAL_MASKED,
	RESAMP_RGB
};

#define MAX_MIP_MAPS 4

class Texture
{
public:	
	uint32_t id; // OpenGL texture ID
	int arrayId; // array texture ID, or -1 if not added to an array
	int layer; // layer in the texture array. 0 if not in an array or at layer 0
	uint32_t height, width, depth;
	uint8_t * data; // RGB(A) data
	MipTexture mipmaps[MAX_MIP_MAPS]; // mipmap data
	int numMipMaps = 0;
	int nearFilter;
	int farFilter;
	uint32_t format = 0; // format of the data
	uint32_t iformat = 0; // format of the data when uploaded to GL
	bool uploaded = false;
	bool isLightmap = false; // always filtered
	bool is3d;
	int uploadedDataSize;

	Texture(int width, int height);
	Texture(int width, int height, int depth);
	Texture(int width, int height, void * data);
	~Texture();

	static vector<COLOR3> resample(COLOR3* srcData, int srcW, int srcH, COLOR3* dstData,
		int dstW, int dstH, int mode, int outputMode=RESAMP_RGB, COLOR3 maskColor=COLOR3(0,0,255));

	void generateMipMaps(int mipLevels, COLOR3 maskColor);

	void addMipMap(int mipLevel, uint8_t* data, COLOR3* pal);

	// upload the texture with the specified settings
	void upload(int format, bool lighmap=false, bool deleteData=true);

	static int getPixelBytes(int format);

	// use this texture for rendering
	void bind();

	int calcMemoryUsage();
};