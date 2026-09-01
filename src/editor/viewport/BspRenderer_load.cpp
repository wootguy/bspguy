#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "ShaderProgram.h"
#include "BaseRenderer.h"
#include "Texture.h"
#include "TextureArray.h"
#include "TextureAtlas.h"
#include "NavMesh.h"
#include "NavMeshGenerator.h"
#include "LeafNavMesh.h"
#include "LeafNavMeshGenerator.h"
#include "Entity.h"
#include "Editor.h"
#include "PointEntRenderer.h"
#include "quant.h"
#include "tga.h"
#include "bmp.h"

#include <algorithm>

void BspRenderer::loadTextures() {
	for (int i = 0; i < wads.size(); i++) {
		delete wads[i];
	}
	wads.clear();

	vector<string> wadNames = map->get_wad_names();
	vector<string> tryPaths = getAssetPaths();

	int numMips = g_settings.texture_atlas ? 0 : 3;

	for (int i = 0; i < wadNames.size(); i++) {
		string path = findAsset(wadNames[i]);

		if (path.empty()) {
			warnf("Missing WAD: %s\n", wadNames[i].c_str());
			continue;
		}

		debugf("Loading WAD %s\n", path.c_str());
		Wad* wad = new Wad(path);
		wad->readInfo();
		wads.push_back(wad);
	}

	int wadTexCount = 0;
	int missingCount = 0;
	int embedCount = 0;

	COLOR3* palAtlas = NULL;
	glPaletteSwap = NULL;
	if (g_settings.pal_textures) {
		palAtlasHeight = 16;
		while (palAtlasHeight <= map->textureCount / 4) {
			palAtlasHeight *= 2;
		}

		palAtlas = new COLOR3[palAtlasWidth * palAtlasHeight];
		memset(palAtlas, 0, sizeof(COLOR3) * palAtlasWidth * palAtlasHeight);
		glPaletteSwap = new Texture(palAtlasWidth, palAtlasHeight, palAtlas);
		if (map->textureCount > (palAtlasWidth * palAtlasHeight) / 256) {
			errorf("Too many textures for palette atlas!\n");
		}
	}

	bool use_q1_pal = (g_settings.engine == ENGINE_QUAKE_1 || g_settings.engine == ENGINE_QUAKE_1_BSP2);

	glTexturesSwap = new Texture * [map->textureCount];
	for (int i = 0; i < map->textureCount; i++) {
		int32_t texOffset = ((int32_t*)map->textures)[i + 1];
		BSPMIPTEX* tex = map->get_texture(i);

		uint16_t palX, palY;
		palAtlasCoords(i, palX, palY);
		COLOR3* atlasPal = palAtlas + palY * palAtlasWidth + palX;

		if (!tex) {
			Texture* missingCopy = generateMissingTexture(16, 16, numMips, atlasPal);
			glTexturesSwap[i] = missingCopy;
			glTextureArray->add(missingCopy);
			continue;
		}

		COLOR3* palette = NULL;
		byte* mipdat[4] = { NULL, NULL, NULL, NULL };
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
					palette = wadTex->getPalette();
					for (int i = 0; i < 4; i++)
						mipdat[i] = wadTex->getMip(i);

					wadTexCount++;
					break;
				}
			}

			if (!foundInWad) {
				Texture* missingCopy = generateMissingTexture(tex->nWidth, tex->nHeight, numMips, atlasPal);
				glTexturesSwap[i] = missingCopy;
				glTextureArray->add(missingCopy);
				continue;
			}
		}
		else {
			palette = (COLOR3*)(map->textures + texOffset + tex->nOffsets[3] + lastMipSize + 2);
			for (int i = 0; i < 4; i++)
				mipdat[i] = map->textures + texOffset + tex->nOffsets[i];
			embedCount++;
		}

		int sz = tex->nWidth * tex->nHeight;
		bool hasAlpha = tex->szName[0] == '{';

		COLOR3* palSwap = NULL;

		// convert individual palettes to the quake 1 global palette
		if (use_q1_pal) {
			palSwap = new COLOR3[256];
			memcpy(palSwap, palette, 256 * sizeof(COLOR3));
			quantize_to_q1_pal(palSwap);
			palette = palSwap;
		}

		if (g_settings.pal_textures) {
			memcpy(atlasPal, palette, sizeof(COLOR3) * 256);

			uint8_t* imageData = new uint8_t[sz];
			memcpy(imageData, mipdat[0], sz);
			glTexturesSwap[i] = new Texture(tex->nWidth, tex->nHeight, imageData);

			// no mipmaps because filtering between levels is unavoidable and
			// turns the texture into a garbled mess
		}
		else {
			COLOR4* imageData = new COLOR4[tex->nWidth * tex->nHeight];
			uint8_t* mip0 = mipdat[0];

			for (int k = 0; k < sz; k++) {
				imageData[k] = COLOR4(palette[mip0[k]], 255);

				if (hasAlpha && mip0[k] == 255)
					imageData[k].a = 0;
			}

			glTexturesSwap[i] = new Texture(tex->nWidth, tex->nHeight, imageData);

			// looks much nicer in some cases but slow to generate
			//glTexturesSwap[i]->generateMipMaps(numMips, palette[255]);

			for (int k = 1; k <= numMips; k++) {
				glTexturesSwap[i]->addMipMap(k, mipdat[k], palette);
			}
		}

		glTextureArray->add(glTexturesSwap[i]);

		if (wadTex) {
			delete[] wadTex->data;
			delete wadTex;
		}
		if (palSwap)
			delete[] palSwap;
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

Texture* BspRenderer::generateMissingTexture(int width, int height, int mips, COLOR3* pal) {
	static const COLOR4 pink = COLOR4(255, 0, 255, 255);
	static const COLOR4 black = COLOR4(0, 0, 0, 255);

	uint8_t* dat = NULL;

	if (g_settings.pal_textures) {
		dat = new uint8_t[width * height];

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				bool isPink = ((x / 8) + ((y / 8) & 1)) & 1;
				dat[y * width + x] = isPink;
			}
		}

		memset(pal, 0, 256 * sizeof(COLOR3));
		pal[0] = black.rgb();
		pal[1] = pink.rgb();
	}
	else {
		dat = new uint8_t[width * height * sizeof(COLOR4)];

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				bool isPink = ((x / 8) + ((y / 8) & 1)) & 1;
				((COLOR4*)dat)[y * width + x] = isPink ? pink : black;
			}
		}
	}

	Texture* tex = new Texture(width, height, dat);

	if (!g_settings.texture_atlas && !g_settings.pal_textures)
		tex->generateMipMaps(mips, COLOR3());

	return tex;
}

