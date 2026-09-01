#include "Bsp.h"
#include "util.h"
#include "Texture.h"
#include "TextureAtlas.h"
#include "Entity.h"
#include "Editor.h"
#include "lodepng.h"
#include <algorithm>
#include <fstream>

float Bsp::calc_allocblock_usage() {
	int total = 0;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		if (info.nFlags & TEX_SPECIAL)
			continue; // does not use lightmaps

		BSPMIPTEX* tex = get_texture(info.iMiptex);
		if (tex && tex->szName[0] == '!')
			continue; // water doesn't use lightmaps

		int size[2];
		GetFaceLightmapSize(this, i, size);

		for (int m = 0; m < MAXLIGHTMAPS; m++) {
			if (face.nStyles[m] != 255)
				total += size[0] * size[1];
		}
	}

	const int allocBlockSize = 128 * 128;

	return total / (float)allocBlockSize;
}

void Bsp::apply_lightmap(int faceIdx, int layer, COLOR3* data, int srcW, int srcH) {
	if (!lightdata || faceIdx >= faceCount || faceIdx < 0)
		return;

	BSPFACE& face = faces[faceIdx];

	int size[2];
	GetFaceLightmapSize(this, faceIdx, size);
	int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
	byte* lightmapPtr = lightdata + face.nLightmapOffset + layer * lightmapSz;

	if (srcW != size[0] || srcH != size[1]) {
		COLOR3* resizedDat = new COLOR3[size[0] * size[1]];
		Texture::resample(data, srcW, srcH, resizedDat, size[0], size[1], KernelTypeBilinear);
		memcpy(lightmapPtr, resizedDat, lightmapSz);
	}
	else {
		memcpy(lightmapPtr, data, lightmapSz);
	}
}

int Bsp::allocblock_reduction() {
	int modifyCount = 0;

	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		if (model.nFaces == 0)
			continue;

		bool isVisibleModel = false;
		bool isLightmapModel = false;
		string usedBy = "";

		for (Entity* ent : ents) {
			if (ent->getBspModelIdx() == i) {
				if (ent->isEverVisible()) {
					int rendermode = atoi(ent->getKeyvalue("rendermode").c_str());
					if (rendermode != 1 && rendermode != 2 && rendermode != 5) {
						// texture/color/additive render modes don't use lightmaps
						isLightmapModel = true;
					}
					isVisibleModel = true;
				}
				usedBy = ent->getTargetname() + " (" + ent->getClassname() + ")";
			}
		}

		if (isVisibleModel && isLightmapModel)
			continue;

		bool anyStrip = false;
		bool anyScales = false;

		for (int fa = 0; fa < model.nFaces; fa++) {
			BSPFACE& face = faces[model.iFirstFace + fa];
			BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

			if (!isVisibleModel) {
				if (info.vS.length() > 0.01f || info.vT.length() > 0.01f) {
					BSPTEXTUREINFO* newinfo = get_unique_texinfo(model.iFirstFace + fa);

					if (info.vS.length() > 0.01f) {
						newinfo->vS = info.vS.normalize(0.01f);
						anyScales = true;
						modifyCount++;
					}
					if (info.vT.length() > 0.01f) {
						newinfo->vT = info.vT.normalize(0.01f);
						anyScales = true;
						modifyCount++;
					}
				}
			}

			if (!isLightmapModel) {
				// don't remove base lightmap because it gets allocated anyway for dlight effects
				for (int m = 1; m < MAXLIGHTMAPS; m++) {
					if (face.nStyles[m] != 255) {
						face.nStyles[m] = 255;
						modifyCount++;
						anyStrip = true;
					}
				}
			}
		}

		if (anyScales) {
			logf("Scale up textures on model %d used by %s\n", i, usedBy.c_str());
		}
		if (anyStrip) {
			logf("Stripped light styles on model %d used by %s\n", i, usedBy.c_str());
		}
	}

	// recompiling with fewer bounces is probably better than doing this.
	/*
	int blackStyles = 0;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		int size[2];
		GetFaceLightmapSize(this, i, size);
		int lightmapSz = size[0] * size[1];

		for (int m = 1; m < MAXLIGHTMAPS; m++) {
			if (face.nStyles[m] == 255) {
				continue;
			}

			int numNonBlackPixels = 0;

			int offsetSrc = face.nLightmapOffset + m * lightmapSz;
			COLOR3* lightSrc = (COLOR3*)(lightdata + offsetSrc);
			for (int idx = 0; idx < lightmapSz; idx++) {
				if (offsetSrc + idx * sizeof(COLOR3) < lightDataLength) {
					COLOR3& src = lightSrc[idx];
					if (src.r > 8 || src.g > 8 || src.b > 8) {
						numNonBlackPixels++;
					}
				}
			}

			if (numNonBlackPixels < lightmapSz * 0.1f) {
				blackStyles++;
				modifyCount++;
				bake_lightmap_style(face.nStyles[m], true, true, i);
				m--;
			}
		}
	}

	if (blackStyles)
		logf("Stripped %d nearly black light styles\n", blackStyles);
	*/

	return modifyCount;
}

