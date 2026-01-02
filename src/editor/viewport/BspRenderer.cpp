#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"
#include "Editor.h"
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
#include "StudioMdlRenderer.h"
#include "TextureArray.h"
#include "tga.h"
#include "bmp.h"


BspRenderer::BspRenderer(Bsp* map, PointEntRenderer* pointEntRenderer) {
	this->map = map;
	this->pointEntRenderer = pointEntRenderer;

	for (int i = 0; i < MAX_MAP_HULLS+1; i++) {
		megaRenderClipnodes.buffer[i] = NULL;
	}

	memset(skyboxTextures, 0, sizeof(skyboxTextures));
	memset(skyboxTexturesSwap, 0, sizeof(skyboxTexturesSwap));

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

	buildTextureAtlases();
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
		string path = findAsset(wadNames[i]);

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

		// map->textures + texOffset + tex.nOffsets[0]

		glTexturesSwap[i] = new Texture(tex->nWidth, tex->nHeight, imageData);
		glTextureArray->add(glTexturesSwap[i]);
		glTexturesSwap[i]->generateMipMaps(3, palette[255]);

		if (wadTex) {
			delete[] wadTex->data;
			delete wadTex;
		}
	}

	fillTextureAtlases();

	loadSkyboxTextures();

	if (wadTexCount)
		debugf("Loaded %d wad textures\n", wadTexCount);
	if (embedCount)
		debugf("Loaded %d embedded textures\n", embedCount);
	if (missingCount)
		debugf("%d missing textures\n", missingCount);
}

void BspRenderer::loadSkyboxTextures() {
	// load skybox textures
	if (map->ents.size()) {
		memset(skyboxTexturesSwap, 0, sizeof(skyboxTexturesSwap));
		Entity* world = map->ents[0];
		string skyname = world->getKeyvalue("skyname");
		if (skyname.empty())
			skyname = "2desert";
		//static const char* skySuffixes[6] = { "ft", "bk", "lf", "rt", "up", "dn" };
		static const char* skySuffixes[6] = { "lf", "rt", "dn", "up", "bk", "ft" };

		for (int i = 0; i < 6; i++) {
			string skyPath = "gfx/env/" + skyname + skySuffixes[i];

			string path = findAsset(skyPath + ".tga");
			if (!path.empty()) {
				COLOR3* pixels;
				int width, height;
				if (loadTGA(path.c_str(), pixels, width, height)) {
					skyboxTexturesSwap[i] = new Texture(width, height, pixels);
				}
				else {
					logf("Failed to load TGA file: %s\n", path.c_str());
				}
			}
			else {
				path = findAsset(skyPath + ".bmp");

				if (!path.empty()) {
					WADTEX tex = load8BitBMP(path.c_str());
					if (tex.data) {
						COLOR3* pixels = new COLOR3[tex.nWidth * tex.nHeight];
						COLOR3* pal = tex.getPalette();
						for (int k = 0; k < tex.nWidth * tex.nHeight; k++) {
							pixels[k] = pal[tex.data[k]];
						}

						skyboxTexturesSwap[i] = new Texture(tex.nWidth, tex.nHeight, pixels);

						delete[] tex.data;
					}
					else {
						logf("Failed to load BMP as 8-bit: %s\n", path.c_str());
					}
				}
				else {
					logf("Missing skybox image: %s\n", (skyPath + ".tga").c_str());
				}
			}
		}
	}
}

void BspRenderer::buildTextureAtlases() {
	if (!g_settings.texture_atlas) {
		textureAtlasInfos.clear();
		textureAtlasInfos.reserve(map->textureCount);
		numTextureAtlasesSwap = 0;
		return;
	}

	double startTime = glfwGetTime();

	int idealMaxSize = min(g_max_texture_size, 4096);
	int maxSize = g_settings.renderer == RENDERER_OPENGL_21_LEGACY ? 1024 : idealMaxSize; // old gpu lied about max texture size
	textureAtlasSz = clamp(g_max_texture_size, 512, maxSize);
	textureAtlasZoneSz = textureAtlasSz; // 64 is too small for maps like snd, 256 or greater is slower
	if (textureAtlasZoneSz == 4096) {
		textureAtlasZoneSz = 2048;
	}

	vector<TextureAtlas*> atlases;
	atlases.push_back(new TextureAtlas(textureAtlasSz, textureAtlasSz, textureAtlasZoneSz));

	textureAtlasInfos.clear();
	textureAtlasInfos.reserve(map->textureCount);
	for (int i = 0; i < map->textureCount; i++) {
		BSPMIPTEX* tex = map->get_texture(i);

		SubTexture sub;
		sub.idx = i;
		sub.w = (tex ? tex->nWidth : 16);
		sub.h = (tex ? tex->nHeight : 16);
		sub.x = sub.y = 0;
		sub.sz = sub.w * sub.h;

		textureAtlasInfos.push_back(sub);
	}
	sort(textureAtlasInfos.begin(), textureAtlasInfos.end(), [](const SubTexture& a, const SubTexture& b) {
		return a.sz > b.sz;
	});

	debugf("Building texture atlases\n");

	int atlasId = 0;
	for (int i = 0; i < textureAtlasInfos.size(); i++) {
		SubTexture& info = textureAtlasInfos[i];

		// TODO: Try fitting in earlier atlases before using the latest one
		if (!atlases[atlasId]->insert(i, info.w, info.h, info.x, info.y)) {
			atlases.push_back(new TextureAtlas(textureAtlasSz, textureAtlasSz, textureAtlasZoneSz));
			atlasId++;

			if (!atlases[atlasId]->insert(i, info.w, info.h, info.x, info.y)) {
				logf("Texture too big for atlas size! (%dx%d)\n", info.w, info.h);
				continue;
			}
		}

		info.atlasId = atlasId;
	}

	for (int i = 0; i < atlases.size(); i++) {
		delete atlases[i];
	}

	numTextureAtlasesSwap = atlases.size();

	// so array index == texture index
	sort(textureAtlasInfos.begin(), textureAtlasInfos.end(), [](const SubTexture& a, const SubTexture& b) {
		return a.idx < b.idx;
	});

	debugf("Fit %d textures into %d atlases (%dx%d) in %.2fs\n",
		map->textureCount, atlasId + 1, textureAtlasSz, textureAtlasSz, glfwGetTime() - startTime);
}

