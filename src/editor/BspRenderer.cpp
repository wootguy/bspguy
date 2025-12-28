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
#include "TextureAtlas.h"
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
#include "TextureArray.h"


BspRenderer::BspRenderer(Bsp* map, PointEntRenderer* pointEntRenderer) {
	this->map = map;
	this->pointEntRenderer = pointEntRenderer;

	renderEnts = NULL;
	renderModels = NULL;
	faceMaths = NULL;
	miptexToTexArray = NULL;

	whiteTex = new Texture(1, 1);
	greyTex = new Texture(1, 1);
	redTex = new Texture(1, 1);
	blackTex = new Texture(1, 1);
	whiteTex3D = new Texture(1, 1, max(1, min(1024, g_max_texture_array_layers)));

	glTextureArray = new TextureArray();

	*((COLOR3*)(whiteTex->data)) = { 255, 255, 255 };
	*((COLOR3*)(redTex->data)) = { 110, 0, 0 };
	*((COLOR3*)(greyTex->data)) = { 64, 64, 64 };
	*((COLOR3*)(blackTex->data)) = { 0, 0, 0 };
	*((COLOR3*)(whiteTex3D->data)) = { 255, 255, 255 };

	for (int i = 0; i < whiteTex3D->depth; i++) {
		((COLOR3*)whiteTex3D->data)[i] = COLOR3(255, 255, 255);
	}

	whiteTex->upload(GL_RGB);
	redTex->upload(GL_RGB);
	greyTex->upload(GL_RGB);
	blackTex->upload(GL_RGB);

	if (g_opengl_3d_texture_support || g_opengl_texture_array_support)
		whiteTex3D->upload(GL_RGB); // only needed if texture arrays/3d textures are supported

	glCheckError("creating plain textures in BSP renderer");

	preloadTextures();
	//loadTextures();
	//loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();

	numRenderClipnodes = map->modelCount;
	lightmapFuture = async(launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this);

	if (g_app->pickMode == PICK_LEAF) {
		leavesFuture = async(launch::async, &BspRenderer::loadLeaves, this);
	}
	else {
		// leaves take a while to load, and aren't needed in most use cases
		leavesThreadFinished = true;
	}	

	// cache ent targets so first selection doesn't lag
	for (int i = 0; i < map->ents.size(); i++) {
		map->ents[i]->getTargets();
	}

	//write_obj_file();
}

void BspRenderer::preloadTextures() {
	if (miptexToTexArray) {
		delete[] miptexToTexArray;
	}
	miptexToTexArray = new TexArrayOffset[map->textureCount];

	glTextureArray->clear();
	for (int i = 0; i < map->textureCount; i++) {
		BSPMIPTEX* tex = map->get_texture(i);
		if (!tex) {
			miptexToTexArray[i] = glTextureArray->tally(16, 16);
			continue;
		}
		
		miptexToTexArray[i] = glTextureArray->tally(tex->nWidth, tex->nHeight);
	}
}

Texture* BspRenderer::generateMissingTexture(int width, int height) {
	Texture* tex = new Texture(width, height);

	static const COLOR4 pink = COLOR4(255, 0, 255, 255);
	static const COLOR4 black = COLOR4(0, 0, 0, 255);
	COLOR4* dat = (COLOR4*)tex->data;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			bool isPink = ((x / 8) + ((y / 8) & 1)) & 1;
			dat[y * width + x] = isPink ? pink : black;
		}
	}

	return tex;
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
		BSPMIPTEX* tex = map->get_texture(i);

		if (!tex) {
			Texture* missingCopy = generateMissingTexture(16, 16);
			glTexturesSwap[i] = missingCopy;
			glTextureArray->add(missingCopy);
			glTexturesSwap[i]->generateMipMaps(3, COLOR3());
			continue;
		}

		COLOR3* palette = NULL;
		byte* src = NULL;
		WADTEX* wadTex = NULL;

		int lastMipSize = (tex->nWidth / 8) * (tex->nHeight / 8);

		if (tex->nOffsets[0] <= 0) {

			bool foundInWad = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex->szName)) {
					wadTex = wads[k]->readTexture(tex->szName);

					if (wadTex->nWidth != tex->nWidth || wadTex->nHeight != tex->nHeight) {
						debugf("Found a texture named %s in %s but the dimensions don't match. Skipping.\n",
							tex->szName, wads[k]->filename.c_str());
						delete wadTex;
						wadTex = NULL;
						continue;
					}

					foundInWad = true;
					palette = (COLOR3*)(wadTex->data + wadTex->nOffsets[3] + lastMipSize + 2 - 40);
					src = wadTex->data;

					wadTexCount++;
					break;
				}
			}

			if (!foundInWad) {
				Texture* missingCopy = generateMissingTexture(tex->nWidth, tex->nHeight);
				glTexturesSwap[i] = missingCopy;
				glTextureArray->add(missingCopy);
				glTexturesSwap[i]->generateMipMaps(3, COLOR3());
				continue;
			}
		}
		else {
			palette = (COLOR3*)(map->textures + texOffset + tex->nOffsets[3] + lastMipSize + 2);
			src = map->textures + texOffset + tex->nOffsets[0];
			embedCount++;
		}

		COLOR4* imageData = new COLOR4[tex->nWidth * tex->nHeight];

		int sz = tex->nWidth * tex->nHeight;
		bool hasAlpha = tex->szName[0] == '{';

		for (int k = 0; k < sz; k++) {
			imageData[k] = COLOR4(palette[src[k]], 255);

			if (hasAlpha && src[k] == 255)
				imageData[k].a = 0;
		}

		if (wadTex) {
			delete[] wadTex->data;
			delete wadTex;
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTexturesSwap[i] = new Texture(tex->nWidth, tex->nHeight, imageData);
		glTextureArray->add(glTexturesSwap[i]);
		glTexturesSwap[i]->generateMipMaps(3, palette[255]);
	}

	if (wadTexCount)
		debugf("Loaded %d wad textures\n", wadTexCount);
	if (embedCount)
		debugf("Loaded %d embedded textures\n", embedCount);
	if (missingCount)
		debugf("%d missing textures\n", missingCount);
}

void BspRenderer::reload() {
	g_app->isLoading = true;
	preloadTextures();
	updateLightmapInfos();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();
	reloadTextures();
	reloadLightmaps();
	reloadClipnodes();

	if (g_app->pickMode == PICK_LEAF) {
		reloadLeaves();
	}
	else {
		deleteRenderLeaves();
	}
}

void BspRenderer::reloadTextures(bool reloadNow) {
	preloadTextures();

	if (reloadNow) {
		preloadTextures();
		loadTextures();

		deleteTextures();
		glTextures = glTexturesSwap;
		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGBA);
		}
		glTextureArray->upload();
		numLoadedTextures = map->textureCount;
		preRenderFaces();
	}
	else {
		texturesLoaded = false;
		texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
	}	
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

