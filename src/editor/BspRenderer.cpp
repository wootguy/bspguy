#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include <algorithm>
#include "Renderer.h"
#include "Clipper.h"

#include "icons/missing.h"

BspRenderer::BspRenderer(Bsp* map, ShaderProgram* bspShader, ShaderProgram* fullBrightBspShader, 
	ShaderProgram* colorShader, PointEntRenderer* pointEntRenderer) {
	this->map = map;
	this->bspShader = bspShader;
	this->fullBrightBspShader = fullBrightBspShader;
	this->colorShader = colorShader;
	this->pointEntRenderer = pointEntRenderer;

	renderEnts = NULL;
	renderModels = NULL;
	faceMaths = NULL;

	whiteTex = new Texture(1, 1);
	greyTex = new Texture(1, 1);
	redTex = new Texture(1, 1);
	yellowTex = new Texture(1, 1);
	blackTex = new Texture(1, 1);
	blueTex = new Texture(1, 1);

	*((COLOR3*)(whiteTex->data)) = { 255, 255, 255 };
	*((COLOR3*)(redTex->data)) = { 110, 0, 0 };
	*((COLOR3*)(yellowTex->data)) = { 255, 255, 0 };
	*((COLOR3*)(greyTex->data)) = { 64, 64, 64 };
	*((COLOR3*)(blackTex->data)) = { 0, 0, 0 };
	*((COLOR3*)(blueTex->data)) = { 0, 0, 200 };

	whiteTex->upload(GL_RGB);
	redTex->upload(GL_RGB);
	yellowTex->upload(GL_RGB);
	greyTex->upload(GL_RGB);
	blackTex->upload(GL_RGB);
	blueTex->upload(GL_RGB);

	byte* img_dat = NULL;
	uint w, h;
	lodepng_decode24(&img_dat, &w, &h, missing_dat, sizeof(missing_dat));
	missingTex = new Texture(w, h, img_dat);
	missingTex->upload(GL_RGB);

	//loadTextures();
	//loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();

	bspShader->bind();

	uint sTexId = glGetUniformLocation(bspShader->ID, "sTex");
	glUniform1i(sTexId, 0);
	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		uint sLightmapTexIds = glGetUniformLocation(bspShader->ID, ("sLightmapTex" + std::to_string(s)).c_str());
		
		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds, s + 1);
	}

	fullBrightBspShader->bind();

	uint sTexId2 = glGetUniformLocation(fullBrightBspShader->ID, "sTex");
	glUniform1i(sTexId2, 0);

	colorShaderMultId = glGetUniformLocation(colorShader->ID, "colorMult");

	numRenderClipnodes = map->modelCount;
	lightmapFuture = std::async(std::launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = std::async(std::launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);

	// cache ent targets so first selection doesn't lag
	for (int i = 0; i < map->ents.size(); i++) {
		map->ents[i]->getTargets();
	}
}

void BspRenderer::loadTextures() {

	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
	wads.clear();

	std::vector<std::string> wadNames;
	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->keyvalues["classname"] == "worldspawn") {
			wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	std::vector<std::string> tryPaths = {
		"./"
	};

	tryPaths.insert(tryPaths.end(), g_settings.resPaths.begin(), g_settings.resPaths.end());
	
	for (int i = 0; i < wadNames.size(); i++) {
		std::string path;
		for (int k = 0; k < tryPaths.size(); k++) {
			std::string tryPath = tryPaths[k] + wadNames[i];
			if (!fileExists(tryPath)) 
				tryPath = g_settings.gamedir + tryPaths[k] + wadNames[i];
			if (fileExists(tryPath)) {
				path = tryPath;
				break;
			}
		}

		if (path.empty()) {
			logf("Missing WAD: %s\n", wadNames[i].c_str());
			continue;
		}

		logf("Loading WAD %s\n", path.c_str());
		Wad* wad = new Wad(path);
		wad->readInfo();
		wads.push_back(wad);
	}

	int wadTexCount = 0;
	int missingCount = 0;
	int embedCount = 0;

	glTexturesSwap = new Texture * [map->textureCount];
	for (int i = 0; i < map->textureCount; i++) {
		int32_t texOffset = ((int32_t*)map->textures)[i + 1];
		if (texOffset == -1) {
			glTexturesSwap[i] = missingTex;
			continue;
		}
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

		COLOR3* palette = NULL;
		byte* src = NULL;
		WADTEX* wadTex = NULL;

		int lastMipSize = (tex.nWidth / 8) * (tex.nHeight / 8);

		if (tex.nOffsets[0] <= 0) {

			bool foundInWad = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex.szName)) {
					foundInWad = true;

					wadTex = wads[k]->readTexture(tex.szName);
					palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + 2 - 40);
					src = wadTex->data;

					wadTexCount++;
					break;
				}
			}

			if (!foundInWad) {
				glTexturesSwap[i] = missingTex;
				missingCount++;
				continue;
			}
		}
		else {
			palette = (COLOR3*)(map->textures + texOffset + tex.nOffsets[3] + lastMipSize + 2);
			src = map->textures + texOffset + tex.nOffsets[0];
			embedCount++;
		}

		COLOR3* imageData = new COLOR3[tex.nWidth * tex.nHeight];

		int sz = tex.nWidth * tex.nHeight;
		
        if (src && palette)
        {
            for (int k = 0; k < sz; k++) {
                imageData[k] = palette[src[k]];
            }
        }
		if (wadTex) {
			delete[] wadTex->data;
			delete wadTex;
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTexturesSwap[i] = new Texture(tex.nWidth, tex.nHeight, (byte*)imageData);
	}
	if (wadTexCount)
		debugf("Loaded %d wad textures\n", wadTexCount);
	if (embedCount)
		debugf("Loaded %d embedded textures\n", embedCount);
	if (missingCount)
		debugf("%d missing textures\n", missingCount);
}

void BspRenderer::reload() {
	updateLightmapInfos();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();
	reloadTextures();
	reloadLightmaps();
	reloadClipnodes();
}

void BspRenderer::reloadTextures() {
	texturesLoaded = false;
	texturesFuture = std::async(std::launch::async, &BspRenderer::loadTextures, this);
}

void BspRenderer::reloadLightmaps() {
	lightmapsGenerated = false;
	lightmapsUploaded = false;
	deleteLightmapTextures();
	if (lightmaps != NULL) {
		delete[] lightmaps;
	}
	lightmapFuture = std::async(std::launch::async, &BspRenderer::loadLightmaps, this);
}

void BspRenderer::reloadClipnodes() {
	clipnodesLoaded = false;
	clipnodeLeafCount = 0;

	deleteRenderClipnodes();

	clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);
}