void Bsp::get_lightmap_shift(const LIGHTMAP& oldLightmap, const LIGHTMAP& newLightmap, int& srcOffsetX, int& srcOffsetY) {
	int minWidth = min(newLightmap.width, oldLightmap.width);
	int minHeight = min(newLightmap.height, oldLightmap.height);

	int bestMatch = 0;
	int bestShiftCombo = 0;

	// Try different combinations of shifts to find the best alignment of the lightmaps.
	// Example (2 = unlit, 3 = lit)
	//  old         new
	// 3 3 3      2 3 3 3
	// 3 3 3  ->  2 3 3 3  =  old lightmap matches more luxels when it's shifted right 1 pixel in the new lightmap
	// 3 3 3      2 3 3 3
	// Only works for lightmap resizes caused by precision errors. Faces that are actually different sizes will
	// likely have more than 1 pixel of difference in either dimension.
	for (int t = 0; t < 4; t++) {
		int numMatch = 0;
		for (int y = 0; y < minHeight; y++) {
			for (int x = 0; x < minWidth; x++) {
				int offsetX = x;
				int offsetY = y;

				if (t == 1) {
					offsetX = x + 1;
				}
				if (t == 2) {
					offsetY = y + 1;
				}
				if (t == 3) {
					offsetX = x + 1;
					offsetY = y + 1;
				}

				int srcX = oldLightmap.width > newLightmap.width ? offsetX : x;
				int srcY = oldLightmap.height > newLightmap.height ? offsetY : y;
				int dstX = newLightmap.width > oldLightmap.width ? offsetX : x;
				int dstY = newLightmap.height > oldLightmap.height ? offsetY : y;

				srcX = max(0, min(oldLightmap.width - 1, srcX));
				srcY = max(0, min(oldLightmap.height - 1, srcY));
				dstX = max(0, min(newLightmap.width - 1, dstX));
				dstY = max(0, min(newLightmap.height - 1, dstY));

				int oldLuxelFlag = oldLightmap.luxelFlags[srcY * oldLightmap.width + srcX];
				int newLuxelFlag = newLightmap.luxelFlags[dstY * newLightmap.width + dstX];

				if (oldLuxelFlag == newLuxelFlag) {
					numMatch += 1;
				}
			}
		}

		if (numMatch > bestMatch) {
			bestMatch = numMatch;
			bestShiftCombo = t;
		}
	}

	int shouldShiftLeft = bestShiftCombo == 1 || bestShiftCombo == 3;
	int shouldShiftTop = bestShiftCombo == 2 || bestShiftCombo == 3;

	srcOffsetX = newLightmap.width != oldLightmap.width ? shouldShiftLeft : 0;
	srcOffsetY = newLightmap.height != oldLightmap.height ? shouldShiftTop : 0;
}

int Bsp::lightmap_count(int faceIdx) {
	BSPFACE& face = faces[faceIdx];

	if (texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL || face.nLightmapOffset >= lightDataLength)
		return 0;

	int lightmapCount = 0;
	for (int k = 0; k < 4; k++) {
		lightmapCount += face.nStyles[k] != 255;
	}

	return lightmapCount;
}

int Bsp::lightstyle_count() {
	int maxStyle = TOGGLED_LIGHT_STYLE_OFFSET - 1;

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();

		if (cname.find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			maxStyle = max(maxStyle, style);
		}
	}

	return maxStyle - (TOGGLED_LIGHT_STYLE_OFFSET - 1);
}

int Bsp::bake_lightmap_style(int style, bool deleteNotBake, bool reduceStyles, int faceIdx) {
	int numBakes = 0;

	for (int f = 0; f < faceCount; f++) {
		BSPFACE& face = faces[f];

		if (faceIdx > 0 && faceIdx != f)
			continue;

		int baseLightStyle = face.nStyles[0];

		int numStyles = 0;
		int styleIdx = -1;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == style) {
				if (styleIdx != -1) {
					logf("WARNING: Face %d has 2+ lightmaps linked to the same style\n");
				}
				styleIdx = s;
			}
			if (face.nStyles[s] != 255)
				numStyles++;
		}

		if (styleIdx == -1)
			continue;

		if (styleIdx == 0) {
			// base lightmap had the style link.
			if (deleteNotBake) {
				face.nStyles[0] = 255;
			}
			else {
				// Just remove the link and the light stays on
				face.nStyles[0] = 0;
			}
			continue;
		}

		int size[2];
		int imins[2];
		int imaxs[2];
		GetFaceLightmapSize(this, f, size);
		GetFaceExtents(this, f, imins, imaxs);

		int width = size[0];
		int height = size[1];
		int lightmapSz = width * height * sizeof(COLOR3);

		if (!deleteNotBake) {
			numBakes++;

			if (face.nStyles[0] > 0 && face.nStyles[0] < 255) {
				// move the toggled base light into this previously toggled style
				// then disable the style link in the base light to keep it on

				int offsetSrc = face.nLightmapOffset + styleIdx * lightmapSz;
				int offsetDst = face.nLightmapOffset;
				COLOR3* lightSrc = (COLOR3*)(lightdata + offsetSrc);
				COLOR3* lightDst = (COLOR3*)(lightdata + offsetDst);
				for (int idx = 0; idx < width * height; idx++) {
					if (offsetSrc + idx * sizeof(COLOR3) < lightDataLength) {
						COLOR3& src = lightSrc[idx];
						COLOR3& dst = lightDst[idx];
						COLOR3 temp = src;
						src = dst;
						dst = temp;
					}
				}

				face.nStyles[styleIdx] = baseLightStyle;
				face.nStyles[0] = 0;
				continue;
			}

			// the base lightmap isn't toggled. The toggled style needs to be added to the base light
			// to disable toggling.

			int offset = face.nLightmapOffset + styleIdx * lightmapSz;
			COLOR3* lightSrc = (COLOR3*)(lightdata + offset);
			COLOR3* lightDst = (COLOR3*)(lightdata + face.nLightmapOffset);
			for (int idx = 0; idx < width * height; idx++) {
				if (offset + idx * sizeof(COLOR3) < lightDataLength) {
					COLOR3& src = lightSrc[idx];
					COLOR3& dst = lightDst[idx];
					dst.r = min(255, src.r + dst.r);
					dst.g = min(255, src.g + dst.g);
					dst.b = min(255, src.b + dst.b);
				}
			}
		}

		// keep style refs contiguous
		for (int i = styleIdx; i < MAXLIGHTMAPS - 1; i++) {
			face.nStyles[i] = face.nStyles[i + 1];
		}
		face.nStyles[MAXLIGHTMAPS - 1] = 255;

		int totalLightmapsSz = numStyles * lightmapSz;
		int lightmapsToShift = (numStyles - (styleIdx + 1));
		int shiftBytes = lightmapsToShift * lightmapSz;
		COLOR3* lightTo = (COLOR3*)(lightdata + face.nLightmapOffset + styleIdx * lightmapSz);
		COLOR3* lightFrom = (COLOR3*)(lightdata + face.nLightmapOffset + (styleIdx + 1) * lightmapSz);
		if (shiftBytes > 0)
			memmove(lightTo, lightFrom, shiftBytes);
	}

	if (reduceStyles && faceIdx < 0) {
		// reduce lightstyles count
		for (int f = 0; f < faceCount; f++) {
			BSPFACE& face = faces[f];
			for (int s = 0; s < MAXLIGHTMAPS; s++) {
				if (face.nStyles[s] != 255 && face.nStyles[s] > style) {
					face.nStyles[s] -= 1;
				}
			}
		}
		for (int i = 0; i < ents.size(); i++) {
			string cname = ents[i]->getClassname();

			if (cname.find("light") == 0) {
				int entStyle = atoi(ents[i]->getKeyvalue("style").c_str());

				if (entStyle == style) {
					ents[i]->removeKeyvalue("style");
				}
				else if (entStyle > style) {
					ents[i]->setOrAddKeyvalue("style", cstrf("%d", entStyle - 1));
				}
			}
		}
	}

	return numBakes;
}