void BspRenderer::fillTextureAtlases() {
	if (!g_settings.texture_atlas) {
		glTextureAtlasesSwap = NULL;
		return;
	}

	glTextureAtlasesSwap = new Texture*[numTextureAtlasesSwap];

	//int mipLevels = 3;
	int mipLevels = 0; // mip-map seams aren't fixable without a large perf/memory impact

	for (int i = 0; i < numTextureAtlasesSwap; i++) {
		glTextureAtlasesSwap[i] = new Texture(textureAtlasSz, textureAtlasSz);
		memset(glTextureAtlasesSwap[i]->data, 0, textureAtlasSz * textureAtlasSz * sizeof(COLOR4));

		// mip map generation for the atlas must be manual so textures don't average each other
		for (int m = 1; m <= mipLevels; m++) {
			int mipSz = textureAtlasSz >> m;

			MipTexture mip;
			mip.width = mipSz;
			mip.height = mipSz;
			mip.data = new COLOR4[mipSz * mipSz];
			mip.level = m;

			glTextureAtlasesSwap[i]->mipmaps.push_back(mip);
		}
	}

	for (int i = 0; i < textureAtlasInfos.size(); i++) {
		SubTexture& info = textureAtlasInfos[i];

		if (info.atlasId >= numTextureAtlasesSwap) {
			logf("Bad atlas texture dst id %d\n", info.atlasId);
			continue;
		}
		if (info.idx >= map->textureCount) {
			logf("Bad atlas texture src id %d\n", info.idx);
			continue;
		}

		// copy lightmap data into atlas
		COLOR4* lightSrc = (COLOR4*)(glTexturesSwap[info.idx]->data);
		COLOR4* lightDst = (COLOR4*)(glTextureAtlasesSwap[info.atlasId]->data);

		for (int y = 0; y < info.h; y++) {
			for (int x = 0; x < info.w; x++) {
				int src = y * info.w + x;
				int dst = (info.y + y) * textureAtlasSz + info.x + x;
				lightDst[dst] = lightSrc[src];
			}
		}

		for (int m = 1; m <= mipLevels; m++) {
			if (m > glTexturesSwap[info.idx]->mipmaps.size()) {
				logf("Bad mipmaps for %d\n", info.idx);
				continue;
			}

			int mipSz = textureAtlasSz >> m;

			MipTexture& mip = glTextureAtlasesSwap[info.atlasId]->mipmaps[m - 1];

			int ix = info.x >> m;
			int iy = info.y >> m;
			int iw = info.w >> m;
			int ih = info.h >> m;
			lightSrc = (COLOR4*)(glTexturesSwap[info.idx]->mipmaps[m-1].data);

			for (int y = 0; y < ih; y++) {
				for (int x = 0; x < iw; x++) {
					int src = y * iw + x;
					int dst = (iy + y) * mipSz + ix + x;
					mip.data[dst] = lightSrc[src];
				}
			}
		}

		// individual textures used in face editor, don't delete
		//delete glTexturesSwap[i];
		//glTexturesSwap[i] = NULL;
	}
}

