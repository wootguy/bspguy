#include "Bsp.h"
#include "util.h"
#include "icons/aaatrigger.h"
#include "lodepng.h"
#include "Texture.h"
#include "Entity.h"
#include "Editor.h"
#include "BspRenderer.h"
#include "quant.h"
#include "q1_palette.h"

int Bsp::delete_embedded_textures() {
	uint headerSz = (textureCount + 1) * sizeof(int32_t);
	uint newTexDataSize = headerSz + (textureCount * sizeof(BSPMIPTEX));
	byte* newTextureData = new byte[newTexDataSize];

	BSPMIPTEX* mips = (BSPMIPTEX*)(newTextureData + headerSz);

	int32_t* header = (int32_t*)newTextureData;
	*header = textureCount;
	header++;

	int numRemoved = 0;

	for (int i = 0; i < textureCount; i++) {
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);

		if (oldTex->nOffsets[0] != -1) {
			numRemoved++;
		}

		header[i] = headerSz + i * sizeof(BSPMIPTEX);
		mips[i].nWidth = oldTex->nWidth;
		mips[i].nHeight = oldTex->nHeight;
		memcpy(mips[i].szName, oldTex->szName, MAXTEXTURENAME);
		memset(mips[i].nOffsets, 0, MIPLEVELS * sizeof(int32_t));
	}

	replace_lump(LUMP_TEXTURES, newTextureData, newTexDataSize);

	return numRemoved;
}

void Bsp::get_scaled_texture_dimensions(int textureIdx, float scale, int& newWidth, int& newHeight) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return;
	}

	newWidth = max(16, (((int)(tex->nWidth * scale) + 7) / 16) * 16);
	newHeight = max(16, (((int)(tex->nHeight * scale) + 7) / 16) * 16);
	if (newWidth == tex->nWidth) {
		newWidth = max(16, ((int)(tex->nWidth * scale) / 16) * 16);
	}
	if (newHeight == tex->nHeight) {
		newHeight = max(16, ((int)(tex->nHeight * scale) / 16) * 16);
	}
}

int Bsp::get_default_texture_idx() {
	int32_t totalTextures = ((int32_t*)textures)[0];
	for (uint i = 0; i < totalTextures; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}
		if (strcmp(tex->szName, "aaatrigger") == 0) {
			return i;
		}
	}

	// add the aaatrigger texture if it doesn't already exist
	byte* tex_dat = NULL;
	uint w, h;

	logf("Added aaatrigger texture\n");
	lodepng_decode24(&tex_dat, &w, &h, aaatrigger_dat, sizeof(aaatrigger_dat));
	int aaatriggerIdx = add_texture("aaatrigger", tex_dat, w, h);
	delete[] tex_dat;

	return aaatriggerIdx;
}

bool Bsp::downscale_texture(int textureId, int newWidth, int newHeight, int resampleMode) {
	if ((newWidth % 16 != 0) || (newHeight % 16 != 0) || newWidth <= 0 || newHeight <= 0) {
		logf("Invalid downscale dimensions: %dx%d\n", newWidth, newHeight);
		return false;
	}

	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	int oldWidth = tex->nWidth;
	int oldHeight = tex->nHeight;

	tex->nWidth = newWidth;
	tex->nHeight = newHeight;

	int lastMipSize = (oldWidth >> 3) * (oldHeight >> 3);
	byte* pixels = (byte*)(textures + texOffset + tex->nOffsets[0]);
	byte* palette = (byte*)(textures + texOffset + tex->nOffsets[3] + lastMipSize);
	COLOR3* paletteColors = (COLOR3*)(palette + 2); // skip color count

	int newWidths[4];
	int newHeights[4];
	int newOffset[4];
	for (int i = 0; i < 4; i++) {
		newWidths[i] = tex->nWidth >> (1 * i);
		newHeights[i] = tex->nHeight >> (1 * i);

		if (i > 0) {
			newOffset[i] = newOffset[i - 1] + newWidths[i - 1] * newHeights[i - 1];
		}
		else {
			newOffset[i] = sizeof(BSPMIPTEX);
		}
	}

	float srcScaleX = (float)oldWidth / tex->nWidth;
	float srcScaleY = (float)oldHeight / tex->nHeight;

	byte* srcPixels = (byte*)(textures + texOffset + tex->nOffsets[0]);
	COLOR3* srcColors = new COLOR3[oldWidth * oldHeight];
	for (int i = 0; i < oldWidth * oldHeight; i++) {
		srcColors[i] = paletteColors[srcPixels[i]];
	}

	COLOR3* dstColors = new COLOR3[newWidth * newHeight];
	vector<COLOR3> newColors = Texture::resample(srcColors, oldWidth, oldHeight, dstColors,
		newWidth, newHeight, resampleMode, tex->szName[0] == '{', paletteColors[255]);

	if (newColors.empty()) {
		for (int i = newColors.size(); i < 256; i++) {
			newColors.push_back(paletteColors[i]);
		}
	}

	// convert pixels to palette indexes
	byte* mip0 = (byte*)(textures + texOffset + newOffset[0]);
	for (int i = 0; i < newWidth * newHeight; i++) {
		for (int k = 0; k < newColors.size(); k++) {
			if (newColors[k] == dstColors[i]) {
				mip0[i] = k;
				break;
			}
		}
	}
	delete[] dstColors;

	// nearest neighbor mipmap resize
	byte* srcData = (byte*)(textures + texOffset + newOffset[0]);
	for (int i = 1; i < 4; i++) {
		byte* dstData = (byte*)(textures + texOffset + newOffset[i]);
		int mipWidth = newWidth >> i;
		int mipHeight = newHeight >> i;
		int mipScale = 1 << i;

		for (int y = 0; y < mipHeight; y++) {
			for (int x = 0; x < mipWidth; x++) {
				dstData[y * mipWidth + x] = srcData[y * mipScale * newWidth + x * mipScale];
			}
		}
	}
	// 2 = palette color count (should always be 256)
	byte* newPalette = (byte*)(textures + texOffset + newOffset[3] + newWidths[3] * newHeights[3]);
	memcpy(newPalette, palette, 2);
	memset(newPalette + 2, 0, sizeof(COLOR3) * 256);
	memcpy(newPalette + 2, &newColors[0], sizeof(COLOR3) * newColors.size());

	for (int i = 0; i < 4; i++) {
		tex->nOffsets[i] = newOffset[i];
	}

	adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);

	// shrink texture lump
	int removedBytes = palette - newPalette;
	byte* texEnd = newPalette + 256 * sizeof(COLOR3) + 2;
	int shiftBytes = (texEnd - textures) + removedBytes;

	memcpy(texEnd, texEnd + removedBytes, header.lump[LUMP_TEXTURES].nLength - shiftBytes);
	for (int k = textureId + 1; k < textureCount; k++) {
		((int32_t*)textures)[k + 1] -= removedBytes;
	}

	logf("Downscale %s %dx%d -> %dx%d\n", tex->szName, oldWidth, oldHeight, tex->nWidth, tex->nHeight);

	return true;
}