void BspRenderer::preloadTextures() {
	if (miptexToTexArray) {
		delete[] miptexToTexArray;
	}
	miptexToTexArray = new TexArrayOffset[map->textureCount];

	delete glTextureArray;
	glTextureArray = new TextureArray();
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

void BspRenderer::palAtlasCoords(int textureIdx, uint16_t& x, uint16_t& y) {
	x = (textureIdx % 4) * 256;
	y = textureIdx / 4;
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
					errorf("Failed to load TGA file: %s\n", path.c_str());
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
						errorf("Failed to load BMP as 8-bit: %s\n", path.c_str());
					}
				}
				else {
					warnf("Missing skybox image: %s\n", (skyPath + ".tga").c_str());
				}
			}
		}
	}
}

void BspRenderer::buildTextureAtlases() {
	if (!g_settings.texture_atlas) {
		textureAtlasInfos.clear();
		textureAtlasInfos.resize(map->textureCount);
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

	int totalPixels = 0;
	for (int i = 0; i < map->textureCount; i++) {
		BSPMIPTEX* tex = map->get_texture(i);
		if (!tex)
			continue;

		totalPixels += tex->nWidth * tex->nHeight;
	}

	// shrink atlas size to save VRAM on small maps
	while (totalPixels < textureAtlasSz * textureAtlasSz * 0.3f && textureAtlasSz > 512) {
		textureAtlasSz /= 2;
		textureAtlasZoneSz = min(textureAtlasSz, textureAtlasZoneSz);
	}

	vector<TextureAtlas*> atlases;
	atlases.push_back(new TextureAtlas(textureAtlasSz, textureAtlasSz, textureAtlasZoneSz));

	textureAtlasInfos.resize(map->textureCount);
	for (int i = 0; i < map->textureCount; i++) {
		BSPMIPTEX* tex = map->get_texture(i);

		SubTexture sub;
		sub.idx = i;
		sub.w = (tex ? tex->nWidth : 16);
		sub.h = (tex ? tex->nHeight : 16);
		sub.x = sub.y = 0;
		sub.sz = sub.w * sub.h;

		textureAtlasInfos[i] = sub;
	}
	sort(textureAtlasInfos.begin(), textureAtlasInfos.end(), [](const SubTexture& a, const SubTexture& b) {
		return a.sz > b.sz;
		});

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

	glTextureAtlasesSwap = new Texture * [numTextureAtlasesSwap];

	//int mipLevels = 3;
	int mipLevels = 0; // mip-map seams aren't fixable without a large perf/memory impact

	for (int i = 0; i < numTextureAtlasesSwap; i++) {

		if (g_settings.pal_textures) {
			uint8_t* dat = new uint8_t[textureAtlasSz * textureAtlasSz];
			glTextureAtlasesSwap[i] = new Texture(textureAtlasSz, textureAtlasSz, dat);
			memset(glTextureAtlasesSwap[i]->data, 0, textureAtlasSz * textureAtlasSz);
		}
		else {
			glTextureAtlasesSwap[i] = new Texture(textureAtlasSz, textureAtlasSz);
			memset(glTextureAtlasesSwap[i]->data, 0, textureAtlasSz * textureAtlasSz * sizeof(COLOR4));
		}

		// mip map generation for the atlas must be manual so textures don't average each other
		/*
		for (int m = 1; m <= mipLevels; m++) {
			int mipSz = textureAtlasSz >> m;

			MipTexture mip;
			mip.width = mipSz;
			mip.height = mipSz;
			mip.data = new COLOR4[mipSz * mipSz];
			mip.level = m;

			glTextureAtlasesSwap[i]->mipmaps[glTextureAtlasesSwap[i]->numMipMaps++] = mip;
		}
		*/
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

		// copy texture data into atlas
		COLOR4* pSrc = (COLOR4*)(glTexturesSwap[info.idx]->data);
		COLOR4* pDst = (COLOR4*)(glTextureAtlasesSwap[info.atlasId]->data);

		for (int y = 0; y < info.h; y++) {
			for (int x = 0; x < info.w; x++) {
				int src = y * info.w + x;
				int dst = (info.y + y) * textureAtlasSz + info.x + x;

				if (g_settings.pal_textures) {
					((uint8_t*)pDst)[dst] = ((uint8_t*)pSrc)[src];
				}
				else {
					pDst[dst] = pSrc[src];
				}
			}
		}

		for (int m = 1; m <= mipLevels; m++) {
			if (m > glTexturesSwap[info.idx]->numMipMaps) {
				logf("Bad mipmaps for %d\n", info.idx);
				continue;
			}

			int mipSz = textureAtlasSz >> m;

			MipTexture& mip = glTextureAtlasesSwap[info.atlasId]->mipmaps[m - 1];

			int ix = info.x >> m;
			int iy = info.y >> m;
			int iw = info.w >> m;
			int ih = info.h >> m;
			pSrc = (COLOR4*)(glTexturesSwap[info.idx]->mipmaps[m - 1].data);

			for (int y = 0; y < ih; y++) {
				for (int x = 0; x < iw; x++) {
					int src = y * iw + x;
					int dst = (iy + y) * mipSz + ix + x;

					if (g_settings.pal_textures) {
						((uint8_t*)mip.data)[dst] = ((uint8_t*)pSrc)[src];
					}
					else {
						mip.data[dst] = pSrc[src];
					}
				}
			}
		}

		// individual textures used in face editor, don't delete
		//delete glTexturesSwap[i];
		//glTexturesSwap[i] = NULL;
	}
}

void BspRenderer::postLoadTextures() {
	deleteTextures();

	glTextures = glTexturesSwap;
	glTextureAtlases = glTextureAtlasesSwap;
	numTextureAtlases = numTextureAtlasesSwap;
	glPalette = glPaletteSwap;
	memcpy(skyboxTextures, skyboxTexturesSwap, sizeof(skyboxTextures));

	int texFormat = g_settings.pal_textures ? GL_LUMINANCE : GL_RGBA;

	g_shaders.bsp->bind();
	g_shaders.bsp->setUniform("textureAtlasScale", 1.0f / textureAtlasSz, true);

	glTextureArray->upload();
	glCheckError("uploading texture array");

	if (!g_use_texture_arrays && !g_settings.texture_atlas) {
		for (int i = 0; i < map->textureCount; i++) {
			if (glTextures[i] && !glTextures[i]->uploaded) {
				glTextures[i]->upload(texFormat);
			}
		}
		glCheckError("uploading individual textures");
	}
	for (int i = 0; i < 6; i++) {
		if (skyboxTextures[i]) {
			skyboxTextures[i]->upload(GL_RGB, true);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // Note: GL_CLAMP is significantly slower
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
	}
	glCheckError("uploading skybox textures");

	for (int i = 0; i < numTextureAtlases; i++) {
		//lodepng_encode32_file("atlas_mip.png", (byte*)glTextureAtlases[i]->mipmaps[1].data,
		//	glTextureAtlases[i]->mipmaps[1].width, glTextureAtlases[i]->mipmaps[1].height);
		//lodepng_encode32_file(cstrf("atlas_%d.png", i), glTextureAtlasesSwap[i]->data, textureAtlasSz, textureAtlasSz);
		glTextureAtlases[i]->upload(texFormat);

		// disable mip-maps because they show seams which can't be fixed without 4x texture memory
		// and atlas size. https://0fps.net/2013/07/09/texture-atlases-wrapping-and-mip-mapping/
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	glCheckError("uploading texture atlases");

	if (glPalette) {
		// keep palette in memory for GUI
		//lodepng_encode24_file("atlas_pal.png", glPalette->data, palAtlasWidth, palAtlasHeight);
		g_shaders.bsp->setUniform("paletteAtlasScale", vec2(1.0f / palAtlasWidth, 1.0f / palAtlasHeight), true);
		glPalette->upload(GL_RGB, false, false);
		glCheckError("uploading palette");
	}

	numLoadedTextures = map->textureCount;

	texturesLoaded = true;
	glCheckError("uploading textures");

	preRenderFaces();

	textureFacesLoaded = true;
}

void BspRenderer::reloadTextures(bool reloadNow) {
	preloadTextures();

	if (reloadNow) {
		loadTextures();
		postLoadTextures();
	}
	else {
		texturesLoaded = false;
		texturesFuture = async(launch::async, &BspRenderer::loadTextures, this);
	}
}



void BspRenderer::loadLightmaps() {
	double startTime = glfwGetTime();

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

	int maxSize = g_settings.renderer == RENDERER_OPENGL_21_LEGACY ? 1024 : 2048; // 1024 because old gpu lied about max texture size
	lightmapAtlasSz = clamp(g_max_texture_size, 512, maxSize);
	lightmapAtlasZoneSz = 128; // 64 is too small for maps like snd, 256 or greater is slower

	uint64_t totalPixels = 0;
	for (int i = 0; i < sortedFaces.size(); i++) {
		BSPFACE& face = map->faces[sortedFaces[i].idx];
		FaceLightmap& fmap = sortedFaces[i];

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] != 255)
				totalPixels += fmap.lightmapSz;
		}
	}

	// shrink atlas for small maps
	while (totalPixels < lightmapAtlasSz * lightmapAtlasSz * 0.4f && lightmapAtlasSz > 512) {
		lightmapAtlasSz /= 2;
		lightmapAtlasZoneSz = min(lightmapAtlasZoneSz, lightmapAtlasSz);
	}

	vector<TextureAtlas*> atlases;
	vector<Texture*> atlasTextures;
	atlases.push_back(new TextureAtlas(lightmapAtlasSz, lightmapAtlasSz, lightmapAtlasZoneSz));
	atlasTextures.push_back(new Texture(lightmapAtlasSz, lightmapAtlasSz));
	memset(atlasTextures[0]->data, 0, lightmapAtlasSz * lightmapAtlasSz * sizeof(COLOR3));

	bool monochrome = g_app->monochromeLight;

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

		bool overflowed = false;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == 255)
				continue;

			// TODO: Try fitting in earlier atlases before using the latest one
			if (!atlases[atlasId]->insert(i, info.w, info.h, info.x[s], info.y[s])) {
				if (overflowed) {
					logf("Lightmap too big for atlas size!\n");
					break;
				}

				atlases.push_back(new TextureAtlas(lightmapAtlasSz, lightmapAtlasSz, lightmapAtlasZoneSz));
				atlasTextures.push_back(new Texture(lightmapAtlasSz, lightmapAtlasSz));
				atlasId++;
				memset(atlasTextures[atlasId]->data, 0, lightmapAtlasSz * lightmapAtlasSz * sizeof(COLOR3));

				// start over to make sure all this face's lightmaps are in the same atlas
				s = -1;
				overflowed = true;
				continue;
			}

			lightmapCount++;

			info.atlasId = atlasId;

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
					if (offset + src * sizeof(COLOR3) < map->lightDataLength) {
						if (monochrome) {
							uint8_t b = Bsp::monochrome_lightmap_pixel(lightSrc[src]);
							lightDst[dst] = COLOR3(b, b, b);
						}
						else {
							lightDst[dst] = lightSrc[src];
						}
					}
					else {
						bool checkers = x % 2 == 0 != y % 2 == 0;
						lightDst[dst] = { (byte)(checkers ? 255 : 0), 0, (byte)(checkers ? 255 : 0) };
					}
				}
			}
		}
	}

	numLightmapAtlases = atlasTextures.size();
	lightmapAtlasBlackArea = new AtlasCoord[numLightmapAtlases];
	glLightmapTextures = new Texture * [numLightmapAtlases];
	for (int i = 0; i < numLightmapAtlases; i++) {

		if (!atlases[i]->insert(0, 2, 2, lightmapAtlasBlackArea[i].x, lightmapAtlasBlackArea[i].y)) {
			errorf("Failed to insert black area for lightmap atlas! Some lightmaps will be broken.\n");
		}

		// TODO: reserve space for this 2x2 square during lightmap generation. The full bright faces bug will come again...
		//logf("Black area for %d at %d %d\n", i, lightmapAtlasBlackArea[i].x, lightmapAtlasBlackArea[i].y);
		//lodepng_encode24_file(("lightmap_atlas_" + to_string(i) + ".png").c_str(), atlasTextures[i]->data, lightmapAtlasSz, lightmapAtlasSz);

		delete atlases[i];
		glLightmapTextures[i] = atlasTextures[i];
	}

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
	memset(newLightmaps + numRenderLightmapInfos, 0, addedFaces * sizeof(LightmapInfo));

	delete[] lightmaps;
	lightmaps = newLightmaps;
	numRenderLightmapInfos = map->faceCount;
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



