#pragma once
#include <vector>
#include "ShaderProgram.h"
#include "util.h"

// Combinable flags for setting common vertex attributes
#define TEX_2B   (1 << 0)   // 2D unsigned char texture coordinates
#define TEX_2S   (1 << 1)   // 2D short texture coordinates
#define TEX_2F   (1 << 2)   // 2D float texture coordinates
#define COLOR_3B (1 << 3)   // RGB unsigned char color values
#define COLOR_3F (1 << 4)   // RGB float color values
#define COLOR_4B (1 << 5)   // RGBA unsigned char color values
#define COLOR_4F (1 << 6)   // RGBA float color values
#define NORM_3B  (1 << 7)   // 3D unsigned char normal coordinates
#define NORM_3F  (1 << 8)   // 3D float normal coordinates
#define POS_2B   (1 << 9)   // 2D unsigned char position coordinates
#define POS_2S   (1 << 10)  // 2D short position coordinates
#define POS_2I   (1 << 11)  // 2D integer position coordinates
#define POS_2F   (1 << 12)  // 2D float position coordinates
#define POS_3S   (1 << 13)  // 3D short position coordinates
#define POS_3F   (1 << 14)  // 3D float position coordinates

// starting bits for the different types of vertex attributes
#define VBUF_TEX_START     0 // first bit for texture flags
#define VBUF_COLOR_START   3 // first bit for color flags
#define VBUF_NORM_START    7 // first bit for normals flags
#define VBUF_POS_START     9 // first bit for position flags
#define VBUF_FLAGBITS     15 // number of settable bits
#define VBUF_TEX_MASK    0x7 // mask for all texture flags
#define VBUF_COLOR_MASK 0x78 // mask for all color flags
#define VBUF_NORM_MASK 0x180 // mask for all normal flags

struct VertexAttr
{
	int numValues;
	int valueType;  // Ex: GL_FLOAT
	int handle;     // location in shader program (-1 indicates invalid attribute)
	int size;       // size of the attribute in bytes
	int normalized; // GL_TRUE/GL_FALSE Ex: unsigned char color values are normalized (0-255 = 0.0-1.0)
	const char* varName;

	VertexAttr() : handle(-1) {}

	VertexAttr(int numValues, int valueType, int handle, int normalized, const char* varName);
};

class VertexBuffer
{
public:
	unsigned char* data = NULL;
	std::vector<VertexAttr> attribs;
	int elementSize;
	int numVerts;
	bool ownData = false; // set to true if buffer should delete data on destruction

	// Specify which common attributes to use. They will be located in the
	// shader program. If passing data, note that data is not copied, but referenced
	VertexBuffer(ShaderProgram* shaderProgram, int attFlags);
	VertexBuffer(ShaderProgram* shaderProgram, int attFlags, const void* dat, int numVerts);
	~VertexBuffer();

	// Note: Data is not copied into the class - don't delete your data.
	//       Data will be deleted when the buffer is destroyed.
	void setData(const void* data, int numVerts);

	void upload();
	void deleteBuffer();
	void setShader(ShaderProgram* program, bool hideErrors = false);

	void drawRange(int primitive, int start, int end);
	void draw(int primitive);

	void addAttribute(int numValues, int valueType, int normalized, const char* varName);
	void addAttribute(int type, const char* varName);
	void bindAttributes(bool hideErrors = false); // find handles for all vertex attributes (call from main thread only)

private:
	ShaderProgram* shaderProgram = NULL; // for getting handles to vertex attributes
	unsigned int vboId = -1;
	bool attributesBound = false;

	// add attributes according to the attribute flags
	void addAttributes(int attFlags);
};