bool Bsp::downscale_texture(int textureId, int minDim, bool allowWad) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int oldWidth = tex->nWidth;
	int oldHeight = tex->nHeight;
	int newWidth = tex->nWidth;
	int newHeight = tex->nHeight;

	float scale = get_scale_to_fix_bad_extents(textureId);

	if (scale == 1.0f) {
		return false;
	}

	get_scaled_texture_dimensions(textureId, scale, newWidth, newHeight);

	if (max(newWidth, newHeight) < minDim) {
		return false;
	}

	if (oldWidth == newWidth && oldHeight == newHeight) {
		logf("Failed to downscale texture %s %dx%d\n", tex->szName, oldWidth, oldHeight);
		return false;
	}

	if (tex->nOffsets[0] == 0) {
		if (allowWad) {
			tex->nWidth = newWidth;
			tex->nHeight = newHeight;
			adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
			logf("Texture coords were updated for %s. The WAD texture must be updated separately.\n", tex->szName);
		}
		else {
			logf("Can't downscale WAD texture %s\n", tex->szName);
		}

		return false;
	}

	return downscale_texture(textureId, newWidth, newHeight, KernelTypeLanczos3);
}

string Bsp::get_texture_source(string texname, vector<Wad*>& wads) {
	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			return name + ".bsp";
		}

		if (tex->nOffsets[0] != 0 && !strcasecmp(tex->szName, texname.c_str())) {
			return name + ".bsp";
		}
	}

	string src;

	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(texname.c_str())) {
			src = wads[k]->getName();
			break;
		}
	}

	return src;
}

void Bsp::remove_unused_wads(vector<Wad*>& wads) {
	vector<string> wadNames = get_wad_names();
	unordered_set<Wad*> used_wads;

	int missing_textures = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nOffsets[0] == 0) {
			bool foundTexture = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex->szName)) {
					used_wads.insert(wads[k]);
					foundTexture = true;
					break;
				}
			}
			if (!foundTexture) {
				missing_textures++;
			}
		}
	}

	string newWadList = "";

	for (Wad* wad : wads) {
		if (!used_wads.count(wad)) {
			logf("Removed unused WAD: %s\n", wad->getName().c_str());
		}
		else {
			newWadList += toLowerCase(wad->getName()) + ";";
		}
	}

	logf("Kept %d of %d wads.\n", used_wads.size(), wadNames.size());

	if (missing_textures && wadNames.size() != wads.size()) {
		logf("Warning: The map has missing textures. Missing WADs were removed which may actually be required.\n");
	}

	int worldspawn_count = 0;
	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == "worldspawn") {
			ents[i]->setOrAddKeyvalue("wad", newWadList);
			break;
		}
	}
}

// returns data for all embedded textures, ready to be wrtten to a WAD
vector<WADTEX> Bsp::get_embedded_textures() {
	vector<WADTEX> wadTextures;

	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (tex->nOffsets[0] == 0) {
			continue; // not embedded
		}

		WADTEX copy;
		memcpy(&copy, tex, sizeof(BSPMIPTEX)); // copy name, offset, dimenions
		int dataSz = copy.getDataSize();
		copy.data = new byte[dataSz];
		memcpy(copy.data, (byte*)tex + tex->nOffsets[0], dataSz);

		wadTextures.push_back(copy);
	}

	return wadTextures;
}

int Bsp::get_texture_id(string name) {
	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (!strcasecmp(tex->szName, name.c_str())) {
			return i;
		}
	}

	return -1;
}

vector<string> Bsp::get_wad_names() {
	vector<string> wadNames;

	for (int i = 0; i < ents.size(); i++) {
		if (ents[i]->getClassname() == "worldspawn") {
			wadNames = splitString(ents[i]->getKeyvalue("wad"), ";");

			for (int k = 0; k < wadNames.size(); k++) {
				wadNames[k] = basename(wadNames[k]);
			}
			break;
		}
	}

	return wadNames;
}