void BspRenderer::reload() {
	g_app->isLoading = true;
	reloadTextures(); // geometry data depends on texture preloading
	updateLightmapInfos();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();
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
		glTextureAtlases = glTextureAtlasesSwap;
		numTextureAtlases = numTextureAtlasesSwap;
		for (int i = 0; i < map->textureCount; i++) {
			if (!glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGBA);
		}
		for (int i = 0; i < numTextureAtlases; i++) {
			glTextureAtlases[i]->upload(GL_RGBA);
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
	memcpy(newRenderClipnodes, renderClipnodeDat, numRenderClipnodes * sizeof(RenderClipnodes));
	memset(&newRenderClipnodes[numRenderClipnodes], 0, sizeof(RenderClipnodes));
	numRenderClipnodes++;
	renderClipnodeDat = newRenderClipnodes;
	
	generateClipnodeBuffer(modelIdx);
}

void BspRenderer::updateModelShaders() {
	activeShader = g_shaders.bsp;

	for (int i = 0; i < numRenderModels; i++) {
		RenderModel& model = renderModels[i];
		for (int k = 0; k < model.groupCount; k++) {
			model.renderGroups[k].buffer->setShader(activeShader);
		}
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
	reloadMegaBuffers();

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
	if (renderModel->renderGroups)
		delete[] renderModel->renderGroups;
	if (renderModel->renderFaces)
		delete[] renderModel->renderFaces;

	renderModel->renderGroups = NULL;
	renderModel->renderFaces = NULL;
}

void BspRenderer::deleteRenderClipnodes() {
	if (renderClipnodeDat != NULL) {
		for (int i = 0; i < numRenderClipnodes; i++) {
			deleteRenderModelClipnodes(&renderClipnodeDat[i]);
		}
		delete[] renderClipnodeDat;
	}

	renderClipnodeDat = NULL;
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
		}
		renderClip->clipnodeBuffer[i] = NULL;
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
		glTextures = NULL;
	}
	for (int i = 0; i < 6; i++) {
		if (skyboxTextures[i]) {
			delete skyboxTextures[i];
			skyboxTextures[i] = NULL;
		}
	}
	if (glTextureAtlases) {
		for (int i = 0; i < numTextureAtlases; i++) {
			delete glTextureAtlases[i];
		}
		delete[] glTextureAtlases;
		glTextureAtlases = NULL;
	}
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

	activeShader = g_shaders.bsp;

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
		SubTexture& atlasInfo = textureAtlasInfos[texinfo.iMiptex];
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

			if (g_settings.texture_atlas) {
				verts[e].ux = (atlasInfo.x / (float)textureAtlasSz);
				verts[e].uy = (atlasInfo.y / (float)textureAtlasSz);
				verts[e].uw = (atlasInfo.w / (float)textureAtlasSz);
				verts[e].uh = (atlasInfo.h / (float)textureAtlasSz);
			}
			else {
				verts[e].ux = 0;
				verts[e].uy = 0;
				verts[e].uw = 1;
				verts[e].uh = 1;
			}

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
		lightmapVert* newVerts = new lightmapVert[newCount];

		int idx = 0;
		for (int k = 2; k < face.nEdges; k++, idx += 3) {
			// reverse order due to coordinate system swap
			newVerts[idx+0] = verts[k];
			newVerts[idx+1] = verts[k - 1];
			newVerts[idx+2] = verts[0];

			// barycentric coords for wireframe edge detection
			newVerts[idx + 0].bx = 255;
			newVerts[idx + 0].by = 0;
			newVerts[idx + 0].bz = 0;

			newVerts[idx + 1].bx = 0;
			newVerts[idx + 1].by = 255;
			newVerts[idx + 1].bz = 0;

			newVerts[idx + 2].bx = 0;
			newVerts[idx + 2].by = 0;
			newVerts[idx + 2].bz = 255;

			// select which edges to draw (not the inner edges of the fan)
			for (int j = 0; j < 3; j++) {
				newVerts[idx + j].ex = k == 2;
				newVerts[idx + j].ey = k == face.nEdges - 1;
				newVerts[idx + j].ez = 1;
			}
		}

		delete[] verts;
		verts = newVerts;
		vertCount = newCount;

		Texture* gltex = texturesLoaded && numLoadedTextures > 0 ? glTextures[min((uint)numLoadedTextures-1, texinfo.iMiptex)] : greyTex;

		// add face to a render group (faces that share that same texture array, lightmaps, and opacity flag)
		bool isTransparent = opacity < 1.0f;
		int groupIdx = -1;
		for (int k = 0; k < renderGroups.size(); k++) {
			// split groups on unique texture IDs
			bool textureMatch = !texturesLoaded || 
				renderGroups[k].arrayTextureIdx == miptexToTexArray[texinfo.iMiptex].arrayIdx;
			if (g_settings.texture_atlas) {
				// using texture atlases instead of arrays
				textureMatch = !texturesLoaded ||
					renderGroups[k].atlasTextureIdx == textureAtlasInfos[texinfo.iMiptex].atlasId;
			}
			else if (!g_opengl_texture_array_support && !g_opengl_3d_texture_support) {
				// no batching possible, fall back to one texture ID per texture (ultra slow)
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
			newGroup.atlasTextureIdx = textureAtlasInfos[texinfo.iMiptex].atlasId;
			newGroup.texture = texturesLoaded ? gltex : greyTex;
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				newGroup.lightmapAtlas[s] = lightmapAtlas[s];
			}
			renderGroups.push_back(newGroup);
			renderGroupVerts.push_back(vector<lightmapVert>());
			groupIdx = renderGroups.size() - 1;

			/*
			logf("Render group %d using atlas %d transparent %d, lightatlas %d %d %d %d\n",
				groupIdx, newGroup.atlasTextureIdx, (int)isTransparent,
				lightmapAtlas[0] ? lightmapAtlas[0]->id : -1,
				lightmapAtlas[1] ? lightmapAtlas[1]->id : -1,
				lightmapAtlas[2] ? lightmapAtlas[2]->id : -1,
				lightmapAtlas[3] ? lightmapAtlas[3]->id : -1);
			*/
		}

		renderModel->renderFaces[i].group = groupIdx;
		renderModel->renderFaces[i].vertOffset = renderGroupVerts[groupIdx].size();
		renderModel->renderFaces[i].vertCount = vertCount;

		renderGroupVerts[groupIdx].insert(renderGroupVerts[groupIdx].end(), verts, verts + vertCount);

		delete[] verts;
	}

	renderModel->renderGroups = new RenderGroup[renderGroups.size()];
	renderModel->groupCount = renderGroups.size();

	for (int i = 0; i < renderGroups.size(); i++) {
		renderGroups[i].verts = new lightmapVert[renderGroupVerts[i].size()];
		renderGroups[i].vertCount = renderGroupVerts[i].size();
		memcpy(renderGroups[i].verts, &renderGroupVerts[i][0], renderGroups[i].vertCount * sizeof(lightmapVert));

		renderGroups[i].buffer = new VertexBuffer(activeShader, 0);
		renderGroups[i].buffer->addAttribute(3, GL_FLOAT, 0, "vTex");
		renderGroups[i].buffer->addAttribute(4, GL_FLOAT, 0, "vAtlas", g_settings.texture_atlas);
		renderGroups[i].buffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vBary");
		renderGroups[i].buffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vEdgeEnable");
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

	for (int i = 0; i < model.nFaces; i++) {
		refreshFace(model.iFirstFace + i);
	}

	if (refreshClipnodes) {
		refreshModelClipnodes(modelIdx);
	}

	return renderModel->groupCount;
}

bool BspRenderer::RenderGroupsAreCombinable(RenderGroup& groupa, RenderGroup& groupb) {
	if (g_settings.texture_atlas) {
		if (groupa.atlasTextureIdx != groupb.atlasTextureIdx)
			return false;
	}
	else if (g_opengl_3d_texture_support || g_opengl_texture_array_support) {
		if (groupa.arrayTextureIdx != groupb.arrayTextureIdx)
			return false;
	}
	else if (groupa.texture != groupb.texture) {
		return false;
	}
	
	if (groupa.transparent != groupb.transparent)
		return false;

	for (int s = 0; s < MAXLIGHTMAPS; s++) 
		if (groupa.lightmapAtlas[s] != groupb.lightmapAtlas[s])
			return false;
	
	return true;
}

