#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include "Renderer.h"
#include "Clipper.h"
#include "Polygon3D.h"
#include "NavMeshGenerator.h"
#include "LeafNavMeshGenerator.h"
#include "PointEntRenderer.h"
#include "Texture.h"
#include "LightmapNode.h"
#include "Bsp.h"
#include "NavMesh.h"
#include "Entity.h"
#include "Wad.h"
#include "util.h"
#include "ShaderProgram.h"
#include "globals.h"
#include <iomanip>
#include <set>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <float.h>
#include "MdlRenderer.h"


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
		uint sLightmapTexIds = glGetUniformLocation(bspShader->ID, ("sLightmapTex" + to_string(s)).c_str());
		
		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds, s + 1);
	}

	fullBrightBspShader->bind();

	uint sTexId2 = glGetUniformLocation(fullBrightBspShader->ID, "sTex");
	glUniform1i(sTexId2, 0);

	colorShaderMultId = glGetUniformLocation(colorShader->ID, "colorMult");

	numRenderClipnodes = map->modelCount;
	lightmapFuture = async(launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this);

	// cache ent targets so first selection doesn't lag
	for (int i = 0; i < map->ents.size(); i++) {
		map->ents[i]->getTargets();
	}

	//write_obj_file();
}

void BspRenderer::loadTextures() {
	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
	wads.clear();

	vector<string> wadNames = map->get_wad_names();
	vector<string> tryPaths = getAssetPaths();

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

		if (g_verbose)
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
		
		for (int k = 0; k < sz; k++) {
			imageData[k] = palette[src[k]];
		}

		if (wadTex) {
			delete[] wadTex->data;
			delete wadTex;
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTexturesSwap[i] = new Texture(tex.nWidth, tex.nHeight, imageData);
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
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
}

void BspRenderer::reloadLightmaps() {
	lightmapsGenerated = false;
	lightmapsUploaded = false;
	deleteLightmapTextures();
	if (lightmaps != NULL) {
		delete[] lightmaps;
	}
	lightmapFuture = async(launch::async, &BspRenderer::loadLightmaps, this);
}

void BspRenderer::reloadClipnodes() {
	clipnodesLoaded = false;
	clipnodeLeafCount = 0;

	deleteRenderClipnodes();

	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this);
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
	vector<LightmapNode*> atlases;
	vector<Texture*> atlasTextures;
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
		int dummy[2];
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
	BSPMODEL& model = map->models[modelIdx];
	RenderModel* renderModel = &renderModels[modelIdx];

	deleteRenderModel(renderModel);
	
	renderModel->renderFaces = new RenderFace[model.nFaces];

	vector<RenderGroup> renderGroups;
	vector<vector<lightmapVert>> renderGroupVerts;
	vector<vector<lightmapVert>> renderGroupWireframeVerts;

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
		int newCount = face.nEdges + max(0, face.nEdges - 3) * 2;
		int wireframeVertCount = face.nEdges * 2;
		lightmapVert* newVerts = new lightmapVert[newCount];
		lightmapVert* wireframeVerts = new lightmapVert[wireframeVertCount];

		int idx = 0;
		for (int k = 2; k < face.nEdges; k++) {
			// reverse order due to coordinate system swap
			newVerts[idx++] = verts[k];
			newVerts[idx++] = verts[k - 1];
			newVerts[idx++] = verts[0];	
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
			renderGroupVerts.push_back(vector<lightmapVert>());
			renderGroupWireframeVerts.push_back(vector<lightmapVert>());
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

	if (refreshClipnodes) {
		generateClipnodeBuffer(modelIdx);
	}

	return renderModel->groupCount;
}

void BspRenderer::write_obj_file() {
	int modelIdx = 0;
	BSPMODEL& model = map->models[modelIdx];
	vector<vec3> allVerts;

	for (int i = 0; i < model.nFaces; i++) {
		int faceIdx = model.iFirstFace + i;
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (texinfo.nFlags & TEX_SPECIAL) {
			continue;
		}

		vec3* verts = new vec3[face.nEdges];
		int vertCount = face.nEdges;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3& vert = map->verts[vertIdx];
			verts[e].x = vert.x;
			verts[e].y = vert.z;
			verts[e].z = -vert.y;
		}

		// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
		int idx = 0;
		for (int k = 2; k < face.nEdges; k++) {
			allVerts.push_back(verts[0]);
			allVerts.push_back(verts[k - 1]);
			allVerts.push_back(verts[k]);
		}
	}

	ofstream file(map->name + ".obj", ios::out | ios::trunc);
	for (int i = 0; i < allVerts.size(); i++) {
		vec3 v = allVerts[i];
		file << "v " << fixed << std::setprecision(2) << v.x << " " << v.y << " " << v.z << endl;
	}
	for (int i = 0; i < allVerts.size(); i += 3) {
		file << "f " << (i+3) << " " << (i+2) << " " << (i+1) << endl;
	}
	logf("Wrote %d verts\n", allVerts.size());
	file.close();
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

void BspRenderer::generateNavMeshBuffer() {
	int hull = 3;
	RenderClipnodes* renderClip = &renderClipnodes[0];
	renderClip->clipnodeBuffer[hull] = NULL;
	renderClip->wireframeClipnodeBuffer[hull] = NULL;

	NavMesh* navMesh = NavMeshGenerator().generate(map, hull);
	vector<Polygon3D> navPolys = navMesh->getPolys();

	g_app->debugNavMesh = navMesh;
	g_app->debugNavPoly = 529;
	debugNavMesh = navMesh;
	debugFaces = navPolys;

	static COLOR4 hullColors[] = {
		COLOR4(255, 255, 255, 128),
		COLOR4(96, 255, 255, 128),
		COLOR4(255, 96, 255, 128),
		COLOR4(255, 255, 96, 128),
	};
	COLOR4 color = hullColors[hull];

	vector<cVert> allVerts;
	vector<cVert> wireframeVerts;
	vector<FaceMath> faceMaths;

	for (int m = 0; m < navPolys.size(); m++) {
		Polygon3D& poly = navPolys[m];

		vec3 normal = poly.plane_z;

		// calculations for face picking
		{
			FaceMath faceMath;
			faceMath.plane_x = poly.plane_x;
			faceMath.plane_y = poly.plane_y;
			faceMath.plane_z = poly.plane_z;
			faceMath.fdist = poly.fdist;
			faceMath.worldToLocal = poly.worldToLocal;
			faceMath.verts = poly.verts;
			faceMath.localVerts = poly.localVerts;
			faceMaths.push_back(faceMath);
		}

		// create the verts for rendering
		{
			vector<vec3> renderVerts;
			renderVerts.resize(poly.verts.size());
			for (int i = 0; i < poly.verts.size(); i++) {
				renderVerts[i] = poly.verts[i].flip();
			}

			COLOR4 wireframeColor = { 0, 0, 0, 255 };
			for (int k = 0; k < renderVerts.size(); k++) {
				wireframeVerts.push_back(cVert(renderVerts[k], wireframeColor));
				wireframeVerts.push_back(cVert(renderVerts[(k + 1) % renderVerts.size()], wireframeColor));
			}

			vec3 lightDir = vec3(1, 1, -1).normalize();
			float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
			if (dot > 0.5f) {
				dot = dot * dot;
			}
			color = hullColors[hull];
			if (normal.z < -0.8 || true) {
				static int r = 0;
				r = (r + 1) % 8;
				if (r == 0) {
					color = COLOR4(255, 32, 32, 255);
				}
				else if (r == 1) {
					color = COLOR4(255, 255, 32, 255);
				}
				else if (r == 2) {
					color = COLOR4(255, 32, 255, 255);
				}
				else if (r == 3) {
					color = COLOR4(255, 128, 255, 255);
				}
				else if (r == 4) {
					color = COLOR4(32, 32, 255, 255);
				}
				else if (r == 5) {
					color = COLOR4(32, 255, 255, 255);
				}
				else if (r == 6) {
					color = COLOR4(32, 128, 255, 255);
				}
				else if (r == 7) {
					color = COLOR4(32, 255, 128, 255);
				}
			}
			COLOR4 faceColor = color * (dot);

			// convert from TRIANGLE_FAN style verts to TRIANGLES
			for (int k = 2; k < renderVerts.size(); k++) {
				allVerts.push_back(cVert(renderVerts[0], faceColor));
				allVerts.push_back(cVert(renderVerts[k - 1], faceColor));
				allVerts.push_back(cVert(renderVerts[k], faceColor));
			}
		}
	}

	cVert* output = new cVert[allVerts.size()];
	for (int i = 0; i < allVerts.size(); i++) {
		output[i] = allVerts[i];
	}

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	for (int i = 0; i < wireframeVerts.size(); i++) {
		wireOutput[i] = wireframeVerts[i];
	}

	if (allVerts.size() == 0 || wireframeVerts.size() == 0) {
		renderClip->clipnodeBuffer[hull] = NULL;
		renderClip->wireframeClipnodeBuffer[hull] = NULL;
		return;
	}

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(colorShader, COLOR_4B | POS_3F, output, allVerts.size());
	renderClip->clipnodeBuffer[hull]->ownData = true;

	renderClip->wireframeClipnodeBuffer[hull] = new VertexBuffer(colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());
	renderClip->wireframeClipnodeBuffer[hull]->ownData = true;

	renderClip->faceMaths[hull] = faceMaths;

	ofstream file(map->name + "_hull" + to_string(hull) + ".obj", ios::out | ios::trunc);
	for (int i = 0; i < allVerts.size(); i++) {
		vec3 v = vec3(allVerts[i].x, allVerts[i].y, allVerts[i].z);
		file << "v " << fixed << std::setprecision(2) << v.x << " " << v.y << " " << v.z << endl;
	}
	for (int i = 0; i < allVerts.size(); i += 3) {
		file << "f " << (i + 1) << " " << (i + 2) << " " << (i + 3) << endl;
	}
	logf("Wrote %d verts\n", allVerts.size());
	file.close();
}

void BspRenderer::generateSingleLeafNavMeshBuffer(LeafNode* node) {
	COLOR4 color;
	int r = (node->id*13) % 8;

	if (r == 0) { color = COLOR4(255, 32, 32, 128); }
	else if (r == 1) { color = COLOR4(255, 255, 32, 128); }
	else if (r == 2) { color = COLOR4(255, 32, 255, 128); }
	else if (r == 3) { color = COLOR4(255, 128, 255, 128); }
	else if (r == 4) { color = COLOR4(32, 32, 255, 128); }
	else if (r == 5) { color = COLOR4(32, 255, 255, 128); }
	else if (r == 6) { color = COLOR4(32, 128, 255, 128); }
	else if (r == 7) { color = COLOR4(32, 255, 128, 128); }

	color.a = 64;

	vector<cVert> allVerts;
	vector<cVert> wireframeVerts;

	LeafNode& mesh = *node;

	for (int m = 0; m < mesh.leafFaces.size(); m++) {
		Polygon3D& poly = mesh.leafFaces[m];

		vec3 normal = poly.plane_z;

		// create the verts for rendering
		vector<vec3> renderVerts;
		renderVerts.resize(poly.verts.size());
		for (int i = 0; i < poly.verts.size(); i++) {
			renderVerts[i] = poly.verts[i].flip();
		}

		COLOR4 wireframeColor = { 0, 0, 0, 255 };
		for (int k = 0; k < renderVerts.size(); k++) {
			wireframeVerts.push_back(cVert(renderVerts[k], wireframeColor));
			wireframeVerts.push_back(cVert(renderVerts[(k + 1) % renderVerts.size()], wireframeColor));
		}

		const vec3 lightDir = vec3(1, 1, -1).normalize();
		float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
		if (dot > 0.5f) {
			dot = dot * dot;
		}
		COLOR4 faceColor = color * (dot);

		// convert from TRIANGLE_FAN style verts to TRIANGLES
		for (int k = 2; k < renderVerts.size(); k++) {
			allVerts.push_back(cVert(renderVerts[0], faceColor));
			allVerts.push_back(cVert(renderVerts[k - 1], faceColor));
			allVerts.push_back(cVert(renderVerts[k], faceColor));
		}
	}

	if (allVerts.size() == 0 || wireframeVerts.size() == 0) {
		return;
	}

	cVert* output = new cVert[allVerts.size()];
	for (int i = 0; i < allVerts.size(); i++) {
		output[i] = allVerts[i];
	}

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	for (int i = 0; i < wireframeVerts.size(); i++) {
		wireOutput[i] = wireframeVerts[i];
	}

	if (node->face_buffer) {
		delete node->face_buffer;
		delete node->wireframe_buffer;
	}

	node->face_buffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, output, allVerts.size());
	node->wireframe_buffer = new VertexBuffer(colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());

	node->face_buffer->ownData = true;
	node->wireframe_buffer->ownData = true;
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
		vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, i, CONTENTS_SOLID);

		vector<CMesh> solidMeshes;
		for (int k = 0; k < solidNodes.size(); k++) {
			solidMeshes.push_back(clipper.clip(solidNodes[k].cuts));
		}
		
		static COLOR4 hullColors[] = {
			COLOR4(255, 255, 255, 128),
			COLOR4(96, 255, 255, 128),
			COLOR4(255, 96, 255, 128),
			COLOR4(255, 255, 96, 128),
		};
		COLOR4 color = hullColors[i];

		vector<cVert> allVerts;
		vector<cVert> wireframeVerts;
		vector<FaceMath> faceMaths;

		for (int m = 0; m < solidMeshes.size(); m++) {
			CMesh& mesh = solidMeshes[m];
			clipnodeLeafCount++;

			for (int i = 0; i < mesh.faces.size(); i++) {

				if (!mesh.faces[i].visible) {
					continue;
				}

				set<int> uniqueFaceVerts;

				for (int k = 0; k < mesh.faces[i].edges.size(); k++) {
					for (int v = 0; v < 2; v++) {
						int vertIdx = mesh.edges[mesh.faces[i].edges[k]].verts[v];
						if (!mesh.verts[vertIdx].visible) {
							continue;
						}
						uniqueFaceVerts.insert(vertIdx);
					}
				}

				vector<vec3> faceVerts;
				for (auto vertIdx : uniqueFaceVerts) {
					faceVerts.push_back(mesh.verts[vertIdx].pos);
				}

				sortPlanarVerts(faceVerts);

				if (faceVerts.size() < 3) {
					//logf("Degenerate clipnode face discarded\n");
					continue;
				}

				vec3 normal = getNormalFromVerts(faceVerts);

				if (dotProduct(mesh.faces[i].normal, normal) < 0) {
					reverse(faceVerts.begin(), faceVerts.end());
					normal = normal.invert();
				}

				// calculations for face picking
				{
					FaceMath faceMath;
					faceMath.plane_z = mesh.faces[i].normal;
					faceMath.fdist = getDistAlongAxis(mesh.faces[i].normal, faceVerts[0]);

					vec3 v0 = faceVerts[0];
					vec3 v1;
					bool found = false;
					for (int z = 1; z < faceVerts.size(); z++) {
						if (faceVerts[z] != v0) {
							v1 = faceVerts[z];
							found = true;
							break;
						}
					}
					if (!found) {
						logf("Failed to find non-duplicate vert for clipnode face\n");
					}

					vec3 plane_z = mesh.faces[i].normal;
					vec3 plane_x = faceMath.plane_x = (v1 - v0).normalize();
					vec3 plane_y = faceMath.plane_y = crossProduct(plane_z, plane_x).normalize();
					faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

					faceMath.verts = vector<vec3>(faceVerts.size());
					faceMath.localVerts = vector<vec2>(faceVerts.size());
					for (int k = 0; k < faceVerts.size(); k++) {
						faceMath.verts[k] = faceVerts[k];
						faceMath.localVerts[k] = (faceMath.worldToLocal * vec4(faceVerts[k], 1)).xy();
					}

					faceMaths.push_back(faceMath);
				}

				// create the verts for rendering
				{
					for (int i = 0; i < faceVerts.size(); i++) {
						faceVerts[i] = faceVerts[i].flip();
					}

					COLOR4 wireframeColor = { 0, 0, 0, 255 };
					for (int k = 0; k < faceVerts.size(); k++) {
						wireframeVerts.push_back(cVert(faceVerts[k], wireframeColor));
						wireframeVerts.push_back(cVert(faceVerts[(k + 1) % faceVerts.size()], wireframeColor));
					}

					vec3 lightDir = vec3(1, 1, -1).normalize();
					float dot = (dotProduct(normal*-1, lightDir) + 1) / 2.0f;
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
		for (int i = 0; i < allVerts.size(); i++) {
			output[i] = allVerts[i];
		}

		cVert* wireOutput = new cVert[wireframeVerts.size()];
		for (int i = 0; i < wireframeVerts.size(); i++) {
			wireOutput[i] = wireframeVerts[i];
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

	if (modelIdx == 0) {
		//generateNavMeshBuffer();
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
	renderEnts[entIdx].angles = vec3(0, 0, 0);
	renderEnts[entIdx].pointEntCube = pointEntRenderer->getEntCube(ent);
	ent->hasCachedMdl = false;
	ent->drawCached = false;

	if (ent->hasKey("origin")) {
		vec3 origin = parseVector(ent->keyvalues["origin"]);
		renderEnts[entIdx].modelMat.translate(origin.x, origin.z, -origin.y);
		renderEnts[entIdx].offset = origin;
	}
	if (ent->hasKey("angles")) {
		renderEnts[entIdx].angles = parseVector(ent->keyvalues["angles"]).flip() * (PI / 180.0f);
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

	faceMath.plane_z = planeNormal;
	faceMath.fdist = fDist;
	
	vector<vec3> allVerts(face.nEdges);
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

	vec3 plane_x = faceMath.plane_x = (v1 - allVerts[0]).normalize(1.0f);
	vec3 plane_y = faceMath.plane_y = crossProduct(planeNormal, plane_x).normalize(1.0f);
	vec3 plane_z = planeNormal;

	faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	faceMath.verts = vector<vec3>(allVerts.size());
	faceMath.localVerts = vector<vec2>(allVerts.size());
	for (int i = 0; i < allVerts.size(); i++) {
		faceMath.verts[i] = allVerts[i];
		faceMath.localVerts[i] = (faceMath.worldToLocal * vec4(allVerts[i], 1)).xy();
	}
}

BspRenderer::~BspRenderer() {
	if (lightmapFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		texturesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		clipnodesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready) {
		logf("ERROR: Deleted bsp renderer while it was loading\n");
	}

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
	if (!lightmapsUploaded && lightmapFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			glLightmapTextures[i]->upload(GL_RGB);
		}

		lightmapsGenerated = true;

		preRenderFaces();

		lightmapsUploaded = true;
	}
	else if (!texturesLoaded && texturesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		deleteTextures();
		
		glTextures = glTexturesSwap;

		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGB);
		}
		numLoadedTextures = map->textureCount;

		texturesLoaded = true;

		preRenderFaces();

		textureFacesLoaded = true;
	}

	if (!clipnodesLoaded && clipnodesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {

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
	return lightmapsUploaded && texturesLoaded && textureFacesLoaded && clipnodesLoaded ||
		map->ents.empty();
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

	if (texinfo.iMiptex > 0 && texinfo.iMiptex < numLoadedTextures)
		return glTextures[texinfo.iMiptex]->id;
	else
		return 0;
}

Texture* BspRenderer::uploadTexture(WADTEX* tex) {
	int lastMipSize = (tex->nWidth / 8) * (tex->nHeight / 8);
	COLOR3* palette = (COLOR3*)(tex->data + tex->nOffsets[3] + lastMipSize + 2 - 40);
	byte* src = tex->data;

	COLOR3* imageData = new COLOR3[tex->nWidth * tex->nHeight];

	int sz = tex->nWidth * tex->nHeight;

	for (int k = 0; k < sz; k++) {
		imageData[k] = palette[src[k]];
	}

	Texture* newTex = new Texture(tex->nWidth, tex->nHeight, imageData);
	newTex->upload(GL_RGB);

	return newTex;
}

void BspRenderer::loadTexture(WADTEX* tex) {
	Texture** newTextures = new Texture*[numLoadedTextures + 1];
	memcpy(newTextures, glTextures, sizeof(Texture*) * numLoadedTextures);

	newTextures[numLoadedTextures++] = uploadTexture(tex);

	delete[] glTextures;
	glTextures = newTextures;
}

void BspRenderer::render(const vector<int>& highlightedEnts, bool highlightAlwaysOnTop, int clipnodeHull, bool transparencyPass) {
	if (map->ents.empty())
		return;
	
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

	std::unordered_set<int> highlighted;
	for (int highlightEnt : highlightedEnts) {
		highlighted.insert(highlightEnt);
	}

	static float lol = 0;
	lol += 0.001f;

	// draw highlighted ent first so other ent edges don't overlap the highlighted edges
	if (highlightedEnts.size() && !highlightAlwaysOnTop && !transparencyPass) {
		for (int highlightEnt : highlightedEnts) {
			if (renderEnts[highlightEnt].modelIdx >= 0 && renderEnts[highlightEnt].modelIdx < map->modelCount) {				
				activeShader->pushMatrix(MAT_MODEL);
				*activeShader->modelMat = renderEnts[highlightEnt].modelMat;
				*activeShader->modelMat = *activeShader->modelMat * map->ents[highlightEnt]->getRotationMatrix(false);
				activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				activeShader->updateMatrixes();

				drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
				drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);

				activeShader->popMatrix(MAT_MODEL);
			}
		}
	}

	for (int pass = 0; pass < 2; pass++) {
		bool drawTransparentFaces = pass == 1;
		if (drawTransparentFaces != transparencyPass) {
			continue;
		}

		drawModel(0, drawTransparentFaces, false, false);

		for (int i = 0, sz = map->ents.size(); i < sz; i++) {
			if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount) {
				bool isHighlighted = highlighted.count(i);
				activeShader->pushMatrix(MAT_MODEL);
				*activeShader->modelMat = renderEnts[i].modelMat;
				*activeShader->modelMat = *activeShader->modelMat * map->ents[i]->getRotationMatrix(false);
				activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				activeShader->updateMatrixes();

				drawModel(renderEnts[i].modelIdx, drawTransparentFaces, isHighlighted, false);

				activeShader->popMatrix(MAT_MODEL);
			}
		}

		if ((g_render_flags & RENDER_POINT_ENTS) && pass == 0) {
			drawPointEntities(highlightedEnts);
			activeShader->bind();
		}
	}

	if (clipnodesLoaded && transparencyPass) {
		colorShader->bind();

		if (g_render_flags & RENDER_WORLD_CLIPNODES && clipnodeHull != -1) {
			drawModelClipnodes(0, false, clipnodeHull);
		}

		if ((g_render_flags & RENDER_ENTS) && (g_render_flags & RENDER_ENT_CLIPNODES)) {
			for (int i = 0, sz = map->ents.size(); i < sz; i++) {
				if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount) {
					if (clipnodeHull == -1 && renderModels[renderEnts[i].modelIdx].groupCount > 0) {
						continue; // skip rendering for models that have faces, if in auto mode
					}
					bool isHighlighted = highlighted.count(i);
					colorShader->pushMatrix(MAT_MODEL);
					*colorShader->modelMat = renderEnts[i].modelMat;
					*colorShader->modelMat = *colorShader->modelMat * map->ents[i]->getRotationMatrix(false);
					colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
					colorShader->updateMatrixes();

					if (isHighlighted) {
						glUniform4f(colorShaderMultId, 1, 0.25f, 0.25f, 1);
					}

					drawModelClipnodes(renderEnts[i].modelIdx, false, clipnodeHull);

					if (isHighlighted) {
						glUniform4f(colorShaderMultId, 1, 1, 1, 1);
					}

					colorShader->popMatrix(MAT_MODEL);
				}
			}
		}		
	}

	if (!transparencyPass) {
		return;
	}

	if (highlightedEnts.size() && highlightAlwaysOnTop) {
		activeShader->bind();

		for (int highlightEnt : highlightedEnts) {
			glDisable(GL_DEPTH_TEST);
			if (renderEnts[highlightEnt].modelIdx >= 0 && renderEnts[highlightEnt].modelIdx < map->modelCount) {
				activeShader->pushMatrix(MAT_MODEL);
				*activeShader->modelMat = renderEnts[highlightEnt].modelMat;
				activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				activeShader->updateMatrixes();
				
				drawModel(renderEnts[highlightEnt].modelIdx, false, true, true);
				drawModel(renderEnts[highlightEnt].modelIdx, true, true, true);
				
				activeShader->popMatrix(MAT_MODEL);
			}
			glEnable(GL_DEPTH_TEST);
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

void BspRenderer::drawPointEntities(const vector<int>& highlightedEnts) {
	vec3 renderOffset = mapOffset.flip();

	colorShader->bind();

	if (highlightedEnts.empty() && !(g_render_flags & RENDER_STUDIO_MDL)) {
		if (pointEnts->numVerts > 0)
			pointEnts->draw(GL_TRIANGLES);
		return;
	}

	int pointEntIdx = 0;
	int nextRangeDrawIdx = 0; // starting index for the next range draw

	std::unordered_set<int> highlighted;
	for (int highlightEnt : highlightedEnts) {
		highlighted.insert(highlightEnt);
	}

	const int cubeVerts = 6 * 6;

	// skip worldspawn
	for (int i = 1, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0)
			continue;

		if (highlighted.count(i) || map->ents[i]->didStudioDraw) {
			if (pointEntIdx - nextRangeDrawIdx > 0) {
				pointEnts->drawRange(GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
			}
			nextRangeDrawIdx = pointEntIdx+1;

			if (!map->ents[i]->didStudioDraw) {
				colorShader->pushMatrix(MAT_MODEL);
				*colorShader->modelMat = renderEnts[i].modelMat;
				colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				colorShader->updateMatrixes();

				renderEnts[i].pointEntCube->selectBuffer->draw(GL_TRIANGLES);
				renderEnts[i].pointEntCube->buffer->draw(GL_TRIANGLES);
				renderEnts[i].pointEntCube->wireframeBuffer->draw(GL_LINES);

				colorShader->popMatrix(MAT_MODEL);
			}
		}

		pointEntIdx++;
	}

	if (pointEntIdx - nextRangeDrawIdx > 0) {
		pointEnts->drawRange(GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
	}
}

bool BspRenderer::pickPoly(vec3 start, vec3 dir, int hullIdx, int& entIdx, int& faceIdx) {
	bool foundBetterPick = false;
	float bestDist = FLT_MAX;
	entIdx = -1;
	faceIdx = -1;

	start -= mapOffset;

	if (!map || map->ents.size() == 0)
	{
		return false;
	}

	if (pickModelPoly(start, dir, vec3(), vec3(), 0, hullIdx, 0, faceIdx, bestDist)) {
		entIdx = 0;
		foundBetterPick = true;
	}

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		Entity* ent = map->ents[i];

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

			vec3 angles = map->ents[i]->canRotate() ? renderEnts[i].angles : vec3();
			if (pickModelPoly(start, dir, renderEnts[i].offset, angles,
				renderEnts[i].modelIdx, hullIdx, i, faceIdx, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
		}
		else if (i > 0 && g_render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;

			if (pickAABB(start, dir, mins, maxs, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
			else if (ent->cachedMdl && ent->cachedMdl->pick(start, dir, ent, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
		}
	}

	return foundBetterPick;
}

void rotateFaceMath(FaceMath& faceMath, mat4x4& rotation) {
	vec3 pointOnPlane = (faceMath.plane_z * faceMath.fdist);
	pointOnPlane = (rotation * vec4(pointOnPlane, 1)).xyz();
	faceMath.plane_x = (rotation * vec4(faceMath.plane_x, 1)).xyz();
	faceMath.plane_y = (rotation * vec4(faceMath.plane_y, 1)).xyz();
	faceMath.plane_z = (rotation * vec4(faceMath.plane_z, 1)).xyz();
	faceMath.fdist = dotProduct(faceMath.plane_z, pointOnPlane);
	faceMath.worldToLocal = worldToLocalTransform(faceMath.plane_x, faceMath.plane_y, faceMath.plane_z);

	faceMath.localVerts = vector<vec2>(faceMath.verts.size());
	for (int k = 0; k < faceMath.verts.size(); k++) {
		vec3 rotVert = (rotation * vec4(faceMath.verts[k], 1)).xyz();
		faceMath.localVerts[k] = (faceMath.worldToLocal * vec4(rotVert, 1)).xy();
	}
}

bool BspRenderer::pickModelPoly(vec3 start, vec3 dir, vec3 offset, vec3 rot, int modelIdx, int hullIdx,
	int testEntidx, int& faceIdx, float& bestDist) {
	BSPMODEL& model = map->models[modelIdx];

	start -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_render_flags & RENDER_SPECIAL);

	bool hasAngles = rot != vec3();
	mat4x4 angleTransform = map->ents[testEntidx]->getRotationMatrix(true);

	for (int k = 0; k < model.nFaces; k++) {
		FaceMath faceMath = faceMaths[model.iFirstFace + k];

		if (hasAngles) {
			rotateFaceMath(faceMath, angleTransform);
		}

		BSPFACE& face = map->faces[model.iFirstFace + k];
		
		if (skipSpecial && modelIdx == 0) {
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (info.nFlags & TEX_SPECIAL) {
				continue;
			}
		}

		float t = bestDist;
		if (pickFaceMath(start, dir, faceMath, t)) {
			foundBetterPick = true;
			bestDist = t;
			faceIdx = model.iFirstFace + k;
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
			FaceMath faceMath = renderClipnodes[modelIdx].faceMaths[hullIdx][i];

			if (hasAngles) {
				rotateFaceMath(faceMath, angleTransform);
			}

			float t = bestDist;
			if (pickFaceMath(start, dir, faceMath, t)) {
				foundBetterPick = true;
				bestDist = t;

				// Nav mesh WIP code
				if (g_app->debugNavMesh && modelIdx == 0 && hullIdx == 3) {
					static int lastPick = 0;
					
					g_app->debugPoly = debugFaces[i];
					g_app->debugNavPoly = i;

					//Polygon3D merged = debugFaces[lastPick].merge(debugFaces[i]);
					//vector<vector<vec3>> split = debugFaces[i].split(debugFaces[lastPick]);
					//logf("split %d by %d == %d\n", i, lastPick, split.size());

					NavNode& node = g_app->debugNavMesh->nodes[i];

					lastPick = i;
					logf("Picked hull %d, face %d, verts %d, area %.1f\nNav links %d\n", hullIdx, i, debugFaces[i].verts.size(), debugFaces[i].area, node.numLinks());
				}
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickFaceMath(vec3 start, vec3 dir, FaceMath& faceMath, float& bestDist) {
	float dot = dotProduct(dir, faceMath.plane_z);
	if (dot >= 0) {
		return false; // don't select backfaces or parallel faces
	}

	float t = dotProduct((faceMath.plane_z * faceMath.fdist) - start, faceMath.plane_z) / dot;
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


void PickInfo::selectEnt(int entIdx) {
	Bsp* map = getMap();

	if (entIdx >= 0 && entIdx < map->ents.size())
		ents.push_back(entIdx);
	else
		logf("Failed to select ent index out of range %d\n", entIdx);

	//logf("select ent %d\n", entIdx);
}

void PickInfo::selectFace(int faceIdx) {
	faces.push_back(faceIdx);
	//logf("select face %d\n", faceIdx);
}

void PickInfo::deselect() {
	ents.clear();
	faces.clear();
	//logf("Deselect\n");
}

void PickInfo::deselectEnt(int entIdx) {
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i] == entIdx) {
			ents.erase(ents.begin() + i);
			return;
		}
	}
}

void PickInfo::deselectFace(int faceIdx) {
	for (int i = 0; i < faces.size(); i++) {
		if (faces[i] == faceIdx) {
			faces.erase(faces.begin() + i);
			return;
		}
	}
}

Bsp* PickInfo::getMap() {
	return g_app->mapRenderer->map;
}

Entity* PickInfo::getEnt() {
	Bsp* map = getMap();
	int idx = getEntIndex();
	return idx != -1 ? map->ents[idx] : NULL;
}

int PickInfo::getEntIndex() {
	Bsp* map = getMap();
	if (ents.size() && map && ents[0] >= 0 && ents[0] < map->ents.size()) {
		return ents[0];
	}
	return -1;
}

int PickInfo::getModelIndex() {
	Bsp* map = getMap();
	int idx = getEntIndex();
	Entity* ent = idx != -1 ? map->ents[idx] : NULL;
	int faceIdx = faces.size() == 1 ? faces[0] : -1;

	if (idx == 0) {
		return 0;
	}
	else if (ent) {
		return ent->getBspModelIdx();
	}
	else if (faceIdx >= 0 && faceIdx < map->faceCount) {
		return map->get_model_from_face(faceIdx);
	}

	return -1;
}

BSPMODEL* PickInfo::getModel() {
	Bsp* map = getMap();
	int idx = getModelIndex();

	return idx > 0 && idx < map->modelCount ? &map->models[idx] : NULL;
}

BSPFACE* PickInfo::getFace() {
	Bsp* map = getMap();
	int idx = getFaceIndex();
	return idx >= 0 ? &map->faces[idx] : NULL;
}

int PickInfo::getFaceIndex() {
	Bsp* map = getMap();
	int faceIdx = faces.size() == 1 ? faces[0] : -1;
	return faceIdx >= 0 && faceIdx < map->faceCount ? faceIdx : -1;
}

vec3 PickInfo::getOrigin() {
	Entity* ent = getEnt();
	return ent ? ent->getOrigin() : vec3();
}

bool PickInfo::isFaceSelected(int faceIdx) {
	for (int idx : faces) {
		if (idx == faceIdx) {
			return true;
		}
	}

	return false;
}

bool PickInfo::isEntSelected(int entIdx) {
	for (int idx : ents) {
		if (idx == entIdx) {
			return true;
		}
	}

	return false;
}

vector<Entity*> PickInfo::getEnts() {
	vector<Entity*> outEnts;
	Bsp* map = getMap();

	for (int i = 0; i < ents.size(); i++) {
		int idx = ents[i];
		if (map && idx >= 0 && idx < map->ents.size()) {
			outEnts.push_back(map->ents[idx]);
		}
	}

	return outEnts;
}