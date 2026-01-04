#include "SprRenderer.h"
#include "Wad.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "primitives.h"
#include "ShaderProgram.h"
#include "Editor.h"
#include "globals.h"

SprRenderer::SprRenderer(string sprPath) {
	this->fpath = sprPath;
	valid = false;

	loadState = MODEL_LOAD_WAITING;
}

SprRenderer::~SprRenderer() {
	if (!valid) {
		return;
	}

	for (int i = 0; i < header->frames; i++) {
		delete glTextures[i];
	}
	delete[] glTextures;
	delete frameBuffer;
	delete outlineBuffer;
	data.freeBuf();
}

bool SprRenderer::validate() {
	data.seek(0);
	data.seek(0, SEEK_END);
	int len = data.tell();

	if (len < sizeof(SpriteHeader)) {
		logf("Sprite has invalid size: %s\n", fpath.c_str());
		return false;
	}

	data.seek(0);
	header = (SpriteHeader*)data.getBuffer();

	if (strncmp(header->ident, "IDSP", 4)) {
		logf("Sprite has invalid magic bytes: %s\n", fpath.c_str());
		return false;
	}

	if (header->version != 2) {
		logf("Sprite has invalid version %u: %s\n", header->version, fpath.c_str());
		return false;
	}

	if (header->paletteSz > 256) {
		logf("Sprite has invalid palette size %u: %s\n", (int)header->paletteSz, fpath.c_str());
		return false;
	}

	int frameOffset = sizeof(SpriteHeader) + header->paletteSz * sizeof(COLOR3);	

	for (int i = 0; i < header->frames; i++) {
		data.seek(frameOffset);
		FrameHeader* frame = (FrameHeader*)data.getOffsetBuffer();
		uint32_t frameSz = frame->width * frame->height;

		if (data.eom()) {
			logf("Sprite frame %d / %d is missing: %s\n", i+1, header->frames, fpath.c_str());
			return false;
		}

		if (frameSz*sizeof(COLOR3) > 1024 * 1024 * 10) {
			logf("Sprite frame %d is over 10 MB in size: %s\n", i + 1, header->frames, fpath.c_str());
			return false;
		}

		if (frameSz == 0) {
			logf("Sprite frame %d is empty: %s\n", i + 1, header->frames, fpath.c_str());
			return false;
		}

		frameOffset += sizeof(FrameHeader) + frameSz;

		data.seek(frameOffset-1);
		if (data.eom()) {
			logf("Sprite frame %d / %d is missing data: %s\n", i + 1, header->frames, fpath.c_str());
			return false;
		}
	}

	return true;
}

void SprRenderer::upload() {
	if (loadState != MODEL_LOAD_UPLOAD) {
		logf("SPR upload called before initial load\n");
		return;
	}

	for (int i = 0; i < header->frames; i++) {
		glTextures[i]->upload(glTextures[i]->format);
	}

	frameBuffer->upload();
	outlineBuffer->upload();

	loadState = MODEL_LOAD_DONE;
}