void BspRenderer::addClipnodeModel(int modelIdx) {
	RenderClipnodes* newRenderClipnodes = new RenderClipnodes[numRenderClipnodes +1];
	memcpy(newRenderClipnodes, renderClipnodes, numRenderClipnodes * sizeof(RenderClipnodes));
	memset(&newRenderClipnodes[numRenderClipnodes], 0, sizeof(RenderClipnodes));
	numRenderClipnodes++;
	renderClipnodes = newRenderClipnodes;
	
	generateClipnodeBuffer(modelIdx);
}

void BspRenderer::updateModelShaders() {
	ShaderProgram* activeShader = (g_render_flags & RENDER_LIGHTMAPS) ? bspShader : fullBrightBspShader;

	for (int i = 0; i < numRenderModels; i++) {
		RenderModel& model = renderModels[i];
		for (int k = 0; k < model.groupCount; k++) {
			model.renderGroups[k].buffer->setShader(activeShader, true);
			model.renderGroups[k].wireframeBuffer->setShader(activeShader, true);
		}
	}
}

void BspRenderer::loadLightmaps() {
	std::vector<LightmapNode*> atlases;
	std::vector<Texture*> atlasTextures;
	atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	memset(atlasTextures[0]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

	numRenderLightmapInfos = map->faceCount;
	lightmaps = new LightmapInfo[map->faceCount];
	memset(lightmaps, 0, map->faceCount * sizeof(LightmapInfo));

	debugf("Calculating lightmaps\n");

	int lightmapCount = 0;
	int atlasId = 0;
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL) || face.nLightmapOffset >= map->header.lump[LUMP_LIGHTING].nLength)
			continue;

		int size[2];
		int imins[2];
		int imaxs[2];
		GetFaceLightmapSize(map, i, size);
		GetFaceExtents(map, i, imins, imaxs);

		LightmapInfo& info = lightmaps[i];
		info.w = size[0];
		info.h = size[1];
		info.midTexU = (float)(size[0]) / 2.0f;
		info.midTexV = (float)(size[1]) / 2.0f;

		// TODO: float mins/maxs not needed?
		info.midPolyU = (imins[0] + imaxs[0]) * 16 / 2.0f;
		info.midPolyV = (imins[1] + imaxs[1]) * 16 / 2.0f;

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == 255)
				continue;

			// TODO: Try fitting in earlier atlases before using the latest one
			if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s])) {
				atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
				atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
				atlasId++;
				memset(atlasTextures[atlasId]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

				if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s])) {
					logf("Lightmap too big for atlas size!\n");
					continue;
				}
			}

			lightmapCount++;

			info.atlasId[s] = atlasId;

			// copy lightmap data into atlas
			int lightmapSz = info.w * info.h * sizeof(COLOR3);
			int offset = face.nLightmapOffset + s * lightmapSz;
			COLOR3* lightSrc = (COLOR3*)(map->lightdata + offset);
			COLOR3* lightDst = (COLOR3*)(atlasTextures[atlasId]->data);
			for (int y = 0; y < info.h; y++) {
				for (int x = 0; x < info.w; x++) {
					int src = y * info.w + x;
					int dst = (info.y[s] + y) * LIGHTMAP_ATLAS_SIZE + info.x[s] + x;
					if (offset + src*sizeof(COLOR3) < map->lightDataLength) {
						lightDst[dst] = lightSrc[src];
					}
					else {
						bool checkers = x % 2 == 0 != y % 2 == 0;
						lightDst[dst] = { (byte)(checkers ? 255 : 0), 0, (byte)(checkers ? 255 : 0) };
					}
				}
			}
		}
	}

	glLightmapTextures = new Texture * [atlasTextures.size()];
	for (int i = 0; i < atlasTextures.size(); i++) {
		delete atlases[i];
		glLightmapTextures[i] = atlasTextures[i];
	}

	numLightmapAtlases = atlasTextures.size();

	//lodepng_encode24_file("atlas.png", atlasTextures[0]->data, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE);
	debugf("Fit %d lightmaps into %d atlases\n", lightmapCount, atlasId + 1);
}

void BspRenderer::updateLightmapInfos() {

	if (numRenderLightmapInfos == map->faceCount) {
		return;
	}
	if (map->faceCount < numRenderLightmapInfos) {
		debugf("TODO: Recalculate lightmaps when faces deleted\n");
		return;
	}

	// assumes new faces have no light data
	int addedFaces = map->faceCount - numRenderLightmapInfos;

	LightmapInfo* newLightmaps = new LightmapInfo[map->faceCount];
	memcpy(newLightmaps, lightmaps, numRenderLightmapInfos * sizeof(LightmapInfo));
	memset(newLightmaps + numRenderLightmapInfos, 0, addedFaces*sizeof(LightmapInfo));

	delete[] lightmaps;
	lightmaps = newLightmaps;
	numRenderLightmapInfos = map->faceCount;
}

void BspRenderer::preRenderFaces() {
	deleteRenderFaces();

	genRenderFaces(numRenderModels);

	for (int i = 0; i < numRenderModels; i++) {
		RenderModel& model = renderModels[i];
		RenderClipnodes& clip = renderClipnodes[i];
		for (int k = 0; k < model.groupCount; k++) {
			model.renderGroups[k].buffer->bindAttributes(true);
			model.renderGroups[k].wireframeBuffer->bindAttributes(true);
			model.renderGroups[k].buffer->upload();
			model.renderGroups[k].wireframeBuffer->upload();
		}
	}
}

void BspRenderer::genRenderFaces(int& renderModelCount) {
	renderModels = new RenderModel[map->modelCount];
	memset(renderModels, 0, sizeof(RenderModel) * map->modelCount);
	renderModelCount = map->modelCount;

	int worldRenderGroups = 0;
	int modelRenderGroups = 0;

	for (int m = 0; m < map->modelCount; m++) {
		int groupCount = refreshModel(m, false);
		if (m == 0)
			worldRenderGroups += groupCount;
		else
			modelRenderGroups += groupCount;
	}

	debugf("Created %d solid render groups (%d world, %d entity)\n", 
		worldRenderGroups + modelRenderGroups,
		worldRenderGroups,
		modelRenderGroups);
}

void BspRenderer::deleteRenderModel(RenderModel* renderModel) {
	if (renderModel == NULL || renderModel->renderGroups == NULL || renderModel->renderFaces == NULL) {
		return;
	}
	for (int k = 0; k < renderModel->groupCount; k++) {
		RenderGroup& group = renderModel->renderGroups[k];
		delete[] group.verts;
		delete[] group.wireframeVerts;
		delete group.buffer;
		delete group.wireframeBuffer;
	}

	delete[] renderModel->renderGroups;
	delete[] renderModel->renderFaces;
}