void BspRenderer::refreshMegaBuffers(vector<OrderedEnt>& ents) {
	if (g_app->pickCount == megaGroupUpdateIdx)
		return;
	megaGroupUpdateIdx = g_app->pickCount;

	float start = glfwGetTime();

	for (MegaRenderGroup& mega : megaRenderGroups) {
		delete mega.group.buffer;
	}

	for (int i = 0; i < MAX_MAP_HULLS+1; i++) {
		delete megaRenderClipnodes.buffer[i];
		megaRenderClipnodes.buffer[i] = NULL;
	}
	memset(megaRenderClipnodes.totalVerts, 0, sizeof(megaRenderClipnodes.totalVerts));
	megaRenderClipnodes.refs.clear();

	megaRenderGroups.clear();
	megaGroupEnts.clear();
	int totalModelGroups = 0;

	// find which entities can be included in the mega buffer and tally vertex counts
	for (int i = 0; i < ents.size(); i++) {
		OrderedEnt& ent = ents[i];
		EntRenderOpts& opts = ent.ent->getRenderOpts();

		// don't combine models for entities that have special rendering properties applied
		if (ent.modelIdx == -1 || ent.ent->highlighted || ent.ent->hidden)
			continue;
		if ((g_settings.render_flags & RENDER_RENDER_MODES) || g_app->previewMode) {
			if (opts.rendermode != RENDER_MODE_NORMAL)
				continue;
		}
		megaGroupEnts.insert(ent.entIdx);

		RenderModel& model = renderModels[ent.modelIdx];
		totalModelGroups += model.groupCount;
		for (int g = 0; g < model.groupCount; g++) {
			RenderGroup& group = model.renderGroups[g];

			bool wasCombined = false;
			for (int k = 0; k < megaRenderGroups.size(); k++) {
				MegaRenderGroup& mega = megaRenderGroups[k];

				if (RenderGroupsAreCombinable(group, mega.group)) {
					mega.group.vertCount += group.vertCount;
					mega.refs.push_back({i, g});
					wasCombined = true;
					break;
				}
			}

			if (!wasCombined) {
				MegaRenderGroup newGroup;
				newGroup.group = group;
				newGroup.refs.push_back({ i, g });
				megaRenderGroups.push_back(newGroup);
			}
		}

		if (clipnodesLoaded) {
			megaRenderClipnodes.refs.push_back(i);
			for (int k = 0; k < MAX_MAP_HULLS+1; k++) {
				int hull = k;

				if (hull == MAX_MAP_HULLS) {
					hull = getBestClipnodeHull(ent.modelIdx);

					if (hull == -1)
						continue; // has no clipnodes

					if (renderModels[ent.modelIdx].groupCount > 0)
						continue; // has faces. clipnodes won't be shown in auto mode
				}

				VertexBuffer* buf = renderClipnodeDat[ent.modelIdx].clipnodeBuffer[hull];
				if (buf) {
					megaRenderClipnodes.totalVerts[k] += buf->numVerts;
				}
			}
		}
	}

	// create solid models buffer
	for (int i = 0; i < megaRenderGroups.size(); i++) {
		MegaRenderGroup& mega = megaRenderGroups[i];

		// solid models
		lightmapVert* verts = new lightmapVert[mega.group.vertCount];
		int vertIdx = 0;

		for (EntModelGroupIdx& ref : mega.refs) {
			OrderedEnt& ent = ents[ref.entIdx];
			RenderGroup& refGroup = renderModels[ent.modelIdx].renderGroups[ref.groupIdx];

			for (int k = 0; k < refGroup.vertCount; k++, vertIdx++) {
				verts[vertIdx] = refGroup.verts[k];

				vec3* v = (vec3*)(&verts[vertIdx].x);
				*v = ent.transform.multRowMajor(*v);
			}
		}

		if (vertIdx == 0)
			continue;

		VertexBuffer* megaBuffer = new VertexBuffer(g_shaders.bsp, 0, verts, mega.group.vertCount);
		megaBuffer->addAttributes(mega.group.buffer->attribs);
		megaBuffer->ownData = true;
		mega.group.buffer = megaBuffer;
		mega.group.buffer->upload();
	}

	// create clipnode models buffer
	for (int i = 0; i < MAX_MAP_HULLS+1 && clipnodesLoaded; i++) {
		clipnodeVert* verts = new clipnodeVert[megaRenderClipnodes.totalVerts[i]];

		int vertIdx = 0;
		VertexBuffer* sampleBuf = NULL;
		for (int idx : megaRenderClipnodes.refs) {
			OrderedEnt& ent = ents[idx];

			int hull = i;

			if (hull == MAX_MAP_HULLS) {
				hull = getBestClipnodeHull(ent.modelIdx);

				if (hull == -1)
					continue; // has no clipnodes

				if (renderModels[ent.modelIdx].groupCount > 0)
					continue; // has faces. clipnodes won't be shown in auto mode
			}

			VertexBuffer* srcBuf = renderClipnodeDat[ent.modelIdx].clipnodeBuffer[hull];
			if (!srcBuf)
				continue;

			sampleBuf = srcBuf;
			int numVerts = srcBuf->numVerts;
			clipnodeVert* srcData = (clipnodeVert*)srcBuf->data;

			for (int k = 0; k < numVerts; k++, vertIdx++) {
				verts[vertIdx] = srcData[k];

				verts[vertIdx].pos = ent.transform.multRowMajor(srcData[k].pos);
			}
		}

		if (vertIdx == 0)
			continue;

		VertexBuffer* megaBuffer = new VertexBuffer(g_shaders.clipnode, 0, verts, megaRenderClipnodes.totalVerts[i]);
		megaBuffer->addAttributes(sampleBuf->attribs);
		megaBuffer->ownData = true;
		megaRenderClipnodes.buffer[i] = megaBuffer;
		megaRenderClipnodes.buffer[i]->upload();
	}

	logf("Created %d mega groups from %d model groups in %dms\n",
		(int)megaRenderGroups.size(), totalModelGroups, (int)((glfwGetTime() - start)*1000));
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
		renderLeafDat->leafBuffer = NULL;
	}

	if (leafNavMesh) {
		delete leafNavMesh;
		leafNavMesh = NULL;
	}

	deleteRenderModelClipnodes(&renderClipnodeDat[modelIdx]);
	generateClipnodeBuffer(modelIdx);

	RenderClipnodes& renderClip = renderClipnodeDat[modelIdx];

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (renderClip.clipnodeBuffer[i])
			renderClip.clipnodeBuffer[i]->upload();
	}

	return true;
}

