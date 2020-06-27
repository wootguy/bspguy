#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include <algorithm>
#include "Renderer.h"

BspRenderer::BspRenderer(Bsp* map, ShaderProgram* bspShader, ShaderProgram* colorShader, PointEntRenderer* pointEntRenderer) {
	this->map = map;
	this->bspShader = bspShader;
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

	*((COLOR3*)(whiteTex->data)) = { 255, 255, 255 };
	*((COLOR3*)(redTex->data)) = { 110, 0, 0 };
	*((COLOR3*)(yellowTex->data)) = { 255, 255, 0 };
	*((COLOR3*)(greyTex->data)) = { 64, 64, 64 };
	*((COLOR3*)(blackTex->data)) = { 0, 0, 0 };

	whiteTex->upload();
	redTex->upload();
	yellowTex->upload();
	greyTex->upload();
	blackTex->upload();

	//loadTextures();
	//loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();

	bspShader->bind();

	uint sTexId = glGetUniformLocation(bspShader->ID, "sTex");

	glUniform1i(sTexId, 0);

	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		uint sLightmapTexIds = glGetUniformLocation(bspShader->ID, ("sLightmapTex" + to_string(s)).c_str());

		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds, s + 1);
	}

	lightmapFuture = async(launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
}

void BspRenderer::loadTextures() {
	deleteTextures();

	vector<Wad*> wads;
	vector<string> wadNames;
	for (int i = 0; i < map->ents.size(); i++) {
		if (map->ents[i]->keyvalues["classname"] == "worldspawn") {
			wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	vector<string> tryPaths = {
		"./",
		g_settings.gamedir + "/svencoop/",
		g_settings.gamedir + "/svencoop_addon/",
		g_settings.gamedir + "/svencoop_downloads/",
		g_settings.gamedir + "/svencoop_hd/",
	};

	
	for (int i = 0; i < wadNames.size(); i++) {
		string path;
		for (int k = 0; k < tryPaths.size(); k++) {
			string tryPath = tryPaths[k] + wadNames[i];
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
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

		COLOR3* palette;
		byte* src;
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
				glTexturesSwap[i] = whiteTex;
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

		for (int k = 0; k < sz; k++) {
			imageData[k] = palette[src[k]];
		}

		if (wadTex) {
			delete wadTex;
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTexturesSwap[i] = new Texture(tex.nWidth, tex.nHeight, imageData);
	}

	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}

	if (wadTexCount)
		logf("Loaded %d wad textures\n", wadTexCount);
	if (embedCount)
		logf("Loaded %d embedded textures\n", embedCount);
	if (missingCount)
		logf("%d missing textures\n", missingCount);
}

void BspRenderer::reloadTextures() {
	texturesLoaded = false;
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
}

void BspRenderer::loadLightmaps() {
	vector<LightmapNode*> atlases;
	vector<Texture*> atlasTextures;
	atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	memset(atlasTextures[0]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

	numRenderLightmapInfos = map->faceCount;
	lightmaps = new LightmapInfo[map->faceCount];
	memset(lightmaps, 0, map->faceCount * sizeof(LightmapInfo));

	logf("Calculating lightmaps\n");
	qrad_init_globals(map);

	int lightmapCount = 0;
	int atlasId = 0;
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL) || face.nLightmapOffset >= map->header.lump[LUMP_LIGHTING].nLength)
			continue;

		int size[2];
		int dummy[2];
		int imins[2];
		int imaxs[2];
		GetFaceLightmapSize(i, size);
		GetFaceExtents(i, imins, imaxs);

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
			COLOR3* lightSrc = (COLOR3*)(map->lightdata + face.nLightmapOffset + s * lightmapSz);
			COLOR3* lightDst = (COLOR3*)(atlasTextures[atlasId]->data);
			for (int y = 0; y < info.h; y++) {
				for (int x = 0; x < info.w; x++) {
					int src = y * info.w + x;
					int dst = (info.y[s] + y) * LIGHTMAP_ATLAS_SIZE + info.x[s] + x;
					lightDst[dst] = lightSrc[src];
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
	logf("Fit %d lightmaps into %d atlases\n", lightmapCount, atlasId + 1);

	lightmapsGenerated = true;
	renderModelsSwap = genRenderFaces(numRenderModelsSwap);
}

void BspRenderer::updateLightmapInfos() {

	if (numRenderLightmapInfos == map->faceCount) {
		return;
	}
	if (map->faceCount < numRenderLightmapInfos) {
		logf("TODO: Recalculate lightmaps when faces deleted\n");
		return;
	}

	// assumes new faces have no light data
	int addedFaces = map->faceCount - numRenderLightmapInfos;

	LightmapInfo* newLightmaps = new LightmapInfo[map->faceCount];
	memcpy(newLightmaps, lightmaps, numRenderLightmapInfos * sizeof(LightmapInfo));
	memset(newLightmaps + numRenderLightmapInfos, 0, addedFaces*sizeof(LightmapInfo));

	logf("UPDATE FACE COUNT %d -> %d\n", numRenderLightmapInfos, map->faceCount);

	delete[] lightmaps;
	lightmaps = newLightmaps;
	numRenderLightmapInfos = map->faceCount;
}

void BspRenderer::preRenderFaces() {
	deleteRenderFaces();

	renderModels = genRenderFaces(numRenderModels);

	for (int i = 0; i < numRenderModels; i++) {
		RenderModel& model = renderModels[i];
		for (int k = 0; k < model.groupCount; k++) {
			model.renderGroups[k].buffer->upload();
			model.renderGroups[k].wireframeBuffer->upload();
		}
	}
}

RenderModel* BspRenderer::genRenderFaces(int& renderModelCount) {
	RenderModel* newRenderModels = new RenderModel[map->modelCount];
	renderModelCount = map->modelCount;

	int worldRenderGroups = 0;
	int modelRenderGroups = 0;

	for (int m = 0; m < map->modelCount; m++) {
		int groupCount = refreshModel(m, &newRenderModels[m]);
		if (m == 0)
			worldRenderGroups += groupCount;
		else
			modelRenderGroups += groupCount;
	}

	logf("Created %d solid render groups (%d world, %d entity)\n", 
		worldRenderGroups + modelRenderGroups,
		worldRenderGroups,
		modelRenderGroups);

	return newRenderModels;
}

void BspRenderer::deleteRenderFaces() {
	if (renderModels != NULL) {
		for (int i = 0; i < numRenderModels; i++) {
			for (int k = 0; k < renderModels[i].groupCount; k++) {
				RenderGroup& group = renderModels[i].renderGroups[k];
				delete[] group.verts;
				delete[] group.wireframeVerts;
				delete group.buffer;
				delete group.wireframeBuffer;
			}
			delete[] renderModels[i].renderGroups;
		}
		delete[] renderModels;
	}

	renderModels = NULL;
}

void BspRenderer::deleteTextures() {
	if (glTextures != NULL) {
		for (int i = 0; i < map->textureCount; i++) {
			if (glTextures[i] != whiteTex)
				delete glTextures[i];
		}
		delete[] glTextures;
	}

	glTextures = NULL;
}

int BspRenderer::refreshModel(int modelIdx, RenderModel* renderModel) {
	BSPMODEL& model = map->models[modelIdx];
	if (renderModel == NULL) {
		renderModel = &renderModels[modelIdx];
	}

	vector<RenderGroup> renderGroups;
	vector<vector<lightmapVert>> renderGroupVerts;
	vector<vector<lightmapVert>> renderGroupWireframeVerts;

	for (int i = 0; i < model.nFaces; i++) {
		int faceIdx = model.iFirstFace + i;
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

		LightmapInfo* lmap = lightmapsGenerated ? &lightmaps[faceIdx] : NULL;

		lightmapVert* verts = new lightmapVert[face.nEdges];
		int vertCount = face.nEdges;
		Texture* lightmapAtlas[MAXLIGHTMAPS];

		float tw = 1.0f / (float)tex.nWidth;
		float th = 1.0f / (float)tex.nHeight;

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

			// texture coords
			float fU = dotProduct(texinfo.vS, vert) + texinfo.shiftS;
			float fV = dotProduct(texinfo.vT, vert) + texinfo.shiftT;
			verts[e].u = fU * tw;
			verts[e].v = fV * th;
			verts[e].opacity = isSpecial ? 0.5f : 1.0f;

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
		int newCount = face.nEdges + max(0, face.nEdges - 3) * 2;
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
		for (int k = 0; k < face.nEdges; k++) {
			wireframeVerts[idx++] = verts[k];
			wireframeVerts[idx++] = verts[(k + 1) % face.nEdges];
		}
		for (int k = 0; k < wireframeVertCount; k++) {
			wireframeVerts[k].luv[0][2] = 1.0f;
			wireframeVerts[k].luv[1][2] = 0.0f;
			wireframeVerts[k].luv[2][2] = 0.0f;
			wireframeVerts[k].luv[3][2] = 0.0f;
			wireframeVerts[k].opacity = 1.0f;
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
			renderGroupVerts.push_back(vector<lightmapVert>());
			renderGroupWireframeVerts.push_back(vector<lightmapVert>());
			groupIdx = renderGroups.size() - 1;
		}

		for (int k = 0; k < vertCount; k++)
			renderGroupVerts[groupIdx].push_back(verts[k]);
		for (int k = 0; k < wireframeVertCount; k++) {
			renderGroupWireframeVerts[groupIdx].push_back(wireframeVerts[k]);
		}

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

		renderGroups[i].buffer = new VertexBuffer(bspShader, 0);
		renderGroups[i].buffer->addAttribute(TEX_2F, "vTex");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
		renderGroups[i].buffer->addAttribute(1, GL_FLOAT, 0, "vOpacity");
		renderGroups[i].buffer->addAttribute(POS_3F, "vPosition");
		renderGroups[i].buffer->setData(renderGroups[i].verts, renderGroups[i].vertCount);

		renderGroups[i].wireframeBuffer = new VertexBuffer(bspShader, 0);
		renderGroups[i].wireframeBuffer->addAttribute(TEX_2F, "vTex");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
		renderGroups[i].wireframeBuffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
		renderGroups[i].wireframeBuffer->addAttribute(1, GL_FLOAT, 0, "vOpacity");
		renderGroups[i].wireframeBuffer->addAttribute(POS_3F, "vPosition");
		renderGroups[i].wireframeBuffer->setData(renderGroups[i].wireframeVerts, renderGroups[i].wireframeVertCount);

		renderModel->renderGroups[i] = renderGroups[i];
	}

	for (int i = 0; i < model.nFaces; i++) {
		refreshFace(model.iFirstFace + i);
	}
	return renderModel->groupCount;
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

	for (int i = 1; i < map->ents.size(); i++) {
		refreshEnt(i);

		if (!map->ents[i]->isBspModel()) {
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

	pointEnts = new VertexBuffer(colorShader, COLOR_3B | POS_3F, entCubes, numPointEnts * 6 * 6);
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
	if (faceMaths != NULL) {
		for (int i = 0; i < numFaceMaths; i++) {
			delete[] faceMaths[i].verts;
		}
		delete[] faceMaths;
	}

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

	faceMath.verts = new vec3[face.nEdges];
	faceMath.vertCount = face.nEdges;

	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = map->edges[abs(edgeIdx)];
		int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];
		faceMath.verts[e] = map->verts[vertIdx];
	}

	vec3 plane_x = (faceMath.verts[1] - faceMath.verts[0]).normalize(1.0f);
	vec3 plane_y = crossProduct(planeNormal, plane_x).normalize(1.0f);
	vec3 plane_z = planeNormal;

	faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);
}

BspRenderer::~BspRenderer() {
	for (int i = 0; i < map->textureCount; i++) {
		delete glTextures[i];
	}
	delete[] glTextures;

	// TODO: more stuff to delete
}

void BspRenderer::delayLoadData() {
	if (!lightmapsUploaded && lightmapFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {

		for (int i = 0; i < numLightmapAtlases; i++) {
			glLightmapTextures[i]->upload();
		}

		deleteRenderFaces();

		renderModels = renderModelsSwap;
		numRenderModels = numRenderModelsSwap;

		for (int i = 0; i < numRenderModels; i++) {
			RenderModel& model = renderModels[i];
			for (int k = 0; k < model.groupCount; k++) {
				model.renderGroups[k].buffer->upload();
				model.renderGroups[k].wireframeBuffer->upload();
			}
		}

		lightmapsUploaded = true;
	}

	if (!texturesLoaded && texturesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		deleteTextures();
		
		glTextures = glTexturesSwap;

		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload();
		}

		texturesLoaded = true;

		preRenderFaces();
	}
}

bool BspRenderer::isFinishedLoading() {
	return lightmapsUploaded && texturesLoaded;
}

void BspRenderer::render(int highlightEnt, bool highlightAlwaysOnTop) {
	BSPMODEL& world = map->models[0];	

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// draw highlighted ent first so other ent edges don't overlap the highlighted edges
	if (highlightEnt > 0 && !highlightAlwaysOnTop) {
		if (renderEnts[highlightEnt].modelIdx >= 0) {
			bspShader->pushMatrix(MAT_MODEL);
			*bspShader->modelMat = renderEnts[highlightEnt].modelMat;
			bspShader->updateMatrixes();

			drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
			drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);

			bspShader->popMatrix(MAT_MODEL);
		}
	}

	for (int pass = 0; pass < 2; pass++) {
		bool drawTransparentFaces = pass == 1;

		drawModel(0, drawTransparentFaces, false, false);

		for (int i = 0, sz = map->ents.size(); i < sz; i++) {
			if (renderEnts[i].modelIdx >= 0) {
				bspShader->pushMatrix(MAT_MODEL);
				*bspShader->modelMat = renderEnts[i].modelMat;
				bspShader->updateMatrixes();

				drawModel(renderEnts[i].modelIdx, drawTransparentFaces, i == highlightEnt, false);

				bspShader->popMatrix(MAT_MODEL);
			}
		}

		if ((g_render_flags & RENDER_POINT_ENTS) && pass == 0) {
			drawPointEntities(highlightEnt);
		}
	}

	if (highlightEnt > 0 && highlightAlwaysOnTop) {
		if (renderEnts[highlightEnt].modelIdx >= 0) {
			bspShader->pushMatrix(MAT_MODEL);
			*bspShader->modelMat = renderEnts[highlightEnt].modelMat;
			bspShader->updateMatrixes();

			glDisable(GL_DEPTH_TEST);
			drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
			drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);
			glEnable(GL_DEPTH_TEST);

			bspShader->popMatrix(MAT_MODEL);
		}
	}

	delayLoadData();
}

void BspRenderer::drawModel(int modelIdx, bool transparent, bool highlight, bool edgesOnly) {

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
			else
				greyTex->bind();
			glActiveTexture(GL_TEXTURE1);
			whiteTex->bind();

			rgroup.wireframeBuffer->draw(GL_LINES);
		}


		glActiveTexture(GL_TEXTURE0);
		if (g_render_flags & RENDER_TEXTURES) {
			rgroup.texture->bind();
		}
		else {
			whiteTex->bind();
		}

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			glActiveTexture(GL_TEXTURE1 + s);

			if (highlight) {
				redTex->bind();
			}
			else if (lightmapsUploaded && g_render_flags & RENDER_LIGHTMAPS) {
				rgroup.lightmapAtlas[s]->bind();
			}
			else {
				if (s == 0) {
					whiteTex->bind();
				}
				else {
					blackTex->bind();
				}
			}
		}

		rgroup.buffer->draw(GL_TRIANGLES);
	}
}