void BspRenderer::deleteRenderClipnodes() {
	if (renderClipnodes != NULL) {
		for (int i = 0; i < numRenderClipnodes; i++) {
			deleteRenderModelClipnodes(&renderClipnodes[i]);
		}
		delete[] renderClipnodes;
	}

	renderClipnodes = NULL;
}

void BspRenderer::deleteRenderModelClipnodes(RenderClipnodes* renderClip) {
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (renderClip->clipnodeBuffer[i]) {
			delete renderClip->clipnodeBuffer[i];
			delete renderClip->wireframeClipnodeBuffer[i];
		}
		renderClip->clipnodeBuffer[i] = NULL;
		renderClip->wireframeClipnodeBuffer[i] = NULL;
	}
}

void BspRenderer::deleteRenderFaces() {
	if (renderModels != NULL) {
		for (int i = 0; i < numRenderModels; i++) {
			deleteRenderModel(&renderModels[i]);
		}
		delete[] renderModels;
	}

	renderModels = NULL;
}

void BspRenderer::deleteTextures() {
	if (glTextures != NULL) {
		for (int i = 0; i < numLoadedTextures; i++) {
			if (glTextures[i] != missingTex)
				delete glTextures[i];
		}
		delete[] glTextures;
	}

	glTextures = NULL;
}

void BspRenderer::deleteLightmapTextures() {
	if (glLightmapTextures != NULL) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			if (glLightmapTextures[i])
				delete glLightmapTextures[i];
		}
		delete[] glLightmapTextures;
	}

	glLightmapTextures = NULL;
}

void BspRenderer::deleteFaceMaths() {
	if (faceMaths != NULL) {
		delete[] faceMaths;
	}

	faceMaths = NULL;
}

int BspRenderer::refreshModel(int modelIdx, bool refreshClipnodes) {
	if (modelIdx < 0)
		return 0;

	BSPMODEL& model = map->models[modelIdx];
	RenderModel* renderModel = &renderModels[modelIdx];

	deleteRenderModel(renderModel);
	
	renderModel->renderFaces = new RenderFace[model.nFaces];

	std::vector<RenderGroup> renderGroups;
	std::vector<std::vector<lightmapVert>> renderGroupVerts;
	std::vector<std::vector<lightmapVert>> renderGroupWireframeVerts;

	ShaderProgram* activeShader = (g_render_flags & RENDER_LIGHTMAPS) ? bspShader : fullBrightBspShader;

	for (int i = 0; i < model.nFaces; i++) {
		int faceIdx = model.iFirstFace + i;
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];

		int texWidth, texHeight;
		if (texOffset != -1) {
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			texWidth = tex.nWidth;
			texHeight = tex.nHeight;
		}
		else {
			// missing texture
			texWidth = 16;
			texHeight = 16;
		}
		

		LightmapInfo* lmap = lightmapsGenerated ? &lightmaps[faceIdx] : NULL;

		lightmapVert* verts = new lightmapVert[face.nEdges];
		int vertCount = face.nEdges;
		Texture* lightmapAtlas[MAXLIGHTMAPS];

		float lw = 0;
		float lh = 0;
		if (lightmapsGenerated) {
			lw = (float)lmap->w / (float)LIGHTMAP_ATLAS_SIZE;
			lh = (float)lmap->h / (float)LIGHTMAP_ATLAS_SIZE;
		}

		bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
		bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			lightmapAtlas[s] = lightmapsGenerated ? glLightmapTextures[lmap->atlasId[s]] : NULL;
		}

		if (isSpecial) {
			lightmapAtlas[0] = whiteTex;
		}

		float opacity = isSpecial ? 0.5f : 1.0f;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3& vert = map->verts[vertIdx];
			verts[e].x = vert.x;
			verts[e].y = vert.z;
			verts[e].z = -vert.y;

			verts[e].r = 1.0f;
			verts[e].g = 1.0f;
			verts[e].b = 1.0f;
			verts[e].a = isSpecial ? 0.5f : 1.0f;

			// texture coords
			float tw = 1.0f / (float)texWidth;
			float th = 1.0f / (float)texHeight;
			float fU = dotProduct(texinfo.vS, vert) + texinfo.shiftS;
			float fV = dotProduct(texinfo.vT, vert) + texinfo.shiftT;
			verts[e].u = fU * tw;
			verts[e].v = fV * th;

			// lightmap texture coords
			if (hasLighting && lightmapsGenerated) {
				float fLightMapU = lmap->midTexU + (fU - lmap->midPolyU) / 16.0f;
				float fLightMapV = lmap->midTexV + (fV - lmap->midPolyV) / 16.0f;

				float uu = (fLightMapU / (float)lmap->w) * lw;
				float vv = (fLightMapV / (float)lmap->h) * lh;

				float pixelStep = 1.0f / (float)LIGHTMAP_ATLAS_SIZE;

				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][0] = uu + lmap->x[s] * pixelStep;
					verts[e].luv[s][1] = vv + lmap->y[s] * pixelStep;
				}
			}
			// set lightmap scales
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				verts[e].luv[s][2] = (hasLighting && face.nStyles[s] != 255) ? 1.0f : 0.0f;
				if (isSpecial && s == 0) {
					verts[e].luv[s][2] = 1.0f;
				}
			}
		}


		// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
		int newCount = face.nEdges + std::max(0, face.nEdges - 3) * 2;
		int wireframeVertCount = face.nEdges * 2;
		lightmapVert* newVerts = new lightmapVert[newCount];
		lightmapVert* wireframeVerts = new lightmapVert[wireframeVertCount];

		int idx = 0;
		for (int k = 2; k < face.nEdges; k++) {
			newVerts[idx++] = verts[0];
			newVerts[idx++] = verts[k - 1];
			newVerts[idx++] = verts[k];
		}

		idx = 0;
		for (int k = 0; k < face.nEdges && (k + 1) % face.nEdges < face.nEdges; k++) {
			wireframeVerts[idx++] = verts[k];
			wireframeVerts[idx++] = verts[(k + 1) % face.nEdges];
		}
		for (int k = 0; k < wireframeVertCount; k++) {
			wireframeVerts[k].luv[0][2] = 1.0f;
			wireframeVerts[k].luv[1][2] = 0.0f;
			wireframeVerts[k].luv[2][2] = 0.0f;
			wireframeVerts[k].luv[3][2] = 0.0f;
			wireframeVerts[k].r = 1.0f;
			wireframeVerts[k].g = 1.0f;
			wireframeVerts[k].b = 1.0f;
			wireframeVerts[k].a = 1.0f;
		}

		delete[] verts;
		verts = newVerts;
		vertCount = newCount;

		// add face to a render group (faces that share that same textures and opacity flag)
		bool isTransparent = opacity < 1.0f;
		int groupIdx = -1;
		for (int k = 0; k < renderGroups.size(); k++) {
			bool textureMatch = !texturesLoaded || renderGroups[k].texture == glTextures[texinfo.iMiptex];
			if (textureMatch && renderGroups[k].transparent == isTransparent) {
				bool allMatch = true;
				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					if (renderGroups[k].lightmapAtlas[s] != lightmapAtlas[s]) {
						allMatch = false;
						break;
					}
				}
				if (allMatch) {
					groupIdx = k;
					break;
				}
			}
		}

		// add the verts to a new group if no existing one share the same properties
		if (groupIdx == -1) {
			RenderGroup newGroup = RenderGroup();
			newGroup.vertCount = 0;
			newGroup.verts = NULL;
			newGroup.transparent = isTransparent;
			newGroup.texture = texturesLoaded ? glTextures[texinfo.iMiptex] : greyTex;
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				newGroup.lightmapAtlas[s] = lightmapAtlas[s];
			}
			renderGroups.push_back(newGroup);
			renderGroupVerts.push_back(std::vector<lightmapVert>());
			renderGroupWireframeVerts.push_back(std::vector<lightmapVert>());
			groupIdx = renderGroups.size() - 1;
		}

		renderModel->renderFaces[i].group = groupIdx;
		renderModel->renderFaces[i].vertOffset = renderGroupVerts[groupIdx].size();
		renderModel->renderFaces[i].vertCount = vertCount;

		renderGroupVerts[groupIdx].insert(renderGroupVerts[groupIdx].end(), verts, verts + vertCount);
		renderGroupWireframeVerts[groupIdx].insert(renderGroupWireframeVerts[groupIdx].end(), wireframeVerts, wireframeVerts + wireframeVertCount);

		delete[] verts;
		delete[] wireframeVerts;
	}

	renderModel->renderGroups = new RenderGroup[renderGroups.size()];
	renderModel->groupCount = renderGroups.size();

	for (int i = 0; i < renderGroups.size(); i++) {
		renderGroups[i].verts = new lightmapVert[renderGroupVerts[i].size()];
		renderGroups[i].vertCount = renderGroupVerts[i].size();
		memcpy(renderGroups[i].verts, &renderGroupVerts[i][0], renderGroups[i].vertCount * sizeof(lightmapVert));

		renderGroups[i].wireframeVerts = new lightmapVert[renderGroupWireframeVerts[i].size()];
		renderGroups[i].wireframeVertCount = renderGroupWireframeVerts[i].size();
		memcpy(renderGroups[i].wireframeVerts, &renderGroupWireframeVerts[i][0], renderGroups[i].wireframeVertCount * sizeof(lightmapVert));

		renderGroups[i].buffer = new VertexBuffer(activeShader, 0);
		renderGroups[i].buffer->addAttribute(TEX_2F, "vTex");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
		renderGroups[i].buffer->addAttribute(4, GL_FLOAT, 0, "vColor");
		renderGroups[i].buffer->addAttribute(POS_3F, "vPosition");
		renderGroups[i].buffer->setData(renderGroups[i].verts, renderGroups[i].vertCount);

		renderGroups[i].wireframeBuffer = new VertexBuffer(activeShader, 0);
		renderGroups[i].wireframeBuffer->addAttribute(TEX_2F, "vTex");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
		renderGroups[i].wireframeBuffer->addAttribute(4, GL_FLOAT, 0, "vColor");
		renderGroups[i].wireframeBuffer->addAttribute(POS_3F, "vPosition");
		renderGroups[i].wireframeBuffer->setData(renderGroups[i].wireframeVerts, renderGroups[i].wireframeVertCount);

		renderModel->renderGroups[i] = renderGroups[i];
	}

	for (int i = 0; i < model.nFaces; i++) {
		refreshFace(model.iFirstFace + i);
	}

	if (refreshClipnodes)
		generateClipnodeBuffer(modelIdx);

	return renderModel->groupCount;
}

