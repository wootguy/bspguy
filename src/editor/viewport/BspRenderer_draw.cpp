#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "ShaderProgram.h"
#include "BaseRenderer.h"
#include "Texture.h"
#include "Entity.h"
#include "Editor.h"
#include "PointEntRenderer.h"

void BspRenderer::renderSolids(bool highlightAlwaysOnTop, bool transparencyPass) {
	if (map->ents.empty())
		return;

	BSPMODEL& world = map->models[0];

	int shaderbits = 0;

	if ((g_settings.render_flags & RENDER_WIREFRAME) && !g_app->previewMode) {
		shaderbits |= SH_BSP_WIREFRAME;
	}

	if (g_settings.pal_textures && (g_settings.render_flags & RENDER_TEXTURES)) {
		shaderbits |= SH_BSP_TEX_PAL;
	}

	if (g_use_texture_arrays) {
		shaderbits |= SH_BSP_TEX_ARRAY;
	}
	else {
		shaderbits |= g_settings.texture_atlas ? SH_BSP_TEX_ATLAS : 0;
	}

	activeShader = g_shaders.bsp;
	activeShader->bind(shaderbits);
	activeShader->modelMat->loadIdentity();
	activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	activeShader->updateMatrixes();

	if (g_settings.pal_textures && glPalette) {
		glActiveTexture(GL_TEXTURE2);
		glPalette->bind();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	bool wireframeOnly = false;
	if (!(g_settings.render_flags & (RENDER_LIGHTMAPS | RENDER_TEXTURES))) {
		if (g_settings.render_flags & RENDER_WIREFRAME) {
			wireframeOnly = true;
		}
	}
	if (g_settings.backface_wireframe && shaderbits & SH_BSP_WIREFRAME)
		glDisable(GL_CULL_FACE); // expensive on fill-rate limited hardware, so only for this special case
	activeShader->setUniform("wireframeOnly", wireframeOnly ? 1.0f : 0.0f);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	if ((g_settings.render_flags & RENDER_LIGHTMAPS) && (g_settings.render_flags & RENDER_TEXTURES)) {
		activeShader->setUniform("gamma", 1.5f); // for brighter lighting
	}
	else {
		activeShader->setUniform("gamma", 1.0f); // for accurate lightmap colors
	}

	if (!map->ents[0]->hidden && map->modelCount > 0) {
		activeShader->setUniform("wireframeColorDark", vec4(0.5f, 0.5f, 0.5f, 1));
		activeShader->setUniform("wireframeColorBright", vec4(0, 0, 0, 1));
		drawModel(map->ents[0], 0, transparencyPass, false);
	}

	if (!(g_settings.render_flags & RENDER_ENTS))
		return;

	activeShader->modelMat->loadIdentity();
	activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	activeShader->updateMatrixes();
	activeShader->setUniform("wireframeColorDark", vec4(0.2f, 0.2f, 1, 1));
	activeShader->setUniform("wireframeColorBright", vec4(0, 0, 0.8f, 1));

	bool useMegaGroups = megaGroupUpdateProgress == -1;

	if (useMegaGroups) {
		for (MegaRenderGroup& mega : megaRenderGroups) {
			RenderGroup& rgroup = mega.group;

			if (rgroup.transparent != transparencyPass)
				continue;
			if (rgroup.transparent && !(g_settings.render_flags & RENDER_SPECIAL_ENTS))
				continue;

			drawModelRenderGroup(rgroup, false, true);
		}
	}

	int renderEnts = 0;
	activeShader->pushMatrix(MAT_MODEL);
	for (int i = 0, sz = orderEnts.size(); i < sz; i++) {
		const OrderedEnt& orderEnt = orderEnts[i];
		int modelIdx = orderEnt.modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount) {
			Entity* ent = orderEnt.ent;
			if (ent->hidden || (useMegaGroups && orderEnt.isInMegaRenderGroup))
				continue;
			if (!willDrawModel(ent, modelIdx, transparencyPass))
				continue;

			if (highlightAlwaysOnTop && ent->highlighted)
				glDisable(GL_DEPTH_TEST);

			renderEnts++;
			*activeShader->modelMat = orderEnt.transformWorld;
			activeShader->updateMatrixes();

			if (ent->highlighted) {
				activeShader->setUniform("wireframeColorDark", vec4(1, 1, 0, 1));
				activeShader->setUniform("wireframeColorBright", vec4(1, 1, 0, 1));
			}
			else {
				activeShader->setUniform("wireframeColorDark", vec4(0.2f, 0.2f, 1, 1));
				activeShader->setUniform("wireframeColorBright", vec4(0, 0, 0.8f, 1));
			}

			drawModel(ent, modelIdx, transparencyPass, ent->highlighted);
		}
	}
	activeShader->popMatrix(MAT_MODEL);

	//if (!transparencyPass)
	//	logf("Rendered %d solids + %d mega groups\n", renderEnts, (int)megaRenderGroups.size());

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
}

