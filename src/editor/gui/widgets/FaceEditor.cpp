#include "Widget.h"
#include "Texture.h"
#include "bmp.h"
#include <lodepng.h>

void FaceEditor::draw() {
	if (app->mapRenderer == NULL || map == NULL || app->pickMode != PICK_FACE || app->pickInfo.faces.size() == 0)
	{
		ImGui::Text("No face selected");

		static char findtex[MAXTEXTURENAME];
		static char findfaceid[256];
		static char findtexid[256];

		ImGui::Dummy(ImVec2(0, 20 * uiScale));

		ImGui::Text("Texture Name");
		ImGui::InputText("##Texture", findtex, MAXTEXTURENAME);
		ImGui::SameLine();
		if (ImGui::Button("Find##texture")) {
			int numSelect = 0;

			for (int i = 0; i < map->faceCount; i++) {
				BSPFACE& face = map->faces[i];
				BSPTEXTUREINFO& tinfo = map->texinfos[face.iTextureInfo];
				BSPMIPTEX* tex = map->get_texture(tinfo.iMiptex);

				if ((tex && !strcasecmp(findtex, tex->szName)) || (!tex && findtex[0] == 0)) {
					app->pickInfo.selectFace(i);
					numSelect++;
				}
			}

			logf("Selected %d faces with texture '%s'\n", numSelect, findtex);
			g_app->mapRenderer->highlightPickedFaces(true);
			g_app->updateTextureAxes();
		}
		tooltip("Select all faces that use the given texture");

		ImGui::Dummy(ImVec2(0, 20 * uiScale));

		ImGui::Text("Texture ID");
		ImGui::InputText("##TextureId", findtexid, 256);
		ImGui::SameLine();
		if (ImGui::Button("Find##textureidbut")) {
			int numSelect = 0;
			int texid = atoi(findtexid);

			for (int i = 0; i < map->faceCount; i++) {
				BSPFACE& face = map->faces[i];
				BSPTEXTUREINFO& tinfo = map->texinfos[face.iTextureInfo];

				if (tinfo.iMiptex == texid) {
					app->pickInfo.selectFace(i);
					numSelect++;
				}
			}

			logf("Selected %d faces with texture ID %d\n", numSelect, texid);
			g_app->mapRenderer->highlightPickedFaces(true);
			g_app->updateTextureAxes();
		}
		tooltip("Select all faces that use the given texture ID.");

		ImGui::Dummy(ImVec2(0, 20 * uiScale));

		ImGui::Text("Face ID");
		ImGui::InputText("##Face ID", findfaceid, 256);
		ImGui::SameLine();
		if (ImGui::Button("Find##faceid")) {
			int faceid = atoi(findfaceid);

			if (faceid >= 0 && faceid < map->faceCount) {
				app->pickInfo.selectFace(faceid);
				g_app->mapRenderer->highlightPickedFaces(true);
				g_app->updateTextureAxes();
				logf("Selected face %d\n", faceid);
			}
			else {
				logf("Invalid face ID %d (max is %d)\n", faceid, map->faceCount);
			}
		}
		tooltip("Select the face with the given ID. Useful for toubleshooting errors in the engine or compilers.");
	}
	else {
		if (ImGui::BeginTabBar("##face-tabs"))
		{
			static int currentTab = 0;

			if (ImGui::BeginTabItem("Texture")) {
				currentTab = 0;
				drawTextureEditor();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Lightmaps")) {
				if (currentTab != 1)
					gui->lightmapEditorNeedsUpdate = true;
				currentTab = 1;
				drawLightmapsEditor();
				ImGui::EndTabItem();
			}
		}

		ImGui::EndTabBar();
	}
}