bool Bsp::embed_texture(int textureId, vector<Wad*>& wads) {
	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return false;
	}

	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	if (tex->nOffsets[0] != 0) {
		logf("Texture %s is already embedded\n", tex->szName);
		return false;
	}

	bool embedded = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex->szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex->szName);

			if (tex->nHeight != wadTex->nHeight || tex->nWidth != wadTex->nWidth) {
				errorf("Failed to embed texture %s from wad %s (dimensions don't match)\n", tex->szName, wads[k]->filename.c_str());
				delete wadTex;
				continue;
			}

			for (int i = 0; i < 4; i++) {
				tex->nOffsets[i] = wadTex->nOffsets[i];
			}

			int sz = tex->nWidth * tex->nHeight;	   // miptex 0
			int sz2 = sz / 4;  // miptex 1
			int sz3 = sz2 / 4; // miptex 2
			int sz4 = sz3 / 4; // miptex 3
			int texDataSz = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
			int startOffset = texOffset + sizeof(BSPMIPTEX);
			int oldRemainder = header.lump[LUMP_TEXTURES].nLength - startOffset;

			for (int i = 0; i < textureCount; i++) {
				int32_t& offset = ((int32_t*)textures)[i + 1];

				if (offset >= startOffset) {
					offset += texDataSz;
				}
			}

			byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength + texDataSz];
			memcpy(newTexData, lumps[LUMP_TEXTURES], startOffset);
			memcpy(newTexData + startOffset, wadTex->data, texDataSz);
			memcpy(newTexData + startOffset + texDataSz, lumps[LUMP_TEXTURES] + startOffset, oldRemainder);

			logf("Embedded texture %s from wad %s\n", tex->szName, wads[k]->filename.c_str());

			delete wadTex;
			delete[] lumps[LUMP_TEXTURES];
			lumps[LUMP_TEXTURES] = newTexData;
			header.lump[LUMP_TEXTURES].nLength += texDataSz;
			update_lump_pointers();
			embedded = true;

			break;
		}
	}

	if (!embedded) {
		errorf("Failed to embed %s. Texture not found in any loaded WAD.\n", tex->szName);
	}

	return embedded;
}

int Bsp::unembed_texture(int textureId, vector<Wad*>& wads, bool force, bool quiet) {
	int32_t texOffset = ((int32_t*)textures)[textureId + 1];

	BSPMIPTEX* tex = get_texture(textureId);
	if (!tex) {
		return 0;
	}

	if (tex->nOffsets[0] == 0) {
		logf("Texture %s is already unembedded\n", tex->szName);
		return 0;
	}

	int sz = tex->nWidth * tex->nHeight;	   // miptex 0
	int sz2 = sz / 4;  // miptex 1
	int sz3 = sz2 / 4; // miptex 2
	int sz4 = sz3 / 4; // miptex 3
	int texDataSz = sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
	int endOffset = texOffset + sizeof(BSPMIPTEX);
	int newTexBufferSz = header.lump[LUMP_TEXTURES].nLength - texDataSz;

	// reset texture dimensions in case it was edited inside the BSP
	bool wasResized = false;
	bool isInWad = false;
	for (int k = 0; k < wads.size(); k++) {
		if (wads[k]->hasTexture(tex->szName)) {
			WADTEX* wadTex = wads[k]->readTexture(tex->szName);

			if (tex->nWidth != wadTex->nWidth || tex->nHeight != wadTex->nHeight) {
				int oldWidth = tex->nWidth;
				int oldHeight = tex->nHeight;
				tex->nWidth = wadTex->nWidth;
				tex->nHeight = wadTex->nHeight;
				adjust_resized_texture_coordinates(textureId, oldWidth, oldHeight);
				wasResized = true;
			}

			isInWad = true;
			delete wadTex;
			break;
		}
	}
	if (!isInWad && !force) {
		logf("Aborted unembed of %s. No WAD contains this texture. Data would be lost.\n", tex->szName);
		return 0;
	}

	for (int i = 0; i < textureCount; i++) {
		int32_t& offset = ((int32_t*)textures)[i + 1];

		if (offset >= endOffset) {
			offset -= texDataSz;
		}
	}

	for (int i = 0; i < 4; i++) {
		tex->nOffsets[i] = 0;
	}

	byte* newTexData = new byte[newTexBufferSz];
	memcpy(newTexData, lumps[LUMP_TEXTURES], endOffset);
	memcpy(newTexData + endOffset, lumps[LUMP_TEXTURES] + endOffset + texDataSz, newTexBufferSz - endOffset);

	if (!quiet)
		logf("Unembedded texture %s\n", tex->szName);
	delete[] lumps[LUMP_TEXTURES];
	lumps[LUMP_TEXTURES] = newTexData;
	header.lump[LUMP_TEXTURES].nLength -= texDataSz;
	update_lump_pointers();

	return wasResized ? 2 : 1;
}

int Bsp::embed_all_textures() {
	vector<Wad*> wads = g_app->mapRenderer ? g_app->mapRenderer->wads : vector<Wad*>();

	int count = 0;
	int fail = 0;
	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex)
			continue;

		if (tex->nOffsets[0] != 0) {
			continue;
		}

		if (embed_texture(i, wads)) {
			count++;
		}
		else {
			fail++;
		}
	}

	return count;
}