void BspRenderer::renderClipnodes(int clipnodeHull) {
	if (map->ents.empty() || !clipnodesLoaded || g_app->previewMode)
		return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	// clipnodes are drawn in a separate pass to prevent interleaving shader binds
	g_shaders.clipnode->bind();
	g_shaders.clipnode->modelMat->loadIdentity();
	g_shaders.clipnode->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_shaders.clipnode->updateMatrixes();

	if (g_settings.render_flags & RENDER_CLIPNODE_OPAQUE)
		g_shaders.clipnode->setUniform("opacity", 1);
	else
		g_shaders.clipnode->setUniform("opacity", 0.5f);

	if ((g_settings.render_flags & RENDER_WORLD_CLIPNODES) && clipnodeHull != -1 && !map->ents[0]->hidden) {
		drawModelClipnodes(0, false, clipnodeHull);
	}

	if (!(g_settings.render_flags & RENDER_ENTS) || !(g_settings.render_flags & RENDER_ENT_CLIPNODES))
		return;

	bool useMegaGroups = megaGroupUpdateProgress == -1;

	if (useMegaGroups) {
		int groupHull = clipnodeHull;
		if (groupHull == -1)
			groupHull = MAX_MAP_HULLS;
		VertexBuffer* buffer = megaRenderClipnodes.buffer[groupHull];
		if (buffer)
			buffer->draw(g_shaders.clipnode, GL_TRIANGLES);
	}

	g_shaders.clipnode->pushMatrix(MAT_MODEL);
	for (int i = 0, sz = orderEnts.size(); i < sz; i++) {
		const OrderedEnt& orderEnt = orderEnts[i];
		int modelIdx = orderEnt.modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount) {
			Entity* ent = orderEnt.ent;
			if (ent->hidden || (useMegaGroups && orderEnt.isInMegaRenderGroup))
				continue;

			RenderClipnodes& clip = renderClipnodeDat[modelIdx];
			if (clipnodeHull == -1 && getBestClipnodeHull(modelIdx) == -1) {
				continue; // skip if no hull can be drawn
			}

			if (clipnodeHull == -1 && renderModels[modelIdx].groupCount > 0) {
				continue; // skip rendering for models that have faces, if in auto mode
			}

			*g_shaders.clipnode->modelMat = orderEnt.transformWorld;
			g_shaders.clipnode->updateMatrixes();

			if (ent->highlighted) {
				g_shaders.color->setUniform("colorMult", vec4(1, 0.25f, 0.25f, 1));
			}

			drawModelClipnodes(modelIdx, false, clipnodeHull);

			if (ent->highlighted) {
				g_shaders.clipnode->setUniform("colorMult", vec4(1, 1, 1, 1));
			}
		}
	}
	g_shaders.clipnode->popMatrix(MAT_MODEL);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
}

void BspRenderer::renderLeaves() {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	// draw clipnodes in a separate pass to prevent interleaving shader binds
	if (leavesLoaded) {
		g_shaders.clipnode->bind();
		g_shaders.clipnode->modelMat->loadIdentity();
		g_shaders.clipnode->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
		g_shaders.clipnode->updateMatrixes();

		if (renderLeafDat->leafBuffer) {
			renderLeafDat->leafBuffer->draw(g_shaders.clipnode, GL_TRIANGLES);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	delayLoadData();
}

bool BspRenderer::willDrawModel(Entity* ent, int modelIdx, bool transparent) {
	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_WIREFRAME))) {
		return false;
	}
	if (modelIdx >= numRenderModels) {
		return false;
	}

	EntRenderOpts opts = ent->getRenderOpts();
	bool isTransparent = false;

	if ((g_settings.render_flags & RENDER_RENDER_MODES) || g_app->previewMode) {
		switch (opts.rendermode) {
		case RENDER_MODE_SOLID:
			isTransparent = true;
			break;
		case RENDER_MODE_COLOR:
		case RENDER_MODE_TEXTURE:
		case RENDER_MODE_GLOW:
		case RENDER_MODE_ADDITIVE:
			isTransparent = opts.renderamt < 255;
			break;
		default:
			break;
		}
	}
	else {
		isTransparent = false;
	}

	for (int i = 0; i < renderModels[modelIdx].groupCount; i++) {
		RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

		if ((rgroup.transparent || isTransparent) != transparent)
			continue;

		if (rgroup.transparent) {
			if (modelIdx == 0 && (!(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode))
				continue;
			else if (modelIdx != 0 && !(g_settings.render_flags & RENDER_SPECIAL_ENTS))
				continue;
		}
		else if (modelIdx != 0 && !(g_settings.render_flags & RENDER_ENTS))
			continue;

		return true;
	}

	return false;
}