void SprRenderer::loadData() {
	int len;
	char* buffer = loadFile(fpath, len);
	if (!buffer) {
		loadState = MODEL_LOAD_DONE;
		g_loading_models.dec();
		return;
	}

	data = mstream(buffer, len);

	if (!validate()) {
		delete[] buffer;
		g_loading_models.dec();
		return;
	}

	SpriteHeader* header = (SpriteHeader*)data.getBuffer();

	COLOR3* palette = new COLOR3[256];
	memset(palette, 0, sizeof(COLOR3) * 256);

	data.seek(sizeof(SpriteHeader));
	memcpy(palette, data.getOffsetBuffer(), sizeof(COLOR3) * header->paletteSz);

	int frameOffset = sizeof(SpriteHeader) + header->paletteSz * sizeof(COLOR3);

	glTextures = new Texture*[header->frames];

	int framesVertCount = header->frames * 4;
	int linesVertCount = header->frames * 5;
	tVert* frameVerts = new tVert[framesVertCount];
	vec3* outlineVerts = new vec3[linesVertCount];
	maxCoord = 0;
	maxWidth = 0;
	maxHeight = 0;

	for (int i = 0; i < header->frames; i++) {
		data.seek(frameOffset);
		FrameHeader* frame = (FrameHeader*)data.getOffsetBuffer();
		uint32_t frameSz = frame->width * frame->height;

		COLOR4* imageData = new COLOR4[frameSz];
		data.seek(frameOffset + sizeof(FrameHeader));

		COLOR3 lastColor = palette[header->paletteSz - 1];

		uint8_t* frameData = (uint8_t*)data.getOffsetBuffer();
		for (int k = 0; k < frameSz; k++) {			
			imageData[k] = COLOR4(palette[frameData[k]], 255);

			if (header->format == SPR_ALPHATEST && frameData[k] == header->paletteSz - 1) {
				imageData[k] = COLOR4(0,0,0,0);
			}
			else if (header->format == SPR_INDEXALPHA) {
				uint8_t alpha = frameData[k];
				imageData[k] = COLOR4(lastColor, alpha);
			}
		}

		glTextures[i] = new Texture(frame->width, frame->height, imageData);
		glTextures[i]->format = GL_RGBA;
		glTextures[i]->generateMipMaps(3, lastColor);

		float w = frame->width;
		float h = frame->height;
		float x = frame->x;
		float y = frame->y;

		maxWidth = max(maxWidth, (int)frame->width);
		maxHeight = max(maxHeight, (int)frame->height);

		frameVerts[i * 4 + 3] = tVert(vec3(0, y, x+w), 0, 0);
		frameVerts[i * 4 + 2] = tVert(vec3(0, y-h, x+w), 0, 1);
		frameVerts[i * 4 + 1] = tVert(vec3(0, y-h, x) , 1, 1);
		frameVerts[i * 4 + 0] = tVert(vec3(0, y, x), 1, 0);

		for (int k = 0; k < 4; k++) {
			outlineVerts[i*5 + k] = frameVerts[i * 4 + k].pos();
			maxCoord = max(maxCoord, frameVerts[i * 4 + k].pos().length());
		}
		outlineVerts[i * 5 + 4] = frameVerts[i * 4].pos();

		frameOffset += sizeof(FrameHeader) + (frame->width * frame->height);
	}

	frameBuffer = new VertexBuffer(g_shaders.spr, frameVerts, framesVertCount);
	outlineBuffer = new VertexBuffer(g_shaders.vec3, outlineVerts, linesVertCount);

	delete[] palette;

	valid = true;
	loadState = MODEL_LOAD_UPLOAD;
	g_loading_models.dec();
}

void SprRenderer::getBoundingBox(vec3& mins, vec3& maxs, float scale) {
	float s = maxCoord * scale;
	mins = vec3(-s, -s, -s);
	maxs = vec3(s, s, s);
}

float SprRenderer::getScaleToFitInsideCube(int size) {
	return size / (float)max(maxWidth, maxHeight);
}

float SprRenderer::getScale(vec3 ori, EntRenderOpts& renderOpts) {
	float dist = (ori - g_app->cameraOrigin).length();

	float scale = renderOpts.scale;
	bool useRenderModes = g_app->previewMode || (g_settings.render_flags & RENDER_RENDER_MODES);
	if (renderOpts.rendermode == RENDER_MODE_GLOW && useRenderModes) {
		scale *= dist * 0.02f;
		scale *= 1.0f / scale;
	}

	return scale;
}

void SprRenderer::getPickVerts(Entity* ent, float scale, EntRenderOpts& renderOpts, vec3 pickVerts[4]) {
	mat4x4 transform;
	transform.loadIdentity();
	transform.scale(scale, scale, scale);

	vec3 camAngles = vec3(0, -90 - g_app->cameraAngles.z, g_app->cameraAngles.x) * (PI / 180.0f);
	vec3 entAngles = ent->getAngles() * (PI / 180.0f);

	int sprType = renderOpts.vp_type > 0 ? renderOpts.vp_type - 1 : header->mode;
	switch (sprType) {
	default:
	case VP_PARALLEL:
	case VP_PARALLEL_ORIENTED:
		transform.rotateX(-entAngles.y);
		transform.rotateY(camAngles.z);
		transform.rotateZ(-camAngles.y);
		break;
	case VP_PARALLEL_UPRIGHT:
	case FACING_UPRIGHT: // it's broken in-game, but it sort of looks like parallel_upright
		transform.rotateZ(-camAngles.y);
		break;
	case ORIENTED:
		transform.rotateX(entAngles.z);
		transform.rotateY(entAngles.x);
		transform.rotateZ(-entAngles.y);
		break;
	}

	int frame = (int)ent->drawFrame % header->frames;
	tVert* verts = (tVert*)frameBuffer->data;

	vec3 ori = ent->getOrigin();
	for (int i = 0; i < 4; i++) {
		vec3 vert = verts[frame * 4 + i].pos();
		pickVerts[i] = ((transform * vec4(vert.x, vert.z, vert.y, 1)).xyz() + ori);
	}
}