int Bsp::add_texture_from_wad(WADTEX* tex) {
	((int32_t*)textures)[0]++;

	for (int i = 0; i < textureCount; i++) {
		int32_t& offset = ((int32_t*)textures)[i + 1];
		offset += sizeof(int32_t); // shift after the new header int
	}

	BSPMIPTEX newTex;
	memset(&newTex, 0, sizeof(BSPMIPTEX));
	memcpy(newTex.szName, tex->szName, 16);
	newTex.szName[15] = 0;
	newTex.nOffsets[0] = 0;
	newTex.nOffsets[1] = 0;
	newTex.nOffsets[2] = 0;
	newTex.nOffsets[3] = 0;
	newTex.nWidth = tex->nWidth;
	newTex.nHeight = tex->nHeight;

	int addedSz = sizeof(BSPMIPTEX) + sizeof(int32_t);
	byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength + addedSz];
	byte* srcData = lumps[LUMP_TEXTURES];
	byte* dstData = newTexData;

	int headerCopySz = sizeof(int32_t) * (textureCount + 1);
	memcpy(dstData, srcData, headerCopySz);
	dstData += headerCopySz;
	srcData += headerCopySz;

	int32_t newOffset = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t);
	memcpy(dstData, &newOffset, sizeof(int32_t));
	dstData += sizeof(int32_t);

	int oldDataLeft = header.lump[LUMP_TEXTURES].nLength - (srcData - lumps[LUMP_TEXTURES]);
	memcpy(dstData, srcData, oldDataLeft);
	dstData += oldDataLeft;

	memcpy(dstData, &newTex, sizeof(BSPMIPTEX));

	delete[] lumps[LUMP_TEXTURES];
	lumps[LUMP_TEXTURES] = newTexData;
	header.lump[LUMP_TEXTURES].nLength += addedSz;
	update_lump_pointers();

	return textureCount - 1;
}

WADTEX Bsp::load_texture(int textureIdx) {
	WADTEX out;
	memset(&out, 0, sizeof(WADTEX));

	if (textureIdx < 0 || textureIdx >= textureCount) {
		return out;
	}

	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return out;
	}
	memcpy(&out, tex, sizeof(BSPMIPTEX));

	int sz = out.getDataSize();
	out.data = new byte[sz];

	if (tex->nOffsets[0] != 0) {
		// embedded texture
		memcpy(out.data, ((byte*)tex) + tex->nOffsets[0], sz);
	}
	else {
		// try loading from WAD
		static vector<Wad*> emptyWads;
		vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

		bool foundTex = false;

		for (int k = 0; k < wads.size(); k++) {
			if (wads[k]->hasTexture(tex->szName)) {

				WADTEX* wadtex = wads[k]->readTexture(tex->szName);

				if (!wadtex) {
					logf("Failed to read texture %s from WAD: %s\n", wads[k]->filename.c_str());
					continue;
				}

				if (wadtex->nHeight != out.nHeight || wadtex->nWidth != out.nWidth) {
					debugf("Not using texture %s from wad because dimensions don't match: %s\n",
						tex->szName, wads[k]->filename.c_str());
					delete wadtex;
					continue;
				}

				memcpy(&out, wadtex, sizeof(BSPMIPTEX));
				memcpy(out.data, wadtex->data, sz);
				delete wadtex;
				foundTex = true;
				break;
			}
		}

		if (!foundTex) {
			delete[] out.data;
			out.data = NULL;
		}
	}

	return out;
}

bool Bsp::replace_texture(int textureIdx, WADTEX& newtex) {
	BSPMIPTEX* tex = get_texture(textureIdx);
	if (!tex) {
		return false;
	}

	memcpy(newtex.szName, tex->szName, MAXTEXTURENAME);
	tex->szName[0] = 0;

	int newIdx = add_texture(newtex);

	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& tinfo = texinfos[i];
		if (tinfo.iMiptex == textureIdx) {
			tinfo.iMiptex = newIdx;
		}
	}

	tex = get_texture(textureIdx);
	if (tex && (newtex.nWidth != tex->nWidth || newtex.nHeight != tex->nHeight)) {
		adjust_resized_texture_coordinates(newIdx, tex->nWidth, tex->nHeight);
	}

	return true;
}

int Bsp::downscale_invalid_textures(vector<Wad*>& wads) {
	int count = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			embed_texture(i, wads);
		}
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			if (tex->nOffsets[0] == 0) {
				logf("Skipping WAD texture %s (failed to embed)\n", tex->szName);
				continue;
			}

			int oldWidth = tex->nWidth;
			int oldHeight = tex->nHeight;
			int newWidth = tex->nWidth;
			int newHeight = tex->nHeight;

			float ratio = oldHeight / (float)oldWidth;

			while (newWidth > 16) {
				newWidth -= 16;
				newHeight = newWidth * ratio;

				if (newHeight % 16 != 0) {
					continue;
				}

				if (newWidth * newHeight <= g_limits.max_texturepixels) {
					break;
				}
			}

			downscale_texture(i, newWidth, newHeight, KernelTypeLanczos3);
			count++;
		}
	}

	logf("Downscaled %d textures\n", count);

	return count;
}

bool Bsp::rename_texture(const char* oldName, const char* newName) {
	if (strlen(newName) > 16) {
		errorf("ERROR: New texture name longer than 15 characters (%d)\n", strlen(newName));
		return false;
	}

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			continue;
		}

		if (!strncmp(tex->szName, oldName, 16)) {
			strncpy(tex->szName, newName, 16);
			logf("Renamed texture '%s' -> '%s'\n", oldName, newName);
			return true;
		}
	}

	logf("No texture found with name '%s'\n", oldName);
	return false;
}

int Bsp::find_texture(const char* name) {
	if (!name || name[0] == '\0')
		return -1;

	for (int i = 0; i < textureCount; i++) {
		int32_t oldOffset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* oldTex = (BSPMIPTEX*)(textures + oldOffset);
		if (strcasecmp(name, oldTex->szName) == 0) {
			return i;
		}
	}

	return -1;
}