TextureAtlas* Bsp::create_lightmap_style_atlas(int style, vector<AtlasLightmap>& atlasLightmaps) {
	int totalPixels = 0;

	for (int f = 0; f < faceCount; f++) {
		BSPFACE& face = faces[f];
		BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];

		if (face.nLightmapOffset < 0 || (texinfo.nFlags & TEX_SPECIAL) || face.nLightmapOffset >= header.lump[LUMP_LIGHTING].nLength)
			continue;

		int styleIdx = -1;
		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			if (face.nStyles[s] == style) {
				if (styleIdx != -1) {
					logf("WARNING: Face %d has 2+ lightmaps linked to the same style\n");
				}
				styleIdx = s;
			}
		}

		if (styleIdx == -1)
			continue;

		AtlasLightmap fmap;
		int size[2];
		GetFaceLightmapSize(this, f, size);
		fmap.lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		fmap.w = size[0];
		fmap.h = size[1];
		fmap.idx = f;
		fmap.layer = styleIdx;

		totalPixels += fmap.w * fmap.h;

		atlasLightmaps.push_back(fmap);
	}

	sort(atlasLightmaps.begin(), atlasLightmaps.end(), [](const AtlasLightmap& a, const AtlasLightmap& b) {
		return a.lightmapSz > b.lightmapSz;
		});

	int atlasSz = 16;
	while (atlasSz * atlasSz < totalPixels * 1.05) {
		atlasSz += 16;
	}

	TextureAtlas* atlas = new TextureAtlas(atlasSz, atlasSz, atlasSz);

	for (int i = 0; i < atlasLightmaps.size(); i++) {
		AtlasLightmap& fmap = atlasLightmaps[i];
		BSPFACE& face = faces[fmap.idx];
		BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];

		// TODO: Try fitting in earlier atlases before using the latest one
		if (!atlas->insert(i, fmap.w, fmap.h, fmap.x, fmap.y)) {
			logf("Lightmap atlas too small!\n");
			break;
		}
	}

	return atlas;
}

void Bsp::export_lightmap_style(int style, const char* fname) {
	vector<AtlasLightmap> atlasLightmaps;
	TextureAtlas* atlas = create_lightmap_style_atlas(style, atlasLightmaps);

	COLOR3* atlasData = new COLOR3[atlas->mapW * atlas->mapH * sizeof(COLOR3)];
	memset(atlasData, 0, atlas->mapW * atlas->mapH * sizeof(COLOR3));

	std::string textName = stripExt(fname) + ".txt";
	FILE* txt = fopen(textName.c_str(), "w");

	if (!txt) {
		logf("Failed to open: %s\n", textName.c_str());
		return;
	}

	const char* header =
		"// This file maps a face index to a region in the PNG file. Click on a face in the editor\n"
		"// to see it's face number, then search for that #num in this file to find its lightmap.\n\n";
	fwrite(header, strlen(header), 1, txt);

	sort(atlasLightmaps.begin(), atlasLightmaps.end(), [](const AtlasLightmap& a, const AtlasLightmap& b) {
		return a.idx < b.idx;
		});

	for (int i = 0; i < atlasLightmaps.size(); i++) {
		AtlasLightmap& fmap = atlasLightmaps[i];
		BSPFACE& face = faces[fmap.idx];
		BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];

		// copy lightmap data into atlas
		int offset = face.nLightmapOffset + fmap.layer * fmap.lightmapSz;
		if (offset + fmap.w * fmap.h * sizeof(COLOR3) > lightDataLength) {
			logf("Face %d invalid lightmap %d\n", fmap.idx, fmap.layer);
			continue;
		}

		const char* def = cstrf("Face #%-5d: x=%-4d y=%-4d size=%dx%d\n",
			fmap.idx, fmap.x, fmap.y, fmap.w, fmap.h);
		fwrite(def, strlen(def), 1, txt);

		COLOR3* lightSrc = (COLOR3*)(lightdata + offset);
		for (int y = 0; y < fmap.h; y++) {
			for (int x = 0; x < fmap.w; x++) {
				int src = y * fmap.w + x;
				int dst = (fmap.y + y) * atlas->mapW + fmap.x + x;
				if (offset + src * sizeof(COLOR3) < lightDataLength) {
					atlasData[dst] = lightSrc[src];
				}
				else {
					bool checkers = x % 2 == 0 != y % 2 == 0;
					atlasData[dst] = { (byte)(checkers ? 255 : 0), 0, (byte)(checkers ? 255 : 0) };
				}
			}
		}
	}

	fclose(txt);

	lodepng_encode24_file(fname, (byte*)atlasData, atlas->mapW, atlas->mapH);
	delete[] atlasData;
	delete atlas;
	logf("Wrote %d lightmaps to %s\n", atlasLightmaps.size(), fname);
}