void BspRenderer::reloadLeaves(bool reloadNow) {
	if (!leavesThreadFinished) {
		if (reloadNow) {
			logf("ERROR: can't reload leaves yet\n");
		}
		return;
	}

	deleteRenderLeaves();

	leavesLoaded = false;
	leavesThreadFinished = false;

	leavesFuture = async(launch::async, &BspRenderer::loadLeaves, this);

	if (reloadNow) {
		while (!leavesThreadFinished && leavesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

void BspRenderer::delayLoadLeaves() {
	if (!leavesLoaded && leavesThreadFinished)
		reloadLeaves();
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
	activeShader = g_app->bspShader;

	for (int i = 0; i < numRenderModels; i++) {
		RenderModel& model = renderModels[i];
		for (int k = 0; k < model.groupCount; k++) {
			model.renderGroups[k].buffer->setShader(activeShader, true);
		}
		model.wireframeBuffer->setShader(activeShader, true);
	}
}

void BspRenderer::loadLightmaps() {
	double startTime = glfwGetTime();

	int maxSize = g_settings.renderer == RENDERER_OPENGL_21_LEGACY ? 1024 : 2048; // old gpu lied about max texture size
	lightmapAtlasSz = clamp(g_max_texture_size, 512, maxSize);
	lightmapAtlasZoneSz = 128; // 64 is too small for maps like snd, 256 or greater is slower

	vector<TextureAtlas*> atlases;
	vector<Texture*> atlasTextures;
	atlases.push_back(new TextureAtlas(lightmapAtlasSz, lightmapAtlasSz, lightmapAtlasZoneSz));
	atlasTextures.push_back(new Texture(lightmapAtlasSz, lightmapAtlasSz));
	memset(atlasTextures[0]->data, 0, lightmapAtlasSz * lightmapAtlasSz * sizeof(COLOR3));

	numRenderLightmapInfos = map->faceCount;
	lightmaps = new LightmapInfo[map->faceCount];
	memset(lightmaps, 0, map->faceCount * sizeof(LightmapInfo));

	struct FaceLightmap {
		int idx;
		int size[2];
		int imins[2];
		int imaxs[2];
		int lightmapSz;
	};

	vector<FaceLightmap> sortedFaces;
	sortedFaces.reserve(map->faceCount);
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL) || face.nLightmapOffset >= map->header.lump[LUMP_LIGHTING].nLength)
			continue;

		FaceLightmap fmap;
		GetFaceLightmapSize(map, i, fmap.size);
		GetFaceExtents(map, i, fmap.imins, fmap.imaxs);
		fmap.idx = i;
		fmap.lightmapSz = fmap.size[0] * fmap.size[1];
		sortedFaces.push_back(fmap);
	}
	sort(sortedFaces.begin(), sortedFaces.end(), [](const FaceLightmap& a, const FaceLightmap& b) {
		return a.lightmapSz > b.lightmapSz;
	});

	debugf("Calculating lightmaps\n");

	int lightmapCount = 0;
	int atlasId = 0;
	for (int i = 0; i < sortedFaces.size(); i++) {
		FaceLightmap& fmap = sortedFaces[i];
		BSPFACE& face = map->faces[fmap.idx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		LightmapInfo& info = lightmaps[fmap.idx];
		info.w = fmap.size[0];
		info.h = fmap.size[1];
		info.midTexU = (float)(fmap.size[0]) / 2.0f;
		info.midTexV = (float)(fmap.size[1]) / 2.0f;

		// TODO: float mins/maxs not needed?
		info.midPolyU = (fmap.imins[0] + fmap.imaxs[0]) * 16 / 2.0f;
		info.midPolyV = (fmap.imins[1] + fmap.imaxs[1]) * 16 / 2.0f;

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == 255)
				continue;

			// TODO: Try fitting in earlier atlases before using the latest one
			if (!atlases[atlasId]->insert(i, info.w, info.h, info.x[s], info.y[s])) {
				atlases.push_back(new TextureAtlas(lightmapAtlasSz, lightmapAtlasSz, lightmapAtlasZoneSz));
				atlasTextures.push_back(new Texture(lightmapAtlasSz, lightmapAtlasSz));
				atlasId++;
				memset(atlasTextures[atlasId]->data, 0, lightmapAtlasSz * lightmapAtlasSz * sizeof(COLOR3));

				if (!atlases[atlasId]->insert(i, info.w, info.h, info.x[s], info.y[s])) {
					logf("Lightmap too big for atlas size!\n");
					continue;
				}
			}

			lightmapCount++;

			info.atlasId[s] = atlasId;

			// copy lightmap data into atlas
			int lightmapSz = info.w * info.h * sizeof(COLOR3);
			int offset = face.nLightmapOffset + s * lightmapSz;
			if (offset + info.w * info.h > map->lightDataLength) {
				logf("Face %d invalid lightmap %d\n", fmap.idx, s);
				continue;
			}
			COLOR3* lightSrc = (COLOR3*)(map->lightdata + offset);
			COLOR3* lightDst = (COLOR3*)(atlasTextures[atlasId]->data);
			for (int y = 0; y < info.h; y++) {
				for (int x = 0; x < info.w; x++) {
					int src = y * info.w + x;
					int dst = (info.y[s] + y) * lightmapAtlasSz + info.x[s] + x;
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

	//lodepng_encode24_file("atlas.png", atlasTextures[0]->data, lightmapAtlasSz, lightmapAtlasSz);
	debugf("Fit %d lightmaps into %d atlases (%dx%d) in %.2fs\n",
		lightmapCount, atlasId + 1, lightmapAtlasSz, lightmapAtlasSz, glfwGetTime() - startTime);
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

	memset(lightStyleCount, 0, sizeof(lightStyleCount));

	for (int i = 0; i < map->faceCount; i++) {
		Polygon3D poly = Polygon3D(map->get_face_verts(i), true);
		facePolys.push_back(poly);

		BSPFACE& face = map->faces[i];
		for (int k = 0; k < MAXLIGHTMAPS; k++) {
			lightStyleCount[k] += face.nStyles[k] != 255 ? 1 : 0;
		}
	}

	renderModels = new RenderModel[map->modelCount];
	memset(renderModels, 0, sizeof(RenderModel) * map->modelCount);
	numRenderModels = map->modelCount;

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

	glCheckError("BSP pre render");
}

void BspRenderer::deleteRenderModel(RenderModel* renderModel) {
	if (renderModel == NULL || renderModel->renderGroups == NULL || renderModel->renderFaces == NULL) {
		return;
	}
	for (int k = 0; k < renderModel->groupCount; k++) {
		RenderGroup& group = renderModel->renderGroups[k];
		if (group.verts)
			delete[] group.verts;
		if (group.buffer)
			delete group.buffer;
		group.verts = NULL;
		group.buffer = NULL;
	}
	if (renderModel->wireframeVerts)
		delete[] renderModel->wireframeVerts;
	if (renderModel->wireframeBuffer)
		delete renderModel->wireframeBuffer;
	if (renderModel->renderGroups)
		delete[] renderModel->renderGroups;
	if (renderModel->renderFaces)
		delete[] renderModel->renderFaces;

	renderModel->wireframeVerts = NULL;
	renderModel->wireframeBuffer = NULL;
	renderModel->renderGroups = NULL;
	renderModel->renderFaces = NULL;
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

void BspRenderer::deleteRenderLeaves() {
	if (!leavesThreadFinished) {
		logf("ERROR: Attempted leaves data delete during construction\n");
		return;
	}

	if (renderLeafDat) {
		if (renderLeafDat->leafBuffer) {
			delete renderLeafDat->leafBuffer;
			renderLeafDat->leafBuffer = NULL;
		}

		if (renderLeafDat->wireframeLeafBuffer) {
			delete renderLeafDat->wireframeLeafBuffer;
			renderLeafDat->wireframeLeafBuffer = NULL;
		}

		delete renderLeafDat;
		renderLeafDat = NULL;
	}

	if (leafNavMesh) {
		delete leafNavMesh;
		leafNavMesh = NULL;
	}

	leavesLoaded = false;
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

	facePolys.clear();

	renderModels = NULL;
}

void BspRenderer::deleteTextures() {
	if (glTextures != NULL) {
		for (int i = 0; i < numLoadedTextures; i++) {
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
	vector<vec3> modelWireframeVerts;

	activeShader = g_app->bspShader;

	for (int i = 0; i < model.nFaces; i++) {
		int faceIdx = model.iFirstFace + i;
		if (faceIdx >= map->faceCount) {
			logf("Failed to refresh model with invalid faces\n");
			break;
		}
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = map->get_texture(texinfo.iMiptex);
		TexArrayOffset& texArrayOffset = miptexToTexArray[texinfo.iMiptex];
		float texArrayIdx = texArrayOffset.layer;

		if (!g_opengl_texture_array_support) {
			texArrayIdx /= (float)glTextureArray->buckets[texArrayOffset.arrayIdx].count;
			texArrayIdx += 0.00001f; // nudge layer up a bit to prevent GL_NEAREST rounding down to a previous texture
		}

		int texWidth, texHeight;
		if (tex) {
			texWidth = tex->nWidth;
			texHeight = tex->nHeight;
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
			lw = (float)lmap->w / (float)lightmapAtlasSz;
			lh = (float)lmap->h / (float)lightmapAtlasSz;
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
			int vertIdx = min(edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0], (uint16_t)(map->vertCount-1));

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
			verts[e].w = texArrayIdx;

			// lightmap texture coords
			if (hasLighting && lightmapsGenerated) {
				float fLightMapU = lmap->midTexU + (fU - lmap->midPolyU) / 16.0f;
				float fLightMapV = lmap->midTexV + (fV - lmap->midPolyV) / 16.0f;

				float uu = (fLightMapU / (float)lmap->w) * lw;
				float vv = (fLightMapV / (float)lmap->h) * lh;

				float pixelStep = 1.0f / (float)lightmapAtlasSz;

				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][0] = uu + lmap->x[s] * pixelStep;
					verts[e].luv[s][1] = vv + lmap->y[s] * pixelStep;
				}
			}
			else {
				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][0] = 0;
					verts[e].luv[s][1] = 0;
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
		vec3* wireframeVerts = new vec3[wireframeVertCount];

		int idx = 0;
		for (int k = 2; k < face.nEdges; k++) {
			// reverse order due to coordinate system swap
			newVerts[idx++] = verts[k];
			newVerts[idx++] = verts[k - 1];
			newVerts[idx++] = verts[0];	
		}

		idx = 0;
		for (int k = 0; k < face.nEdges; k++) {
			wireframeVerts[idx++] = verts[k].pos();
			wireframeVerts[idx++] = verts[(k + 1) % face.nEdges].pos();
		}

		delete[] verts;
		verts = newVerts;
		vertCount = newCount;

		Texture* gltex = texturesLoaded && numLoadedTextures > 0 ? glTextures[min((uint)numLoadedTextures-1, texinfo.iMiptex)] : greyTex;

		// add face to a render group (faces that share that same texture array, lightmaps, and opacity flag)
		bool isTransparent = opacity < 1.0f;
		int groupIdx = -1;
		for (int k = 0; k < renderGroups.size(); k++) {
			bool textureMatch = !texturesLoaded || 
				renderGroups[k].arrayTextureIdx == miptexToTexArray[texinfo.iMiptex].arrayIdx;

			if (!g_opengl_texture_array_support && !g_opengl_3d_texture_support) {
				textureMatch = !texturesLoaded || renderGroups[k].texture == gltex;
			}

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
			newGroup.arrayTextureIdx = miptexToTexArray[texinfo.iMiptex].arrayIdx;
			newGroup.texture = texturesLoaded ? gltex : greyTex;
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				newGroup.lightmapAtlas[s] = lightmapAtlas[s];
			}
			renderGroups.push_back(newGroup);
			renderGroupVerts.push_back(vector<lightmapVert>());
			groupIdx = renderGroups.size() - 1;
		}

		renderModel->renderFaces[i].group = groupIdx;
		renderModel->renderFaces[i].vertOffset = renderGroupVerts[groupIdx].size();
		renderModel->renderFaces[i].vertCount = vertCount;

		renderGroupVerts[groupIdx].insert(renderGroupVerts[groupIdx].end(), verts, verts + vertCount);
		modelWireframeVerts.insert(modelWireframeVerts.end(), wireframeVerts, wireframeVerts + wireframeVertCount);

		delete[] verts;
		delete[] wireframeVerts;
	}

	renderModel->renderGroups = new RenderGroup[renderGroups.size()];
	renderModel->groupCount = renderGroups.size();

	for (int i = 0; i < renderGroups.size(); i++) {
		renderGroups[i].verts = new lightmapVert[renderGroupVerts[i].size()];
		renderGroups[i].vertCount = renderGroupVerts[i].size();
		memcpy(renderGroups[i].verts, &renderGroupVerts[i][0], renderGroups[i].vertCount * sizeof(lightmapVert));

		renderGroups[i].buffer = new VertexBuffer(activeShader, 0);
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vTex");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
		renderGroups[i].buffer->addAttribute(4, GL_FLOAT, 0, "vColor");
		renderGroups[i].buffer->addAttribute(POS_3F, "vPosition");
		renderGroups[i].buffer->setData(renderGroups[i].verts, renderGroups[i].vertCount);
		renderGroups[i].buffer->upload();

		renderModel->renderGroups[i] = renderGroups[i];
	}

	if (modelWireframeVerts.size()) {
		renderModel->wireframeVerts = new vec3[modelWireframeVerts.size()];
		renderModel->wireframeVertCount = modelWireframeVerts.size();
		memcpy(renderModel->wireframeVerts, &modelWireframeVerts[0], renderModel->wireframeVertCount * sizeof(vec3));

		renderModel->wireframeBuffer = new VertexBuffer(g_app->vec3Shader, 0);
		renderModel->wireframeBuffer->addAttribute(POS_3F, "vPosition");
		renderModel->wireframeBuffer->setData(renderModel->wireframeVerts, renderModel->wireframeVertCount);
		renderModel->wireframeBuffer->upload();
	}
	else {
		renderModel->wireframeVerts = NULL;
		renderModel->wireframeVertCount = 0;
	}

	for (int i = 0; i < model.nFaces; i++) {
		refreshFace(model.iFirstFace + i);
	}

	if (refreshClipnodes) {
		refreshModelClipnodes(modelIdx);
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

	if (modelIdx == 0 && renderLeafDat != NULL && renderLeafDat->leafBuffer) {
		delete renderLeafDat->leafBuffer;
		delete renderLeafDat->wireframeLeafBuffer;
		renderLeafDat->leafBuffer = NULL;
		renderLeafDat->wireframeLeafBuffer = NULL;
	}

	if (leafNavMesh) {
		delete leafNavMesh;
		leafNavMesh = NULL;
	}

	deleteRenderModelClipnodes(&renderClipnodes[modelIdx]);
	generateClipnodeBuffer(modelIdx);

	RenderClipnodes& renderClip = renderClipnodes[modelIdx];

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (renderClip.clipnodeBuffer[i])
			renderClip.clipnodeBuffer[i]->upload();
		if (renderClip.wireframeClipnodeBuffer[i])
			renderClip.wireframeClipnodeBuffer[i]->upload();
	}

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

void BspRenderer::loadLeaves() {
	renderLeafDat = new RenderLeaves();
	generateLeafBuffer();
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

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, output, allVerts.size());
	renderClip->clipnodeBuffer[hull]->ownData = true;

	renderClip->wireframeClipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());
	renderClip->wireframeClipnodeBuffer[hull]->ownData = true;

	renderClip->faceMaths[hull] = faceMaths;

	/*
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
	*/
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

	node->face_buffer = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, output, allVerts.size());
	node->wireframe_buffer = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());

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

		for (int k = 0; k < solidNodes.size(); k++) {
			clipnodeLeafCount++;
			generateNodeMesh(&solidNodes[k], color, allVerts, wireframeVerts, faceMaths, solidNodes[k].nodeIdx);
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

		renderClip->clipnodeBuffer[i] = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, output, allVerts.size());
		renderClip->clipnodeBuffer[i]->ownData = true;

		renderClip->wireframeClipnodeBuffer[i] = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());
		renderClip->wireframeClipnodeBuffer[i]->ownData = true;

		renderClip->faceMaths[i] = faceMaths;
	}

	if (modelIdx == 0) {
		//generateNavMeshBuffer();
	}
}