void FaceEditor::drawTextureEditor() {
	ImGuiContext& g = *GImGui;

	static uint16_t resizeWidth = 0;
	static uint16_t resizeHeight = 0;
	static uint16_t resizeOriginalWidth = 0;
	static uint16_t resizeOriginalHeight = 0;
	static int resizeTextureIdx = 0;
	static bool resizeMasked = false;
	static COLOR3 resizeMaskColor;

	//ImGui::SetNextWindowSize(ImVec2(400, 600));

	static float scaleX, scaleY, shiftX, shiftY, rotate;
	static bool isSpecial;
	static int width, height;
	static ImTextureID textureId = NULL; // OpenGL ID
	static Texture* buttonTexture;
	static int lastTextureIdx;
	static char textureName[16];
	static int lastPickCount = -1;
	static bool validTexture = true;
	static bool isEmbedded = false;
	static string texture_src;
	static string last_texture_name;
	static int tex_size_kb;
	BspRenderer* mapRenderer = app->mapRenderer ? app->mapRenderer : NULL;
	Bsp* map = app->pickInfo.getMap();
	vector<Wad*> wads = g_app->mapRenderer ? g_app->mapRenderer->wads : vector<Wad*>();
	static FacesEditCommand* faceUndoCommand = NULL;

	if (lastPickCount != app->pickCount && app->pickMode == PICK_FACE) {
		if (app->pickInfo.faces.size() && mapRenderer != NULL) {
			int faceIdx = app->pickInfo.faces[0];
			Bsp* map = app->pickInfo.getMap();
			BSPFACE& face = map->faces[faceIdx];
			BSPPLANE& plane = map->planes[face.iPlane];
			BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
			BSPMIPTEX* tex = map->get_texture(texinfo.iMiptex);

			width = height = 0;
			if (tex) {
				width = tex->nWidth;
				height = tex->nHeight;
				strncpy(textureName, tex->szName, MAXTEXTURENAME);
				isEmbedded = tex->nOffsets[0] != 0;

				int w = tex->nWidth;
				int h = tex->nHeight;
				int sz = w * h;	   // miptex 0
				int sz2 = sz / 4;  // miptex 1
				int sz3 = sz2 / 4; // miptex 2
				int sz4 = sz3 / 4; // miptex 3
				int szAll = sizeof(BSPMIPTEX) + sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
				tex_size_kb = (szAll + 512) / 1024;
			}
			else {
				textureName[0] = 0;
			}

			if (textureName != last_texture_name) {
				last_texture_name = textureName;
				texture_src = map->get_texture_source(textureName, wads);
				// TODO: this is slow. cache loaded wads
			}

			int miptex = texinfo.iMiptex;

			scaleX = 1.0f / texinfo.vS.length();
			scaleY = 1.0f / texinfo.vT.length();
			shiftX = texinfo.shiftS;
			shiftY = texinfo.shiftT;
			isSpecial = texinfo.nFlags & TEX_SPECIAL;

			{
				vec3 ref = map->get_face_ut_reference(faceIdx);
				vec3 utNorm = crossProduct(texinfo.vS, texinfo.vT).normalize();
				rotate = signedAngle(texinfo.vS, ref, utNorm);
			}

			if (lastTextureIdx != texinfo.iMiptex) {
				lastTextureIdx = texinfo.iMiptex;
				delete buttonTexture;
				buttonTexture = mapRenderer->getRgbTexture(texinfo.iMiptex);
				textureId = buttonTexture->id;
			}

			validTexture = true;

			// show default values if not all faces share the same values
			for (int i = 1; i < app->pickInfo.faces.size(); i++) {
				int faceIdx2 = app->pickInfo.faces[i];
				BSPFACE& face2 = map->faces[faceIdx2];
				BSPTEXTUREINFO& texinfo2 = map->texinfos[face2.iTextureInfo];

				if (scaleX != 1.0f / texinfo2.vS.length()) scaleX = 1.0f;
				if (scaleY != 1.0f / texinfo2.vT.length()) scaleY = 1.0f;
				if (shiftX != texinfo2.shiftS) shiftX = 0;
				if (shiftY != texinfo2.shiftT) shiftY = 0;
				if (isSpecial != texinfo2.nFlags & TEX_SPECIAL) isSpecial = false;
				if (texinfo2.iMiptex != miptex) {
					validTexture = false;
					textureId = NULL;
					width = 0;
					height = 0;
					textureName[0] = 0;
				}
			}
		}
		else {
			scaleX = scaleY = shiftX = shiftY = width = height = 0;
			textureId = NULL;
			textureName[0] = 0;
		}

		checkFaceErrors();
	}
	lastPickCount = app->pickCount;

	ImGuiStyle& style = ImGui::GetStyle();
	float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
	float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;

	bool scaledX = false;
	bool scaledY = false;
	bool shiftedX = false;
	bool shiftedY = false;
	bool rotated = false;
	bool textureChanged = false;
	bool toggledFlags = false;

	bool userStoppedEditing = false; // user stopped dragging or finished typing an input

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(roundf(2.0f * uiScale), roundf(2.0f * uiScale)));
	if (ImGui::BeginTable("TransformTexTable", 4, ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 60 * uiScale);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed, 5 * uiScale);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::Text("Shift");
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##shiftx", &shiftX, 0.1f, 0, 0, "X: %.2f")) { shiftedX = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			userStoppedEditing = true;
		}
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##shifty", &shiftY, 0.1f, 0, 0, "Y: %.2f")) { shiftedY = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) { userStoppedEditing = true; }
		ImGui::TableNextColumn();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::Text("Rotate");
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##rot", &rotate, 0.1f, 0, 0, "%.2f")) {
			rotated = true;
			if (rotate > 0) {
				rotate = normalizeRangef(rotate, 0, 360);
			}
			else if (rotate < 0) {
				rotate = normalizeRangef(rotate, -360, 0);
			}
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) { userStoppedEditing = true; }
		ImGui::TableNextColumn();
		ImGui::TableNextColumn();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		ImGui::Text("Scale");
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##scalex", &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0) { scaledX = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) { userStoppedEditing = true; }
		ImGui::TableNextColumn();

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::DragFloat("##scaley", &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0) { scaledY = true; }
		if (ImGui::IsItemDeactivatedAfterEdit()) { userStoppedEditing = true; }
		ImGui::TableNextColumn();

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	if (ImGui::Checkbox("Special", &isSpecial)) {
		toggledFlags = true;
		userStoppedEditing = true;
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted("Used with invisible faces to bypass the surface extent limit."
			"\nLightmaps may break in strange ways if this is used on a normal face.");
		ImGui::EndTooltip();
	}

	ImGui::SameLine(0, 20 * uiScale);

	if (g_app->isLoading) {
		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}


	if (ImGui::Checkbox("Embedded", &isEmbedded)) {
		LumpReplaceCommand* command = new LumpReplaceCommand(isEmbedded ? "Unembed texture" : "Embed texture", true);

		unordered_set<int> mipsToEmbed;
		for (int i = 0; i < app->pickInfo.faces.size(); i++) {
			BSPFACE& face = map->faces[app->pickInfo.faces[i]];
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			mipsToEmbed.insert(info.iMiptex);
		}

		bool anySuccess = false;
		for (int mip : mipsToEmbed) {
			bool isActuallyEmbedded = false;

			if (mip > 0 && mip < map->textureCount) {
				BSPMIPTEX* tex = map->get_texture(mip);
				isActuallyEmbedded = tex && tex->nOffsets[0] != 0;
			}

			if (!isEmbedded && isActuallyEmbedded) {
				int ret = map->unembed_texture(mip, wads);
				if (ret > 0) {
					isEmbedded = false;
					anySuccess = true;
					if (ret == 2)
						app->mapRenderer->reloadTextures();
				}
				else
					isEmbedded = true;
			}
			else if (isEmbedded && !isActuallyEmbedded) {
				if (map->embed_texture(mip, wads)) {
					isEmbedded = true;
					anySuccess = true;
				}
				else {
					isEmbedded = false;
				}
			}
		}

		// refresh texture source
		app->pickCount++;
		last_texture_name = "";

		if (anySuccess) {
			command->pushUndoState();
		}
		else {
			delete command;
		}
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted("Embedded textures are stored in this BSP rather than a WAD."
			"\n\nEmbedding allows the texture to be downscaled, but inflates the size of the BSP."
			"\nUnembedding is disallowed if no loaded WAD has a texture by this name.\n");
		ImGui::EndTooltip();
	}
	if (g_app->isLoading) {
		ImGui::PopItemFlag();
		ImGui::PopStyleVar();
	}

	ImGui::Dummy(ImVec2(0, roundf(8 * uiScale)));

	float spaceLeft = ImGui::GetContentRegionAvail().x;
	float buttonWidth = ImGui::CalcTextSize("  512x512  ").x;
	spaceLeft -= buttonWidth;

	ImGui::Text("Texture name");
	ImGui::SetNextItemWidth(spaceLeft);
	if (!validTexture) {
		ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
	}
	ImGui::BeginDisabled(g_app->isLoading);
	if (ImGui::InputText("##texname", textureName, MAXTEXTURENAME)) {
		textureChanged = true;
	}
	if (ImGui::IsItemHovered() && !validTexture) {
		ImGui::SetTooltip("Texture not found in the BSP or any loaded WADs");
	}
	ImGui::EndDisabled();
	if (refreshAfterFacePaste) {
		textureChanged = true;
		refreshAfterFacePaste = false;
		BSPMIPTEX* tex = map->get_texture(gui->copiedMiptex);
		if (tex)
			strncpy(textureName, tex->szName, MAXTEXTURENAME);
	}
	if (!validTexture) {
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();

	ImGui::BeginDisabled(!isEmbedded || app->pickInfo.faces.empty());
	if (ImGui::Button((to_string(width) + "x" + to_string(height)).c_str())) {
		ImGui::OpenPopup("Resize Texture");

		int faceIdx = app->pickInfo.faces[0];
		BSPFACE& face = map->faces[faceIdx];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = map->get_texture(texinfo.iMiptex);
		if (tex) {
			int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];
			int lastMipSize = (tex->nWidth >> 3) * (tex->nHeight >> 3);
			byte* palette = (byte*)(map->textures + texOffset + tex->nOffsets[3] + lastMipSize);
			COLOR3* paletteColors = (COLOR3*)(palette + 2); // skip color count

			resizeWidth = resizeOriginalWidth = width;
			resizeHeight = resizeOriginalHeight = height;
			resizeTextureIdx = texinfo.iMiptex;
			resizeMasked = tex->szName[0] == '{';
			resizeMaskColor = paletteColors[255];
		}

	}
	ImGui::EndDisabled();

	if ((scaledX || scaledY || shiftedX || shiftedY || rotated || textureChanged || refreshAfterFacePaste || toggledFlags)) {
		if (!faceUndoCommand)
			faceUndoCommand = new FacesEditCommand("Edit Face");

		if (scaledX || scaledY) {
			gui->loadedLimit[SORT_EXTENTS] = false;
		}

		uint32_t newMiptex = 0;
		bool texturesNeedReload = false;
		if (textureChanged) {
			validTexture = false;

			int32_t totalTextures = ((int32_t*)map->textures)[0];

			for (uint i = 0; i < totalTextures; i++) {
				BSPMIPTEX* tex = map->get_texture(i);
				if (tex && !strcasecmp(tex->szName, textureName)) {
					validTexture = true;
					newMiptex = i;
					// force matching case of real texture reference
					strncpy(textureName, tex->szName, MAXTEXTURENAME);
					textureName[MAXTEXTURENAME - 1] = 0;
					break;
				}
			}

			if (!validTexture) {
				int addedMip = mapRenderer->addTextureToMap(textureName);
				if (addedMip != -1) {
					newMiptex = addedMip;
					validTexture = true;
					faceUndoCommand->textureDataReloadNeeded = true;
				}
			}

			if (validTexture) {
				userStoppedEditing = true;
			}

			app->pickCount++;
		}

		set<int> modelRefreshes;
		for (int i = 0; i < app->pickInfo.faces.size(); i++) {
			int faceIdx = app->pickInfo.faces[i];
			BSPFACE& face = map->faces[faceIdx];
			BSPPLANE& plane = map->planes[face.iPlane];
			BSPTEXTUREINFO& texinfo = *map->get_unique_texinfo(faceIdx);

			if (scaledX) {
				texinfo.vS = texinfo.vS.normalize(1.0f / scaleX);
			}
			if (scaledY) {
				texinfo.vT = texinfo.vT.normalize(1.0f / scaleY);
			}
			if (shiftedX) {
				texinfo.shiftS = shiftX;
			}
			if (shiftedY) {
				texinfo.shiftT = shiftY;
			}
			if (toggledFlags) {
				texinfo.nFlags = isSpecial ? TEX_SPECIAL : 0;
			}
			if ((textureChanged && validTexture) || toggledFlags) {
				if (textureChanged)
					texinfo.iMiptex = newMiptex;
				modelRefreshes.insert(map->get_model_from_face(faceIdx));
			}
			if (rotated) {
				vec3 ref = map->get_face_ut_reference(faceIdx);
				vec3 norm = crossProduct(texinfo.vT, texinfo.vS).normalize();

				// align texture axes to plane (world -> face mode)
				float dot = fabs(dotProduct(plane.vNormal, norm));
				if (dot < 0.99999f) {
					if (dot < 0.999f)
						logf("Texture realigned to face %d\n", faceIdx);
					norm = (plane.vNormal * (face.nPlaneSide ? -1 : 1)).normalize();
				}

				float sLen = texinfo.vS.length();
				float tLen = texinfo.vT.length();
				texinfo.vS = rotateAroundAxis(ref, norm, rotate * (PI / 180.0f)).normalize(sLen);
				texinfo.vT = crossProduct(texinfo.vS, norm).normalize(tLen);
			}
			mapRenderer->updateFaceUVs(faceIdx);
		}
		if (textureChanged || toggledFlags) {
			delete buttonTexture;
			buttonTexture = mapRenderer->getRgbTexture(lastTextureIdx);
			textureId = buttonTexture->id;

			for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++) {
				mapRenderer->refreshModel(*it, false);
			}
			g_app->mapRenderer->highlightPickedFaces(true);
		}

		checkFaceErrors();
		g_app->updateTextureAxes();
	}

	if (userStoppedEditing) {
		if (faceUndoCommand) {
			faceUndoCommand->pushUndoState(true);
			faceUndoCommand = NULL;
		}
	}

	refreshAfterFacePaste = false;

	float imgWidth = min(235.0f * uiScale, ImGui::GetContentRegionAvail().x - style.WindowPadding.x);
	ImVec2 imgSize = ImVec2(imgWidth, imgWidth);
	if (ImGui::ImageButton("texicon", textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1))) {
		logf("Texture browser not implemented.\n");
	}
	if (ImGui::BeginPopupContextItem()) {
		int mip = map->texinfos[map->faces[app->pickInfo.faces[0]].iTextureInfo].iMiptex;

		if (ImGui::MenuItem("Import")) {
			char* fname = tinyfd_openFileDialog("Import Texture", "", 2, imgFilterPatterns, "Image (*.bmp, *.png)", 1);

			if (fname) {
				string fpath = fname;
				int lastDot = fpath.find(".");
				string ext = "";
				if (lastDot != -1) {
					ext = toLowerCase(fpath.substr(lastDot + 1));
				}

				WADTEX tex;
				if (ext == "bmp") {
					tex = load8BitBMP(fname);
				}
				else if (ext == "png") {
					tex = loadTextureFromPng(fname);
				}
				else {
					logf("Invalid file type '%s'\n", ext.c_str());
				}

				if (tex.data) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Import Texture");

					if (map->replace_texture(mip, tex)) {
						map->remove_unused_model_structures().print_delete_stats(1);
						logf("Imported new texture data for %s\n", tex.szName);
						command->pushUndoState();
					}
					else {
						delete command;
					}
				}
			}
		}
		tooltip("Import a PNG/BMP file to replace this texture");

		if (ImGui::MenuItem("Export as BMP")) {
			char* fname = tinyfd_saveFileDialog("Export Texture", (string(textureName) + ".bmp").c_str(),
				1, bmpFilterPatterns, "BMP (*.bmp)");

			if (fname) {
				WADTEX tex = map->load_texture(mip);

				if (tex.data) {
					save8BitBMP(fname, tex);
					logf("Wrote %s\n", fname);
				}
				else {
					logf("failed to load texture %d\n", mip);
				}
			}
		}
		tooltip("Export this texture as an 8-bit BMP file.");

		if (ImGui::MenuItem("Export as PNG")) {
			char* fname = tinyfd_saveFileDialog("Export Texture", (string(textureName) + ".png").c_str(),
				1, pngFilterPatterns, "PNG (*.png)");

			if (fname) {
				WADTEX tex = map->load_texture(mip);

				if (tex.data) {
					COLOR3* pal = tex.getPalette();
					bool isMasked = textureName[0] == '{';
					COLOR3 maskColor = pal[255];
					uint8_t* srcData = tex.getMip(0);
					COLOR4* pngPixels = new COLOR4[tex.nWidth * tex.nHeight];
					for (int y = 0; y < tex.nHeight; y++) {
						for (int x = 0; x < tex.nWidth; x++) {
							COLOR3& src = pal[srcData[y * tex.nWidth + x]];
							COLOR4& dst = pngPixels[y * tex.nWidth + x];
							dst.r = src.r;
							dst.g = src.g;
							dst.b = src.b;
							dst.a = (isMasked && maskColor == src) ? 0 : 255;
						}
					}

					if (lodepng_encode32_file(fname, (uint8_t*)pngPixels, tex.nWidth, tex.nHeight)) {
						logf("Failed to save texture as PNG\n", fname);
					}
					else {
						logf("Wrote %s\n", fname);
					}
				}
				else {
					logf("failed to load texture %d\n", mip);
				}
			}
		}
		tooltip("Export this texture as a 32-bit PNG file.");

		ImGui::EndPopup();
	}

	ImGui::Text(("Source: " + texture_src).c_str());
	//ImGui::Text(("Size: " + to_string(tex_size_kb) + " KB").c_str());

	ImGuiIO& io = ImGui::GetIO();
	int bestWidth = app->windowWidth * 0.5f;
	ImGui::SetNextWindowSize(ImVec2(bestWidth, bestWidth * 0.8f), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(340 * uiScale, 140 * uiScale), ImVec2(FLT_MAX, app->windowHeight - 40 * uiScale));
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Resize Texture", NULL, 0))
	{
		Bsp* map = app->mapRenderer->map;

		static int step = 16;
		static Texture* originalTexture;
		static Texture* previewTexture;
		static bool reloadPreview = false;

		struct ResampOption {
			const char* name;
			const char* desc;
			int mode;
		};

		static ResampOption resamplers[4] = {
			{"Nearest", "Sharp but fails to retain detail.", KernelTypeNearest},
			{"Lanczos", "Sharp and retains detail better than Nearest.", KernelTypeLanczos3},
			{"Gaussian", "Blurry but retains detail best. Use with text.", KernelTypeGaussian},
			{"Bilinear", "A middle ground between Lanczos and Gaussian.", KernelTypeBilinear},
		};

		static ResampOption resampler = resamplers[1];

		BSPMIPTEX* tex = map->get_texture(resizeTextureIdx);

		if (tex) {
			int32_t texOffset = ((int32_t*)map->textures)[resizeTextureIdx + 1];

			int lastMipSize = (tex->nWidth >> 3) * (tex->nHeight >> 3);
			byte* palette = (byte*)(map->textures + texOffset + tex->nOffsets[3] + lastMipSize + 2);
			byte* srcPixels = (byte*)(map->textures + texOffset + tex->nOffsets[0]);

			if (!originalTexture && tex->nOffsets[0] != 0) {
				originalTexture = new Texture(resizeOriginalWidth, resizeOriginalHeight);

				COLOR3* srcColors = new COLOR3[tex->nWidth * tex->nHeight];
				for (int i = 0; i < tex->nWidth * tex->nHeight; i++) {
					srcColors[i] = ((COLOR3*)palette)[srcPixels[i]];
				}

				COLOR4* texColors = (COLOR4*)originalTexture->data;
				for (int i = 0; i < resizeWidth * resizeHeight; i++) {
					texColors[i] = COLOR4(srcColors[i], 255);
				}

				originalTexture->upload(GL_RGBA);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}

			if ((!previewTexture || reloadPreview) && tex->nOffsets[0] != 0) {
				if (previewTexture) {
					delete previewTexture;
				}
				previewTexture = new Texture(resizeWidth, resizeHeight);

				COLOR3* srcColors = new COLOR3[tex->nWidth * tex->nHeight];
				for (int i = 0; i < tex->nWidth * tex->nHeight; i++) {
					srcColors[i] = ((COLOR3*)palette)[srcPixels[i]];
				}

				COLOR3* dstColors = new COLOR3[resizeWidth * resizeHeight];
				int resampOutMode = resizeMasked ? RESAMP_PAL_MASKED : RESAMP_PAL;
				vector<COLOR3> newPal = Texture::resample(srcColors, tex->nWidth, tex->nHeight, dstColors, resizeWidth, resizeHeight,
					resampler.mode, resampOutMode, resizeMaskColor);

				for (int i = 0; i < resizeWidth * resizeHeight; i++) {
					bool foundColor = false;
					for (int k = 0; k < newPal.size(); k++) {
						if (newPal[k] == dstColors[i]) {
							dstColors[i] = newPal[k];
							foundColor = true;
							break;
						}
					}
					if (!foundColor) {
						dstColors[i] = COLOR3(0, 0, 0);
					}
				}

				COLOR4* texColors = (COLOR4*)previewTexture->data;

				for (int i = 0; i < resizeWidth * resizeHeight; i++) {
					texColors[i] = COLOR4(dstColors[i], 255);
				}

				delete[] dstColors;

				previewTexture->upload(GL_RGBA);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				reloadPreview = false;
			}
		}

		ImGui::SetNextItemWidth(200 * uiScale);
		if (ImGui::InputScalar("Width", ImGuiDataType_U16, (void*)&resizeWidth, &step)) {
			resizeWidth = min((int)resizeOriginalWidth, max(16, (resizeWidth / 16) * 16));
			reloadPreview = true;
		}
		ImGui::SetNextItemWidth(200 * uiScale);
		if (ImGui::InputScalar("Height", ImGuiDataType_U16, (void*)&resizeHeight, &step)) {
			resizeHeight = min((int)resizeOriginalHeight, max(16, (resizeHeight / 16) * 16));
			reloadPreview = true;
		}
		ImGui::SetNextItemWidth(200 * uiScale);

		if (ImGui::BeginCombo("Resampling", resampler.name)) {
			for (int i = 0; i < 4; i++) {
				if (ImGui::Selectable(resamplers[i].name)) {
					resampler = resamplers[i];
					reloadPreview = true;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", resamplers[i].desc);
				}
			}
			ImGui::EndCombo();
		}

		ImGui::Dummy(ImVec2(0, roundf(5 * uiScale)));
		if (ImGui::Button("Reset")) {
			resizeWidth = resizeOriginalWidth;
			resizeHeight = resizeOriginalHeight;
			reloadPreview = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fix Bad Extents")) {
			float bestScale = map->get_scale_to_fix_bad_extents(resizeTextureIdx);
			int newWidth, newHeight;
			map->get_scaled_texture_dimensions(resizeTextureIdx, bestScale, newWidth, newHeight);
			resizeWidth = newWidth;
			resizeHeight = newHeight;
			reloadPreview = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Calculate the max size for this texture which will fix all bad surface extents.");
		}

		ImGui::Dummy(ImVec2(0, 5));


		ImGui::Columns(2, NULL, false);
		ImGui::Text("Original (%dx%d):", resizeOriginalWidth, resizeOriginalHeight);
		ImGui::NextColumn();
		ImGui::Text("Resized (%dx%d):", resizeWidth, resizeHeight);
		ImGui::NextColumn();
		int imgWidth = ImGui::GetContentRegionAvail().x;
		if (originalTexture) {
			float aspect = (float)originalTexture->height / originalTexture->width;
			ImGui::Image(originalTexture->id, ImVec2(imgWidth, imgWidth * aspect));
		}
		ImGui::NextColumn();
		imgWidth = ImGui::GetContentRegionAvail().x;
		if (previewTexture) {
			float aspect = (float)previewTexture->height / previewTexture->width;
			ImGui::Image(previewTexture->id, ImVec2(imgWidth, imgWidth * aspect));
		}
		ImGui::Columns(1);

		ImGui::Dummy(ImVec2(0, roundf(5 * uiScale)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, roundf(5 * uiScale)));

		ImGui::BeginDisabled((resizeWidth == resizeOriginalWidth && resizeHeight == resizeOriginalHeight) || !originalTexture);
		if (ImGui::Button("Resize", ImVec2(120 * uiScale, 0))) {
			LumpReplaceCommand* command = new LumpReplaceCommand("Resize Texture");
			map->downscale_texture(resizeTextureIdx, resizeWidth, resizeHeight, resampler.mode);
			command->pushUndoState();

			ImGui::CloseCurrentPopup();

			delete previewTexture;
			previewTexture = NULL;
			delete originalTexture;
			originalTexture = NULL;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120 * uiScale, 0))) {
			ImGui::CloseCurrentPopup();
			delete previewTexture;
			previewTexture = NULL;
			delete originalTexture;
			originalTexture = NULL;
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

}