bool BspRenderer::refreshModelClipnodes(int modelIdx) {
	if (!clipnodesLoaded) {
		return false;
	}
	if (modelIdx < 0 || modelIdx >= numRenderClipnodes) {
		logf("Bad model idx\n");
		return false;
	}

	deleteRenderModelClipnodes(&renderClipnodes[modelIdx]);
	generateClipnodeBuffer(modelIdx);
	return true;
}

void BspRenderer::loadClipnodes() {
	numRenderClipnodes = map->modelCount;
	renderClipnodes = new RenderClipnodes[numRenderClipnodes];
	memset(renderClipnodes, 0, numRenderClipnodes * sizeof(RenderClipnodes));

	for (int i = 0; i < numRenderClipnodes; i++) {
		generateClipnodeBuffer(i);
	}
}

void BspRenderer::generateClipnodeBuffer(int modelIdx) {
	BSPMODEL& model = map->models[modelIdx];
	RenderClipnodes* renderClip = &renderClipnodes[modelIdx];

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		renderClip->clipnodeBuffer[i] = NULL;
		renderClip->wireframeClipnodeBuffer[i] = NULL;
	}

	Clipper clipper;
	
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		std::vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, i);

		std::vector<CMesh> meshes;
		for (int k = 0; k < solidNodes.size(); k++) {
			meshes.push_back(clipper.clip(solidNodes[k].cuts));
			clipnodeLeafCount++;
		}
		
		static COLOR4 hullColors[] = {
			COLOR4(255, 255, 255, 128),
			COLOR4(96, 255, 255, 128),
			COLOR4(255, 96, 255, 128),
			COLOR4(255, 255, 96, 128),
		};
		COLOR4 color = hullColors[i];

		std::vector<cVert> allVerts;
		std::vector<cVert> wireframeVerts;
		std::vector<FaceMath> faceMaths;

		for (int m = 0; m < meshes.size(); m++) {
			CMesh& mesh = meshes[m];

			for (int n = 0; n < mesh.faces.size(); n++) {

				if (!mesh.faces[n].visible) {
					continue;
				}

				std::set<int> uniqueFaceVerts;

				for (int k = 0; k < mesh.faces[n].edges.size(); k++) {
					for (int v = 0; v < 2; v++) {
						int vertIdx = mesh.edges[mesh.faces[n].edges[k]].verts[v];
						if (!mesh.verts[vertIdx].visible) {
							continue;
						}
						uniqueFaceVerts.insert(vertIdx);
					}
				}

				std::vector<vec3> faceVerts;
				for (auto vertIdx : uniqueFaceVerts) {
					faceVerts.push_back(mesh.verts[vertIdx].pos);
				}

				faceVerts = getSortedPlanarVerts(faceVerts);

				if (faceVerts.size() < 3) {
					//logf("Degenerate clipnode face discarded\n");
					continue;
				}

				vec3 normal = getNormalFromVerts(faceVerts);

				if (dotProduct(mesh.faces[n].normal, normal) > 0) {
					reverse(faceVerts.begin(), faceVerts.end());
					normal = normal.invert();
				}

				// calculations for face picking
				{
					FaceMath faceMath;
					faceMath.normal = mesh.faces[n].normal;
					faceMath.fdist = getDistAlongAxis(mesh.faces[n].normal, faceVerts[0]);

					vec3 v0 = faceVerts[0];
					vec3 v1;
					bool found = false;
					for (int c = 1; c < faceVerts.size(); c++) {
						if (faceVerts[c] != v0) {
							v1 = faceVerts[c];
							found = true;
							break;
						}
					}
					if (!found) {
						logf("Failed to find non-duplicate vert for clipnode face\n");
					}

					vec3 plane_z = mesh.faces[n].normal;
					vec3 plane_x = (v1 - v0).normalize();
					vec3 plane_y = crossProduct(plane_z, plane_x).normalize();
					faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

					faceMath.localVerts = std::vector<vec2>(faceVerts.size());
					for (int k = 0; k < faceVerts.size(); k++) {
						faceMath.localVerts[k] = (faceMath.worldToLocal * vec4(faceVerts[k], 1)).xy();
					}

					faceMaths.push_back(faceMath);
				}

				// create the verts for rendering
				{
					for (int c = 0; c < faceVerts.size(); c++) {
						faceVerts[c] = faceVerts[c].flip();
					}

					COLOR4 wireframeColor = { 0, 0, 0, 255 };
					for (int k = 0; k < faceVerts.size(); k++) {
						wireframeVerts.push_back(cVert(faceVerts[k], wireframeColor));
						wireframeVerts.push_back(cVert(faceVerts[(k + 1) % faceVerts.size()], wireframeColor));
					}

					vec3 lightDir = vec3(1, 1, -1).normalize();
					float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
					if (dot > 0.5f) {
						dot = dot * dot;
					}
					COLOR4 faceColor = color * (dot);

					// convert from TRIANGLE_FAN style verts to TRIANGLES
					for (int k = 2; k < faceVerts.size(); k++) {
						allVerts.push_back(cVert(faceVerts[0], faceColor));
						allVerts.push_back(cVert(faceVerts[k - 1], faceColor));
						allVerts.push_back(cVert(faceVerts[k], faceColor));
					}
				}
			}
		}

		cVert* output = new cVert[allVerts.size()];
		for (int c = 0; c < allVerts.size(); c++) {
			output[c] = allVerts[c];
		}

		cVert* wireOutput = new cVert[wireframeVerts.size()];
		for (int c = 0; c < wireframeVerts.size(); c++) {
			wireOutput[c] = wireframeVerts[c];
		}

		if (allVerts.size() == 0 || wireframeVerts.size() == 0) {
			renderClip->clipnodeBuffer[i] = NULL;
			renderClip->wireframeClipnodeBuffer[i] = NULL;
			continue;
		}

		renderClip->clipnodeBuffer[i] = new VertexBuffer(colorShader, COLOR_4B | POS_3F, output, allVerts.size());
		renderClip->clipnodeBuffer[i]->ownData = true;

		renderClip->wireframeClipnodeBuffer[i] = new VertexBuffer(colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());
		renderClip->wireframeClipnodeBuffer[i]->ownData = true;

		renderClip->faceMaths[i] = faceMaths;
	}
}