bool SprRenderer::pick(vec3 start, vec3 rayDir, Entity* ent, float& bestDist) {
	if (!valid || loadState != MODEL_LOAD_DONE) {
		return false;
	}

	if (!ent->didStudioDraw) {
		return false;
	}

	EntRenderOpts renderOpts = ent->getRenderOpts();

	float scale = getScale(ent->getOrigin(), renderOpts);

	vec3 mins, maxs;
	getBoundingBox(mins, maxs, scale);
	mins += ent->drawOrigin;
	maxs += ent->drawOrigin;

	float oldBestDist = bestDist;
	if (!pickAABB(start, rayDir, mins, maxs, bestDist) && !pointInBox(start, mins, maxs)) {
		return false;
	}
	bestDist = oldBestDist;

	vec3 pickVerts[4];
	getPickVerts(ent, scale, renderOpts, pickVerts);

	//Polygon3D poly({ pickVerts[0], pickVerts[1], pickVerts[2], pickVerts[3] });
	//g_app->debugPoly = poly;

	float t = rayTriangleIntersect(start, rayDir, pickVerts[0], pickVerts[1], pickVerts[2]);
	if (t > 0 && t < bestDist) {
		bestDist = t;
		return true;
	}

	t = rayTriangleIntersect(start, rayDir, pickVerts[0], pickVerts[2], pickVerts[3]);
	if (t > 0 && t < bestDist) {
		bestDist = t;
		return true;
	}

	return false;
}

bool SprRenderer::pick(Frustum& frustum, Entity* ent) {
	if (!valid || loadState != MODEL_LOAD_DONE) {
		return false;
	}

	if (!ent->didStudioDraw) {
		return false;
	}

	EntRenderOpts renderOpts = ent->getRenderOpts();

	float scale = getScale(ent->getOrigin(), renderOpts);

	vec3 mins, maxs;
	getBoundingBox(mins, maxs, scale);
	mins += ent->drawOrigin;
	maxs += ent->drawOrigin;

	if (!isBoxInView(mins, maxs, frustum, 0)) {
		return false;
	}

	vec3 pickVerts[4];
	getPickVerts(ent, scale, renderOpts, pickVerts);

	if (isPolyInView(Polygon3D({ pickVerts[0], pickVerts[1], pickVerts[2] }, true), frustum)) {
		return true;
	}
	if (isPolyInView(Polygon3D({ pickVerts[0], pickVerts[2], pickVerts[3] }, true), frustum)) {
		return true;
	}

	return false;
}