void Bsp::import_lightmap_style(int style, const char* fname) {
	vector<AtlasLightmap> atlasLightmaps;
	TextureAtlas* atlas = create_lightmap_style_atlas(style, atlasLightmaps);

	COLOR3* atlasData;
	unsigned int pngWidth, pngHeight;
	if (lodepng_decode24_file((unsigned char**)&atlasData, &pngWidth, &pngHeight, fname)) {
		logf("Failed to load PNG file\n");
		return;
	}

	if (pngWidth != atlas->mapW || pngHeight != atlas->mapH) {
		logf("PNG dimensions don't match expected atlas size (%dx%d != %dx%d)\n",
			pngWidth, pngHeight, atlas->mapW, atlas->mapH);
		return;
	}

	for (int i = 0; i < atlasLightmaps.size(); i++) {
		AtlasLightmap& fmap = atlasLightmaps[i];
		BSPFACE& face = faces[fmap.idx];
		BSPTEXTUREINFO& texinfo = texinfos[face.iTextureInfo];

		// copy lightmap data into atlas
		int offset = face.nLightmapOffset + fmap.layer * fmap.lightmapSz;
		if (offset + fmap.w * fmap.h * sizeof(COLOR3) > lightDataLength) {
			logf("Face %d invalid lightmap %d\n", fmap.idx, fmap.layer);
			continue;
		}

		COLOR3* lightDst = (COLOR3*)(lightdata + offset);
		for (int y = 0; y < fmap.h; y++) {
			for (int x = 0; x < fmap.w; x++) {
				int dst = y * fmap.w + x;
				int src = (fmap.y + y) * atlas->mapW + fmap.x + x;
				if (offset + dst * sizeof(COLOR3) < lightDataLength) {
					lightDst[dst] = atlasData[src];
				}
			}
		}
	}

	delete[] atlasData;
	delete atlas;
	logf("Loaded %d lightmaps\n", atlasLightmaps.size());
}

int Bsp::remove_unused_lightstyles() {
	bool usedStyles[256];
	memset(usedStyles, 0, sizeof(bool) * 256);

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname().find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			if (style > 0 && style < 255)
				usedStyles[style] = true;
		}
	}

	bool deletedStyles[256];
	uint8_t styleRemaps[256];
	for (int i = 0; i < 256; i++) {
		styleRemaps[i] = i;
		deletedStyles[i] = false;
	}

	int lightBakes = 0;
	int lastUsedIdx = 31;
	for (int i = TOGGLED_LIGHT_STYLE_OFFSET; i < 255; i++) {
		if (usedStyles[i]) {
			int gap = (i - lastUsedIdx) - 1;
			if (gap > 0) {
				for (int k = lastUsedIdx + 1; k < i; k++) {
					deletedStyles[k] = true;
					bake_lightmap_style(k, false, false);
					lightBakes++;
				}
				for (int k = i; k < 256; k++) {
					styleRemaps[k] -= gap;
				}
			}
			lastUsedIdx = i;
		}
	}

	for (int k = 0; k < faceCount; k++) {
		BSPFACE& face = faces[k];

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			uint8_t& style = face.nStyles[s];
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255) {
				if (!deletedStyles[style] && style != styleRemaps[style]) {
					style = styleRemaps[style];
				}
			}
		}
	}

	for (int i = 0; i < ents.size(); i++) {
		string cname = ents[i]->getClassname();

		if (cname.find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255 && style != styleRemaps[style]) {
				ents[i]->setOrAddKeyvalue("style", to_string(styleRemaps[style]));
			}
		}
	}

	return lightBakes;
}

bool Bsp::shift_lightstyles(uint32_t shift) {
	if (lightstyle_count() + shift >= 255) {
		logf("Unable to shift lightstyles by %u without losing some of them\n", shift);
		return false;
	}

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname().find("light") == 0) {
			int style = atoi(ents[i]->getKeyvalue("style").c_str());
			ents[i]->setOrAddKeyvalue("style", to_string(style + shift));
		}
	}

	for (int k = 0; k < faceCount; k++) {
		BSPFACE& face = faces[k];

		for (int s = 0; s < MAXLIGHTMAPS; s++) {
			uint8_t& style = face.nStyles[s];
			if (style >= TOGGLED_LIGHT_STYLE_OFFSET && style < 255) {
				style += shift;
			}
		}
	}

	return true;
}

COLOR3 Bsp::get_lighting(vec3 pos)
{
	COLOR3 light = COLOR3(0, 0, 0);
	int u, v;

	int faceIdx = traceFace(pos, pos - vec3(0, 0, 2048), u, v);

	if (faceIdx == -1) {
		return light;
	}

	BSPFACE& face = faces[faceIdx];

	int lmap_sz[2];
	GetFaceLightmapSize(this, faceIdx, lmap_sz);

	int lx = u / 16;
	int ly = v / 16;
	int offset = face.nLightmapOffset + (ly * lmap_sz[0] + lx) * 3;

	for (int s = 0; s < MAXLIGHTMAPS; s++) {
		if (face.nStyles[s] == 255 || !g_app->lightStylesEnabled[s])
			break;
		light.r += lightdata[offset];
		light.g += lightdata[offset + 1];
		light.b += lightdata[offset + 2];
		offset += lmap_sz[0] * lmap_sz[1] * 3;
	}

	return light;
}

