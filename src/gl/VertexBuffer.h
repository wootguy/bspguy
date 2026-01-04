#pragma once
#include <vector>

class ShaderProgram;

#define MAX_VERTEX_ATTRIBUTES 8 // keep low for ancient intel igpus

struct VertexAttr
{
	const char* varName;
	int valueType;		// Ex: GL_FLOAT
	uint8_t size;		// size of the attribute in bytes
	uint8_t numValues;	// number of components in the attribute (must be 1, 2, 3, 4)
	int8_t handle;		// location in shader program (-1 indicates invalid attribute)
	bool normalized;	// GL_TRUE/GL_FALSE Ex: byte color values are normalized (0-255 = 0.0-1.0)

	VertexAttr() : handle(-1) {}

	VertexAttr(int numValues, int valueType, int handle, int normalized, const char* varName);
};

class VertexBuffer
{
public:
	uint8_t* data = NULL;
	int numVerts = 0;

	// Specify which common attributes to use. They will be located in the
	// shader program. If passing data, note that data is not copied, but referenced
	VertexBuffer(ShaderProgram* shader, bool ownData=false);
	VertexBuffer(ShaderProgram* shader, const void* dat, int numVerts, bool ownData=false);
	~VertexBuffer();

	// Note: Data is not copied into the class - don't delete your data.
	//       Data will be deleted when the buffer is destroyed.
	void setData(const void * data, int numVerts);

	bool isUploaded();
	void upload(ShaderProgram* shader=NULL); // pass a shader to use VAO
	void deleteBuffer();

	void drawRange(ShaderProgram* shader, int primitive, int start, int end);
	void draw(ShaderProgram* shader, int primitive);

	int calcMemoryUsage();

private:
	uint32_t vboId = -1;
	uint32_t vaoId = -1; // vertex array object (binds attributes to the buffer)

	bool ownData = false; // true if buffer should delete data on destruction
	int vertexSize;

	// add attributes according to the attribute flags
	void addAttribute(const VertexAttr& attr);

	void bindAttributes(ShaderProgram* shader);
};

