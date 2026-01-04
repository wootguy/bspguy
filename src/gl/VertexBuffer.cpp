#include <GL/glew.h>
#include "VertexBuffer.h"
#include "util.h"
#include <string.h>
#include "ShaderProgram.h"

// Vertex Array Objects should in theory be faster due to fewer opengl call per draw
// but I see a slowdown and they crash my win7 opengl 3.0 netbook, so don't use them.
bool g_use_vao = false;

VertexAttr::VertexAttr(int numValues, int valueType, int handle, int normalized, const char* varName)
	: numValues(numValues), valueType(valueType), handle(handle), normalized(normalized), varName(varName)
{
	switch (valueType)
	{
	case(GL_BYTE):
	case(GL_UNSIGNED_BYTE):
		size = numValues;
		break;
	case(GL_SHORT):
	case(GL_UNSIGNED_SHORT):
		size = numValues * 2;
		break;
	case(GL_FLOAT):
	case(GL_INT):
	case(GL_UNSIGNED_INT):
		size = numValues * 4;
		break;
	default:
		logf("Unknown attribute value type: %d", valueType);
		handle = -1;
		size = 0;
	}
}


VertexBuffer::VertexBuffer(ShaderProgram* shader, bool ownData) {
	this->ownData = ownData;
	this->vertexSize = shader->vertexSize;
}

VertexBuffer::VertexBuffer(ShaderProgram* shader, const void* dat, int numVerts, bool ownData)
	: VertexBuffer(shader, ownData) {
	setData(dat, numVerts);
}

VertexBuffer::~VertexBuffer() {
	deleteBuffer();
	if (ownData) {
		delete[] data;
	}
}

void VertexBuffer::setData(const void* data, int numVerts)
{
	this->data = (byte*)data;
	this->numVerts = numVerts;
}

bool VertexBuffer::isUploaded() {
	return vboId != -1;
}

void VertexBuffer::upload(ShaderProgram* shader) {

	if (vboId != -1) {
		// already uploaded, just replace the data
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize * numVerts, data);
		return;
	}

	if (g_use_vao) {
		glGenVertexArrays(1, &vaoId);
		glBindVertexArray(vaoId);
	}

	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, vertexSize * numVerts, data, GL_STATIC_DRAW);

	if (g_use_vao && shader) {
		bindAttributes(shader);
	}

	if (g_use_vao)
		glBindVertexArray(0);

	g_renderStats.vertMem += numVerts * vertexSize;
}

void VertexBuffer::deleteBuffer() {
	if (vboId != -1) {
		glDeleteBuffers(1, &vboId);
		g_renderStats.vertMem -= numVerts * vertexSize;
	}
	if (vaoId != -1)
		glDeleteBuffers(1, &vaoId);
	vboId = -1;
	vaoId = -1;
}

void VertexBuffer::bindAttributes(ShaderProgram* shader) {
	int offset = 0;
	int idx = shader->getActiveProgramIndex();
	for (int i = 0; i < shader->numAttributes; i++)
	{
		VertexAttr& a = shader->attributes[idx][i];
		void* ptr = (char*)NULL + offset;
		offset += a.size;
		if (a.handle == -1) {
			continue;
		}
		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, vertexSize, ptr);
	}
}

void VertexBuffer::drawRange(ShaderProgram* shader, int primitive, int start, int end)
{
	if (vboId == -1) {
		logf("Attempted to draw VBO before upload\n");
		upload();
		return;
	}
	
	g_renderStats.numObjects++;
	g_renderStats.numVerts += end - start;

	if (vaoId != -1)
		glBindVertexArray(vaoId);
	else {
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		bindAttributes(shader);
	}

	if (start < 0 || start > numVerts)
		logf("Invalid start index: %d\n", start);
	else if (end > numVerts || end < 0)
		logf("Invalid end index: %d\n", end);
	else if (end - start <= 0)
		logf("Invalid draw range: %d -> %d\n", start, end);
	else
		glDrawArrays(primitive, start, end - start);

	if (vaoId == -1) {
		// my windows 7 opengl 3.0 netbook needs this or else it crashes
		int idx = shader->getActiveProgramIndex();
		for (int i = 0; i < shader->numAttributes; i++)
		{
			VertexAttr& a = shader->attributes[idx][i];
			if (a.handle == -1) {
				continue;
			}
			glDisableVertexAttribArray(a.handle);
		}
	}
}

void VertexBuffer::draw(ShaderProgram* shader, int primitive)
{
	drawRange(shader, primitive, 0, numVerts);
}

int VertexBuffer::calcMemoryUsage() {
	int bytes = sizeof(VertexBuffer);
	bytes += vertexSize * numVerts;
	return bytes;
}