void BspRenderer::loadClipnodes(bool newNodesOnly) {
	float startTime = glfwGetTime();

	if (!newNodesOnly) {
		numRenderClipnodes = map->modelCount;
		renderClipnodeDat = new RenderClipnodes[numRenderClipnodes];
		memset(renderClipnodeDat, 0, numRenderClipnodes * sizeof(RenderClipnodes));
	}

	for (int i = 0; i < numRenderClipnodes; i++) {
		generateClipnodeBuffer(i, newNodesOnly);
	}
	debugf("Clipnode meshes generated in %.2fs\n", glfwGetTime() - startTime);
}

void BspRenderer::loadLeaves() {
	renderLeafDat = new RenderLeaves();
	generateLeafBuffer();
}

void BspRenderer::loadMoreClipnodes() {
	clipnodesLoaded = false;
	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this, true);
}

void BspRenderer::addClipnodeModel(int modelIdx) {
	RenderClipnodes* newRenderClipnodes = new RenderClipnodes[numRenderClipnodes + 1];
	memcpy(newRenderClipnodes, renderClipnodeDat, numRenderClipnodes * sizeof(RenderClipnodes));
	memset(&newRenderClipnodes[numRenderClipnodes], 0, sizeof(RenderClipnodes));
	numRenderClipnodes++;
	renderClipnodeDat = newRenderClipnodes;

	generateClipnodeBuffer(modelIdx, false);
}