void BspRenderer::updateClipnodeOpacity(byte newValue) {
	for (int i = 0; i < numRenderClipnodes; i++) {
		for (int k = 0; k < MAX_MAP_HULLS; k++) {
			if (renderClipnodes[i].clipnodeBuffer[k]) {
				cVert* data = (cVert*)renderClipnodes[i].clipnodeBuffer[k]->data;
				for (int v = 0; v < renderClipnodes[i].clipnodeBuffer[k]->numVerts; v++) {
					data[v].c.a = newValue;
				}
				renderClipnodes[i].clipnodeBuffer[k]->deleteBuffer();
				renderClipnodes[i].clipnodeBuffer[k]->upload();
			}
		}
	}
}

void BspRenderer::preRenderEnts() {
	if (renderEnts != NULL) {
		delete[] renderEnts;
		delete pointEnts;
	}
	renderEnts = new RenderEnt[map->ents.size()];

	numPointEnts = 0;
	for (int i = 1; i < map->ents.size(); i++) {
		numPointEnts += !map->ents[i]->isBspModel();
	}

	cCube* entCubes = new cCube[numPointEnts];
	int pointEntIdx = 0;

	for (int i = 0; i < map->ents.size(); i++) {
		refreshEnt(i);

		if (i != 0 && !map->ents[i]->isBspModel()) {
			memcpy(entCubes + pointEntIdx, renderEnts[i].pointEntCube->buffer->data, sizeof(cCube));
			cVert* verts = (cVert*)(entCubes + pointEntIdx);
			vec3 offset = renderEnts[i].offset.flip();
			for (int k = 0; k < 6 * 6; k++) {
				verts[k].x += offset.x;
				verts[k].y += offset.y;
				verts[k].z += offset.z;
			}
			pointEntIdx++;
		}
	}

	pointEnts = new VertexBuffer(colorShader, COLOR_4B | POS_3F, entCubes, numPointEnts * 6 * 6);
	pointEnts->ownData = true;
	pointEnts->upload();
}

void BspRenderer::refreshPointEnt(int entIdx) {
	int skipIdx = 0;

	if (entIdx == 0)
		return;

	// skip worldspawn
	for (int i = 1, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0)
			continue;

		if (i == entIdx) {
			break;
		}

		skipIdx++;
	}

	if (skipIdx >= numPointEnts) {
		logf("Failed to update point ent\n");
		return;
	}

	cCube* entCubes = (cCube*)pointEnts->data;

	memcpy(entCubes + skipIdx, renderEnts[entIdx].pointEntCube->buffer->data, sizeof(cCube));
	cVert* verts = (cVert*)(entCubes + skipIdx);
	vec3 offset = renderEnts[entIdx].offset.flip();
	for (int k = 0; k < 6 * 6; k++) {
		verts[k].x += offset.x;
		verts[k].y += offset.y;
		verts[k].z += offset.z;
	}

	pointEnts->deleteBuffer();
	pointEnts->upload();
}