void BspRenderer::loadClipnodes() {
	numRenderClipnodes = map->modelCount;
	renderClipnodeDat = new RenderClipnodes[numRenderClipnodes];
	memset(renderClipnodeDat, 0, numRenderClipnodes * sizeof(RenderClipnodes));

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
	RenderClipnodes* renderClip = &renderClipnodeDat[0];
	renderClip->clipnodeBuffer[hull] = NULL;

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

	vector<clipnodeVert> allVerts;
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
				allVerts.push_back(clipnodeVert(renderVerts[0], faceColor));
				allVerts.push_back(clipnodeVert(renderVerts[k - 1], faceColor));
				allVerts.push_back(clipnodeVert(renderVerts[k], faceColor));
			}
		}
	}

	clipnodeVert* output = new clipnodeVert[allVerts.size()];
	for (int i = 0; i < allVerts.size(); i++) {
		output[i] = allVerts[i];
	}

	if (allVerts.size() == 0) {
		renderClip->clipnodeBuffer[hull] = NULL;
		return;
	}

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(g_shaders.color, COLOR_4B | POS_3F, output, allVerts.size());
	renderClip->clipnodeBuffer[hull]->ownData = true;

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

	vector<clipnodeVert> allVerts;

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

		const vec3 lightDir = vec3(1, 1, -1).normalize();
		float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
		if (dot > 0.5f) {
			dot = dot * dot;
		}
		COLOR4 faceColor = color * (dot);

		// convert from TRIANGLE_FAN style verts to TRIANGLES
		for (int k = 2; k < renderVerts.size(); k++) {
			allVerts.push_back(clipnodeVert(renderVerts[0], faceColor));
			allVerts.push_back(clipnodeVert(renderVerts[k - 1], faceColor));
			allVerts.push_back(clipnodeVert(renderVerts[k], faceColor));
		}
	}

	if (allVerts.size() == 0) {
		return;
	}

	clipnodeVert* output = new clipnodeVert[allVerts.size()];
	for (int i = 0; i < allVerts.size(); i++) {
		output[i] = allVerts[i];
	}

	if (node->face_buffer) {
		delete node->face_buffer;
	}

	node->face_buffer = new VertexBuffer(g_shaders.clipnode, 0, output, allVerts.size());
	node->face_buffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vBary");
	node->face_buffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vEdgeEnable");
	node->face_buffer->addAttribute(4, GL_UNSIGNED_BYTE, 1, "vColor");
	node->face_buffer->addAttribute(3, GL_FLOAT, 0, "vPosition");
	node->face_buffer->ownData = true;
}

void BspRenderer::generateClipnodeBuffer(int modelIdx) {
	BSPMODEL& model = map->models[modelIdx];
	RenderClipnodes* renderClip = &renderClipnodeDat[modelIdx];

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		renderClip->clipnodeBuffer[i] = NULL;
	}

	Clipper clipper;
	
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, i, CONTENTS_SOLID);
		
		static COLOR4 hullColors[] = {
			COLOR4(255, 255, 255, 255),
			COLOR4(96, 255, 255, 255),
			COLOR4(255, 96, 255, 255),
			COLOR4(255, 255, 96, 255),
		};
		COLOR4 color = hullColors[i];

		vector<clipnodeVert> allVerts;
		vector<FaceMath> faceMaths;

		for (int k = 0; k < solidNodes.size(); k++) {
			clipnodeLeafCount++;
			generateNodeMesh(&solidNodes[k], color, allVerts, faceMaths, solidNodes[k].nodeIdx);
		}

		clipnodeVert* output = new clipnodeVert[allVerts.size()];
		for (int i = 0; i < allVerts.size(); i++) {
			output[i] = allVerts[i];
		}

		if (allVerts.size() == 0) {
			renderClip->clipnodeBuffer[i] = NULL;
			continue;
		}

		renderClip->clipnodeBuffer[i] = new VertexBuffer(g_shaders.clipnode, 0, output, allVerts.size());
		renderClip->clipnodeBuffer[i]->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vBary");
		renderClip->clipnodeBuffer[i]->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vEdgeEnable");
		renderClip->clipnodeBuffer[i]->addAttribute(4, GL_UNSIGNED_BYTE, 1, "vColor");
		renderClip->clipnodeBuffer[i]->addAttribute(3, GL_FLOAT, 0, "vPosition");
		renderClip->clipnodeBuffer[i]->ownData = true;

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
	leafNavMesh = NULL;

	Clipper clipper;

	vector<NodeVolumeCuts> leafNodes = map->get_model_leaf_volume_cuts(0, 0, CONTENTS_NOT_LEAF_0);
	static COLOR4 color = COLOR4(255, 255, 255, 255);

	vector<clipnodeVert> allVerts;
	vector<FaceMath> faceMaths;

	for (int i = 0; i < 65536; i++) {
		renderLeafDat->leafRanges[i].clear();
	}

	for (int k = 0; k < leafNodes.size(); k++) {
		int leafIdx = leafNodes[k].leafIdx;
		int start = allVerts.size();
		generateNodeMesh(&leafNodes[k], color, allVerts, faceMaths, leafNodes[k].leafIdx);
		
		for (int i = start; i < allVerts.size(); i++) {
			renderLeafDat->leafRanges[leafIdx].push_back(i);
		}
	}

	clipnodeVert* output = new clipnodeVert[allVerts.size()];
	for (int i = 0; i < allVerts.size(); i++) {
		output[i] = allVerts[i];
	}

	if (allVerts.size() == 0) {
		return;
	}

	renderLeafDat->leafBuffer = new VertexBuffer(g_shaders.clipnode, 0, output, allVerts.size());
	renderLeafDat->leafBuffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vBary");
	renderLeafDat->leafBuffer->addAttribute(3, GL_UNSIGNED_BYTE, 0, "vEdgeEnable");
	renderLeafDat->leafBuffer->addAttribute(4, GL_UNSIGNED_BYTE, 1, "vColor");
	renderLeafDat->leafBuffer->addAttribute(POS_3F, "vPosition");
	renderLeafDat->leafBuffer->ownData = true;

	renderLeafDat->faceMaths = faceMaths;

	renderLeafDat->originalColors.resize(allVerts.size());
	for (int i = 0; i < allVerts.size(); i++) {
		renderLeafDat->originalColors[i] = allVerts[i].c;
	}

	leafNavMesh = LeafNavMeshGenerator().generate(map, true, CONTENTS_NOT_LEAF_0, 0);
}