void BspRenderer::generateClipnodeBuffer(int modelIdx, bool newOnly) {
	BSPMODEL& model = map->models[modelIdx];
	RenderClipnodes* renderClip = &renderClipnodeDat[modelIdx];

	if (newOnly && renderClip->faceMathVerts.size()) {
		return;
	}

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		renderClip->clipnodeBuffer[i] = NULL;
	}

	Clipper clipper;

	renderClip->faceMathLocalVerts.clear();
	renderClip->faceMathVerts.clear();

	bool wantWorldNodes = g_app->clipnodeRenderHull != -1 && (g_settings.render_flags & RENDER_WORLD_CLIPNODES);
	bool wantEntNodes = (g_settings.render_flags & RENDER_ENT_CLIPNODES);

	if (!wantWorldNodes && modelIdx == 0)
		return;

	if (modelIdx != 0) {
		if (!wantEntNodes)
			return;

		if (g_app->clipnodeRenderHull == -1 && map->models[modelIdx].nFaces != 0)
			return; // auto clipnode hulls won't show for this model because it has selectable faces
	}

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (modelIdx != 0) {
			int worldHeadNode = map->models[0].iHeadnodes[i];
			int headNode = map->models[modelIdx].iHeadnodes[i];
			if (headNode == worldHeadNode) {
				// Quake 1 maps generally don't use HULL 3, but instead of redirecting the hull to a
				// solid/empty node, it redirects to the world HULL 3. This results in massive slowdowns
				// due to generating the entire world hull for every model, especially in BSP2 maps.
				continue;
			}
		}

		vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, i, CONTENTS_SOLID);
		//vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, i, CONTENTS_EMPTY);

		static COLOR4 hullColors[] = {
			COLOR4(255, 255, 255, 255),
			COLOR4(96, 255, 255, 255),
			COLOR4(255, 96, 255, 255),
			COLOR4(255, 255, 96, 255),
		};
		COLOR4 color = hullColors[i];

		vector<clipnodeVert> allVerts;

		for (int k = 0; k < solidNodes.size(); k++) {

			/*
			for (int j = 0; j < solidNodes[k].cuts.size(); j++) {
				solidNodes[k].cuts[j].fDist *= -1;
				solidNodes[k].cuts[j].vNormal *= -1;
			}
			*/

			CMesh mesh = clipper.clip(solidNodes[k].cuts);
			clipnodeLeafCount++;

			//color = hullColors[k % 4];

			generateNodeMesh(&mesh, color, allVerts, renderClip->faceMathVerts,
				renderClip->faceMathLocalVerts, renderClip->faceMaths[i], solidNodes[k].nodeIdx);
		}

		//logf("Generate node with %d faces and %d verts\n", totalFaces, totalVerts);

		if (allVerts.size() == 0) {
			renderClip->clipnodeBuffer[i] = NULL;
			continue;
		}

		clipnodeVert* output = new clipnodeVert[allVerts.size()];
		memcpy(output, &allVerts[0], allVerts.size() * sizeof(clipnodeVert));

		renderClip->clipnodeBuffer[i] = new VertexBuffer(g_shaders.clipnode, output, allVerts.size(), true);
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

	renderLeafDat->faceMaths.clear();
	renderLeafDat->leafRanges.clear();
	renderLeafDat->leafRanges.resize(map->leafCount);

	for (int k = 0; k < leafNodes.size(); k++) {
		int leafIdx = leafNodes[k].leafIdx;
		int start = allVerts.size();
		CMesh mesh = clipper.clip(leafNodes[k].cuts);
		generateNodeMesh(&mesh, color, allVerts, renderLeafDat->faceMathVerts,
			renderLeafDat->faceMathLocalVerts, renderLeafDat->faceMaths, leafNodes[k].leafIdx);

		for (int i = start; i < allVerts.size(); i++) {
			renderLeafDat->leafRanges[leafIdx].push_back(i);
		}
	}

	if (allVerts.size() == 0) {
		return;
	}

	clipnodeVert* output = new clipnodeVert[allVerts.size()];
	memcpy(output, &allVerts[0], allVerts.size() * sizeof(clipnodeVert));

	renderLeafDat->leafBuffer = new VertexBuffer(g_shaders.clipnode, output, allVerts.size(), true);

	renderLeafDat->originalColors.resize(allVerts.size());
	for (int i = 0; i < allVerts.size(); i++) {
		renderLeafDat->originalColors[i] = allVerts[i].c;
	}

	leafNavMesh = LeafNavMeshGenerator().generate(map, true, CONTENTS_NOT_LEAF_0, 0);
}