int Bsp::add_texture(const char* texname, byte* data, int width, int height) {
	if (width % 16 != 0 || height % 16 != 0) {
		logf("Texture %s dimensions are not divisible by 16.", texname);
		return -1;
	}
	if (width * height > g_limits.max_texturepixels) {
		logf("Texture %s is too big to add.", texname);
		return -1;
	}

	int existingIdx = find_texture(texname);

	if (existingIdx != -1)
	{
		debugf("A texture with the name %s already exists in this map.\n", texname);
		return existingIdx;
	}

	COLOR3 palette[256];
	memset(&palette, 0, sizeof(COLOR3) * 256);
	int colorCount = 0;

	// create pallete and full-rez mipmap
	byte* mip[MIPLEVELS];
	mip[0] = new byte[width * height];
	COLOR3* src = (COLOR3*)data;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int paletteIdx = -1;
			for (int k = 0; k < colorCount; k++) {
				if (*src == palette[k]) {
					paletteIdx = k;
					break;
				}
			}
			if (paletteIdx == -1) {
				if (colorCount >= 256) {
					logf("Too many colors");
					delete[] mip[0];
					return -1;
				}
				palette[colorCount] = *src;
				paletteIdx = colorCount;
				colorCount++;
			}

			mip[0][y * width + x] = paletteIdx;
			src++;
		}
	}

	int texDataSize = width * height + sizeof(COLOR3) * 256 + 4; // 4 = padding

	// generate mipmaps
	for (int i = 1; i < MIPLEVELS; i++) {
		int div = 1 << i;
		int mipWidth = width / div;
		int mipHeight = height / div;
		texDataSize += mipWidth * mipHeight;
		mip[i] = new byte[mipWidth * mipHeight];

		src = (COLOR3*)data;
		for (int y = 0; y < mipHeight; y++) {
			for (int x = 0; x < mipWidth; x++) {

				int paletteIdx = -1;
				for (int k = 0; k < colorCount; k++) {
					if (*src == palette[k]) {
						paletteIdx = k;
						break;
					}
				}

				mip[i][y * mipWidth + x] = paletteIdx;
				src += div;
			}
		}
	}

	int newTexLumpSize = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t) + sizeof(BSPMIPTEX) + texDataSize;
	byte* newTexData = new byte[newTexLumpSize];
	memset(newTexData, 0, sizeof(newTexLumpSize));

	// create new texture lump header
	int32_t* newLumpHeader = (int32_t*)newTexData;
	int32_t* oldLumpHeader = (int32_t*)lumps[LUMP_TEXTURES];
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++) {
		*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int32_t); // make room for the new offset
	}

	// copy old texture data
	int oldTexHeaderSize = (textureCount + 1) * sizeof(int32_t);
	int newTexHeaderSize = oldTexHeaderSize + sizeof(int32_t);
	int oldTexDatSize = header.lump[LUMP_TEXTURES].nLength - (textureCount + 1) * sizeof(int32_t);
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	int newTexOffset = newTexHeaderSize + oldTexDatSize;
	newLumpHeader[textureCount + 1] = newTexOffset;
	BSPMIPTEX* newMipTex = (BSPMIPTEX*)(newTexData + newTexOffset);
	newMipTex->nWidth = width;
	newMipTex->nHeight = height;
	strncpy(newMipTex->szName, texname, MAXTEXTURENAME);

	newMipTex->nOffsets[0] = sizeof(BSPMIPTEX);
	newMipTex->nOffsets[1] = newMipTex->nOffsets[0] + width * height;
	newMipTex->nOffsets[2] = newMipTex->nOffsets[1] + (width >> 1) * (height >> 1);
	newMipTex->nOffsets[3] = newMipTex->nOffsets[2] + (width >> 2) * (height >> 2);
	int palleteOffset = newMipTex->nOffsets[3] + (width >> 3) * (height >> 3) + 2;

	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[0], mip[0], width * height);
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[1], mip[1], (width >> 1) * (height >> 1));
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[2], mip[2], (width >> 2) * (height >> 2));
	memcpy(newTexData + newTexOffset + newMipTex->nOffsets[3], mip[3], (width >> 3) * (height >> 3));
	memcpy(newTexData + newTexOffset + palleteOffset, palette, sizeof(COLOR3) * 256);
	uint16_t* colorCountPtr = (uint16_t*)(newTexData + newTexOffset + palleteOffset - 2);
	*colorCountPtr = 256; // required for hlrad

	for (int i = 0; i < MIPLEVELS; i++) {
		delete[] mip[i];
	}

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);

	return textureCount - 1;
}

int Bsp::add_texture(WADTEX texture) {
	int existingIdx = find_texture(texture.szName);

	if (existingIdx != -1)
	{
		debugf("A texture with the name %s already exists in this map.\n", texture.szName);
		return existingIdx;
	}

	int newTexLumpSize = header.lump[LUMP_TEXTURES].nLength + sizeof(int32_t) + sizeof(BSPMIPTEX) + texture.getDataSize();
	byte* newTexData = new byte[newTexLumpSize];
	memset(newTexData, 0, sizeof(newTexLumpSize));

	// create new texture lump header
	int32_t* newLumpHeader = (int32_t*)newTexData;
	int32_t* oldLumpHeader = (int32_t*)lumps[LUMP_TEXTURES];
	*newLumpHeader = textureCount + 1;

	for (int i = 0; i < textureCount; i++) {
		*(newLumpHeader + i + 1) = *(oldLumpHeader + i + 1) + sizeof(int32_t); // make room for the new offset
	}

	// copy old texture data
	int oldTexHeaderSize = (textureCount + 1) * sizeof(int32_t);
	int newTexHeaderSize = oldTexHeaderSize + sizeof(int32_t);
	int oldTexDatSize = header.lump[LUMP_TEXTURES].nLength - (textureCount + 1) * sizeof(int32_t);
	memcpy(newTexData + newTexHeaderSize, lumps[LUMP_TEXTURES] + oldTexHeaderSize, oldTexDatSize);

	// add new texture to the end of the lump
	int newTexOffset = newTexHeaderSize + oldTexDatSize;
	newLumpHeader[textureCount + 1] = newTexOffset;

	memcpy(newTexData + newTexOffset, (BSPMIPTEX*)&texture, sizeof(BSPMIPTEX));
	memcpy(newTexData + newTexOffset + sizeof(BSPMIPTEX), texture.data, texture.getDataSize());

	replace_lump(LUMP_TEXTURES, newTexData, newTexLumpSize);

	logf("Embedded new texture: %s (%dx%d)\n", texture.szName, texture.nWidth, texture.nHeight);

	return textureCount - 1;
}