void BspRenderer::generateLeafBuffer() {
	BSPMODEL& model = map->models[0];

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	renderLeafDat->leafBuffer = NULL;
	renderLeafDat->wireframeLeafBuffer = NULL;
	leafNavMesh = NULL;

	Clipper clipper;

	vector<NodeVolumeCuts> leafNodes = map->get_model_leaf_volume_cuts(0, 0, CONTENTS_NOT_LEAF_0);
	static COLOR4 color = COLOR4(255, 255, 255, 128);

	vector<cVert> allVerts;
	vector<cVert> wireframeVerts;
	vector<FaceMath> faceMaths;

	for (int i = 0; i < 65536; i++) {
		renderLeafDat->leafRanges[i].clear();
		renderLeafDat->leafWireRanges[i].clear();
	}

	for (int k = 0; k < leafNodes.size(); k++) {
		int leafIdx = leafNodes[k].leafIdx;
		int start = allVerts.size();
		int wstart = wireframeVerts.size();
		generateNodeMesh(&leafNodes[k], color, allVerts, wireframeVerts, faceMaths, leafNodes[k].leafIdx);
		
		for (int i = start; i < allVerts.size(); i++) {
			renderLeafDat->leafRanges[leafIdx].push_back(i);
		}
		for (int i = wstart; i < wireframeVerts.size(); i++) {
			renderLeafDat->leafWireRanges[leafIdx].push_back(i);
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
		return;
	}

	renderLeafDat->leafBuffer = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, output, allVerts.size());
	renderLeafDat->leafBuffer->ownData = true;

	renderLeafDat->wireframeLeafBuffer = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, wireOutput, wireframeVerts.size());
	renderLeafDat->wireframeLeafBuffer->ownData = true;

	renderLeafDat->faceMaths = faceMaths;

	renderLeafDat->originalColors.resize(allVerts.size());
	for (int i = 0; i < allVerts.size(); i++) {
		renderLeafDat->originalColors[i] = allVerts[i].c;
	}

	leafNavMesh = LeafNavMeshGenerator().generate(map, true, CONTENTS_NOT_LEAF_0, 0);
}