void BspRenderer::generateNodeMesh(CMesh* mesh, COLOR4 color, vector<clipnodeVert>& allVerts,
	vector<vec3>& allFaceMathVerts, vector<vec2>& allFaceMathLocalVerts, vector<FaceMath>& faceMaths, int elementIndex) {
	clipnodeLeafCount++;

	vec3 faceVerts[1024]; // index into mesh verts
	bool addedFaceVerts[1024]; // true if vert already added

	for (int i = 0; i < mesh->faces.size(); i++) {

		if (!mesh->faces[i].visible) {
			continue;
		}
		if (mesh->verts.size() > 1024) {
			logf("Too many verts in clipnode face (%d)\n", mesh->verts.size());
			continue;
		}

		memset(addedFaceVerts, 0, sizeof(bool) * mesh->verts.size());
		int numFaceVerts = 0;

		for (int k = 0; k < mesh->faces[i].edges.size(); k++) {
			for (int v = 0; v < 2; v++) {
				int vertIdx = mesh->edges[mesh->faces[i].edges[k]].verts[v];
				if (!mesh->verts[vertIdx].visible || addedFaceVerts[vertIdx]) {
					continue;
				}
				addedFaceVerts[vertIdx] = true;
				faceVerts[numFaceVerts++] = mesh->verts[vertIdx].pos;
			}
		}

		if (!sortPlanarVerts(faceVerts, numFaceVerts)) {
			//logf("Degenerate clipnode face discarded\n");
			continue;
		}

		vec3 normal = getNormalFromVerts(faceVerts, numFaceVerts);

		if (dotProduct(mesh->faces[i].normal, normal) < 0) {
			reverse(faceVerts, faceVerts + numFaceVerts);
			normal = normal.invert();
		}

		// calculations for face picking
		{
			FaceMath faceMath;
			faceMath.plane_z = mesh->faces[i].normal;
			faceMath.fdist = getDistAlongAxis(mesh->faces[i].normal, faceVerts[0]);
			faceMath.index = elementIndex;

			vec3 v0 = faceVerts[0];
			vec3 v1;
			bool found = false;
			for (int z = 1; z < numFaceVerts; z++) {
				if (faceVerts[z] != v0) {
					v1 = faceVerts[z];
					found = true;
					break;
				}
			}
			if (!found) {
				logf("Failed to find non-duplicate vert for clipnode face\n");
			}

			vec3 plane_z = mesh->faces[i].normal;
			vec3 plane_x = (v1 - v0).normalize();
			vec3 plane_y = crossProduct(plane_z, plane_x).normalize();
			faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

			faceMath.vertIdx = allFaceMathLocalVerts.size();
			faceMath.numVerts = numFaceVerts;

			for (int k = 0; k < numFaceVerts; k++) {
				allFaceMathVerts.push_back(faceVerts[k]);
				allFaceMathLocalVerts.push_back(faceMath.worldToLocal.multColMajor(faceVerts[k]).xy());
			}

			faceMaths.push_back(faceMath);
		}

		// create the verts for rendering
		{
			for (int i = 0; i < numFaceVerts; i++) {
				faceVerts[i] = faceVerts[i].flip();
			}

			vec3 lightDir = vec3(1, 1, -1).normalize();
			float dot = (dotProduct(normal * -1, lightDir) + 1) / 2.0f;
			if (dot > 0.5f) {
				dot = dot * dot;
			}
			COLOR4 faceColor = color * (dot);

			// convert from TRIANGLE_FAN style verts to TRIANGLES
			for (int k = 2; k < numFaceVerts; k++) {
				clipnodeVert verts[3] = {
					clipnodeVert(faceVerts[0], faceColor),
					clipnodeVert(faceVerts[k - 1], faceColor),
					clipnodeVert(faceVerts[k], faceColor),
				};

				// which edges to draw
				uint8_t edgeEnableMask = 1;
				if (k == 2)
					edgeEnableMask |= 4;
				if (k == numFaceVerts - 1)
					edgeEnableMask |= 2;

				// barycentric coords for wireframe edge detection
				verts[0].edges = (1 << 3) | edgeEnableMask;
				verts[1].edges = (2 << 3) | edgeEnableMask;
				verts[2].edges = (4 << 3) | edgeEnableMask;

				// select which edges to draw (not the inner edges of the fan)
				for (int j = 0; j < 3; j++) {
					allVerts.push_back(verts[j]);
				}
			}
		}
	}
}

void BspRenderer::delayLoadLeaves() {
	if (!leavesLoaded && leavesThreadFinished)
		reloadLeaves();
}

void BspRenderer::reloadClipnodes() {
	clipnodesLoaded = false;
	clipnodeLeafCount = 0;

	deleteRenderClipnodes();

	clipnodesFuture = async(launch::async, &BspRenderer::loadClipnodes, this, false);
}