void BspRenderer::drawModel(Entity* ent, int modelIdx, bool transparent, bool highlight) {
	EntRenderOpts opts = ent->getRenderOpts();
	bool isTransparent = false;
	bool useLightmaps = true;

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_WIREFRAME))) {
		return;
	}

	if ((g_settings.render_flags & RENDER_RENDER_MODES) || g_app->previewMode) {
		switch (opts.rendermode) {
		default:
		case RENDER_MODE_NORMAL:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			activeShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));
			activeShader->setUniform("alphaTest", 0);
			isTransparent = false;
			useLightmaps = true;
			break;
		case RENDER_MODE_SOLID:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			activeShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));
			activeShader->setUniform("alphaTest", 1);
			isTransparent = true;
			useLightmaps = true;
			break;
		case RENDER_MODE_COLOR:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			activeShader->setUniform("colorMult", vec4(opts.rendercolor.toVec(), opts.renderamt / 255.0f));
			activeShader->setUniform("alphaTest", 0);
			isTransparent = opts.renderamt < 255;
			useLightmaps = false;
			break;
		case RENDER_MODE_TEXTURE:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			activeShader->setUniform("colorMult", vec4(1, 1, 1, opts.renderamt / 255.0f));
			activeShader->setUniform("alphaTest", 0);
			isTransparent = opts.renderamt < 255;
			useLightmaps = false;
			break;
		case RENDER_MODE_GLOW:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			activeShader->setUniform("colorMult", vec4(1, 1, 1, opts.renderamt / 255.0f));
			activeShader->setUniform("alphaTest", 0);
			isTransparent = opts.renderamt < 255;
			useLightmaps = false;
			break;
		case RENDER_MODE_ADDITIVE:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			activeShader->setUniform("colorMult", vec4(1, 1, 1, opts.renderamt / 255.0f));
			activeShader->setUniform("alphaTest", 0);
			isTransparent = opts.renderamt < 255;
			useLightmaps = false;
			break;
		}
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		activeShader->setUniform("colorMult", vec4(1.0f, 1.0f, 1.0f, 1.0f));
		activeShader->setUniform("alphaTest", 0);
		isTransparent = false;
		useLightmaps = true;
	}

	for (int i = 0; i < renderModels[modelIdx].groupCount; i++) {
		RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

		if ((rgroup.transparent || isTransparent) != transparent)
			continue;

		if (rgroup.transparent) {
			if (modelIdx == 0 && (!(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode))
				continue;
			else if (modelIdx != 0 && !(g_settings.render_flags & RENDER_SPECIAL_ENTS))
				continue;
		}

		drawModelRenderGroup(rgroup, highlight, useLightmaps);
	}
}

void BspRenderer::drawModelRenderGroup(RenderGroup& rgroup, bool highlight, bool useLightmaps) {
	// bind the texture
	glActiveTexture(GL_TEXTURE0);
	if (texturesLoaded && (g_settings.render_flags & RENDER_TEXTURES)) {
		if (g_settings.texture_atlas) {
			glTextureAtlases[rgroup.atlasTextureIdx]->bind();
		}
		else {
			rgroup.texture->bind();
		}
	}
	else {
		if (g_use_texture_arrays) {
			whiteTex3D->bind();
		}
		else {
			whiteTex->bind();
		}
	}

	// toggle lightmaps for each style
	vec4 lightmapMult;

	glActiveTexture(GL_TEXTURE1);
	if (highlight) {
		redTex->bind();
		lightmapMult = vec4(1, 0, 0, 0);
	}
	else if (!(g_settings.render_flags & RENDER_LIGHTMAPS) || !useLightmaps || !lightmapsUploaded) {
		whiteTex->bind();
		lightmapMult = vec4(1, 0, 0, 0);
	}
	else if (lightmapsUploaded) {
		rgroup.lightmapAtlas->bind();

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			((float*)&lightmapMult.x)[s] = g_app->lightStylesEnabled[s] ? 1 : 0;
		}
	}

	activeShader->setUniform("lightmapMult", lightmapMult);

	rgroup.buffer->draw(activeShader, GL_TRIANGLES);
}