void BspRenderer::generateNodeMesh(NodeVolumeCuts* volume, COLOR4 color, vector<cVert>& allVerts,
	vector<cVert>& wireframeVerts, vector<FaceMath>& faceMaths, int elementIndex) {
	Clipper clipper;
	CMesh mesh = clipper.clip(volume->cuts);
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
			faceMath.index = elementIndex;

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
			float dot = (dotProduct(normal * -1, lightDir) + 1) / 2.0f;
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
		Entity* ent = map->ents[i];
		numPointEnts += !ent->isBspModel() && !ent->hidden;
	}

	cCube* entCubes = new cCube[numPointEnts];
	int pointEntIdx = 0;

	for (int i = 0; i < map->ents.size(); i++) {
		Entity* ent = map->ents[i];
		refreshEnt(i);

		if (i != 0 && !ent->isBspModel() && !ent->hidden) {
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

	pointEnts = new VertexBuffer(g_app->colorShader, COLOR_4B | POS_3F, entCubes, numPointEnts * 6 * 6);
	pointEnts->ownData = true;
	pointEnts->upload();

	glCheckError("BSP pre render ents");
}

void BspRenderer::refreshPointEnt(int entIdx) {
	int skipIdx = 0;

	if (entIdx == 0)
		return;

	Entity* ent = map->ents[entIdx];
	if (ent->hidden)
		return;

	// skip worldspawn
	for (int i = 1, sz = map->ents.size(); i < sz; i++) {
		if (renderEnts[i].modelIdx >= 0 || map->ents[i]->hidden)
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
	ent->clearCache();

	if (ent->hasKey("origin")) {
		vec3 origin = ent->getOrigin();
		renderEnts[entIdx].modelMat.translate(origin.x, origin.z, -origin.y);
		renderEnts[entIdx].offset = origin;
	}
	
	renderEnts[entIdx].angles = ent->getAngles().flip() * (PI / 180.0f);
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

	if (faceIdx >= map->faceCount) {
		logf("Failed to refresh invalid face %d / %d\n", faceIdx, map->faceCount);
		return;
	}

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
		int vertIdx = min(edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0], (uint16_t)(map->vertCount-1));
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
		clipnodesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready ||
		(leavesFuture.valid() && leavesFuture.wait_for(chrono::milliseconds(0)) != future_status::ready)) {
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
	if (pvsDat) {
		delete pvsDat->wireframePvsBuffer;
		delete pvsDat;
	}

	deleteTextures();
	deleteLightmapTextures();
	deleteRenderFaces();
	deleteRenderClipnodes();
	deleteRenderLeaves();
	deleteFaceMaths();

	// TODO: share these with all renderers
	delete whiteTex;
	delete redTex;
	delete greyTex;
	delete blackTex;
	delete whiteTex3D;

	delete glTextureArray;

	if (map)
		delete map;
}

void BspRenderer::delayLoadData() {
	if (!lightmapsUploaded && lightmapFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		for (int i = 0; i < numLightmapAtlases; i++) {
			glLightmapTextures[i]->upload(GL_RGB, true);
		}

		lightmapsGenerated = true;

		preRenderFaces();

		if (g_app->pickMode == PICK_FACE) {
			highlightPickedFaces(true); // re-highlight selection
		}

		lightmapsUploaded = true;
	}
	else if (!texturesLoaded && texturesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		deleteTextures();
		
		glTextures = glTexturesSwap;

		// non-3D version of textures needed for GUI
		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGBA);
		}

		glTextureArray->upload();
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
					clip.wireframeClipnodeBuffer[k]->upload();
				}
			}
		}

		clipnodesLoaded = true;
		debugf("Loaded %d clipnode leaves\n", clipnodeLeafCount);
	}

	if (!leavesThreadFinished && leavesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		leavesLoaded = renderLeafDat != NULL;
		leavesThreadFinished = true;

		if (renderLeafDat) {
			renderLeafDat->leafBuffer->upload();
			renderLeafDat->wireframeLeafBuffer->upload();

			if (g_app->pickMode == PICK_LEAF) {
				highlightPickedLeaves(true); // switching from face to leaf pick mode for the first time
				hideLeaves(true);
			}
		}

		debugf("Loaded leaves\n");
	}
}