void BspRenderer::reloadLeaves(bool reloadNow) {
	if (!leavesThreadFinished) {
		if (reloadNow) {
			errorf("ERROR: can't reload leaves yet\n");
		}
		return;
	}

	g_app->hiddenLeaves.clear();
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

bool BspRenderer::isFinishedLoading() {
	return (lightmapsUploaded && texturesLoaded && textureFacesLoaded && clipnodesLoaded && leavesThreadFinished) ||
		map->ents.empty();
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

		g_shaders.bsp->bind();
		g_shaders.bsp->setUniform("lightmapAtlasScale", (1.0f / lightmapAtlasSz) * (1.0f / 16.0f), true);

		lightmapsUploaded = true;
	}
	else if (!texturesLoaded && texturesFuture.wait_for(chrono::milliseconds(0)) == future_status::ready) {
		postLoadTextures();
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

		if (renderLeafDat && renderLeafDat->leafBuffer) {
			renderLeafDat->leafBuffer->upload();

			if (g_app->pickMode == PICK_LEAF) {
				highlightPickedLeaves(true); // switching from face to leaf pick mode for the first time
				hideLeaves(true);
			}
		}

		debugf("Loaded leaves\n");
	}
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
		int texArrayIdx = texArrayOffset.layer;

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

		uint16_t palX, palY;
		palAtlasCoords(texinfo.iMiptex, palX, palY);
		palX /= 256;

		LightmapInfo* lmap = lightmapsGenerated ? &lightmaps[faceIdx] : NULL;

		lightmapVert* verts = new lightmapVert[face.nEdges];
		int vertCount = face.nEdges;
		Texture* lightmapAtlas;

		float lw = 0;
		float lh = 0;
		if (lightmapsGenerated) {
			lw = (float)lmap->w;
			lh = (float)lmap->h;
		}

		bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
		bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;
		lightmapAtlas = lightmapsGenerated ? glLightmapTextures[lmap->atlasId] : NULL;

		AtlasCoord blackLightmap = { 0, 0 };
		if (lightmapsGenerated) {
			// center of the black pixel in the atlas
			blackLightmap.x = lightmapAtlasBlackArea[lmap->atlasId].x * 16 + 16;
			blackLightmap.y = lightmapAtlasBlackArea[lmap->atlasId].y * 16 + 16;
		}

		if (isSpecial) {
			lightmapAtlas = whiteTex;
		}

		float opacity = isSpecial ? 0.5f : 1.0f;

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = min(edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0], (uint32_t)(map->vertCount - 1));

			vec3& vert = map->verts[vertIdx];
			verts[e].x = vert.x;
			verts[e].y = vert.z;
			verts[e].z = -vert.y;

			verts[e].c = COLOR4(255, 255, 255, isSpecial ? 128 : 255);

			// texture coords
			float tw = 1.0f / (float)texWidth;
			float th = 1.0f / (float)texHeight;
			float fU = dotProduct(texinfo.vS, vert) + texinfo.shiftS;
			float fV = dotProduct(texinfo.vT, vert) + texinfo.shiftT;
			verts[e].u = fU * tw;
			verts[e].v = fV * th;

			if (g_settings.texture_atlas) {
				verts[e].ax = atlasInfo.x / 16;
				verts[e].ay = atlasInfo.y / 16;
				verts[e].aw = atlasInfo.w / 16;
				verts[e].ah = atlasInfo.h / 16;
			}
			else {
				verts[e].ax = texArrayIdx % 256;
				verts[e].ay = texArrayIdx / 256;
				verts[e].aw = 0;
				verts[e].ah = 0;
			}

			verts[e].palX = palX;
			verts[e].palY_hi = palY >> 8;
			verts[e].palY_lo = palY & 0xff;

			// lightmap texture coords
			if (hasLighting && lightmapsGenerated) {
				float fLightMapU = lmap->midTexU + (fU - lmap->midPolyU) / 16.0f;
				float fLightMapV = lmap->midTexV + (fV - lmap->midPolyV) / 16.0f;

				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][0] = (fLightMapU + lmap->x[s]) * 16;
					verts[e].luv[s][1] = (fLightMapV + lmap->y[s]) * 16;
				}
			}
			else {
				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					verts[e].luv[s][0] = 0;
					verts[e].luv[s][1] = 0;
				}
			}

			// redirect unused lightmaps to black section.
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				if (!hasLighting || face.nStyles[s] == 255) {
					verts[e].luv[s][0] = blackLightmap.x;
					verts[e].luv[s][1] = blackLightmap.y;
				}
			}
		}


		// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
		int newCount = face.nEdges + max((uint32_t)0, face.nEdges - 3) * 2;
		lightmapVert* newVerts = new lightmapVert[newCount];

		int idx = 0;
		for (int k = 2; k < face.nEdges; k++, idx += 3) {
			// reverse order due to coordinate system swap
			newVerts[idx + 0] = verts[k];
			newVerts[idx + 1] = verts[k - 1];
			newVerts[idx + 2] = verts[0];

			// which edges to draw
			uint8_t edgeEnableMask = 4;
			if (k == 2)
				edgeEnableMask |= 1;
			if (k == face.nEdges - 1)
				edgeEnableMask |= 2;

			// barycentric coords for wireframe edge detection
			newVerts[idx + 0].edges = (1 << 3) | edgeEnableMask;
			newVerts[idx + 1].edges = (2 << 3) | edgeEnableMask;
			newVerts[idx + 2].edges = (4 << 3) | edgeEnableMask;
		}

		delete[] verts;
		verts = newVerts;
		vertCount = newCount;

		Texture* gltex = texturesLoaded && numLoadedTextures > 0 ? glTextures[min((uint)numLoadedTextures - 1, texinfo.iMiptex)] : greyTex;

		// add face to a render group (faces that share that same texture array, lightmaps, and opacity flag)
		bool isTransparent = opacity < 1.0f;
		int groupIdx = -1;
		RenderGroup newGroup = RenderGroup();
		newGroup.vertCount = 0;
		newGroup.verts = NULL;
		newGroup.transparent = isTransparent;
		newGroup.arrayTextureIdx = miptexToTexArray[texinfo.iMiptex].arrayIdx;
		newGroup.atlasTextureIdx = textureAtlasInfos[texinfo.iMiptex].atlasId;
		newGroup.texture = texturesLoaded ? gltex : greyTex;
		newGroup.lightmapAtlas = lightmapAtlas;

		for (int k = 0; k < renderGroups.size(); k++) {
			if (RenderGroupsAreCombinable(newGroup, renderGroups[k])) {
				groupIdx = k;
				break;
			}
		}

		// add the verts to a new group if no existing one share the same properties
		if (groupIdx == -1) {
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

		renderGroups[i].buffer = new VertexBuffer(g_shaders.bsp);
		renderGroups[i].buffer->setData(renderGroups[i].verts, renderGroups[i].vertCount);
		renderGroups[i].buffer->upload();

		renderModel->renderGroups[i] = renderGroups[i];
	}

	glCheckError("Upload render group");

	for (int i = 0; i < model.nFaces; i++) {
		refreshFace(model.iFirstFace + i);
	}

	if (refreshClipnodes) {
		refreshModelClipnodes(modelIdx);
	}

	glCheckError("Upload render clipnodes");

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

	if (modelIdx == 0 && renderLeafDat != NULL && renderLeafDat->leafBuffer) {
		delete renderLeafDat->leafBuffer;
		renderLeafDat->leafBuffer = NULL;
	}

	if (leafNavMesh) {
		delete leafNavMesh;
		leafNavMesh = NULL;
	}

	deleteRenderModelClipnodes(&renderClipnodeDat[modelIdx]);
	generateClipnodeBuffer(modelIdx, false);

	RenderClipnodes& renderClip = renderClipnodeDat[modelIdx];

	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		if (renderClip.clipnodeBuffer[i])
			renderClip.clipnodeBuffer[i]->upload();
	}

	return true;
}