void BspRenderer::refreshEnt(int entIdx) {
	Entity* ent = map->ents[entIdx];
	renderEnts[entIdx].modelIdx = ent->getBspModelIdx();
	renderEnts[entIdx].modelMat.loadIdentity();
	renderEnts[entIdx].offset = vec3(0, 0, 0);
	renderEnts[entIdx].pointEntCube = pointEntRenderer->getEntCube(ent);

	if (ent->hasKey("origin")) {
		vec3 origin = parseVector(ent->keyvalues["origin"]);
		renderEnts[entIdx].modelMat.translate(origin.x, origin.z, -origin.y);
		renderEnts[entIdx].offset = origin;
	}
}

void BspRenderer::calcFaceMaths() {
	deleteFaceMaths();

	numFaceMaths = map->faceCount;
	faceMaths = new FaceMath[map->faceCount];

	vec3 world_x = vec3(1, 0, 0);
	vec3 world_y = vec3(0, 1, 0);
	vec3 world_z = vec3(0, 0, 1);

	for (int i = 0; i < map->faceCount; i++) {
		refreshFace(i);
	}
}

void BspRenderer::refreshFace(int faceIdx) {
	const vec3 world_x = vec3(1, 0, 0);
	const vec3 world_y = vec3(0, 1, 0);
	const vec3 world_z = vec3(0, 0, 1);

	FaceMath& faceMath = faceMaths[faceIdx];
	BSPFACE& face = map->faces[faceIdx];
	BSPPLANE& plane = map->planes[face.iPlane];
	vec3 planeNormal = face.nPlaneSide ? plane.vNormal * -1 : plane.vNormal;
	float fDist = face.nPlaneSide ? -plane.fDist : plane.fDist;

	faceMath.normal = planeNormal;
	faceMath.fdist = fDist;
	
	std::vector<vec3> allVerts(face.nEdges);
	vec3 v1;
	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = map->edges[abs(edgeIdx)];
		int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];
		allVerts[e] = map->verts[vertIdx];

		// 2 verts can share the same position on a face, so need to find one that isn't shared (aomdc_1intro)
		if (e > 0 && allVerts[e] != allVerts[0]) {
			v1 = allVerts[e];
		}
	}

	vec3 plane_x = (v1 - allVerts[0]).normalize(1.0f);
	vec3 plane_y = crossProduct(planeNormal, plane_x).normalize(1.0f);
	vec3 plane_z = planeNormal;

	faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	faceMath.localVerts = std::vector<vec2>(allVerts.size());
	for (int i = 0; i < allVerts.size(); i++) {
		faceMath.localVerts[i] = (faceMath.worldToLocal * vec4(allVerts[i], 1)).xy();
	}
}

BspRenderer::~BspRenderer() {
	if (lightmapFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
		texturesFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
		clipnodesFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
		logf("ERROR: Deleted bsp renderer while it was loading\n");
	}

	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
	wads.clear();

	if (lightmaps != NULL) {
		delete[] lightmaps;
	}
	if (renderEnts != NULL) {
		delete[] renderEnts;
	}
	if (pointEnts != NULL) {
		delete pointEnts;
	}

	deleteTextures();
	deleteLightmapTextures();
	deleteRenderFaces();
	deleteRenderClipnodes();
	deleteFaceMaths();

	// TODO: share these with all renderers
	delete whiteTex;
	delete redTex;
	delete yellowTex;
	delete greyTex;
	delete blackTex;
	delete blueTex;
	delete missingTex;

	delete map;
}

void BspRenderer::delayLoadData() {
	if (!lightmapsUploaded && lightmapFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			glLightmapTextures[i]->upload(GL_RGB);
		}

		lightmapsGenerated = true;

		preRenderFaces();

		lightmapsUploaded = true;
	}
	else if (!texturesLoaded && texturesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		deleteTextures();
		
		glTextures = glTexturesSwap;

		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGB);
		}
		numLoadedTextures = map->textureCount;

		texturesLoaded = true;

		preRenderFaces();
	}

	if (!clipnodesLoaded && clipnodesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

		for (int i = 0; i < numRenderClipnodes; i++) {
			RenderClipnodes& clip = renderClipnodes[i];
			for (int k = 0; k < MAX_MAP_HULLS; k++) {
				if (clip.clipnodeBuffer[k]) {
					clip.clipnodeBuffer[k]->bindAttributes(true);
					clip.clipnodeBuffer[k]->upload();
				}
			}
		}

		clipnodesLoaded = true;
		debugf("Loaded %d clipnode leaves\n", clipnodeLeafCount);
	}
}

bool BspRenderer::isFinishedLoading() {
	return lightmapsUploaded && texturesLoaded && clipnodesLoaded;
}

void BspRenderer::highlightFace(int faceIdx, bool highlight) {
	RenderFace* rface;
	RenderGroup* rgroup;
	if (!getRenderPointers(faceIdx, &rface, &rgroup)) {
		logf("Bad face index\n");
		return;
	}

	float r, g, b;
	r = g = b = 1.0f;

	if (highlight) {
		r = 0.86f;
		g = 0;
		b = 0;
	}

	for (int i = 0; i < rface->vertCount; i++) {
		rgroup->verts[rface->vertOffset + i].r = r;
		rgroup->verts[rface->vertOffset + i].g = g;
		rgroup->verts[rface->vertOffset + i].b = b;
	}

	rgroup->buffer->deleteBuffer();
	rgroup->buffer->upload();
}

void BspRenderer::updateFaceUVs(int faceIdx) {
	RenderFace* rface;
	RenderGroup* rgroup;
	if (!getRenderPointers(faceIdx, &rface, &rgroup)) {
		logf("Bad face index\n");
		return;
	}

	BSPFACE& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];
	BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

	for (int i = 0; i < rface->vertCount; i++) {
		lightmapVert& vert = rgroup->verts[rface->vertOffset + i];
		vec3 pos = vec3(vert.x, -vert.z, vert.y);

		float tw = 1.0f / (float)tex.nWidth;
		float th = 1.0f / (float)tex.nHeight;
		float fU = dotProduct(texinfo.vS, pos) + texinfo.shiftS;
		float fV = dotProduct(texinfo.vT, pos) + texinfo.shiftT;
		vert.u = fU * tw;
		vert.v = fV * th;
	}

	rgroup->buffer->deleteBuffer();
	rgroup->buffer->upload();
}