bool BspRenderer::isFinishedLoading() {
	return lightmapsUploaded && texturesLoaded && textureFacesLoaded && clipnodesLoaded && leavesThreadFinished ||
		map->ents.empty();
}

void BspRenderer::highlightPickedFaces(bool highlight) {
	unordered_set<RenderGroup*> uploadGroups;

	for (int i = 0; i < g_app->pickInfo.faces.size(); i++) {
		RenderFace* rface;
		RenderGroup* rgroup;
		if (!getRenderPointers(g_app->pickInfo.faces[i], &rface, &rgroup)) {
			logf("Bad face index for highlight %d\n", g_app->pickInfo.faces[i]);
			continue;
		}

		float r, g, b;
		r = g = b = 1.0f;

		if (highlight) {
			r = 0.86f;
			g = 0;
			b = 0;
		}

		for (int k = 0; k < rface->vertCount; k++) {
			rgroup->verts[rface->vertOffset + k].r = r;
			rgroup->verts[rface->vertOffset + k].g = g;
			rgroup->verts[rface->vertOffset + k].b = b;
		}

		uploadGroups.insert(rgroup);
	}

	for (RenderGroup* rgroup : uploadGroups) {
		rgroup->buffer->deleteBuffer();
		rgroup->buffer->upload();
	}
}