void BspRenderer::refreshPointEnt(int entIdx, bool uploadBuffer) {
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

	if (uploadBuffer) {
		pointEnts->deleteBuffer();
		pointEnts->upload();
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

	vec3 v1;
	vec3 v0;
	vec3* faceVerts = new vec3[face.nEdges];
	for (int e = 0; e < face.nEdges; e++) {
		int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE& edge = map->edges[abs(edgeIdx)];
		int vertIdx = min(edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0], (uint32_t)(map->vertCount - 1));
		vec3 v = map->verts[vertIdx];

		// 2 verts can share the same position on a face, so need to find one that isn't shared (aomdc_1intro)
		if (e > 0 && v != v0) {
			v1 = v;
		}
		else if (e == 0) {
			v0 = v;
		}
		faceVerts[e] = v;
	}

	vec3 plane_x = (v1 - v0).normalize(1.0f);
	vec3 plane_y = crossProduct(planeNormal, plane_x).normalize(1.0f);
	vec3 plane_z = planeNormal;

	faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	if (faceMath.vertIdx == -1) {
		faceMath.vertIdx = faceMathLocalVerts.size();
		faceMath.numVerts = face.nEdges;
		faceMathVerts.resize(faceMathVerts.size() + face.nEdges);
		faceMathLocalVerts.resize(faceMathLocalVerts.size() + face.nEdges);
	}

	for (int e = 0; e < faceMath.numVerts; e++) {
		faceMathVerts[faceMath.vertIdx + e] = faceVerts[e];
		faceMathLocalVerts[faceMath.vertIdx + e] = faceMath.worldToLocal.multColMajor(faceVerts[e]).xy();
	}

	delete[] faceVerts;
}