void FaceEditor::drawLightmapsEditor() {
	Bsp* map = app->pickInfo.getMap();
	ImGuiContext& g = *GImGui;

	if (!map || app->pickInfo.faces.size() != 1) {
		ImGui::Text("Multiple faces selected");
		return;
	}

	static Texture* currentlightMap[MAXLIGHTMAPS];
	static int lightmaps = 0;
	int faceIdx = app->pickInfo.getFaceIndex();
	BSPFACE& face = *app->pickInfo.getFace();
	static int size[2];
	static int lightmapSz;

	if (gui->lightmapEditorNeedsUpdate) {
		lightmaps = 0;
		GetFaceLightmapSize(map, faceIdx, size);

		for (int i = 0; i < MAXLIGHTMAPS; i++) {
			if (currentlightMap[i])
				delete currentlightMap[i];
			currentlightMap[i] = NULL;

			if (face.nStyles[i] == 255)
				continue;

			currentlightMap[i] = new Texture(size[0], size[1]);
			lightmapSz = size[0] * size[1] * sizeof(COLOR3);
			int offset = face.nLightmapOffset + i * lightmapSz;
			memcpy(currentlightMap[i]->data, map->lightdata + offset, lightmapSz);
			currentlightMap[i]->upload(GL_RGB, true);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			lightmaps++;
			//logf("upload %d style at offset %d\n", i, offset);
		}

		gui->lightmapEditorNeedsUpdate = false;
	}

	if (size[0] <= 0 || size[1] <= 0)
		return;

	int padding = 40;
	float windowWidth = ImGui::GetContentRegionAvail().x - padding;

	float ratio = size[1] / (float)size[0];
	float imgWidth = (windowWidth * 0.5f);
	float imgHeight = imgWidth * ratio;

	// don't grow too tall
	float maxHeight = windowWidth * 0.6f;
	float heightScale = maxHeight / imgHeight;
	if (heightScale < 1) {
		imgWidth *= heightScale;
		imgHeight *= heightScale;
	}

	if (!lightmaps) {
		ImGui::Text("No lightmaps");
		return;
	}

	float rowHeight = maxHeight + ImGui::CalcTextSize("Style").y + 10;
	ImVec2 imgSize = ImVec2(imgWidth, imgHeight);

	if (ImGui::BeginTable("tbl", 2, ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter))
	{
		for (int i = 0; i < 4; i++) {
			if (i % 2 == 0)
				ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
			ImGui::TableNextColumn();

			if (face.nStyles[i] == 255 || !currentlightMap[i])
				continue;

			const char* text = cstrf("Style %d", face.nStyles[i]);
			float cellWidth = ImGui::GetColumnWidth();
			float textWidth = ImGui::CalcTextSize(text).x;

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellWidth - textWidth) * 0.5f);
			ImGui::Text(text);

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellWidth - imgSize.x) * 0.5f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

			ImGui::PushID(i);
			ImGui::ImageButton(cstrf("b%d", i), (ImTextureID)currentlightMap[i]->id, imgSize);
			ImGui::PopStyleVar(2);

			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Copy")) {
					copyLightmap(faceIdx, i);
				}
				tooltip(cstrf("Copy lightmap layer", i));

				if (ImGui::MenuItem("Paste", "", false, copiedLightmapFace >= 0)) {
					pasteLightmap(faceIdx, i);
				}
				tooltip("Paste lightmap layer. If the target lightmap dimensions don't match "
					"what you copied, the lightmap will be scaled to fit using bilinear filtering.");

				if (ImGui::MenuItem("Bake", "", false, face.nStyles[i] != 0)) {
					LightmapsEditCommand* command = new LightmapsEditCommand("Bake Lightmap");

					map->bake_lightmap_style(face.nStyles[i], false, false, faceIdx);

					command->pushUndoState();
				}
				tooltip("Merge this lightmap with the base lightmap layer");

				if (ImGui::MenuItem("Delete")) {
					LightmapsEditCommand* command = new LightmapsEditCommand("Delete Lightmap");

					map->bake_lightmap_style(face.nStyles[i], true, false, faceIdx);

					command->pushUndoState();
				}
				tooltip("Delete this lightmap layer");

				ImGui::Separator();

				if (ImGui::MenuItem("Import")) {
					char* fname = tinyfd_openFileDialog("Import Texture", "", 1, pngFilterPatterns, "PNG (*.png)", 1);

					if (fname) {
						uint8_t* pngpixels;
						unsigned int w, h;

						if (!lodepng_decode24_file(&pngpixels, &w, &h, fname)) {
							LightmapsEditCommand* command = new LightmapsEditCommand("Import Lightmap");

							map->apply_lightmap(faceIdx, i, (COLOR3*)pngpixels, w, h);
							delete[] pngpixels;

							command->pushUndoState();
						}
						else {
							logf("Failed to load PNG data\n");
						}
					}
				}
				tooltip("Import a 24-bit PNG file to replace this lightmap. If dimensions don't match, "
					"the image will be scaled to fit the target lightmap with bilinear filtering.");

				if (ImGui::MenuItem("Export")) {
					char* fname = tinyfd_saveFileDialog("Export Lightmap",
						cstrf("lightmap_%d_%d.png", faceIdx, i),
						1, pngFilterPatterns, "PNG (*.png)");

					if (fname) {
						Texture* tex = currentlightMap[i];

						if (tex && tex->data) {
							if (lodepng_encode24_file(fname, tex->data, tex->width, tex->height)) {
								logf("Failed to save texture as PNG\n", fname);
							}
							else {
								logf("Wrote %s\n", fname);
							}
						}
						else {
							logf("failed to load lightmap data for face %d, layer %d\n", faceIdx, i);
						}
					}
				}
				tooltip("Export this lightmap as a 24-bit PNG file");

				ImGui::Separator();

				if (ImGui::BeginMenu(cstrf("Style %d", face.nStyles[i]))) {
					if (ImGui::MenuItem("Bake", "", false, face.nStyles[i] != 0)) {
						LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Bake Light Style %d", face.nStyles[i]));

						int numBakes = map->bake_lightmap_style(face.nStyles[i], false, true);
						logf("Baked %d lightmaps\n", numBakes);

						command->pushUndoState();
					}
					tooltip(cstrf("Merge all lightmaps for style %d into the base lightmap layer. "
						"This will reduce lightstyles and disable animations/toggling for all lights "
						"linked to this style.", face.nStyles[i]));

					if (ImGui::MenuItem("Delete", "", false, face.nStyles[i] != 0)) {
						LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Delete Light Style %d", face.nStyles[i]));

						int numBakes = map->bake_lightmap_style(face.nStyles[i], true, true);
						logf("Baked %d lightmaps\n", numBakes);

						command->pushUndoState();
					}
					tooltip(cstrf("Delete all lightmaps linked to style %d. This reduces light style count.", face.nStyles[i]));

					if (ImGui::MenuItem("Select")) {
						Bsp* map = app->pickInfo.getMap();
						g_app->mapRenderer->highlightPickedFaces(false);
						app->pickInfo.deselect();

						for (int k = 0; k < map->faceCount; k++) {
							BSPFACE& testface = map->faces[k];
							for (int s = 0; s < MAXLIGHTMAPS; s++) {
								if (testface.nStyles[s] == face.nStyles[i]) {
									app->pickInfo.selectFace(k);
									break;
								}
							}
						}

						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size());
						g_app->pickCount++;
					}
					tooltip(cstrf("Select all faces that use light style %d", face.nStyles[i]));

					ImGui::Separator();

					if (ImGui::MenuItem("Import")) {
						char* fname = tinyfd_openFileDialog("Import Lightmaps", "", 1, pngFilterPatterns, "PNG (*.png)", 1);

						if (fname) {
							LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Import Light Style %d", face.nStyles[i]));
							map->import_lightmap_style(face.nStyles[i], fname);
							command->pushUndoState();
						}
					}
					tooltip(cstrf("Import a PNG file to replace all lightmaps for style %d", face.nStyles[i]));

					if (ImGui::MenuItem("Export")) {
						char* fname = tinyfd_saveFileDialog("Export Lightmaps",
							cstrf("lightmap_style_%d.png", face.nStyles[i]),
							1, pngFilterPatterns, "PNG (*.png)");

						if (fname) {
							map->export_lightmap_style(face.nStyles[i], fname);
						}
					}
					tooltip(cstrf("Export all lightmaps for style %d. Use this to bulk edit all faces affected by a light source.", face.nStyles[i]));

					ImGui::EndMenu();
				}

				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::Text("Dimensions: %dx%d", size[0], size[1]);
}