void BspRenderer::highlightPickedLeaves(bool highlight) {
	if (!leavesLoaded || !renderLeafDat->leafBuffer)
		return;

	cVert* verts = (cVert*)renderLeafDat->leafBuffer->data;
	cVert* wireVerts = (cVert*)renderLeafDat->wireframeLeafBuffer->data;

	if (!highlight) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			COLOR4& og = renderLeafDat->originalColors[i];
			verts[i].c.r = og.r;
			verts[i].c.g = og.g;
			verts[i].c.b = og.b;
		}
		for (int i = 0; i < renderLeafDat->wireframeLeafBuffer->numVerts; i++) {
			wireVerts[i].c.r = 0;
			wireVerts[i].c.g = 0;
			wireVerts[i].c.b = 0;
		}
		hideLeaves(true);
	}
	else {
		for (int i = 0; i < g_app->pickInfo.leaves.size(); i++) {
			uint16_t leafIdx = g_app->pickInfo.leaves[i];

			for (int idx : renderLeafDat->leafRanges[leafIdx]) {
				verts[idx].c.r = 255;
				verts[idx].c.g = 0;
				verts[idx].c.b = 0;
			}

			for (int idx : renderLeafDat->leafWireRanges[leafIdx]) {
				wireVerts[idx].c.r = 255;
				wireVerts[idx].c.g = 255;
				wireVerts[idx].c.b = 0;
			}
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
	renderLeafDat->wireframeLeafBuffer->deleteBuffer();
	renderLeafDat->wireframeLeafBuffer->upload();
}

void BspRenderer::hideLeaves(bool hideNotUnhide) {
	if (!leavesLoaded || !renderLeafDat->leafBuffer)
		return;

	cVert* verts = (cVert*)renderLeafDat->leafBuffer->data;
	cVert* wireVerts = (cVert*)renderLeafDat->wireframeLeafBuffer->data;

	if (!hideNotUnhide) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			verts[i].c.a = renderLeafDat->originalColors[i].a;
		}
		for (int i = 0; i < renderLeafDat->wireframeLeafBuffer->numVerts; i++) {
			wireVerts[i].c.a = 255;
		}
	}
	else {
		for (auto leafIdx : g_app->hiddenLeaves) {
			for (int idx : renderLeafDat->leafRanges[leafIdx]) {
				verts[idx].c.a = 0;
			}
			for (int idx : renderLeafDat->leafWireRanges[leafIdx]) {
				wireVerts[idx].c.a = 0;
			}
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
	renderLeafDat->wireframeLeafBuffer->deleteBuffer();
	renderLeafDat->wireframeLeafBuffer->upload();
}

void BspRenderer::hideFaces(bool hideNotUnhide) {
	unordered_set<RenderGroup*> uploadGroups;

	for (auto faceIdx : g_app->hiddenFaces) {
		RenderFace* rface;
		RenderGroup* rgroup;

		if (!getRenderPointers(faceIdx, &rface, &rgroup)) {
			logf("Bad face index for hide %d\n", g_app->pickInfo.faces[faceIdx]);
			continue;
		}

		float a = 0;
		
		if (!hideNotUnhide) {
			BSPFACE& face = map->faces[faceIdx];
			BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
			a = (texinfo.nFlags & TEX_SPECIAL) ? 128 : 255;
		}

		for (int k = 0; k < rface->vertCount; k++) {
			rgroup->verts[rface->vertOffset + k].a = a;
		}

		uploadGroups.insert(rgroup);
	}

	for (RenderGroup* rgroup : uploadGroups) {
		rgroup->buffer->deleteBuffer();
		rgroup->buffer->upload();
	}
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
	BSPMIPTEX* tex = map->get_texture(texinfo.iMiptex);
	if (!tex) {
		return;
	}

	for (int i = 0; i < rface->vertCount; i++) {
		lightmapVert& vert = rgroup->verts[rface->vertOffset + i];
		vec3 pos = vec3(vert.x, -vert.z, vert.y);

		float tw = 1.0f / (float)tex->nWidth;
		float th = 1.0f / (float)tex->nHeight;
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

	if (texinfo.iMiptex >= 0 && texinfo.iMiptex < numLoadedTextures)
		return glTextures[texinfo.iMiptex]->id;
	else
		return 0;
}

Texture* BspRenderer::uploadTexture(WADTEX* tex) {
	int lastMipSize = (tex->nWidth / 8) * (tex->nHeight / 8);
	COLOR3* palette = (COLOR3*)(tex->data + tex->nOffsets[3] + lastMipSize + 2 - 40);
	byte* src = tex->data;

	COLOR4* imageData = new COLOR4[tex->nWidth * tex->nHeight];

	int sz = tex->nWidth * tex->nHeight;

	for (int k = 0; k < sz; k++) {
		imageData[k] = COLOR4(palette[src[k]], 255);
	}

	Texture* newTex = new Texture(tex->nWidth, tex->nHeight, imageData);
	newTex->upload(GL_RGBA);

	return newTex;
}

int BspRenderer::addTextureToMap(string textureName) {
	WADTEX* tex = NULL;
	for (int i = 0; i < wads.size(); i++) {
		if (wads[i]->hasTexture(textureName)) {
			tex = wads[i]->readTexture(textureName);
			break;
		}
	}

	if (!tex) {
		return -1;
	}

	int newMiptex = map->add_texture_from_wad(tex);

	reloadTextures(true);
	
	logf("Added new texture reference for %s\n", tex->szName);

	delete tex;
	return newMiptex;
}

void BspRenderer::getRenderEnts(vector<OrderedEnt>& ents) {
	ents.reserve(map->ents.size());
	mapOffset = map->ents.size() ? map->ents[0]->getOrigin() : vec3();
	renderOffset = vec3(mapOffset.x, mapOffset.z, -mapOffset.y);

	for (int i = 0; i < map->ents.size(); i++) {
		map->ents[i]->highlighted = false;
		OrderedEnt ent;
		ent.ent = map->ents[i];
		ent.modelIdx = map->ents[i]->getBspModelIdx();
		if (ent.modelIdx >= map->modelCount) {
			continue;
		}
		ent.transform = renderEnts[i].modelMat;
		ent.transform.translate(renderOffset.x, renderOffset.y, renderOffset.z);
		ent.transform = ent.transform * map->ents[i]->getRotationMatrix(false);
		ents.push_back(ent);
	}
	for (Entity* highlightEnt : g_app->pickInfo.getEnts()) {
		highlightEnt->highlighted = true;
	}

	// draw highlighted ents last
	sort(ents.begin(), ents.end(), [](const OrderedEnt& a, const OrderedEnt& b) {
		return a.ent->highlighted < b.ent->highlighted;
		});
}

void BspRenderer::render(const vector<OrderedEnt>& orderedEnts, bool highlightAlwaysOnTop,
	int clipnodeHull, bool transparencyPass, bool wireframePass) {
	if (map->ents.empty())
		return;
	
	BSPMODEL& world = map->models[0];

	activeShader = g_app->bspShader;

	if (wireframePass)
		activeShader = g_app->vec3Shader;

	activeShader->bind();
	activeShader->modelMat->loadIdentity();
	activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	activeShader->updateMatrixes();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	if (!wireframePass) {
		if ((g_settings.render_flags & RENDER_LIGHTMAPS) && (g_settings.render_flags & RENDER_TEXTURES)) {
			activeShader->setUniform("gamma", 1.5f);
		}
		else {
			activeShader->setUniform("gamma", 1.0f);
		}
	}

	if (!map->ents[0]->hidden) {
		if (wireframePass)
			drawModelWireframe(0, false);
		else {
			if (map->modelCount > 0)
				drawModel(map->ents[0], 0, transparencyPass, false);
		}
	}

	activeShader->pushMatrix(MAT_MODEL);
	for (int i = 0, sz = orderedEnts.size(); i < sz; i++) {
		const OrderedEnt& orderEnt = orderedEnts[i];
		int modelIdx = orderEnt.modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount) {
			Entity* ent = orderEnt.ent;
			if (ent->hidden)
				continue;
			if (!wireframePass && !willDrawModel(ent, modelIdx, transparencyPass))
				continue;

			if (highlightAlwaysOnTop && ent->highlighted)
				glDisable(GL_DEPTH_TEST);
			
			*activeShader->modelMat = orderEnt.transform;
			activeShader->updateMatrixes();

			if (wireframePass)
				drawModelWireframe(modelIdx, ent->highlighted);
			else
				drawModel(ent, modelIdx, transparencyPass, ent->highlighted);
		}
	}
	activeShader->popMatrix(MAT_MODEL);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if ((g_settings.render_flags & RENDER_POINT_ENTS) && !transparencyPass && !wireframePass) {
		drawPointEntities();
		activeShader->bind();
	}

	// draw clipnodes in a separate pass to prevent interleaving shader binds
	if (clipnodesLoaded && transparencyPass && !wireframePass) {
		g_app->colorShader->bind();

		if (g_settings.render_flags & RENDER_WORLD_CLIPNODES && clipnodeHull != -1 && !map->ents[0]->hidden) {
			drawModelClipnodes(0, false, clipnodeHull);
		}

		if ((g_settings.render_flags & RENDER_ENTS) && (g_settings.render_flags & RENDER_ENT_CLIPNODES)) {
			g_app->colorShader->pushMatrix(MAT_MODEL);
			for (int i = 0, sz = orderedEnts.size(); i < sz; i++) {
				const OrderedEnt& orderEnt = orderedEnts[i];
				int modelIdx = orderEnt.modelIdx;

				if (modelIdx >= 0 && modelIdx < map->modelCount) {
					Entity* ent = orderEnt.ent;
					if (ent->hidden)
						continue;

					RenderClipnodes& clip = renderClipnodes[modelIdx];
					if (clipnodeHull == -1 && getBestClipnodeHull(modelIdx) == -1) {
						continue; // skip if no hull can be drawn
					}

					if (clipnodeHull == -1 && renderModels[modelIdx].groupCount > 0) {
						continue; // skip rendering for models that have faces, if in auto mode
					}
					
					*g_app->colorShader->modelMat = orderEnt.transform;
					g_app->colorShader->updateMatrixes();

					if (ent->highlighted) {
						g_app->colorShader->setUniform("colorMult", vec4(1, 0.25f, 0.25f, 1));
					}

					drawModelClipnodes(modelIdx, false, clipnodeHull);

					if (ent->highlighted) {
						g_app->colorShader->setUniform("colorMult", vec4(1, 1, 1, 1));
					}
				}
			}
			g_app->colorShader->popMatrix(MAT_MODEL);
		}		
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	delayLoadData();
}

void BspRenderer::renderLeaves() {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	// draw clipnodes in a separate pass to prevent interleaving shader binds
	if (leavesLoaded) {
		g_app->colorShader->bind();
		g_app->colorShader->modelMat->loadIdentity();
		g_app->colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
		g_app->colorShader->updateMatrixes();

		if (renderLeafDat->leafBuffer) {
			renderLeafDat->leafBuffer->draw(GL_TRIANGLES);
			renderLeafDat->wireframeLeafBuffer->draw(GL_LINES);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	delayLoadData();
}

void BspRenderer::drawModelWireframe(int modelIdx, bool highlight) {
	if (!(g_settings.render_flags & RENDER_ENTS))
		return;

	if (renderModels[modelIdx].wireframeBuffer && modelIdx < numRenderModels) {
		if (highlight)
			g_app->vec3Shader->setUniform("color", vec4(1, 1, 0, 1));
		else if (modelIdx > 0)
			g_app->vec3Shader->setUniform("color", vec4(0, 0, 0.78f, 0));
		else
			g_app->vec3Shader->setUniform("color", vec4(0.25f, 0.25f, 0.25f, 1));

		renderModels[modelIdx].wireframeBuffer->draw(GL_LINES);
	}
}

bool BspRenderer::willDrawModel(Entity* ent, int modelIdx, bool transparent) {
	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS))) {
		return false;
	}
	if (modelIdx >= numRenderModels) {
		return false;
	}

	EntRenderOpts opts = ent->getRenderOpts();
	bool isTransparent = false;

	if (g_settings.render_flags & RENDER_RENDER_MODES) {
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
			if (modelIdx == 0 && !(g_settings.render_flags & RENDER_SPECIAL))
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

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS))) {
		return;
	}

	if (g_settings.render_flags & RENDER_RENDER_MODES) {
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
			useLightmaps = true;
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
			if (modelIdx == 0 && !(g_settings.render_flags & RENDER_SPECIAL))
				continue;
			else if (modelIdx != 0 && !(g_settings.render_flags & RENDER_SPECIAL_ENTS))
				continue;
		}
		else if (modelIdx != 0 && !(g_settings.render_flags & RENDER_ENTS))
			continue;

		// bind the texture
		glActiveTexture(GL_TEXTURE0);
		if (texturesLoaded && (g_settings.render_flags & RENDER_TEXTURES))
			rgroup.texture->bind();
		else {
			if (g_opengl_3d_texture_support || g_opengl_texture_array_support) {
				whiteTex3D->bind();
			}
			else {
				whiteTex->bind();
			}
		}

		// bind lightmaps for each style
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			glActiveTexture(GL_TEXTURE1 + s);

			if (highlight) {
				redTex->bind();
			}
			else if (!(g_settings.render_flags & RENDER_LIGHTMAPS) || !useLightmaps) {
				if (s == 0) {
					whiteTex->bind();
				}
				else {
					blackTex->bind();
				}
			}
			else if (lightmapsUploaded) {
				if (!g_app->lightStylesEnabled[s]) {
					blackTex->bind();
					continue;
				}

				rgroup.lightmapAtlas[s]->bind();
			}
			else {
				if (s == 0)
					greyTex->bind();
				else
					blackTex->bind();
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

void BspRenderer::drawPointEntities() {
	g_app->colorShader->bind();
	g_app->colorShader->updateMatrixes();

	if (g_app->pickInfo.ents.empty() && !(g_settings.render_flags & (RENDER_STUDIO_MDL | RENDER_SPRITES))) {
		if (pointEnts->numVerts > 0)
			pointEnts->draw(GL_TRIANGLES);
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
				pointEnts->drawRange(GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
			}
			nextRangeDrawIdx = pointEntIdx+1;

			if (!map->ents[i]->didStudioDraw) {
				g_app->colorShader->pushMatrix(MAT_MODEL);
				*g_app->colorShader->modelMat = renderEnts[i].modelMat;
				g_app->colorShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				g_app->colorShader->updateMatrixes();

				if (ent->highlighted)
					renderEnts[i].pointEntCube->selectBuffer->draw(GL_TRIANGLES);
				else
					renderEnts[i].pointEntCube->buffer->draw(GL_TRIANGLES);
				renderEnts[i].pointEntCube->wireframeBuffer->draw(GL_LINES);

				g_app->colorShader->popMatrix(MAT_MODEL);
			}
		}

		pointEntIdx++;
	}

	if (pointEntIdx - nextRangeDrawIdx > 0) {
		pointEnts->drawRange(GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
	}
}

void BspRenderer::drawPvs() {
	if (!pvsDat || !pvsDat->wireframePvsBuffer)
		return;

	glDisable(GL_DEPTH_TEST);

	g_app->vec3Shader->bind();
	g_app->vec3Shader->modelMat->loadIdentity();
	g_app->vec3Shader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_app->vec3Shader->updateMatrixes();
	g_app->vec3Shader->setUniform("color", vec4(1, 1, 1, 1));

	pvsDat->wireframePvsBuffer->draw(GL_LINES);

	glEnable(GL_DEPTH_TEST);
}

void BspRenderer::addPvsPoly(int faceIdx, vec3 faceOffset, vec3 viewOrigin, Frustum* frustum, bool makeBuffer, vector<vec3>& allVerts) {
	Polygon3D& poly = facePolys[faceIdx];

	BSPFACE& face = map->faces[faceIdx];
	if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
		return; // special faces not rendered

	if (poly.distance(viewOrigin - faceOffset) > 0) {
		return; // back face culled
	}

	if (!isBoxInView(faceOffset + poly.worldMins, faceOffset + poly.worldMaxs, *frustum, 0))
		return; // frustum culled

	pvsDat->wpoly++;

	if (!makeBuffer)
		return;

	vector<vec3>& verts = poly.verts;

	for (int k = 0; k < verts.size(); k++) {
		allVerts.push_back((faceOffset + verts[k]).flip());
		allVerts.push_back((faceOffset + verts[(k + 1) % verts.size()]).flip());
	}
}

void BspRenderer::updatePvs(vec3 viewOrigin) {
	int ileaf = map->get_leaf(viewOrigin, 0);

	if (pvsDat) {
		delete pvsDat->wireframePvsBuffer;
		delete pvsDat;
		pvsDat = NULL;
	}

	bool makeBuffer = g_settings.render_flags & RENDER_PVS;

	pvsDat = new RenderPvs();
	pvsDat->leaf = ileaf;
	pvsDat->wireframePvsBuffer = NULL;

	if (ileaf == 0 || map->ents.empty()) {
		return;
	}

	pvsDat->pvsLeaves = map->get_pvs(pvsDat->leaf);
	pvsDat->pvsFaces = map->get_leaf_faces(pvsDat->pvsLeaves);

	vector<vec3> allVerts;

	Frustum frustum = g_app->getCameraFrustum();

	pvsDat->wpoly = 0;

	if (!map->ents[0]->hidden) {
		for (int faceIdx : pvsDat->pvsFaces) {
			addPvsPoly(faceIdx, vec3(), viewOrigin, &frustum, makeBuffer, allVerts);
		}
	}

	if (g_settings.render_flags & RENDER_ENTS) {
		for (Entity* ent : map->ents) {
			if (ent->hidden)
				continue;

			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 || modelIdx >= map->modelCount)
				continue;

			vec3 ori = ent->getOrigin();
			BSPMODEL& model = map->models[modelIdx];
			vec3 entMin = model.nMins + ori;
			vec3 entMax = model.nMaxs + ori;

			if (!isBoxInView(entMin, entMax, frustum, 0))
				continue; // frustum culled model

			// TODO: configure this in the FGD or something
			static unordered_set<string> visibleClassnames = {
				"func_breakable",
				"func_button",
				"func_conveyor",
				"func_detail",
				"func_door",
				"func_door_rotating",
				"func_guntarget",
				"func_healthcharger",
				"func_illusionary",
				"func_pendulum",
				"func_plat",
				"func_platrot",
				"func_pushable",
				"func_recharge",
				"func_rot_button",
				"func_rotating",
				"func_tank",
				"func_tanklaser",
				"func_tankmortar",
				"func_tankrocket",
				"func_trackautochange",
				"func_trackchange",
				"func_tracktrain",
				"func_train",
				"func_wall",
				"func_wall_toggle",
				//"func_water",
				"momentary_door",
				"momentary_rot_button",
				"button_target",
			};

			if (!visibleClassnames.count(ent->getClassname()))
				continue;

			bool inPvs = false;
			for (int leafIdx : pvsDat->pvsLeaves) {
				BSPLEAF& leaf = map->leaves[leafIdx];
				vec3 leafMins(leaf.nMins[0], leaf.nMins[1], leaf.nMins[2]);
				vec3 leafMaxs(leaf.nMaxs[0], leaf.nMaxs[1], leaf.nMaxs[2]);
			
				if (boxesIntersect(leafMins, leafMaxs, entMin, entMax)) {
					inPvs = true;
					break;
				}
			}

			if (!inPvs)
				continue;

			for (int i = model.iFirstFace; i < model.iFirstFace + model.nFaces; i++) {
				addPvsPoly(i, ori, viewOrigin, &frustum, makeBuffer, allVerts);
			}
		}
	}

	if (makeBuffer && allVerts.size()) {
		vec3* vertDat = new vec3[allVerts.size()];
		memcpy(vertDat, &allVerts[0], allVerts.size() * sizeof(vec3));

		pvsDat->wireframePvsBuffer = new VertexBuffer(g_app->vec3Shader, POS_3F, vertDat, allVerts.size());
		pvsDat->wireframePvsBuffer->ownData = true;
		pvsDat->wireframePvsBuffer->upload();
	}
}