int Bsp::make_unique_texlight_models() {
	unordered_map<string, string> texlights = get_tex_lights();
	unordered_set<int> texlight_models;

	for (Entity* ent : ents) {
		if (ent->getClassname() == "light_surface") {
			texlights[toUpperCase(ent->getKeyvalue("_tex"))] = ent->getKeyvalue("_light");
		}
	}


	for (int i = 1; i < modelCount; i++) {
		BSPMODEL& model = models[i];

		for (int k = model.iFirstFace; k < model.iFirstFace + model.nFaces; k++) {
			BSPMIPTEX* tex = get_texture(texinfos[faces[k].iTextureInfo].iMiptex);

			if (tex && texlights.count(toUpperCase(tex->szName))) {
				// Distance, name, and classnames filters aren't checked for simplicity.
				// Duplicated models can be deduplicated after running RAD.
				texlight_models.insert(i);
				break;
			}
		}
	}

	int numDups = 0;

	for (Entity* ent : ents) {
		int modelidx = ent->getBspModelIdx();

		if (texlight_models.count(modelidx)) {
			bool isUnique = true;

			for (Entity* ent2 : ents) {
				if (ent != ent2 && ent2->getBspModelIdx() == modelidx) {
					isUnique = false;
					break;
				}
			}

			if (!isUnique) {
				logf("Duplicate model %d\n", modelidx);
				int newModelIdx = duplicate_model(modelidx);
				ent->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));
				numDups++;
			}
		}
	}

	return numDups;
}

unordered_map<string, string> Bsp::get_tex_lights() {
	unordered_map<string, string> texlights;

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->getClassname() == "info_texlights") {
			StringMap keys = ent->getAllKeyvalues();

			StringMap::iterator_t iter;
			while (keys.iterate(iter)) {
				if (strcmp(iter.key, "classname") && strcmp(iter.key, "origin"))
					texlights[toUpperCase(iter.key)] = iter.value;
			}
		}
	}

	return texlights;
}

unordered_map<string, string> Bsp::filter_tex_lights(const unordered_map<string, string>& inputLights) {
	unordered_map<string, string> filtered;

	unordered_set<string> textureNames;
	for (int i = 0; i < faceCount; i++) {
		BSPMIPTEX* tex = get_texture(texinfos[faces[i].iTextureInfo].iMiptex);

		if (tex) {
			textureNames.insert(toUpperCase(tex->szName));
		}
	}

	for (auto item : inputLights) {
		if (textureNames.count(toUpperCase(item.first))) {
			filtered[item.first] = item.second;
		}
	}

	return filtered;
}

unordered_map<string, string> Bsp::load_texlights_from_file(string fname) {
	std::ifstream file(fname); // Open file
	if (!file) {
		logf("Failed to open file: %s\n", fname.c_str());
		return unordered_map<string, string>();
	}

	unordered_map<string, string> texlights;

	string line;
	while (std::getline(file, line)) {
		string name, args;
		if (load_texlight_from_string(line, name, args)) {
			texlights[name] = args;
		}
	}

	return filter_tex_lights(texlights);
}

bool Bsp::load_texlight_from_string(string line, string& name, string& args) {
	int comment = line.find("//");
	if (comment != -1) {
		line = line.substr(0, comment);
	}
	line = trimSpaces(line);

	int space = line.find_first_of(" \t");
	if (space == -1)
		return false;

	name = toUpperCase(line.substr(0, space));
	args = trimSpaces(line.substr(space));

	return true;
}

bool Bsp::add_texlights(const unordered_map<string, string>& newLights) {
	unordered_map<string, string> texlights = get_tex_lights();

	bool anyChanges = false;

	if (texlights.size())
		logf("Parsed %d texlights from existing info_texlights entities.\n", texlights.size());

	int oldSize = texlights.size();

	unordered_map<string, string> filteredNewLights = filter_tex_lights(newLights);

	if (!filteredNewLights.size()) {
		logf("No new texlights to add/update.\n");
		return false;
	}

	logf("Added/updated %d texlights\n", filteredNewLights.size());
	for (auto item : filteredNewLights) {
		texlights[toUpperCase(item.first)] = item.second;
	}

	vec3 ori;

	for (int i = 0; i < ents.size(); i++) {
		Entity* ent = ents[i];
		if (ent->getClassname() == "info_texlights") {
			ori = ent->getOrigin();
			delete ent;
			ents.erase(ents.begin() + i);
			i--;
		}
	}

	Entity* texlights_ent = new Entity();

	std::vector<std::string> keys;
	for (const auto& item : texlights) {
		keys.push_back(item.first);
	}

	sort(keys.begin(), keys.end());

	for (const string& key : keys) {
		texlights_ent->setOrAddKeyvalue(key, texlights[key]);
	}
	if (ori != vec3()) {
		texlights_ent->setOrAddKeyvalue("origin", ori.toKeyvalueString());
	}

	texlights_ent->setOrAddKeyvalue("classname", "info_texlights");

	ents.push_back(texlights_ent);

	return true;
}

bool Bsp::replace_texlights(string texlightString) {
	vector<string> lines = splitString(texlightString, "\n");

	unordered_map<string, string> texlights;

	for (string line : lines) {
		int comment = line.find("//");
		if (comment != -1) {
			line = line.substr(0, comment);
		}
		line = trimSpaces(line);

		int space = line.find_first_of(" \t");
		if (space == -1)
			continue;

		string name = toUpperCase(line.substr(0, space));
		string args = trimSpaces(line.substr(space));
		texlights[name] = args;
	}

	return add_texlights(texlights);
}