void FaceEditor::copyLightmap(int faceIdx, int layer) {
	Bsp* map = app->pickInfo.getMap();

	if (faceIdx < 0 || faceIdx >= map->faceCount || layer < 0 || layer >= MAXLIGHTMAPS) {
		logf("Failed to copy lightmap face %d layer %d\n", faceIdx, layer);
		return;
	}

	copiedLightmapFace = faceIdx;
	copiedLightmapLayer = layer;
}

void FaceEditor::pasteLightmap(int faceIdx, int layer) {
	Bsp* map = app->pickInfo.getMap();

	if (!map || copiedLightmapFace >= map->faceCount || copiedLightmapFace < 0) {
		logf("Failed to paste lightmap from face %d\n", copiedLightmapFace);
		return;
	}

	LightmapsEditCommand* command = new LightmapsEditCommand("Paste Lightmap");

	int size[2];
	GetFaceLightmapSize(map, copiedLightmapFace, size);
	int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
	BSPFACE& srcFace = map->faces[copiedLightmapFace];
	COLOR3* srcData = (COLOR3*)(map->lightdata + srcFace.nLightmapOffset + lightmapSz * copiedLightmapLayer);

	map->apply_lightmap(faceIdx, layer, srcData, size[0], size[1]);

	command->pushUndoState();
}

void FaceEditor::checkFaceErrors() {
	gui->lightmapTooLarge = gui->badSurfaceExtents = false;

	Bsp* map = app->pickInfo.getMap();

	for (int i = 0; i < app->pickInfo.faces.size(); i++) {
		int size[2];
		if (!GetFaceLightmapSize(map, app->pickInfo.faces[i], size)) {
			gui->badSurfaceExtents = true;
		}
		if (size[0] * size[1] > MAX_LUXELS) {
			gui->lightmapTooLarge = true;
		}
	}
}