bool BspRenderer::getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup) {
	int modelIdx = map->get_model_from_face(faceIdx);

	if (modelIdx == -1) {
		return false;
	}

	int relativeFaceIdx = faceIdx - map->models[modelIdx].iFirstFace;
	*renderFace = &renderModels[modelIdx].renderFaces[relativeFaceIdx];
	*renderGroup = &renderModels[modelIdx].renderGroups[(*renderFace)->group];

	return true;
}

uint BspRenderer::getFaceTextureId(int faceIdx) {
	BSPFACE& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	return glTextures[texinfo.iMiptex]->id;
}

void BspRenderer::render(int highlightEnt, bool highlightAlwaysOnTop, int clipnodeHull) {
	BSPMODEL& world = map->models[0];
	mapOffset = map->ents.size() ? map->ents[0]->getOrigin() : vec3();
	vec3 renderOffset = mapOffset.flip();

	ShaderProgram* activeShader = (g_render_flags & RENDER_LIGHTMAPS) ? bspShader : fullBrightBspShader;

	activeShader->bind();
	activeShader->modelMat->loadIdentity();
	activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	activeShader->updateMatrixes();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// draw highlighted ent first so other ent edges don't overlap the highlighted edges
	if (highlightEnt > 0 && !highlightAlwaysOnTop) {
		if (renderEnts[highlightEnt].modelIdx >= 0 && renderEnts[highlightEnt].modelIdx < map->modelCount) {
			activeShader->pushMatrix(MAT_MODEL);
			*activeShader->modelMat = renderEnts[highlightEnt].modelMat;
			activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			activeShader->updateMatrixes();

			drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
			drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);

			activeShader->popMatrix(MAT_MODEL);
		}
	}

	for (int pass = 0; pass < 2; pass++) {
		bool drawTransparentFaces = pass == 1;

		drawModel(0, drawTransparentFaces, false, false);

		for (int i = 0, sz = map->ents.size(); i < sz; i++) {
			if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount) {
				activeShader->pushMatrix(MAT_MODEL);
				*activeShader->modelMat = renderEnts[i].modelMat;
				activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				activeShader->updateMatrixes();

				drawModel(renderEnts[i].modelIdx, drawTransparentFaces, i == highlightEnt, false);

				activeShader->popMatrix(MAT_MODEL);
			}
		}

		if ((g_render_flags & RENDER_POINT_ENTS) && pass == 0) {
			drawPointEntities(highlightEnt);
			activeShader->bind();
		}
	}

	if (clipnodesLoaded) {
		colorShader->bind();

		if (g_render_flags & RENDER_WORLD_CLIPNODES && clipnodeHull != -1) {
			drawModelClipnodes(0, false, clipnodeHull);
		}

		if (g_render_flags & RENDER_ENT_CLIPNODES) {
			for (int i = 0, sz = map->ents.size(); i < sz; i++) {
				if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount) {
					if (clipnodeHull == -1 && renderModels[renderEnts[i].modelIdx].groupCount > 0) {
						continue; // skip rendering for models that have faces, if in auto mode
					}
					colorShader->pushMatrix(MAT_MODEL);
					*colorShader->modelMat = renderEnts[i].modelMat;
					colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
					colorShader->updateMatrixes();

					bool hightlighted = highlightEnt == i;

					if (hightlighted) {
						glUniform4f(colorShaderMultId, 1, 0.25f, 0.25f, 1);
					}

					drawModelClipnodes(renderEnts[i].modelIdx, false, clipnodeHull);

					if (hightlighted) {
						glUniform4f(colorShaderMultId, 1, 1, 1, 1);
					}

					colorShader->popMatrix(MAT_MODEL);
				}
			}
		}		
	}

	if (highlightEnt > 0 && highlightAlwaysOnTop) {
		activeShader->bind();

		if (renderEnts[highlightEnt].modelIdx >= 0 && renderEnts[highlightEnt].modelIdx < map->modelCount) {
			activeShader->pushMatrix(MAT_MODEL);
			*activeShader->modelMat = renderEnts[highlightEnt].modelMat;
			activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			activeShader->updateMatrixes();

			glDisable(GL_DEPTH_TEST);
			drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
			drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);
			glEnable(GL_DEPTH_TEST);

			activeShader->popMatrix(MAT_MODEL);
		}
	}

	delayLoadData();
}

void BspRenderer::drawModel(int modelIdx, bool transparent, bool highlight, bool edgesOnly) {
	if (modelIdx < 0 || modelIdx >= numRenderModels)
	{
		return;
	}

	if (edgesOnly) {
		for (int i = 0; i < renderModels[modelIdx].groupCount; i++) {
			RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

			glActiveTexture(GL_TEXTURE0);
			if (highlight)
				yellowTex->bind();
			else
				greyTex->bind();
			glActiveTexture(GL_TEXTURE1);
			whiteTex->bind();

			rgroup.wireframeBuffer->draw(GL_LINES);
		}
		return;
	}

	for (int i = 0; i < renderModels[modelIdx].groupCount; i++) {
		RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

		if (rgroup.transparent != transparent)
			continue;

		if (rgroup.transparent) {
			if (modelIdx == 0 && !(g_render_flags & RENDER_SPECIAL)) {
				continue;
			}
			else if (modelIdx != 0 && !(g_render_flags & RENDER_SPECIAL_ENTS)) {
				continue;
			}
		}
		else if (modelIdx != 0 && !(g_render_flags & RENDER_ENTS)) {
			continue;
		}
		
		if (highlight || (g_render_flags & RENDER_WIREFRAME)) {
			glActiveTexture(GL_TEXTURE0);
			if (highlight)
				yellowTex->bind();
			else {
				if (modelIdx > 0)
					blueTex->bind();
				else
					greyTex->bind();
			}
			glActiveTexture(GL_TEXTURE1);
			whiteTex->bind();

			rgroup.wireframeBuffer->draw(GL_LINES);
		}


		glActiveTexture(GL_TEXTURE0);
		if (texturesLoaded && g_render_flags & RENDER_TEXTURES) {
			rgroup.texture->bind();
		}
		else {
			whiteTex->bind();
		}

		if (g_render_flags & RENDER_LIGHTMAPS) {
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				glActiveTexture(GL_TEXTURE1 + s);


				if (highlight) {
					redTex->bind();
				}
				else if (lightmapsUploaded) {
					if (showLightFlag != -1)
					{
						if (showLightFlag == s)
						{
							blackTex->bind();
							continue;
						}
					}
					rgroup.lightmapAtlas[s]->bind();
				}
				else {
					if (s == 0) {
						greyTex->bind();
					}
					else {
						blackTex->bind();
					}
				}
			}
		}

		rgroup.buffer->draw(GL_TRIANGLES);
	}
}

