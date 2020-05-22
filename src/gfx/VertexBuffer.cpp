#include "VertexBuffer.h"
#include "util.h"
#include "Renderer.h"

VertexAttr commonAttr[VBUF_FLAGBITS] =
{
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE), // TEX_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE), // TEX_2S
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE), // TEX_2F
	VertexAttr(3, GL_UNSIGNED_BYTE, -1, GL_TRUE),  // COLOR_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE),  // COLOR_3F
	VertexAttr(4, GL_UNSIGNED_BYTE, -1, GL_TRUE),  // COLOR_4B
	VertexAttr(4, GL_FLOAT,         -1, GL_TRUE),  // COLOR_4F
	VertexAttr(3, GL_BYTE,          -1, GL_TRUE),  // NORM_3B
	VertexAttr(3, GL_FLOAT,         -1, GL_TRUE),  // NORM_3F
	VertexAttr(2, GL_BYTE,          -1, GL_FALSE), // POS_2B
	VertexAttr(2, GL_SHORT,         -1, GL_FALSE), // POS_2S
	VertexAttr(2, GL_INT,           -1, GL_FALSE), // POS_2I
	VertexAttr(2, GL_FLOAT,         -1, GL_FALSE), // POS_2F
	VertexAttr(3, GL_SHORT,         -1, GL_FALSE), // POS_3S
	VertexAttr(3, GL_FLOAT,         -1, GL_FALSE), // POS_3F
};

VertexAttr::VertexAttr( int numValues, int valueType, int handle, int normalized )
	: numValues(numValues), valueType(valueType), handle(handle), normalized(normalized)
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
		printf("Unknown attribute value type: %d", valueType);
		handle = -1;
		size = 0;
	}
}



VertexBuffer::VertexBuffer( ShaderProgram * shaderProgram, int attFlags, const void * dat, int numVerts )
{
	this->shaderProgram = shaderProgram;
	addAttributes(attFlags);
	setData(dat, numVerts);
}

VertexBuffer::VertexBuffer( ShaderProgram * shaderProgram, int attFlags )
{
	numVerts = 0;
	data = NULL;
	this->shaderProgram = shaderProgram;
	addAttributes(attFlags);
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
				printf("Unused vertex buffer flag bit %d", i);

			attribs.push_back(commonAttr[i]);
			elementSize += commonAttr[i].size;
		}
	}
}

void VertexBuffer::setData( const void * data, int numVerts )
{
	this->data = (byte*)data;
	this->numVerts = numVerts;
}

void VertexBuffer::drawRange( int primitive, int start, int end )
{
	shaderProgram->bind();

	int offset = 0;
	int enabledArrays = 0;
	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		void * ptr = (char*)data + offset;
		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle, a.numValues, a.valueType, a.normalized != 0, elementSize, ptr);
		offset += a.size;
	}

	if (start < 0 || start > numVerts)
		printf("Invalid start index: %d\n", start);
	else if (end > numVerts || end < 0)
		printf("Invalid start index: %d\n", end);
	else if (end - start <= 0)
		printf("Invalid draw range: %d -> %d\n", start, end);
	else
		glDrawArrays(primitive, start, end-start);

	for (int i = 0; i < attribs.size(); i++)
	{
		VertexAttr& a = attribs[i];
		glDisableVertexAttribArray(a.handle);
	}
}

void VertexBuffer::draw( int primitive )
{
	drawRange(primitive, 0, numVerts);
}
