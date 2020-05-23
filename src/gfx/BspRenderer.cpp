#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "lodepng.h"

BspRenderer::BspRenderer(Bsp* map, ShaderProgram* pipeline) {
	this->map = map;
	this->pipeline = pipeline;

	loadTextures();
	loadLightmaps();
	preRenderFaces();

	sTexId = glGetUniformLocation(pipeline->ID, "sTex");

	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		sLightmapTexIds[s] = glGetUniformLocation(pipeline->ID, ("sLightmapTex" + to_string(s)).c_str());
		lightmapScaleIds[s] = glGetUniformLocation(pipeline->ID, ("lightmapScale" + to_string(s)).c_str());

		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds[s], s + 1);
	}

	faceBuffer = new VertexBuffer(pipeline, 0);
	faceBuffer->addAttribute(TEX_2F, "vTex");
	faceBuffer->addAttribute(TEX_2F, "vLightmapTex0");
	faceBuffer->addAttribute(TEX_2F, "vLightmapTex1");
	faceBuffer->addAttribute(TEX_2F, "vLightmapTex2");
	faceBuffer->addAttribute(TEX_2F, "vLightmapTex3");
	faceBuffer->addAttribute(POS_3F, "vPosition");
}

void BspRenderer::loadTextures() {
	whiteTex = new Texture(16, 16);
	memset(whiteTex->data, 255, 16 * 16 * sizeof(COLOR3));
	whiteTex->upload();

	glTextures = new Texture * [map->textureCount];
	for (int i = 0; i < map->textureCount; i++) {
		int32_t texOffset = ((int32_t*)map->textures)[i + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

		if (tex.nOffsets[0] <= 0) {
			glTextures[i] = whiteTex;
			continue;
		}

		COLOR3* imageData = new COLOR3[tex.nWidth * tex.nHeight];
		int sz = tex.nWidth * tex.nHeight;
		int lastMipSize = (tex.nWidth / 8) * (tex.nHeight / 8);
		COLOR3* palette = (COLOR3*)(map->textures + texOffset + tex.nOffsets[3] + lastMipSize + 2);

		for (int k = 0; k < sz; k++) {
			byte paletteIdx = *(map->textures + texOffset + tex.nOffsets[0] + k);
			imageData[k] = palette[paletteIdx];
		}

		// map->textures + texOffset + tex.nOffsets[0]

		glTextures[i] = new Texture(tex.nWidth, tex.nHeight, imageData);
	}
}

void BspRenderer::loadLightmaps() {
	vector<LightmapNode*> atlases;
	vector<Texture*> atlasTextures;
	atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	memset(atlasTextures[0]->data, 0, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

	lightmaps = new LightmapInfo[map->faceCount];
	memset(lightmaps, 0, map->faceCount * sizeof(LightmapInfo));

	printf("Calculating lightmaps\n");
	qrad_init_globals(map);

	int lightmapCount = 0;
	int atlasId = 0;
	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL))
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
					printf("Lightmap too big for atlas size!\n");
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
		glLightmapTextures[i]->upload();
	}

	lodepng_encode24_file("atlas.png", atlasTextures[0]->data, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE);
	printf("Fit %d lightmaps into %d atlases\n", lightmapCount, atlasId + 1);
}

void BspRenderer::preRenderFaces() {
	renderFaces = new RenderFace[map->faceCount];

	for (int i = 0; i < map->faceCount; i++) {
		BSPFACE& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];
		BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
		LightmapInfo& lmap = lightmaps[i];

		RenderFace& rface = renderFaces[i];

		rface.verts = new lightmapVert[face.nEdges];
		rface.vertCount = face.nEdges;

		float tw = 1.0f / (float)tex.nWidth;
		float th = 1.0f / (float)tex.nHeight;

		float lw = (float)lmap.w / (float)LIGHTMAP_ATLAS_SIZE;
		float lh = (float)lmap.h / (float)LIGHTMAP_ATLAS_SIZE;

		rface.texture = glTextures[texinfo.iMiptex];

		bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
		bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			rface.lightmapScales[s] = (hasLighting && face.nStyles[s] != 255) ? 1.0f : 0.0f;
			rface.lightmapAtlas[s] = glLightmapTextures[lmap.atlasId[s]];
		}

		if (isSpecial) {
			rface.lightmapScales[0] = 1.0f;
			rface.lightmapAtlas[0] = whiteTex;
		}

		for (int e = 0; e < face.nEdges; e++) {
			int32_t edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3& vert = map->verts[vertIdx];
			rface.verts[e].x = vert.x;
			rface.verts[e].y = vert.z;
			rface.verts[e].z = -vert.y;

			// texture coords
			float fU = dotProduct(texinfo.vS, vert) + texinfo.shiftS;
			float fV = dotProduct(texinfo.vT, vert) + texinfo.shiftT;
			rface.verts[e].u = fU * tw;
			rface.verts[e].v = fV * th;

			// lightmap texture coords
			if (hasLighting) {
				float fLightMapU = lmap.midTexU + (fU - lmap.midPolyU) / 16.0f;
				float fLightMapV = lmap.midTexV + (fV - lmap.midPolyV) / 16.0f;

				float uu = (fLightMapU / (float)lmap.w) * lw;
				float vv = (fLightMapV / (float)lmap.h) * lh;

				float pixelStep = 1.0f / (float)LIGHTMAP_ATLAS_SIZE;

				for (int s = 0; s < MAXLIGHTMAPS; s++) {
					rface.verts[e].luv[s][0] = uu + lmap.x[s] * pixelStep;
					rface.verts[e].luv[s][1] = vv + lmap.y[s] * pixelStep;
				}
			}
		}
	}
}

BspRenderer::~BspRenderer() {
	for (int i = 0; i < map->textureCount; i++) {
		delete glTextures[i];
	}
	delete[] glTextures;

	// TODO: more stuff to delete
}

void BspRenderer::render() {
	BSPMODEL& world = map->models[0];	

	for (int i = 0; i < world.nFaces; i++) {
		renderLightmapFace(i);
	}
}

void BspRenderer::renderLightmapFace(int faceIdx) {
	RenderFace& rface = renderFaces[faceIdx];

	glActiveTexture(GL_TEXTURE0);
	rface.texture->bind();
	glUniform1i(sTexId, 0);

	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		// set unused lightmaps to black
		glUniform1f(lightmapScaleIds[s], rface.lightmapScales[s]);

		if (rface.lightmapScales[s] != 0.0f) {
			glActiveTexture(GL_TEXTURE1 + s);
			rface.lightmapAtlas[s]->bind();
		}
	}

	faceBuffer->setData(rface.verts, rface.vertCount);
	faceBuffer->draw(GL_TRIANGLE_FAN);
}