void BspRenderer::drawModelClipnodes(int modelIdx, bool highlight, int hullIdx) {
	RenderClipnodes& clip = renderClipnodes[modelIdx];

	if (hullIdx == -1) {
		hullIdx = getBestClipnodeHull(modelIdx);
		if (hullIdx == -1) {
			return; // nothing can be drawn
		}
	}
	
	if (clip.clipnodeBuffer[hullIdx]) {
		clip.clipnodeBuffer[hullIdx]->draw(GL_TRIANGLES);
		clip.wireframeClipnodeBuffer[hullIdx]->draw(GL_LINES);
	}
}

void BspRenderer::drawPointEntities(int highlightEnt) {
	vec3 renderOffset = mapOffset.flip();

	colorShader->bind();

	if (highlightEnt <= 0 || highlightEnt >= map->ents.size()) {
		if (pointEnts->numVerts > 0)
			pointEnts->draw(GL_TRIANGLES);
		return;
	}

	int skipIdx = 0;

	// skip worldspawn
	for (int i = 1, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0)
			continue;

		if (highlightEnt == i) {
			colorShader->pushMatrix(MAT_MODEL);
			*colorShader->modelMat = renderEnts[i].modelMat;
			colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
			colorShader->updateMatrixes();

			renderEnts[i].pointEntCube->selectBuffer->draw(GL_TRIANGLES);
			renderEnts[i].pointEntCube->wireframeBuffer->draw(GL_LINES);

			colorShader->popMatrix(MAT_MODEL);

			break;
		}

		skipIdx++;
	}

	const int cubeVerts = 6 * 6;
	if (skipIdx > 0)
		pointEnts->drawRange(GL_TRIANGLES, 0, cubeVerts * skipIdx);
	if (skipIdx+1 < numPointEnts)
		pointEnts->drawRange(GL_TRIANGLES, cubeVerts * (skipIdx + 1), cubeVerts * numPointEnts);
}

bool BspRenderer::pickPoly(vec3 start, vec3 dir, int hullIdx, PickInfo& pickInfo) {
	bool foundBetterPick = false;

	start -= mapOffset;

	if (!map || map->ents.size() == 0)
	{
		return false;
	}

	if (pickModelPoly(start, dir, vec3(0, 0, 0), 0, hullIdx, pickInfo)) {
		pickInfo.entIdx = 0;
		pickInfo.modelIdx = 0;
		pickInfo.map = map;
		pickInfo.ent = map->ents[0];
		foundBetterPick = true;
	}

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount) {

			bool isSpecial = false;
			for (int k = 0; k < renderModels[renderEnts[i].modelIdx].groupCount; k++) {
				if (renderModels[renderEnts[i].modelIdx].renderGroups[k].transparent) {
					isSpecial = true;
					break;
				}
			}

			if (isSpecial && !(g_render_flags & RENDER_SPECIAL_ENTS)) {
				continue;
			} else if (!isSpecial && !(g_render_flags & RENDER_ENTS)) {
				continue;
			}

			if (pickModelPoly(start, dir, renderEnts[i].offset, renderEnts[i].modelIdx, hullIdx, pickInfo)) {
				pickInfo.entIdx = i;
				pickInfo.modelIdx = renderEnts[i].modelIdx;
				pickInfo.map = map;
				pickInfo.ent = map->ents[i];
				foundBetterPick = true;
			}
		}
		else if (i > 0 && g_render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;
			if (pickAABB(start, dir, mins, maxs, pickInfo.bestDist)) {
				pickInfo.entIdx = i;
				pickInfo.modelIdx = -1;
				pickInfo.faceIdx = -1;
				pickInfo.map = map;
				pickInfo.ent = map->ents[i];
				foundBetterPick = true;
			};
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickModelPoly(vec3 start, vec3 dir, vec3 offset, int modelIdx, int hullIdx, PickInfo& pickInfo) {
	BSPMODEL& model = map->models[modelIdx];

	start -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_render_flags & RENDER_SPECIAL);

	for (int k = 0; k < model.nFaces; k++) {
		FaceMath& faceMath = faceMaths[model.iFirstFace + k];
		BSPFACE& face = map->faces[model.iFirstFace + k];
		
		if (skipSpecial && modelIdx == 0) {
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}
		}

		float t = pickInfo.bestDist;
		if (pickFaceMath(start, dir, faceMath, t)) {
			foundBetterPick = true;
			pickInfo.bestDist = t;
			pickInfo.faceIdx = model.iFirstFace + k;
		}
	}

	bool selectWorldClips = modelIdx == 0 && (g_render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_render_flags & RENDER_ENT_CLIPNODES);

	if (hullIdx == -1 && renderModels[modelIdx].groupCount == 0) {
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx != -1) {
		for (int i = 0; i < renderClipnodes[modelIdx].faceMaths[hullIdx].size(); i++) {
			FaceMath& faceMath = renderClipnodes[modelIdx].faceMaths[hullIdx][i];

			float t = pickInfo.bestDist;
			if (pickFaceMath(start, dir, faceMath, t)) {
				foundBetterPick = true;
				pickInfo.bestDist = t;
				pickInfo.faceIdx = -1;
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, float& bestDist) {
	float dot = dotProduct(dir, faceMath.normal);
	if (dot >= 0) {
		return false; // don't select backfaces or parallel faces
	}

	float t = dotProduct((faceMath.normal * faceMath.fdist) - start, faceMath.normal) / dot;
	if (t < 0 || t >= bestDist) {
		return false; // intersection behind camera, or not a better pick
	}

	// transform intersection point to the plane's coordinate system
	vec3 intersection = start + dir * t;
	vec2 localRayPoint = (faceMath.worldToLocal * vec4(intersection, 1)).xy();

	// check if point is inside the polygon using the plane's 2D coordinate system
	if (!pointInsidePolygon(faceMath.localVerts, localRayPoint)) {
		return false;
	}

	bestDist = t;
	g_app->debugVec0 = intersection;

	return true;
}

int BspRenderer::getBestClipnodeHull(int modelIdx) {
	if (!clipnodesLoaded) {
		return -1;
	}

	RenderClipnodes& clip = renderClipnodes[modelIdx];

	// prefer hull that most closely matches the object size from a player's perspective
	if (clip.clipnodeBuffer[0]) {
		return 0;
	}
	else if (clip.clipnodeBuffer[3]) {
		return 3;
	}
	else if (clip.clipnodeBuffer[1]) {
		return 1;
	}
	else if (clip.clipnodeBuffer[2]) {
		return 2;
	}
	
	return -1;
}