void BspRenderer::drawModelClipnodes(int modelIdx, bool highlight, int hullIdx) {
	RenderClipnodes& clip = renderClipnodeDat[modelIdx];

	if (hullIdx == -1) {
		hullIdx = getBestClipnodeHull(modelIdx);
		if (hullIdx == -1) {
			return; // nothing can be drawn
		}
	}

	if (clip.clipnodeBuffer[hullIdx]) {
		clip.clipnodeBuffer[hullIdx]->draw(g_shaders.clipnode, GL_TRIANGLES);
	}
}

void BspRenderer::drawPointEntities() {
	if (!(g_settings.render_flags & RENDER_POINT_ENTS) || g_app->previewMode) {
		return;
	}

	g_shaders.color->bind();
	g_shaders.color->updateMatrixes();

	if (g_app->pickInfo.ents.empty() && !(g_settings.render_flags & (RENDER_STUDIO_MDL | RENDER_SPRITES))) {
		if (pointEnts->numVerts > 0)
			pointEnts->draw(g_shaders.color, GL_TRIANGLES);
		return;
	}

	int pointEntIdx = 0;
	int nextRangeDrawIdx = 0; // starting index for the next range draw

	const int cubeVerts = 6 * 6;

	// skip worldspawn
	for (int i = 1, sz = map->ents.size(); i < sz; i++) {
		Entity* ent = map->ents[i];
		if (renderEnts[i].modelIdx >= 0 || ent->hidden)
			continue;

		if (ent->highlighted || map->ents[i]->didStudioDraw) {
			if (pointEntIdx - nextRangeDrawIdx > 0) {
				pointEnts->drawRange(g_shaders.color, GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
			}
			nextRangeDrawIdx = pointEntIdx + 1;

			if (!map->ents[i]->didStudioDraw) {
				g_shaders.color->pushMatrix(MAT_MODEL);
				*g_shaders.color->modelMat = renderEnts[i].modelMat;
				g_shaders.color->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				g_shaders.color->updateMatrixes();

				if (ent->highlighted)
					renderEnts[i].pointEntCube->selectBuffer->draw(g_shaders.color, GL_TRIANGLES);
				else
					renderEnts[i].pointEntCube->buffer->draw(g_shaders.color, GL_TRIANGLES);
				renderEnts[i].pointEntCube->wireframeBuffer->draw(g_shaders.color, GL_LINES);

				g_shaders.color->popMatrix(MAT_MODEL);
			}
		}

		pointEntIdx++;
	}

	if (pointEntIdx - nextRangeDrawIdx > 0) {
		pointEnts->drawRange(g_shaders.color, GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
	}
}

void BspRenderer::drawSkybox() {
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glDepthMask(GL_FALSE);

	vec3 ori = g_app->cameraOrigin.flip();
	g_shaders.texture->bind();
	g_shaders.texture->modelMat->loadIdentity();
	g_shaders.texture->modelMat->translate(ori.x, ori.y, ori.z);
	g_shaders.texture->updateMatrixes();

	if (!skyBoxBuffer) {
		tCube cube(vec3(-64, -64, -64), vec3(64, 64, 64));

		skyBoxBuffer = new VertexBuffer(g_shaders.texture, &cube, 6 * 6);
		skyBoxBuffer->upload();
	}

	for (int i = 0; i < 6; i++) {
		if (!skyboxTextures[i])
			continue;

		skyboxTextures[i]->bind();
		skyBoxBuffer->drawRange(g_shaders.texture, GL_TRIANGLES, i * 6, i * 6 + 6);
	}

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}

void BspRenderer::drawPvs() {
	if (!pvsDat || !pvsDat->wireframePvsBuffer)
		return;

	glDisable(GL_DEPTH_TEST);

	g_shaders.vec3->bind(0);
	g_shaders.vec3->modelMat->loadIdentity();
	g_shaders.vec3->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_shaders.vec3->updateMatrixes();
	g_shaders.vec3->setUniform("color", vec4(1, 1, 1, 1));

	pvsDat->wireframePvsBuffer->draw(g_shaders.vec3, GL_LINES);

	glEnable(GL_DEPTH_TEST);
}