int BspRenderer::allocMegaBufferData() {
	int totalMegaModelGroups = 0;

	// find which entities can be included in the mega buffer and tally vertex counts
	for (int i = 0; i < orderEnts.size(); i++) {
		OrderedEnt& ent = orderEnts[i];
		const EntRenderOpts& opts = ent.ent->getRenderOpts();

		// don't combine models for entities that have special rendering properties applied
		if (ent.modelIdx == -1 || ent.ent->highlighted || ent.ent->hidden) {
			ent.isInMegaRenderGroup = false;
			continue;
		}
		if ((g_settings.render_flags & RENDER_RENDER_MODES) || g_app->previewMode) {
			if (opts.rendermode != RENDER_MODE_NORMAL) {
				ent.isInMegaRenderGroup = false;
				continue;
			}
		}
		ent.isInMegaRenderGroup = true;

		RenderModel& model = renderModels[ent.modelIdx];
		totalMegaModelGroups += model.groupCount;
		for (int g = 0; g < model.groupCount; g++) {
			RenderGroup& group = model.renderGroups[g];

			bool wasCombined = false;
			for (int k = 0; k < megaRenderGroups.size(); k++) {
				MegaRenderGroup& mega = megaRenderGroups[k];

				if (RenderGroupsAreCombinable(group, mega.group)) {
					mega.group.vertCount += group.vertCount;
					mega.refs.push_back({ i, g });
					wasCombined = true;
					break;
				}
			}

			if (!wasCombined) {
				MegaRenderGroup newGroup;
				newGroup.group = group;
				newGroup.group.buffer = NULL;
				newGroup.refs.push_back({ i, g });
				megaRenderGroups.push_back(newGroup);
			}
		}

		if (clipnodesLoaded) {
			megaRenderClipnodes.refs.push_back(i);
			for (int k = 0; k < MAX_MAP_HULLS + 1; k++) {
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

	// create mega group buffers but don't fill them yet
	for (int i = 0; i < megaRenderGroups.size(); i++) {
		MegaRenderGroup& mega = megaRenderGroups[i];

		if (mega.group.vertCount == 0) {
			mega.group.buffer = NULL;
			continue;
		}

		lightmapVert* verts = new lightmapVert[mega.group.vertCount];

		VertexBuffer* megaBuffer = new VertexBuffer(g_shaders.bsp, verts, mega.group.vertCount, true);
		mega.group.buffer = megaBuffer;
	}

	for (int i = 0; i < MAX_MAP_HULLS + 1 && clipnodesLoaded; i++) {
		if (megaRenderClipnodes.totalVerts[i] == 0) {
			megaRenderClipnodes.buffer[i] = NULL;
			continue;
		}

		clipnodeVert* verts = new clipnodeVert[megaRenderClipnodes.totalVerts[i]];

		VertexBuffer* megaBuffer = new VertexBuffer(g_shaders.clipnode, verts, megaRenderClipnodes.totalVerts[i], true);
		megaRenderClipnodes.buffer[i] = megaBuffer;
	}

	return totalMegaModelGroups;
}

void BspRenderer::reloadMegaBuffers() {
	megaGroupUpdateIdx = -1;
	megaGroupUpdateProgress = -1;
	megaGroupUpdateLastPickCount = -1;
	lastOrderEntFullUpdatePickCount = -1;
}

void BspRenderer::refreshMegaBuffers() {
	if (g_app->pickCount != megaGroupUpdateLastPickCount) {
		megaGroupUpdateIdx = -1;
		megaGroupUpdateProgress = -1;
		megaGroupUpdateLastPickCount = g_app->pickCount;
	}
	else if (megaGroupUpdateProgress == -1) {
		return;
	}

	static int createMillis = 0;
	static int totalModelGroups = 0;

	if (megaGroupUpdateProgress == -1) {
		megaGroupUpdateStartTime = glfwGetTime();
		for (MegaRenderGroup& mega : megaRenderGroups) {
			delete mega.group.buffer;
		}

		for (int i = 0; i < MAX_MAP_HULLS + 1; i++) {
			delete megaRenderClipnodes.buffer[i];
			megaRenderClipnodes.buffer[i] = NULL;
		}
		memset(megaRenderClipnodes.totalVerts, 0, sizeof(megaRenderClipnodes.totalVerts));
		megaRenderClipnodes.refs.clear();

		megaRenderGroups.clear();

		totalModelGroups = allocMegaBufferData();

		megaGroupUpdateProgress = 0;

		createMillis = (int)((glfwGetTime() - megaGroupUpdateStartTime) * 1000);
		return; // draw frame now so that entities are highlighted
	}

	const int maxVertsPerUpdate = 16384 * 1000;
	int oldProgress = megaGroupUpdateProgress;
	megaGroupUpdateProgress = 0;

	// create solid models buffer
	for (int i = 0; i < megaRenderGroups.size(); i++) {
		MegaRenderGroup& mega = megaRenderGroups[i];

		if (!mega.group.buffer)
			continue;

		// solid models
		lightmapVert* verts = (lightmapVert*)mega.group.buffer->data;

		bool didAnyWork = false;

		int vertIdx = 0;
		for (EntModelGroupIdx& ref : mega.refs) {
			OrderedEnt& ent = orderEnts[ref.entIdx];
			RenderGroup& refGroup = renderModels[ent.modelIdx].renderGroups[ref.groupIdx];

			if (megaGroupUpdateProgress < oldProgress) {
				megaGroupUpdateProgress += refGroup.vertCount;
				vertIdx += refGroup.vertCount;
				continue;
			}

			int workDone = megaGroupUpdateProgress - oldProgress;
			if (workDone > 0 && workDone + refGroup.vertCount > maxVertsPerUpdate) {
				return; // do more work later
			}

			for (int k = 0; k < refGroup.vertCount; k++, vertIdx++) {
				verts[vertIdx] = refGroup.verts[k];

				vec3* v = (vec3*)(&verts[vertIdx].x);
				*v = ent.transform.multRowMajor(*v);
			}

			megaGroupUpdateProgress += refGroup.vertCount;
			didAnyWork = true;
		}

		if (didAnyWork) {
			mega.group.buffer->upload();
		}
	}

	// create clipnode models buffer
	for (int i = 0; i < MAX_MAP_HULLS + 1 && clipnodesLoaded; i++) {
		if (!megaRenderClipnodes.buffer[i])
			continue;

		clipnodeVert* verts = (clipnodeVert*)megaRenderClipnodes.buffer[i]->data;

		bool didAnyWork = false;
		int vertIdx = 0;
		VertexBuffer* sampleBuf = NULL;
		for (int idx : megaRenderClipnodes.refs) {
			OrderedEnt& ent = orderEnts[idx];

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

			if (megaGroupUpdateProgress < oldProgress) {
				megaGroupUpdateProgress += numVerts;
				vertIdx += numVerts;
				continue;
			}

			int workDone = megaGroupUpdateProgress - oldProgress;
			if (workDone > 0 && workDone + numVerts > maxVertsPerUpdate) {
				return; // do more work later
			}

			for (int k = 0; k < numVerts; k++, vertIdx++) {
				verts[vertIdx] = srcData[k];

				verts[vertIdx].pos = ent.transform.multRowMajor(srcData[k].pos);
			}

			megaGroupUpdateProgress += numVerts;
			didAnyWork = true;
		}

		if (didAnyWork)
			megaRenderClipnodes.buffer[i]->upload();
	}

	megaGroupUpdateIdx = g_app->pickCount;
	megaGroupUpdateProgress = -1;

	int updateMillis = (int)((glfwGetTime() - megaGroupUpdateStartTime) * 1000);
	debugf("Created %d mega groups from %d model groups in %d + %dms\n",
		(int)megaRenderGroups.size(), totalModelGroups, createMillis, updateMillis);
}



void BspRenderer::preRenderFaces() {
	deleteRenderFaces();
	reloadMegaBuffers();

	memset(lightStyleCount, 0, sizeof(lightStyleCount));

	facePolys.resize(map->faceCount);
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];

		PvsPoly& pvsPoly = facePolys[i];
		map->get_face_plane(i, pvsPoly.v0, pvsPoly.normal);
		map->get_face_bounding_box(i, pvsPoly.mins, pvsPoly.maxs);

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

bool BspRenderer::RenderGroupsAreCombinable(RenderGroup& groupa, RenderGroup& groupb) {
	if (g_settings.texture_atlas) {
		if (groupa.atlasTextureIdx != groupb.atlasTextureIdx)
			return false;
	}
	else if (g_use_texture_arrays) {
		if (groupa.arrayTextureIdx != groupb.arrayTextureIdx)
			return false;
	}
	else if (groupa.texture != groupb.texture) {
		return false;
	}

	if (groupa.transparent != groupb.transparent)
		return false;

	if (groupa.lightmapAtlas != groupb.lightmapAtlas)
		return false;

	return true;
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

	pointEnts = new VertexBuffer(g_shaders.color, entCubes, numPointEnts * 6 * 6, true);
	pointEnts->upload();

	reloadMegaBuffers();

	glCheckError("BSP pre render ents");
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
			faceMath.plane_z = poly.plane_z;
			faceMath.fdist = poly.fdist;
			faceMath.worldToLocal = poly.worldToLocal;
			faceMath.vertIdx = -1;
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

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(g_shaders.color, output, allVerts.size(), true);

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
	int r = (node->id * 13) % 8;

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

	node->face_buffer = new VertexBuffer(g_shaders.clipnode, output, allVerts.size(), true);
}