void BspRenderer::generateNodeMesh(NodeVolumeCuts* volume, COLOR4 color, vector<clipnodeVert>& allVerts,
	vector<FaceMath>& faceMaths, int elementIndex) {
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

			vec3 lightDir = vec3(1, 1, -1).normalize();
			float dot = (dotProduct(normal * -1, lightDir) + 1) / 2.0f;
			if (dot > 0.5f) {
				dot = dot * dot;
			}
			COLOR4 faceColor = color * (dot);

			// convert from TRIANGLE_FAN style verts to TRIANGLES
			for (int k = 2; k < faceVerts.size(); k++) {
				clipnodeVert verts[3] = {
					clipnodeVert(faceVerts[0], faceColor),
					clipnodeVert(faceVerts[k - 1], faceColor),
					clipnodeVert(faceVerts[k], faceColor),
				};

				// barycentric coords for wireframe edge detection
				verts[0].bx = 1;
				verts[0].by = 0;
				verts[0].bz = 0;

				verts[1].bx = 0;
				verts[1].by = 1;
				verts[1].bz = 0;

				verts[2].bx = 0;
				verts[2].by = 0;
				verts[2].bz = 1;

				// select which edges to draw (not the inner edges of the fan)
				for (int j = 0; j < 3; j++) {
					verts[j].ex = 1;
					verts[j].ey = k == faceVerts.size() - 1;
					verts[j].ez = k == 2;
					
					allVerts.push_back(verts[j]);
				}
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

	pointEnts = new VertexBuffer(g_shaders.color, COLOR_4B | POS_3F, entCubes, numPointEnts * 6 * 6);
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
	if (skyBoxBuffer)
		delete skyBoxBuffer;
	for (MegaRenderGroup& mega : megaRenderGroups) {
		delete mega.group.buffer;
	}
	for (int i = 0; i < MAX_MAP_HULLS+1; i++) {
		delete megaRenderClipnodes.buffer[i];
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
		glTextureAtlases = glTextureAtlasesSwap;
		numTextureAtlases = numTextureAtlasesSwap;
		memcpy(skyboxTextures, skyboxTexturesSwap, sizeof(skyboxTextures));

		// non-3D version of textures needed for GUI
		for (int i = 0; i < map->textureCount; i++) {
			if (glTextures[i] && !glTextures[i]->uploaded)
				glTextures[i]->upload(GL_RGBA);
		}
		for (int i = 0; i < 6; i++) {
			if (skyboxTextures[i]) {
				skyboxTextures[i]->upload(GL_RGB, true);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // Note: GL_CLAMP is significantly slower
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
		}
		for (int i = 0; i < numTextureAtlases; i++) {
			//lodepng_encode32_file("atlas_mip.png", (byte*)glTextureAtlases[i]->mipmaps[1].data,
			//	glTextureAtlases[i]->mipmaps[1].width, glTextureAtlases[i]->mipmaps[1].height);
			//lodepng_encode32_file(cstrf("atlas_%d.png", i), glTextureAtlasesSwap[i]->data, textureAtlasSz, textureAtlasSz);
			glTextureAtlases[i]->upload(GL_RGBA);

			// disable mip-maps because they show seams which can't be fixed without 4x texture memory
			// and atlas size. https://0fps.net/2013/07/09/texture-atlases-wrapping-and-mip-mapping/
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}

		glTextureArray->upload();
		numLoadedTextures = map->textureCount;

		texturesLoaded = true;

		preRenderFaces();

		textureFacesLoaded = true;
	}

	if (!clipnodesLoaded && clipnodesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {

		for (int i = 0; i < numRenderClipnodes; i++) {
			RenderClipnodes& clip = renderClipnodeDat[i];
			for (int k = 0; k < MAX_MAP_HULLS; k++) {
				if (clip.clipnodeBuffer[k]) {
					clip.clipnodeBuffer[k]->upload();
				}
			}
		}

		clipnodesLoaded = true;
		reloadMegaBuffers();
		debugf("Loaded %d clipnode leaves\n", clipnodeLeafCount);
	}

	if (!leavesThreadFinished && leavesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		leavesLoaded = renderLeafDat != NULL;
		leavesThreadFinished = true;

		if (renderLeafDat) {
			renderLeafDat->leafBuffer->upload();

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

	clipnodeVert* verts = (clipnodeVert*)renderLeafDat->leafBuffer->data;

	if (!highlight) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			COLOR4& og = renderLeafDat->originalColors[i];
			verts[i].c.r = og.r;
			verts[i].c.g = og.g;
			verts[i].c.b = og.b;
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
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
}

void BspRenderer::hideLeaves(bool hideNotUnhide) {
	if (!leavesLoaded || !renderLeafDat->leafBuffer)
		return;

	clipnodeVert* verts = (clipnodeVert*)renderLeafDat->leafBuffer->data;

	if (!hideNotUnhide) {
		for (int i = 0; i < renderLeafDat->leafBuffer->numVerts; i++) {
			verts[i].c.a = renderLeafDat->originalColors[i].a;
		}
	}
	else {
		for (auto leafIdx : g_app->hiddenLeaves) {
			for (int idx : renderLeafDat->leafRanges[leafIdx]) {
				verts[idx].c.a = 0;
			}
		}
	}

	renderLeafDat->leafBuffer->deleteBuffer();
	renderLeafDat->leafBuffer->upload();
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
		ent.isInMegaRenderGroup = megaGroupEnts.count(i);
		ent.ent = map->ents[i];
		ent.entIdx = i;
		ent.modelIdx = map->ents[i]->getBspModelIdx();
		if (ent.modelIdx >= map->modelCount) {
			continue;
		}
		const mat4x4& rotMat = map->ents[i]->getRotationMatrix(false);
		ent.transform = renderEnts[i].modelMat * rotMat;

		ent.transformWorld = renderEnts[i].modelMat;
		ent.transformWorld.translate(renderOffset.x, renderOffset.y, renderOffset.z);
		ent.transformWorld = ent.transformWorld * rotMat;

		ents.push_back(ent);
	}
	for (Entity* ent : g_app->pickInfo.getEnts()) {
		ent->highlighted = true;
	}

	// draw highlighted ents last
	sort(ents.begin(), ents.end(), [](const OrderedEnt& a, const OrderedEnt& b) {
		return a.ent->highlighted < b.ent->highlighted;
	});

	refreshMegaBuffers(ents);
}

void BspRenderer::renderSolids(const vector<OrderedEnt>& orderedEnts, bool highlightAlwaysOnTop, bool transparencyPass) {
	if (map->ents.empty())
		return;
	
	BSPMODEL& world = map->models[0];

	activeShader = g_shaders.bsp;
	activeShader->bind();
	activeShader->modelMat->loadIdentity();
	activeShader->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	activeShader->updateMatrixes();

	bool allWireframes = (g_settings.render_flags & RENDER_WIREFRAME);
	activeShader->setUniform("wireframeEnable", allWireframes);

	//glDisable(GL_CULL_FACE); // too expensive on fill-rate limited hardware
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	if ((g_settings.render_flags & RENDER_LIGHTMAPS) && (g_settings.render_flags & RENDER_TEXTURES)) {
		activeShader->setUniform("gamma", 1.5f);
	}
	else {
		activeShader->setUniform("gamma", 1.0f);
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

	for (MegaRenderGroup& mega : megaRenderGroups) {
		RenderGroup& rgroup = mega.group;

		if (rgroup.transparent != transparencyPass)
			continue;
		if (rgroup.transparent && !(g_settings.render_flags & RENDER_SPECIAL_ENTS))
			continue;

		drawModelRenderGroup(rgroup, false, true);
	}

	activeShader->setUniform("wireframeEnable", 1);

	int renderEnts = 0;
	activeShader->pushMatrix(MAT_MODEL);
	for (int i = 0, sz = orderedEnts.size(); i < sz; i++) {
		const OrderedEnt& orderEnt = orderedEnts[i];
		int modelIdx = orderEnt.modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount) {
			Entity* ent = orderEnt.ent;
			if (ent->hidden || orderEnt.isInMegaRenderGroup)
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
				activeShader->setUniform("wireframeColorDark", vec4(0.1f, 0.1f, 1, 1));
				activeShader->setUniform("wireframeColorBright", vec4(0, 0, 0.5f, 1));
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

void BspRenderer::renderClipnodes(const vector<OrderedEnt>& orderedEnts, int clipnodeHull) {
	if (map->ents.empty() || !clipnodesLoaded || g_app->previewMode)
		return;

	BSPMODEL& world = map->models[0];

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthFunc(GL_LEQUAL);

	// clipnodes are drawn in a separate pass to prevent interleaving shader binds
	g_shaders.clipnode->bind();

	if (g_settings.render_flags & RENDER_CLIPNODE_OPAQUE)
		g_shaders.clipnode->setUniform("opacity", 1);
	else
		g_shaders.clipnode->setUniform("opacity", 0.5f);

	if (g_settings.render_flags & RENDER_WORLD_CLIPNODES && clipnodeHull != -1 && !map->ents[0]->hidden) {
		drawModelClipnodes(0, false, clipnodeHull);
	}

	if (!(g_settings.render_flags & RENDER_ENTS) || !(g_settings.render_flags & RENDER_ENT_CLIPNODES))
		return;

	int groupHull = clipnodeHull;
	if (groupHull == -1)
		groupHull = MAX_MAP_HULLS;
	VertexBuffer* buffer = megaRenderClipnodes.buffer[groupHull];
	if (buffer)
		buffer->draw(GL_TRIANGLES);

	g_shaders.clipnode->pushMatrix(MAT_MODEL);
	for (int i = 0, sz = orderedEnts.size(); i < sz; i++) {
		const OrderedEnt& orderEnt = orderedEnts[i];
		int modelIdx = orderEnt.modelIdx;

		if (modelIdx >= 0 && modelIdx < map->modelCount) {
			Entity* ent = orderEnt.ent;
			if (ent->hidden || orderEnt.isInMegaRenderGroup)
				continue;

			RenderClipnodes& clip = renderClipnodeDat[modelIdx];
			if (clipnodeHull == -1 && getBestClipnodeHull(modelIdx) == -1) {
				continue; // skip if no hull can be drawn
			}

			if (clipnodeHull == -1 && renderModels[modelIdx].groupCount > 0) {
				continue; // skip rendering for models that have faces, if in auto mode
			}

			*g_shaders.clipnode->modelMat = orderEnt.transform;
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
			renderLeafDat->leafBuffer->draw(GL_TRIANGLES);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	delayLoadData();
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

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS))) {
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

void BspRenderer::drawModelClipnodes(int modelIdx, bool highlight, int hullIdx) {
	RenderClipnodes& clip = renderClipnodeDat[modelIdx];

	if (hullIdx == -1) {
		hullIdx = getBestClipnodeHull(modelIdx);
		if (hullIdx == -1) {
			return; // nothing can be drawn
		}
	}
	
	if (clip.clipnodeBuffer[hullIdx]) {
		clip.clipnodeBuffer[hullIdx]->draw(GL_TRIANGLES);
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
				g_shaders.color->pushMatrix(MAT_MODEL);
				*g_shaders.color->modelMat = renderEnts[i].modelMat;
				g_shaders.color->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
				g_shaders.color->updateMatrixes();

				if (ent->highlighted)
					renderEnts[i].pointEntCube->selectBuffer->draw(GL_TRIANGLES);
				else
					renderEnts[i].pointEntCube->buffer->draw(GL_TRIANGLES);
				renderEnts[i].pointEntCube->wireframeBuffer->draw(GL_LINES);

				g_shaders.color->popMatrix(MAT_MODEL);
			}
		}

		pointEntIdx++;
	}

	if (pointEntIdx - nextRangeDrawIdx > 0) {
		pointEnts->drawRange(GL_TRIANGLES, cubeVerts * nextRangeDrawIdx, cubeVerts * pointEntIdx);
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

		skyBoxBuffer = new VertexBuffer(g_shaders.texture, 0, &cube, 6 * 6);
		skyBoxBuffer->addAttribute(TEX_2F, "vTex");
		skyBoxBuffer->addAttribute(POS_3F, "vPosition");
		skyBoxBuffer->upload();
	}

	for (int i = 0; i < 6; i++) {
		if (!skyboxTextures[i])
			continue;

		skyboxTextures[i]->bind();
		skyBoxBuffer->drawRange(GL_TRIANGLES, i*6, i*6 + 6);
	}

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}

void BspRenderer::drawPvs() {
	if (!pvsDat || !pvsDat->wireframePvsBuffer)
		return;

	glDisable(GL_DEPTH_TEST);

	g_shaders.vec3->bind();
	g_shaders.vec3->modelMat->loadIdentity();
	g_shaders.vec3->modelMat->translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_shaders.vec3->updateMatrixes();
	g_shaders.vec3->setUniform("color", vec4(1, 1, 1, 1));

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
	int ileaf = map->ents.size() ? map->get_leaf(viewOrigin, 0) : 0;

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

		pvsDat->wireframePvsBuffer = new VertexBuffer(g_shaders.vec3, POS_3F, vertDat, allVerts.size());
		pvsDat->wireframePvsBuffer->ownData = true;
		pvsDat->wireframePvsBuffer->upload();
	}
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
		faceMath.verts[k] = rotVert;
	}
}

void BspRenderer::pickFrustum(Frustum& frustum, unordered_set<int>& pickEnts,
	unordered_set<int>& pickFaces, unordered_set<int>& pickLeaves, int hullIdx) {
	vec3 pickOffset = vec3(mapOffset.x, mapOffset.y, mapOffset.z);
	frustum.origin -= mapOffset;

	if (!map || map->ents.size() == 0) {
		return;
	}

	unordered_set<int> pickFacesWorld;
	if (!map->ents[0]->hidden) {
		pickFrustumFaces(frustum, pickFacesWorld, vec3(), vec3(), 0, hullIdx, 0);
		for (int idx : pickFacesWorld)
			pickFaces.insert(idx);
	}
	if (g_app->pickMode == PICK_LEAF) {
		pickFrustumLeaves(frustum, pickLeaves);
	}

	bool renderSmallSprites = !(g_settings.render_flags & RENDER_RENDER_MODES) && !g_app->previewMode;

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
			}
			else if (!isSpecial && !(g_settings.render_flags & RENDER_ENTS)) {
				continue;
			}

			vec3 angles = map->ents[i]->canRotate() ? renderEnts[i].angles : vec3();
			unordered_set<int> pickFacesOne;
			pickFrustumFaces(frustum, pickFacesOne, renderEnts[i].offset, angles, modelIdx, hullIdx, i);
			if (pickFacesOne.size()) {
				for (int idx : pickFacesOne) {
					pickFaces.insert(idx);
				}
				pickEnts.insert(i);
			}
		}
		else if (i > 0 && g_settings.render_flags & RENDER_POINT_ENTS) {
			vec3 mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			vec3 maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;

			g_app->debugVec0 = mins;
			g_app->debugVec1 = maxs;

			if (isBoxInView(mins, maxs, frustum, 0)) {
				pickEnts.insert(i);
			}
			else if (ent->cachedMdl && !ent->isIconSprite && !renderSmallSprites && ent->cachedMdl->pick(frustum, ent)) {
				pickEnts.insert(i);
			}
		}
	}
}

void BspRenderer::pickFrustumFaces(Frustum frustum, unordered_set<int>& pickFaces, vec3 offset,
	vec3 rot, int modelIdx, int hullIdx, int testEntidx) {
	BSPMODEL& model = map->models[modelIdx];

	if (!(g_settings.render_flags & (RENDER_TEXTURES | RENDER_LIGHTMAPS))) {
		return;
	}
	if (map->modelCount == 0)
		return;

	frustum.origin -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode;

	bool hasAngles = rot != vec3();
	mat4x4 angleTransform = map->ents[testEntidx]->getRotationMatrix(true);

	for (int k = 0; k < model.nFaces && model.iFirstFace + k < map->faceCount; k++) {
		if (g_app->hiddenFaces.count(model.iFirstFace + k))
			continue;

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

		if (isPolyInView(Polygon3D(faceMath.verts, true), frustum)) {
			pickFaces.insert(model.iFirstFace + k);
		}
	}

	bool selectWorldClips = modelIdx == 0 && (g_settings.render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_settings.render_flags & RENDER_ENT_CLIPNODES);

	if (hullIdx == -1 && renderModels[modelIdx].groupCount == 0) {
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx != -1) {
		for (int i = 0; i < renderClipnodeDat[modelIdx].faceMaths[hullIdx].size(); i++) {
			FaceMath faceMath = renderClipnodeDat[modelIdx].faceMaths[hullIdx][i];

			if (hasAngles) {
				rotateFaceMath(faceMath, angleTransform);
			}

			if (isPolyInView(Polygon3D(faceMath.verts, true), frustum)) {
				pickFaces.insert(-1); // face index doesn't matter for ent selection
			}
		}
	}
}

void BspRenderer::pickFrustumLeaves(Frustum frustum, unordered_set<int>& pickLeaves) {
	BSPMODEL& model = map->models[0];

	if (map->modelCount == 0)
		return;

	if (leavesLoaded) {
		for (int i = 0; i < renderLeafDat->faceMaths.size(); i++) {
			FaceMath faceMath = renderLeafDat->faceMaths[i];

			if (g_app->hiddenLeaves.count(faceMath.index))
				continue;

			if (isPolyInView(Polygon3D(faceMath.verts, true), frustum)) {
				pickLeaves.insert(faceMath.index);
			}
		}
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

	bool renderSmallSprites = !(g_settings.render_flags & RENDER_RENDER_MODES) && !g_app->previewMode;

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
			else if (ent->cachedMdl) {
				bool bigSprite = !ent->isIconSprite && !renderSmallSprites;
				bool shouldPickModel = bigSprite || ent->cachedMdl->isStudioModel();
				if (shouldPickModel && ent->cachedMdl->pick(start, dir, ent, bestDist)) {
					entIdx = i;
					foundBetterPick = true;
				}
			}
		}
	}

	return foundBetterPick;
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
	bool skipSpecial = !(g_settings.render_flags & RENDER_SPECIAL) || g_app->previewMode;

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
		for (int i = 0; i < renderClipnodeDat[modelIdx].faceMaths[hullIdx].size(); i++) {
			FaceMath faceMath = renderClipnodeDat[modelIdx].faceMaths[hullIdx][i];

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

	RenderClipnodes& clip = renderClipnodeDat[modelIdx];

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

EntCube* BspRenderer::getEntCube(int idx) {
	return renderEnts[idx].pointEntCube;
}