bool Bsp::is_embedded_rad_texture_name(const char* name) {
	if (strlen(name) > 5) {
		char c = name[0];
		bool hasRadPrefix = strstr(name, "_rad") == name + 1 && (c == '_' || c == '{' || c == '!');
		if (hasRadPrefix && name[5] >= '0' && name[5] <= '9') {
			return true;
		}
	}

	return false;
}

BSPTEXTUREINFO* Bsp::get_embedded_rad_texinfo(BSPTEXTUREINFO& info) {
	if (info.iMiptex >= textureCount) {
		return NULL;
	}

	BSPMIPTEX* tex = get_texture(info.iMiptex);
	if (!tex) {
		return NULL;
	}

	/*
	* -------------------------------------------
	* The VHLT Embedded rad texture naming format
	* -------------------------------------------
	*
	* Example name:    __rad03319Jxi03
	* Name components: __rad 03319 Jxi 03
	*
	* The components are:
	*	- A prefix, which is one of:
	*		__rad
	*		!_rad     (this is commented out in VHLT, but SCHLT may enable it soon for func_water)
	*		{_rad
	*	- The original texinfo index as a decimal value.
	*	- A hash string computed from the texture index and size of the texture.
	*	- A count value representing the face index in the model modulo'd by 62.
	*
	* Only the original texinfo index is read when running hlrad again. The hash and count is there only
	* to make the name unique. So, to keep this in sync with bspguy, rewrite the texinfo if it ever
	* changes, and leave the rest alone. The original map can be used to load lost texinfos.
	*/

	if (is_embedded_rad_texture_name(tex->szName)) {
		int offset = atoi(&tex->szName[5]);
		if (offset >= 0 && offset < texinfoCount) {
			return &texinfos[offset];
		}
	}

	return NULL;
}

BSPTEXTUREINFO* Bsp::get_embedded_rad_texinfo(const char* texName) {
	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& info = texinfos[i];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (tex && !strncmp(texName, tex->szName, MAXTEXTURENAME)) {
			return get_embedded_rad_texinfo(info);
		}
	}

	int hashOffset = strlen("__rad12345");
	const char* searchhash = texName + hashOffset;
	if (strlen(texName) < 12)
		return NULL;

	// no texture with that exact name exists, probably because bspguy changed the texinfo part
	// try finding a match on the last part of the texture name which should be unique per texture
	int matchOffset = 0;
	int matchIdx = -1;

	for (int i = 0; i < texinfoCount; i++) {
		BSPTEXTUREINFO& info = texinfos[i];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (!is_embedded_rad_texture_name(tex->szName) || strlen(texName) < 12) {
			continue;
		}

		const char* hashpart = tex->szName + strlen("__rad12345");

		if (tex && !strncmp(searchhash, hashpart, MAXTEXTURENAME - hashOffset)) {
			int offset = atoi(&tex->szName[5]);

			if (matchIdx == -1 || offset == matchOffset) {
				matchOffset = offset;
				matchIdx = i;
			}
			else {
				matchIdx = -1;
				break;
			}
		}
	}

	if (matchIdx != 1) {
		return get_embedded_rad_texinfo(texinfos[matchIdx]);
	}

	return NULL;
}

int Bsp::count_missing_textures() {
	static vector<Wad*> emptyWads;
	vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;

	int missing_textures = 0;

	for (int i = 0; i < textureCount; i++) {
		BSPMIPTEX* tex = get_texture(i);
		if (!tex) {
			missing_textures++;
			continue;
		}

		if (tex->nOffsets[0] == 0) {
			bool foundTexture = false;
			for (int k = 0; k < wads.size(); k++) {
				if (wads[k]->hasTexture(tex->szName)) {
					foundTexture = true;
					break;
				}
			}
			if (!foundTexture) {
				missing_textures++;
			}
		}

		if (tex->nWidth * tex->nHeight > g_limits.max_texturepixels) {
			logf("Texture '%s' too large (%dx%d)\n", tex->szName, tex->nWidth, tex->nHeight);
		}
	}

	return missing_textures;
}

void Bsp::generate_wa_file() {
	int numMissing = 0;

	vector<WADTEX> wadTextures;
	for (int i = 0; i < textureCount; i++) {
		int32_t offset = ((int32_t*)textures)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

		if (tex->nOffsets[0] != 0) {
			continue; // embedded
		}

		WADTEX copy = load_texture(i);

		if (copy.data)
			wadTextures.push_back(copy);
		else
			numMissing++;
	}

	string fname = path;
	replaceAll(fname, ".bsp", ".wa_");

	Wad outWad = Wad();
	outWad.write(fname, &wadTextures[0], wadTextures.size());
	logf("Wrote %d WAD textures to: %s\n", wadTextures.size(), fname.c_str());

	for (WADTEX& tex : wadTextures) {
		delete[] tex.data;
	}
}

