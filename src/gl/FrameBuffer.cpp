#include "FrameBuffer.h"
#include <GL/glew.h>
#include "primitives.h"
#include "globals.h"
#include "util.h"

FrameBuffer::FrameBuffer(int windowWidth, int windowHeight, float scale) {
	this->windowWidth = windowWidth;
	this->windowHeight = windowHeight;
	this->width = windowWidth * scale;
	this->height = windowHeight * scale;

	glGenTextures(1, &colorBufferId);
	glBindTexture(GL_TEXTURE_2D, colorBufferId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &depthBufferId);
	glBindTexture(GL_TEXTURE_2D, depthBufferId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &frameBufferId);
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBufferId, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthBufferId, 0);

	tQuad* quadDat = new tQuad(0, 0, windowWidth, windowHeight);
	quad = new VertexBuffer(g_shaders.texture, quadDat, 6, true);
	quad->upload();

	g_renderStats.texMem += width * height * (4+3);
}

FrameBuffer::~FrameBuffer() {
	glDeleteTextures(1, &colorBufferId);
	glDeleteTextures(1, &depthBufferId);
	glDeleteFramebuffers(1, &frameBufferId);
	delete quad;
	g_renderStats.texMem -= width * height * (4 + 3);
}

void FrameBuffer::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		errorf("FBO incomplete: 0x%x\n", status);
	}

	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void FrameBuffer::unbind() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, windowWidth, windowHeight);
}

void FrameBuffer::draw() {
	g_shaders.texture->bind();
	g_shaders.texture->pushMatrix(MAT_PROJECTION);
	g_shaders.texture->pushMatrix(MAT_VIEW);
	g_shaders.texture->pushMatrix(MAT_MODEL);
	g_shaders.texture->projMat->ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
	g_shaders.texture->modelMat->loadIdentity();
	g_shaders.texture->viewMat->loadIdentity();
	g_shaders.texture->updateMatrixes();
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glViewport(0, 0, windowWidth, windowHeight);
	glActiveTexture(GL_TEXTURE0);
	
	glBindTexture(GL_TEXTURE_2D, colorBufferId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	
	quad->draw(g_shaders.texture, GL_TRIANGLES);

	g_shaders.texture->popMatrix(MAT_PROJECTION);
	g_shaders.texture->popMatrix(MAT_VIEW);
	g_shaders.texture->popMatrix(MAT_MODEL);
}