bool BspRenderer::pickPoly(vec3 start, vec3 dir, int hullIdx, int& entIdx, int& faceIdx, int& leafIdx, float& bestDist) {
	bool foundBetterPick = false;
	entIdx = -1;
	faceIdx = -1;
	leafIdx = -1;

	vec3 pickOffset = vec3(mapOffset.x, mapOffset.y, mapOffset.z);
	start -= mapOffset;

	if (!map || map->ents.size() == 0) {
		return false;
	}

	if (!map->ents[0]->hidden && pickModelPoly(start, dir, vec3(), vec3(), 0, hullIdx, 0, faceIdx, bestDist)) {
		entIdx = 0;
		foundBetterPick = true;
	}
	if (g_app->pickMode == PICK_LEAF && pickLeaf(start, dir, leafIdx, bestDist)) {
		entIdx = 0;
		foundBetterPick = true;
	}

	for (int i = 0, sz = map->ents.size(); i < sz; i++) {
		Entity* ent = map->ents[i];
		if (ent->hidden)
			continue;

		int modelIdx = renderEnts[i].modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount && modelIdx < numRenderModels) {

			bool isSpecial = false;
			for (int k = 0; k < renderModels[modelIdx].groupCount; k++) {
				if (renderModels[modelIdx].renderGroups[k].transparent) {
					isSpecial = true;
					break;
				}
			}

			if (isSpecial && !(g_settings.render_flags & RENDER_SPECIAL_ENTS)) {
				continue;
			} else if (!isSpecial && !(g_settings.render_flags & RENDER_ENTS)) {
				continue;
			}

			vec3 angles = map->ents[i]->canRotate() ? renderEnts[i].angles : vec3();
			if (pickModelPoly(start, dir, renderEnts[i].offset, angles,
					modelIdx, hullIdx, i, faceIdx, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
		}
		else if (i > 0 && g_settings.render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;

			if (pickAABB(start, dir, mins, maxs, bestDist)) {
				entIdx = i;
				foundBetterPick = true;
			}
			else if (ent->cachedMdl && !ent->isIconSprite && ent->cachedMdl->pick(start, dir, ent, bestDist)) {
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

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS))) {
		return false;
	}
	if (map->modelCount == 0)
		return false;

	start -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_settings.render_flags & RENDER_SPECIAL);

	bool hasAngles = rot != vec3();
	mat4x4 angleTransform = map->ents[testEntidx]->getRotationMatrix(true);

	for (int k = 0; k < model.nFaces && model.iFirstFace + k < map->faceCount; k++) {
		if (g_app->hiddenFaces.count(model.iFirstFace + k))
			continue;

		FaceMath faceMath = faceMaths[model.iFirstFace + k];

		if (hasAngles) {
			rotateFaceMath(faceMath, angleTransform);
		}

		/*
		// debug rotated solid entity picking (not the same transform as rendering for some reason)
		if (modelIdx == 63) {
			vector<vec3> debugVerts;
			for (vec3& ogvert : faceMath.verts) {
				debugVerts.push_back((angleTransform * vec4(ogvert, 1)).xyz());
			}
			g_app->debugPoly = Polygon3D(debugVerts);
		}
		*/

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

	bool selectWorldClips = modelIdx == 0 && (g_settings.render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_settings.render_flags & RENDER_ENT_CLIPNODES);

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

bool BspRenderer::pickLeaf(vec3 start, vec3 dir, int& leafIdx, float& bestDist) {
	BSPMODEL& model = map->models[0];

	if (map->modelCount == 0)
		return false;

	bool foundBetterPick = false;

	if (leavesLoaded) {
		for (int i = 0; i < renderLeafDat->faceMaths.size(); i++) {
			FaceMath faceMath = renderLeafDat->faceMaths[i];

			if (g_app->hiddenLeaves.count(faceMath.index))
				continue;

			float t = bestDist;
			if (pickFaceMath(start, dir, faceMath, t)) {
				foundBetterPick = true;
				bestDist = t;
				leafIdx = faceMath.index;
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

	if (entIdx >= 0 && entIdx < map->ents.size()) {
		for (int i = 0; i < ents.size(); i++) {
			if (ents[i] == entIdx) {
				return;
			}
		}
		ents.push_back(entIdx);
	}
	else
		logf("Failed to select ent index out of range %d\n", entIdx);

	//logf("select ent %d\n", entIdx);
}

void PickInfo::selectFace(int faceIdx) {
	for (int i = 0; i < faces.size(); i++) {
		if (faces[i] == faceIdx) {
			return;
		}
	}
	faces.push_back(faceIdx);
	//logf("select face %d\n", faceIdx);
}

void PickInfo::selectLeaf(int leafIdx) {
	for (int i = 0; i < leaves.size(); i++) {
		if (leaves[i] == leafIdx) {
			return;
		}
	}
	leaves.push_back(leafIdx);
}

void PickInfo::deselect() {
	ents.clear();
	faces.clear();
	leaves.clear();
	g_app->pickCount++;
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

void PickInfo::deselectLeaf(int leafIdx) {
	for (int i = 0; i < leaves.size(); i++) {
		if (leaves[i] == leafIdx) {
			leaves.erase(leaves.begin() + i);
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

int PickInfo::getLeafIndex() {
	Bsp* map = getMap();
	int leafIdx = leaves.size() == 1 ? leaves[0] : -1;
	return leafIdx >= 0 && leafIdx < map->leafCount ? leafIdx : -1;
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

bool PickInfo::isLeafSelected(int leafIdx) {
	for (int idx : leaves) {
		if (idx == leafIdx) {
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

vector<BSPFACE*> PickInfo::getFaces() {
	vector<BSPFACE*> outFaces;
	Bsp* map = getMap();

	for (int i = 0; i < faces.size(); i++) {
		int idx = faces[i];
		if (map && idx >= 0 && idx < map->faceCount) {
			outFaces.push_back(&map->faces[idx]);
		}
	}

	return outFaces;
}

vector<BSPLEAF*> PickInfo::getLeaves() {
	vector<BSPLEAF*> outLeaves;
	Bsp* map = getMap();

	for (int i = 0; i < leaves.size(); i++) {
		int idx = leaves[i];
		if (map && idx >= 0 && idx < map->leafCount) {
			outLeaves.push_back(&map->leaves[idx]);
		}
	}

	return outLeaves;
}

vector<int> PickInfo::getModelIndexes() {
	vector<int> outIdx;
	Bsp* map = getMap();

	for (int i = 0; i < ents.size(); i++) {
		int modelIdx = map->ents[ents[i]]->getBspModelIdx();
		if (modelIdx >= 0)
			outIdx.push_back(modelIdx);
	}
	for (int i = 0; i < faces.size(); i++) {
		outIdx.push_back(map->get_model_from_face(faces[i]));
	}

	return outIdx;
}

bool PickInfo::shouldHideSelection() {
	bool shouldHide = false;
	vector<Entity*> pickEnts = getEnts();
	for (Entity* ent : pickEnts) {
		if (!ent->hidden) {
			return true;
		}
	}

	return false;
}

void PickInfo::selectLeafFaces() {
	Bsp* map = getMap();
	faces.clear();

	for (int i = 0; i < leaves.size(); i++) {
		int idx = leaves[i];
		if (map && idx >= 0 && idx < map->leafCount) {
			BSPLEAF& leaf = map->leaves[idx];

			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				selectFace(map->marksurfs[leaf.iFirstMarkSurface + k]);
			}
		}
	}
}