BSPMIPTEX* Bsp::get_texture(int iMiptex) {
	if (iMiptex < textureCount) {
		int32_t texOffset = ((int32_t*)textures)[iMiptex + 1];
		if (texOffset + sizeof(BSPMIPTEX) <= header.lump[LUMP_TEXTURES].nLength && texOffset > 0) {
			return ((BSPMIPTEX*)(textures + texOffset));
		}
	}

	return NULL;
}

int Bsp::delete_embedded_rad_textures(Bsp* originalMap) {

	// first check that the original texture references are valid
	int numBadRadTexture = 0;
	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		if (!tex)
			continue;

		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(info);

		if (!radinfo)
			continue;

		if (originalMap) {
			radinfo = originalMap->get_embedded_rad_texinfo(tex->szName);

			if (!radinfo) {
				numBadRadTexture++;
				continue;
			}
		}

		BSPPLANE& plane = planes[face.iPlane];
		vec3 faceNormal = plane.vNormal * (face.nPlaneSide ? -1 : 1);
		vec3 texnormal = crossProduct(radinfo->vT, radinfo->vS).normalize();
		float distscale = dotProduct(texnormal, faceNormal);

		if (distscale == 0) {
			debugf("Invalid RAD texture axes in %s\n", tex->szName);
			numBadRadTexture++;
		}
	}

	if (numBadRadTexture > 0) {
		return -1;
	}

	int numRemoved = 0;

	for (int i = 0; i < faceCount; i++) {
		BSPFACE& face = faces[i];
		BSPTEXTUREINFO& info = texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = get_texture(info.iMiptex);

		BSPTEXTUREINFO* radinfo = get_embedded_rad_texinfo(info);

		if (!radinfo) {
			continue;
		}

		numRemoved++;

		if (originalMap) {
			radinfo = originalMap->get_embedded_rad_texinfo(tex->szName);

			if (!radinfo) {
				continue;
			}

			BSPMIPTEX* ogtex = originalMap->get_texture(radinfo->iMiptex);
			if (!ogtex) {
				continue;
			}

			info = *radinfo;
			info.iMiptex = 0;

			bool foundTexture = false;
			for (int k = 0; k < textureCount; k++) {
				BSPMIPTEX* testTex = get_texture(k);
				if (!memcmp(ogtex, testTex, sizeof(BSPMIPTEX))) {
					debugf("Found existing texture referenced by other map: %s\n", ogtex->szName);
					info.iMiptex = k;
					foundTexture = true;
					break;
				}
			}

			if (!foundTexture) {
				if (ogtex->nOffsets[0] == 0) {
					// texture loads from a WAD, just add the reference
					WADTEX* tex = new WADTEX;
					memcpy(tex, ogtex, sizeof(BSPMIPTEX));
					info.iMiptex = add_texture_from_wad(tex);
					delete tex;
					logf("Added texture reference for %s\n", ogtex->szName);
				}
				else {
					WADTEX tex = originalMap->load_texture(radinfo->iMiptex);
					info.iMiptex = add_texture(tex);
					if (tex.data)
						delete tex.data;
					logf("Copied embedded texture %s\n", ogtex->szName);
				}
			}
		}
		else {
			info = *radinfo;
		}
	}

	if (numRemoved)
		remove_unused_model_structures(false).print_delete_stats(1);

	return numRemoved;
}