unordered_map<string, string> Bsp::estimate_texlights(int epsilon) {
	unordered_map<string, string> texlights = get_tex_lights();
	unordered_map<string, string> newTexlights;

	unordered_set<string> light_surface_names;
	unordered_set<string> global_light_surface_names; // texture names that are always affected by light_surface
	vector<Entity*> light_surface_ents;
	for (Entity* ent : ents) {
		string cname = ent->getClassname();
		if (cname == "light_surface" || cname == "light" || cname == "light_spot") {
			string texname = toUpperCase(ent->getKeyvalue("_tex"));
			if (texname.empty())
				continue;

			light_surface_ents.push_back(ent);

			if (!ent->hasKey("_frange") && !ent->hasKey("_fdist") && !ent->hasKey("_fclass") && !ent->hasKey("_fname")) {
				global_light_surface_names.insert(texname);
			}
			else {
				light_surface_names.insert(texname);
			}
		}
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);

		if (!tex || is_embedded_rad_texture_name(tex->szName))
			continue;

		string surfaceName = toUpperCase(tex->szName);

		if (global_light_surface_names.count(surfaceName)) {
			// a light_surface entity in the map is affecting every face using this texture,
			// so adding it as a texlight would have no effect.
			continue;
		}

		if (texlights.count(surfaceName)) {
			// already specified in info_texlights
			continue;
		}

		// true if only some faces are affected by light_surface ents in the map
		bool affectedByLightSurface = light_surface_names.count(surfaceName);

		bool anyLightmaps = false;
		bool isTexlight = true;
		bool hasLightcolor = false;
		COLOR3 minColor(255, 255, 255);
		COLOR3 maxColor(0, 0, 0);
		const int defaultBrightness = 8000; // better too bright than too dark
		int numFaces = 0;

		for (int k = 0; k < faceCount; k++) {
			BSPFACE& face = faces[k];
			BSPTEXTUREINFO& info = texinfos[faces[k].iTextureInfo];
			if (get_texture(info.iMiptex) != tex) {
				continue;
			}

			if (info.nFlags & TEX_SPECIAL) {
				continue; // special faces don't have lightmaps
			}

			if (affectedByLightSurface) {
				// TODO: skip face if this face is affected by light_surface
			}

			int size[2];
			if (!GetFaceLightmapSize(this, k, size)) {
				debugf("Invalid extents for face %d. Can't check lightmaps", k);
				continue;
			}
			int w = size[0];
			int h = size[1];

			if (face.nStyles[0] == 255) {
				continue; // pitch black faces can't be texlights
			}

			// texlights can receive lighting (c1a0 monitor) so don't skip if it has lightstyles
			/*
			if (face.nStyles[1] != 255) {
				isTexlight = false;
				break; // texlights receive no lighting, so this can't be a texlight if it has styles
			}
			*/

			anyLightmaps = true;
			numFaces++;

			int lightmapSz = w * h * sizeof(COLOR3);
			int offset = face.nLightmapOffset;
			COLOR3* lightSrc = (COLOR3*)(lightdata + offset);
			for (int y = 0; y < h && isTexlight; y++) {
				for (int x = 0; x < w; x++) {
					COLOR3 color = lightSrc[y * w + x];
					minColor.r = min(minColor.r, color.r);
					minColor.g = min(minColor.g, color.g);
					minColor.b = min(minColor.b, color.b);
					maxColor.r = max(maxColor.r, color.r);
					maxColor.g = max(maxColor.g, color.g);
					maxColor.b = max(maxColor.b, color.b);
				}
			}

			if (!isTexlight) {
				break;
			}
		}

		COLOR3 avgColor(
			minColor.r + (maxColor.r - minColor.r) * 0.5f,
			minColor.g + (maxColor.g - minColor.g) * 0.5f,
			minColor.b + (maxColor.b - minColor.b) * 0.5f
		);
		int maxChannel = max(avgColor.r, max(avgColor.g, avgColor.b));
		if (maxChannel < 90) {
			// too dark to be a texlight. With -dlight 1.0, the minimum value
			// for the brightest texlight color channel is 92.
			isTexlight = false;
		}
		else {
			if (abs((int)minColor.r - (int)maxColor.r) > epsilon
				|| abs((int)minColor.g - (int)maxColor.g) > epsilon
				|| abs((int)minColor.b - (int)maxColor.b) > epsilon) {
				// lightmap is not entirely the same color as every other lightmap pixel
				// for every other lightmap for this texture. Must not be a texlight.
				isTexlight = false;
			}
		}

		if (isTexlight && anyLightmaps) {
			newTexlights[tex->szName] = to_string(maxColor.r) + " " + to_string(maxColor.g) + " "
				+ to_string(maxColor.b) + " " + to_string(defaultBrightness);
		}
	}

	return newTexlights;
}

