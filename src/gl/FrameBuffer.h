#pragma once
#include <stdint.h>

class VertexBuffer;

class FrameBuffer {
public:
	int width, height;

	FrameBuffer(int windowWidth, int windowHeight, float scale);
	~FrameBuffer();

	void bind();

	// draw contents of the framebuffer as a full screen quad
	void draw();

	void unbind(); // bind the default context frame buffer

private:
	uint32_t colorBufferId;
	uint32_t depthBufferId;
	uint32_t frameBufferId;
	int windowWidth, windowHeight;

	VertexBuffer* quad;
};