void Bsp::convert_texture_palettes(LumpState& state, bool quakeNotHl) {
	if (quakeNotHl) {
		// convert from per-texture palette to global palette
		if (state.lumps[LUMP_TEXTURES]) {
			uint8_t* texLump = state.lumps[LUMP_TEXTURES];
			uint32_t numTex = *(uint32_t*)texLump;

			uint32_t* srcIndexPtr = (uint32_t*)(texLump + sizeof(uint32_t));

			// calc new lump size
			const int palDataSz = sizeof(COLOR3) * 256 + 4;
			int newLumpSz = (sizeof(uint32_t) + sizeof(BSPMIPTEX)) * numTex + sizeof(uint32_t);
			for (int i = 0; i < numTex; i++) {
				uint32_t srcOffset = srcIndexPtr[i];
				BSPMIPTEX* src = (BSPMIPTEX*)(texLump + srcOffset);
				newLumpSz += src->pixelDataSize();
			}
			uint8_t* newTexLump = new uint8_t[newLumpSz];

			*((uint32_t*)newTexLump) = numTex;

			uint32_t* indexPtr = (uint32_t*)(newTexLump + sizeof(uint32_t));
			uint8_t* writePtr = newTexLump + sizeof(uint32_t) * (numTex + 1);

			// cache mapping from RGB to closest q1 color
			uint8_t* hl_to_q1 = new uint8_t[256 * 256 * 256];
			bool* hl_to_q1_cached = new bool[256 * 256 * 256];
			memset(hl_to_q1_cached, 0, 256 * 256 * 256);

			for (int i = 0; i < numTex; i++) {
				uint32_t srcOffset = srcIndexPtr[i];
				BSPMIPTEX* src = (BSPMIPTEX*)(texLump + srcOffset);

				*indexPtr++ = writePtr - newTexLump;

				memcpy(writePtr, src, sizeof(BSPMIPTEX));
				int pixelCount = src->pixelDataSize();
				BSPMIPTEX* dst = (BSPMIPTEX*)writePtr;
				int sz = dst->nWidth * dst->nHeight;
				int sz2 = sz / 4;  // miptex 1
				int sz3 = sz2 / 4; // miptex 2
				dst->nOffsets[0] = sizeof(BSPMIPTEX);
				dst->nOffsets[1] = sizeof(BSPMIPTEX) + sz;
				dst->nOffsets[2] = sizeof(BSPMIPTEX) + sz + sz2;
				dst->nOffsets[3] = sizeof(BSPMIPTEX) + sz + sz2 + sz3;

				writePtr += sizeof(BSPMIPTEX);

				WADTEX tex = load_texture(i);
				if (tex.data != NULL) {
					COLOR3* srcPal = (COLOR3*)(tex.data + pixelCount + 2);
					uint8_t* srcPixel = tex.data;

					for (int k = 0; k < pixelCount; k++) {
						uint8_t pidx = *srcPixel++;
						if (pidx == 255 && tex.szName[0] == '{') {
							*writePtr++ = 255; // transparent color
						}
						else {
							COLOR3& c = srcPal[pidx];
							uint32_t hash = (c.r << 16) | (c.g << 8) | c.b;
							if (!hl_to_q1_cached[hash]) {
								hl_to_q1[hash] = closest_q1_color(c);
								hl_to_q1_cached[hash] = true;
							}
							*writePtr++ = hl_to_q1[hash];
						}
					}

					delete[] tex.data;
				}
				else {
					errorf("Could not embed missing texture: %s\n", src->szName);
					memset(writePtr, 0, pixelCount);
				}
			}

			delete[] hl_to_q1;
			delete[] hl_to_q1_cached;
			delete[] state.lumps[LUMP_TEXTURES];
			state.lumps[LUMP_TEXTURES] = newTexLump;
			state.lumpLen[LUMP_TEXTURES] = newLumpSz;
		}
	}
	else {
		// convert from global palette to per-texture palettes
		if (state.lumps[LUMP_TEXTURES]) {
			uint8_t* texLump = state.lumps[LUMP_TEXTURES];
			uint32_t numTex = *(uint32_t*)texLump;

			// includes padding and color count
			const int palDataSz = sizeof(COLOR3) * 256 + 4;
			uint8_t* palData = new uint8_t[palDataSz];
			memcpy(palData + 2, g_quake_pal, sizeof(COLOR3) * 256);
			*((uint16_t*)palData) = 256;
			*((uint16_t*)(palData + palDataSz - 2)) = 0; // padding

			int newLumpSz = state.lumpLen[LUMP_TEXTURES] + numTex * palDataSz;
			uint8_t* newTexLump = new uint8_t[newLumpSz];

			*((uint32_t*)newTexLump) = numTex;

			uint32_t* srcIndexPtr = (uint32_t*)(texLump + sizeof(uint32_t));
			uint32_t* indexPtr = (uint32_t*)(newTexLump + sizeof(uint32_t));
			uint8_t* writePtr = newTexLump + sizeof(uint32_t) * (numTex + 1);

			for (int i = 0; i < numTex; i++) {
				uint32_t srcOffset = srcIndexPtr[i];
				if (srcOffset == -1)
					continue;
				BSPMIPTEX* src = (BSPMIPTEX*)(texLump + srcOffset);

				*indexPtr++ = writePtr - newTexLump;

				int copySz = sizeof(BSPMIPTEX) + src->pixelDataSize();
				memcpy(writePtr, src, copySz);
				writePtr += copySz;

				memcpy(writePtr, palData, palDataSz);
				writePtr += palDataSz;
			}

			delete[] palData;
			delete[] state.lumps[LUMP_TEXTURES];
			state.lumps[LUMP_TEXTURES] = newTexLump;
			state.lumpLen[LUMP_TEXTURES] = newLumpSz;
		}
	}
}

int Bsp::remove_unused_textures(bool* usedTextures, int* remappedIndexes) {
	int oldTexCount = textureCount;

	int removeCount = 0;
	int removeSize = 0;
	for (int i = 0; i < oldTexCount; i++) {
		if (!usedTextures[i]) {
			int32_t offset = ((int32_t*)textures)[i + 1];
			BSPMIPTEX* tex = (BSPMIPTEX*)(textures + offset);

			// don't delete single frames from animated textures or else game crashes
			if (tex->szName[0] == '-' || tex->szName[0] == '+') {
				usedTextures[i] = true;
				// TODO: delete all frames if none are used
				continue;
			}

			if (offset == -1) {
				removeSize += sizeof(int32_t);
			}
			else {
				removeSize += getBspTextureSize(tex) + sizeof(int32_t);
			}
			removeCount++;
		}
	}

	int newTexCount = oldTexCount - removeCount;
	byte* newTexData = new byte[header.lump[LUMP_TEXTURES].nLength - removeSize];

	uint32_t* texHeader = (uint32_t*)newTexData;
	texHeader[0] = newTexCount;

	int32_t newOffset = (newTexCount + 1) * sizeof(int32_t);
	for (int i = 0, k = 0; i < oldTexCount; i++) {
		if (!usedTextures[i]) {
			continue;
		}
		int32_t oldOffset = ((int32_t*)textures)[i + 1];

		if (oldOffset == -1) {
			texHeader[k + 1] = -1;
		}
		else {
			BSPMIPTEX* tex = (BSPMIPTEX*)(textures + oldOffset);
			int sz = getBspTextureSize(tex);

			memcpy(newTexData + newOffset, textures + oldOffset, sz);
			texHeader[k + 1] = newOffset;
			newOffset += sz;
		}

		remappedIndexes[i] = k;
		k++;
	}

	replace_lump(LUMP_TEXTURES, newTexData, header.lump[LUMP_TEXTURES].nLength - removeSize);

	return removeCount;
}