void Bsp::convert_lightmaps(LumpState& state, bool monochromeNotRgb) {
	if (!state.lumps[LUMP_LIGHTING] || !state.lumps[LUMP_FACES])
		return;

	string litFile = path.substr(0, path.size() - 4) + ".lit";

	if (monochromeNotRgb) {
		// convert from RGB light to monochrome
		int pixels = state.lumpLen[LUMP_LIGHTING] / sizeof(COLOR3);
		uint8_t* newLighting = new uint8_t[pixels];
		COLOR3* srcLighting = (COLOR3*)state.lumps[LUMP_LIGHTING];

		bool hasColorLighting = false;

		for (int i = 0; i < pixels; i++) {
			COLOR3& c = srcLighting[i];
			hasColorLighting |= c.r != c.g || c.r != c.b;
			newLighting[i] = monochrome_lightmap_pixel(srcLighting[i]);
		}

		// write an external .lit file if the map has colored lighting
		if (hasColorLighting) {
			QLITHEADER head;
			memcpy(head.magic, "QLIT", 4);
			head.version = 1;

			int fsize = sizeof(QLITHEADER) + state.lumpLen[LUMP_LIGHTING];
			uint8_t* fdat = new uint8_t[fsize];

			memcpy(fdat, &head, sizeof(QLITHEADER));
			memcpy(fdat + sizeof(QLITHEADER), srcLighting, state.lumpLen[LUMP_LIGHTING]);
			writeFile(litFile.c_str(), (const char*)fdat, fsize);

			delete[] fdat;
		}
		else {
			// no need for the QLIT file if there's no colored lighting. Remove it to prevent colored
			// lights coming back after they were removed explicitly.
			remove(litFile.c_str());
		}

		delete[] state.lumps[LUMP_LIGHTING];
		state.lumps[LUMP_LIGHTING] = newLighting;
		state.lumpLen[LUMP_LIGHTING] = pixels;

		int numFaces = state.lumpLen[LUMP_FACES] / sizeof(BSPFACE);
		BSPFACE* lumpFaces = (BSPFACE*)state.lumps[LUMP_FACES];
		for (int i = 0; i < numFaces; i++) {
			lumpFaces[i].nLightmapOffset /= 3;
		}
	}
	else {
		// update face offsets for RGB data
		int numFaces = state.lumpLen[LUMP_FACES] / sizeof(BSPFACE);
		BSPFACE* lumpFaces = (BSPFACE*)state.lumps[LUMP_FACES];
		for (int i = 0; i < numFaces; i++) {
			lumpFaces[i].nLightmapOffset *= 3;
		}

		// Try to load color lightmaps from an external LIT file
		int numLightPixels = state.lumpLen[LUMP_LIGHTING];
		int len;
		char* litData = loadFile(litFile, len);

		if (litData) {
			QLITHEADER* header = (QLITHEADER*)litData;

			int expectedSize = sizeof(QLITHEADER) + numLightPixels * 3;
			if (len < expectedSize) {
				warnf("QLIT is smaller than expected (%d < %d bytes): %s\n", len, expectedSize, litFile.c_str());
			}
			else if (strncmp(header->magic, "QLIT", 4)) {
				string magic = string(header->magic, 4);
				warnf("QLIT has invalid header '%s': %s\n", magic.c_str(), litFile.c_str());
			}
			else if (header->version != 1) {
				warnf("QLIT version %d not supported: %s\n", header->version, litFile.c_str());
			}
			else {
				int lightDataLen = len - sizeof(QLITHEADER);
				state.lumpLen[LUMP_LIGHTING] = lightDataLen;
				delete[] state.lumps[LUMP_LIGHTING];
				state.lumps[LUMP_LIGHTING] = new uint8_t[lightDataLen];
				memcpy(state.lumps[LUMP_LIGHTING], litData + sizeof(QLITHEADER), lightDataLen);
				logf("QLIT loaded: %s\n", litFile.c_str());
				return;
			}

			warnf("Falling back to monochrome lighting due to failed LIT file load.\n");
		}

		// convert from monochrome light to RGB
		int pixels = state.lumpLen[LUMP_LIGHTING];
		COLOR3* newLighting = new COLOR3[pixels];

		for (int i = 0; i < pixels; i++) {
			uint8_t b = state.lumps[LUMP_LIGHTING][i];
			newLighting[i] = COLOR3(b, b, b);
		}

		delete[] state.lumps[LUMP_LIGHTING];
		state.lumps[LUMP_LIGHTING] = (uint8_t*)newLighting;
		state.lumpLen[LUMP_LIGHTING] = pixels * sizeof(COLOR3);
	}
}

void Bsp::dump_lightmap(int faceIdx, string outputPath) {
	int faceCount = header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	BSPFACE& face = faces[faceIdx];

	int mins[2];
	int extents[2];
	GetFaceExtents(this, faceIdx, mins, extents);

	int lightmapSz = extents[0] * extents[1];

	lodepng_encode24_file(outputPath.c_str(), (byte*)lightdata + face.nLightmapOffset, extents[0], extents[1]);
}

void Bsp::dump_lightmap_atlas(string outputPath) {
	int lightmapWidth = g_limits.max_surface_extents;

	int lightmapsPerDim = ceil(sqrt(faceCount));
	int atlasDim = lightmapsPerDim * lightmapWidth;
	int sz = atlasDim * atlasDim;
	logf("ATLAS SIZE %d x %d (%.2f KB)", lightmapsPerDim, lightmapsPerDim, (sz * sizeof(COLOR3)) / 1024.0f);

	COLOR3* pngData = new COLOR3[sz];

	memset(pngData, 0, sz * sizeof(COLOR3));

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (face.nStyles[0] == 255)
			continue; // no lighting info

		int atlasX = (i % lightmapsPerDim) * lightmapWidth;
		int atlasY = (i / lightmapsPerDim) * lightmapWidth;

		int size[2];
		GetFaceLightmapSize(this, i, size);

		int lightmapWidth = size[0];
		int lightmapHeight = size[1];

		for (int y = 0; y < lightmapHeight; y++) {
			for (int x = 0; x < lightmapWidth; x++) {
				int dstX = atlasX + x;
				int dstY = atlasY + y;

				int lightmapOffset = (y * lightmapWidth + x) * sizeof(COLOR3);

				COLOR3* src = (COLOR3*)(lightdata + face.nLightmapOffset + lightmapOffset);

				pngData[dstY * atlasDim + dstX] = *src;
			}
		}
	}

	lodepng_encode24_file(outputPath.c_str(), (byte*)pngData, atlasDim, atlasDim);
}