void BspRenderer::drawPointEntities(int highlightEnt) {

	colorShader->bind();

	if (highlightEnt <= 0 || highlightEnt >= map->ents.size()) {
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

bool BspRenderer::pickPoly(vec3 start, vec3 dir, PickInfo& pickInfo) {
	bool foundBetterPick = false;

	if (pickPoly(start, dir, vec3(0, 0, 0), 0, pickInfo)) {
		pickInfo.entIdx = 0;
		pickInfo.modelIdx = 0;
		pickInfo.map = map;
		pickInfo.ent = map->ents[0];
		foundBetterPick = true;
	}

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0) {

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

			if (pickPoly(start, dir, renderEnts[i].offset, renderEnts[i].modelIdx, pickInfo)) {
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
				pickInfo.valid = true;
				foundBetterPick = true;
			};
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickPoly(vec3 start, vec3 dir, vec3 offset, int modelIdx, PickInfo& pickInfo) {
	BSPMODEL& model = map->models[modelIdx];

	bool foundBetterPick = false;
	bool skipSpecial = !(g_render_flags & RENDER_SPECIAL);

	for (int k = 0; k < model.nFaces; k++) {
		FaceMath& faceMath = faceMaths[model.iFirstFace + k];
		BSPFACE& face = map->faces[model.iFirstFace + k];
		BSPPLANE& plane = map->planes[face.iPlane];
		vec3 planeNormal = faceMath.normal;
		float fDist = faceMath.fdist;
		
		if (skipSpecial && modelIdx == 0) {
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}
		}
		
		if (offset.x != 0 || offset.y != 0 || offset.z != 0) {
			vec3 newPlaneOri = offset + (planeNormal * fDist);
			fDist = dotProduct(planeNormal, newPlaneOri) / dotProduct(planeNormal, planeNormal);
		}

		float dot = dotProduct(dir, planeNormal);

		// don't select backfaces or parallel faces
		if (dot >= 0) {
			continue;
		}

		float t = dotProduct((planeNormal * fDist) - start, planeNormal) / dot;

		if (t < 0) {
			continue; // intersection behind camera
		}

		vec3 intersection = start + dir * t; // point where ray intersects the plane

		// transform to plane's coordinate system
		vec2 localRayPoint = (faceMath.worldToLocal * vec4(intersection, 1)).xy();

		static vec2 localVerts[128];
		for (int e = 0; e < faceMath.vertCount; e++) {
			localVerts[e] = (faceMath.worldToLocal * vec4(faceMath.verts[e] + offset, 1)).xy();
		}

		// check if point is inside the polygon using the plane's 2D coordinate system
		// https://stackoverflow.com/a/34689268
		bool inside = true;
		float lastd = 0;
		for (int i = 0; i < faceMath.vertCount; i++)
		{
			vec2& v1 = localVerts[i];
			vec2& v2 = localVerts[(i + 1) % faceMath.vertCount];

			if (v1.x == localRayPoint.x && v1.y == localRayPoint.y) {
				break; // on edge = inside
			}
			
			float d = (localRayPoint.x - v1.x) * (v2.y - v1.y) - (localRayPoint.y - v1.y) * (v2.x - v1.x);

			if ((d < 0 && lastd > 0) || (d > 0 && lastd < 0)) {
				// point is outside of this edge
				inside = false;
				break;
			}
			lastd = d;
		}
		if (!inside) {
			continue;
		}

		if (t < pickInfo.bestDist) {
			foundBetterPick = true;
			pickInfo.bestDist = t;
			pickInfo.faceIdx = model.iFirstFace + k;
			pickInfo.valid = true;
		}
	}

	return foundBetterPick;
}
