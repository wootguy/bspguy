#include <GL/glew.h>
#include "VertexBuffer.h"
#include "util.h"

VertexAttr commonAttr[VBUF_FLAGBITS] =
{
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE, ""), // TEX_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE, ""), // TEX_2S
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE, ""), // TEX_2F
	VertexAttr(3, GL_UNSIGNED_BYTE, -1, GL_TRUE, ""),  // COLOR_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE, ""),  // COLOR_3F
	VertexAttr(4, GL_UNSIGNED_BYTE, -1, GL_TRUE, ""),  // COLOR_4B
	VertexAttr(4, GL_FLOAT,         -1, GL_TRUE, ""),  // COLOR_4F
	VertexAttr(3, GL_BYTE,          -1, GL_TRUE, ""),  // NORM_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE, ""),  // NORM_3F
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE, ""), // POS_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE, ""), // POS_2S
	VertexAttr(2, GL_INT,           -1, GL_FALSE, ""), // POS_2I
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE, ""), // POS_2F
	VertexAttr(3, GL_SHORT,         -1, GL_FALSE, ""), // POS_3S
	VertexAttr(3, GL_FLOAT,         -1, GL_FALSE, ""), // POS_3F
};

VertexAttr::VertexAttr( int numValues, int valueType, int handle, int normalized, const char* varName)
	: numValues(numValues), valueType(valueType), handle(handle), normalized(normalized), varName(varName)
{
	switch(valueType)
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



VertexBuffer::VertexBuffer( ShaderProgram * shaderProgram, int attFlags, const void * dat, int numVerts )
{
	this->shaderProgram = shaderProgram;
	addAttributes(attFlags);
	setData(dat, numVerts);
	vboId = -1;
}

VertexBuffer::VertexBuffer( ShaderProgram * shaderProgram, int attFlags )
{
	numVerts = 0;
	data = NULL;
	vboId = -1;
	this->shaderProgram = shaderProgram;
	addAttributes(attFlags);
}

VertexBuffer::~VertexBuffer() {
	deleteBuffer();
	if (ownData) {
		delete[] data;
	}
}

void VertexBuffer::addAttributes( int attFlags )
{
	elementSize = 0;
	for (int i = 0; i < VBUF_FLAGBITS; i++)
	{
		if (attFlags & (1 << i))
		{
			if (i >= VBUF_POS_START)
				commonAttr[i].handle = shaderProgram->vposID;
			else if (i >= VBUF_COLOR_START)
				commonAttr[i].handle = shaderProgram->vcolorID;
			else if (i >= VBUF_TEX_START)
				commonAttr[i].handle = shaderProgram->vtexID;
			else
				logf("Unused vertex buffer flag bit %d", i);

			attribs.push_back(commonAttr[i]);
			elementSize += commonAttr[i].size;
		}
	}
}

void VertexBuffer::addAttribute(int numValues, int valueType, int normalized, const char* varName) {
	VertexAttr attribute(numValues, valueType, -1, normalized, varName);

	attribs.push_back(attribute);
	elementSize += attribute.size;
}

void VertexBuffer::addAttribute(int type, const char* varName) {

	int idx = 0;
	while (type >>= 1) // unroll for more speed...
	{
		idx++;
	}

	if (idx >= VBUF_FLAGBITS) {
		logf("Invalid attribute type\n");
		return;
	}

	VertexAttr attribute = commonAttr[idx];
	attribute.handle = -1;
	attribute.varName = varName;

	attribs.push_back(attribute);
	elementSize += attribute.size;
}

void VertexBuffer::setShader(ShaderProgram* program, bool hideErrors) {
	shaderProgram = program;
	attributesBound = false;
	for (int i = 0; i < attribs.size(); i++)
	{
		if (strlen(attribs[i].varName) > 0) {
			attribs[i].handle = -1;
		}
	}

	bindAttributes(hideErrors);
	if (vboId != -1) {
		deleteBuffer();
		upload();
	}
}

void VertexBuffer::bindAttributes(bool hideErrors) {
	if (attributesBound)
		return;

	for (int i = 0; i < attribs.size(); i++)
	{
		if (attribs[i].handle != -1)
			continue;

		attribs[i].handle = glGetAttribLocation(shaderProgram->ID, attribs[i].varName);

		if (!hideErrors && attribs[i].handle == -1)
			logf("Could not find vertex attribute: %s\n", attribs[i].varName);
	}

	attributesBound = true;
}

void VertexBuffer::setData( const void * data, int numVerts )
{
	this->data = (byte*)data;
	this->numVerts = numVerts;
}

void VertexBuffer::upload() {
	shaderProgram->bind();
	bindAttributes();

	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, elementSize * numVerts, data, GL_STATIC_DRAW);

	int offset = 0;
	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		void* ptr = ((char*)0) + offset;
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, elementSize, ptr);
		offset += a.size;
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::deleteBuffer() {
	if (vboId != -1)
		glDeleteBuffers(1, &vboId);
	vboId = -1;
}

void VertexBuffer::drawRange( int primitive, int start, int end )
{
	shaderProgram->bind();
	bindAttributes();

	char* offsetPtr = (char*)data;
	if (vboId != -1) {
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		offsetPtr = NULL;
	}
	{
		int offset = 0;
		for (int i = 0; i < attribs.size(); i++)
		{
			VertexAttr& a = attribs[i];
			void* ptr = offsetPtr + offset;
			offset += a.size;
			if (a.handle == -1)
				continue;
			glEnableVertexAttribArray(a.handle);
			glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, elementSize, ptr);
		}
	}

	if (start < 0 || start > numVerts)
		logf("Invalid start index: %d\n", start);
	else if (end > numVerts || end < 0)
		logf("Invalid start index: %d\n", end);
	else if (end - start <= 0)
		logf("Invalid draw range: %d -> %d\n", start, end);
	else
		glDrawArrays(primitive, start, end-start);

	if (vboId != -1) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		if (a.handle == -1)
			continue;
		glDisableVertexAttribArray(a.handle);
	}
}

void VertexBuffer::draw( int primitive )
{
	drawRange(primitive, 0, numVerts);
}