void Bsp::resize_lightmaps(LIGHTMAP* oldLightmaps, LIGHTMAP* newLightmaps) {
	g_progress.update("Recalculate lightmaps", faceCount);

	// calculate new lightmap sizes
	int newLightDataSz = 0;
	int totalLightmaps = 0;
	int lightmapsResizeCount = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		g_progress.tick();

		if (lightmap_count(i) == 0)
			continue;

		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];

		int size[2];
		GetFaceLightmapSize(this, i, size);

		int lightmapSz = size[0] * size[1];

		newLightmaps[i].width = size[0];
		newLightmaps[i].height = size[1];
		newLightmaps[i].layers = oldLightmaps[i].layers;

		newLightDataSz += (lightmapSz * newLightmaps[i].layers) * sizeof(COLOR3);

		totalLightmaps += newLightmaps[i].layers;
		if (oldLightmaps[i].width != newLightmaps[i].width || oldLightmaps[i].height != newLightmaps[i].height) {
			lightmapsResizeCount += newLightmaps[i].layers;
		}
	}

	if (lightmapsResizeCount > 0) {
		//logf("%d lightmap(s) to resize\n", lightmapsResizeCount);

		g_progress.update("Resize lightmaps", faceCount);

		int newColorCount = newLightDataSz / sizeof(COLOR3);
		COLOR3* newLightData = new COLOR3[newColorCount];
		memset(newLightData, 255, newColorCount * sizeof(COLOR3));
		int lightmapOffset = 0;

		for (int i = 0; i < faceCount; i++) {
			BSPFACE& face = faces[i];

			g_progress.tick();

			if (lightmap_count(i) == 0) // no lighting
				continue;

			LIGHTMAP& oldLight = oldLightmaps[i];
			LIGHTMAP& newLight = newLightmaps[i];
			int oldLayerSz = (oldLight.width * oldLight.height) * sizeof(COLOR3);
			int newLayerSz = (newLight.width * newLight.height) * sizeof(COLOR3);
			int oldSz = oldLayerSz * oldLight.layers;
			int newSz = newLayerSz * newLight.layers;

			totalLightmaps++;

			bool faceMoved = oldLightmaps[i].luxelFlags != NULL;
			bool lightmapResized = oldLight.width != newLight.width || oldLight.height != newLight.height;

			if (!faceMoved || !lightmapResized) {
				memcpy((byte*)newLightData + lightmapOffset, (byte*)lightdata + face.nLightmapOffset, oldSz);
				newLight.luxelFlags = NULL;
			}
			else {
				newLight.luxelFlags = new byte[newLight.width * newLight.height];
				qrad_get_lightmap_flags(this, i, newLight.luxelFlags);

				int maxWidth = min(newLight.width, oldLight.width);
				int maxHeight = min(newLight.height, oldLight.height);

				int srcOffsetX, srcOffsetY;
				get_lightmap_shift(oldLight, newLight, srcOffsetX, srcOffsetY);

				for (int layer = 0; layer < newLight.layers; layer++) {
					int srcOffset = (face.nLightmapOffset + oldLayerSz * layer) / sizeof(COLOR3);
					int dstOffset = (lightmapOffset + newLayerSz * layer) / sizeof(COLOR3);

					int startX = newLight.width > oldLight.width ? -1 : 0;
					int startY = newLight.height > oldLight.height ? -1 : 0;

					for (int y = startY; y < newLight.height; y++) {
						for (int x = startX; x < newLight.width; x++) {
							int offsetX = x + srcOffsetX;
							int offsetY = y + srcOffsetY;

							int srcX = oldLight.width > newLight.width ? offsetX : x;
							int srcY = oldLight.height > newLight.height ? offsetY : y;
							int dstX = newLight.width > oldLight.width ? offsetX : x;
							int dstY = newLight.height > oldLight.height ? offsetY : y;

							srcX = max(0, min(oldLight.width - 1, srcX));
							srcY = max(0, min(oldLight.height - 1, srcY));
							dstX = max(0, min(newLight.width - 1, dstX));
							dstY = max(0, min(newLight.height - 1, dstY));

							COLOR3& src = ((COLOR3*)lightdata)[srcOffset + srcY * oldLight.width + srcX];
							COLOR3& dst = newLightData[dstOffset + dstY * newLight.width + dstX];

							dst = src;
						}
					}
				}
			}

			face.nLightmapOffset = lightmapOffset;
			lightmapOffset += newSz;
		}

		replace_lump(LUMP_LIGHTING, newLightData, lightmapOffset);
	}
}

int Bsp::remove_unused_lightmaps(bool* usedFaces) {
	int oldLightdataSize = lightDataLength;

	int* lightmapSizes = new int[faceCount];

	int newLightDataSize = 0;
	for (int i = 0; i < faceCount; i++) {
		if (usedFaces[i]) {
			lightmapSizes[i] = GetFaceLightmapSizeBytes(this, i);
			newLightDataSize += lightmapSizes[i];
		}
	}

	byte* newColorData = new byte[newLightDataSize];

	int offset = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];

		if (usedFaces[i] && ((int64)face.nLightmapOffset + lightmapSizes[i]) <= (int64)lightDataLength) {
			memcpy(newColorData + offset, lightdata + face.nLightmapOffset, lightmapSizes[i]);
			face.nLightmapOffset = offset;
			offset += lightmapSizes[i];
		}
	}

	delete[] lightmapSizes;

	replace_lump(LUMP_LIGHTING, newColorData, newLightDataSize);

	return oldLightdataSize - newLightDataSize;
}