void SprRenderer::draw(vec3 ori, vec3 angles, Entity* ent, EntRenderOpts opts, COLOR3 tint, COLOR3 outlineColor, bool noOutline) {
	if (!valid || loadState != MODEL_LOAD_DONE) {
		return;
	}

	COLOR4 color = COLOR4(tint.r, tint.g, tint.b, 255);
	float framerate = opts.framerate != 0 ? opts.framerate : 10.0f;
	float scale = opts.scale;

	if ((g_settings.render_flags & RENDER_RENDER_MODES) || g_app->previewMode) {
		switch (opts.rendermode) {
		default:
		case RENDER_MODE_NORMAL:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case RENDER_MODE_COLOR:
			color = COLOR4(opts.rendercolor, opts.renderamt);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case RENDER_MODE_GLOW:
		{
			const float GLARE_FALLOFF = 19000.0f;
			float dist = (ori - g_app->cameraOrigin).length();
			float brightness = clamp(GLARE_FALLOFF / (dist * dist), 0.05f, 1.0f);
			scale *= dist * 0.02f;
			color = COLOR4(tint.r, tint.g, tint.b, opts.renderamt * brightness);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDisable(GL_DEPTH_TEST);
			break;
		}
		case RENDER_MODE_ADDITIVE:
			color = COLOR4(tint.r, tint.g, tint.b, opts.renderamt);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		case RENDER_MODE_TEXTURE:
		case RENDER_MODE_SOLID:
			color = COLOR4(tint.r, tint.g, tint.b, opts.renderamt);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		}
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glEnable(GL_BLEND);

	float now = glfwGetTime();
	if (ent->lastDrawCall == 0) {
		ent->lastDrawCall = now;
	}
	float deltaTime = now - ent->lastDrawCall;
	ent->lastDrawCall = now;

	ent->drawFrame += opts.framerate * deltaTime;
	int frame = (int)ent->drawFrame % header->frames;
	
	g_shaders.spr->bind();

	glActiveTexture(GL_TEXTURE0);
	g_shaders.spr->setUniform("color", color.toVec());

	glTextures[frame]->bind();

	g_shaders.spr->pushMatrix(MAT_MODEL);
	g_shaders.spr->modelMat->loadIdentity();
	g_shaders.spr->modelMat->translate(ori.x, ori.z, -ori.y);
	g_shaders.spr->modelMat->scale(scale, scale, scale);

	// Worldcraft only sets y rotation, copy to Z (logic from Half-Life's env_sprite)
	if (opts.vp_type == 0 && angles.y != 0 && angles.z == 0)
	{
		angles.z = angles.y;
		angles.y = 0;
	}

	vec3 camAngles = vec3(0, -90 - g_app->cameraAngles.z, g_app->cameraAngles.x) * (PI / 180.0f);
	vec3 entAngles = angles * (PI / 180.0f);

	int sprType = opts.vp_type > 0 ? opts.vp_type-1 : header->mode;

	switch (sprType) {
	default:
	case VP_PARALLEL:
	case VP_PARALLEL_ORIENTED:
		g_shaders.spr->modelMat->rotateY(camAngles.y);
		g_shaders.spr->modelMat->rotateZ(camAngles.z);
		g_shaders.spr->modelMat->rotateX(entAngles.y);
		break;
	case VP_PARALLEL_UPRIGHT:
	case FACING_UPRIGHT: // it's broken in-game, but it sort of looks like parallel_upright
		g_shaders.spr->modelMat->rotateY(camAngles.y);
		break;
	case ORIENTED:
		g_shaders.spr->modelMat->rotateY(entAngles.y + PI);
		g_shaders.spr->modelMat->rotateZ(entAngles.x);
		g_shaders.spr->modelMat->rotateX(-entAngles.z);
		break;
	}

	if (header->format == SPR_ALPHATEST) {
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5f);
	}

	g_shaders.spr->updateMatrixes();
	frameBuffer->drawRange(g_shaders.spr, GL_TRIANGLE_FAN, frame * 4, frame * 4 + 4);

	if (header->format == SPR_ALPHATEST) {
		glDisable(GL_ALPHA_TEST);
	}

	bool useRenderModes = g_app->previewMode || (g_settings.render_flags & RENDER_RENDER_MODES);
	if (opts.rendermode == RENDER_MODE_GLOW && useRenderModes) {
		glEnable(GL_DEPTH_TEST);
		scale = 1.0f / scale; // the growing sprite borders are distracting
		g_shaders.spr->modelMat->scale(scale, scale, scale);
	}

	if ((g_settings.render_flags & RENDER_WIREFRAME) && !noOutline) {
		g_shaders.vec3->bind(SH_VEC3_DEPTH_HACK);
		*g_shaders.vec3->modelMat = *g_shaders.spr->modelMat;
		g_shaders.vec3->updateMatrixes();
		g_shaders.vec3->setUniform("color", vec4(outlineColor.toVec(), 1));
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		outlineBuffer->drawRange(g_shaders.vec3, GL_LINE_STRIP, frame * 5, frame * 5 + 5);
	}

	g_shaders.spr->popMatrix(MAT_MODEL);
}