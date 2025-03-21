#include "Gui.h"
#include "primitives.h"
#include "Renderer.h"
#include <lodepng.h>
#include "Entity.h"
#include "Bsp.h"
#include "Command.h"
#include "Fgd.h"
#include "Texture.h"
#include "Wad.h"
#include "util.h"
#include "globals.h"
#include <fstream>
#include <set>
#include "tinyfiledialogs.h"
#include <algorithm>
#include "BspMerger.h"
#include "LeafNavMesh.h"
#include <unordered_map>
#include <lzma_util.h>

// embedded binary data
#include "fonts/notosans.h"
#include "fonts/notosans_mono.h"
#include "fonts/notosans_unicode.h"
#include "icons/object.h"
#include "icons/face.h"
#include <unordered_set>

// TODO: hack to keep size consistent with bspguy v4. Is there something wrong with the font?
// "22" should have the same height regardless of font. Maybe FontForge was misused.
float g_smallFontSizeMult = 1.1f;

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

string iniPath = getConfigDir() + "imgui.ini";

char const* bspFilterPatterns[1] = { "*.bsp" };

void tooltip(ImGuiContext& g, const char* text) {
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

Gui::Gui(Renderer* app) {
	this->app = app;
	init();
}

void Gui::init() {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	io.IniFilename = iniPath.c_str();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	const char* glsl_version = "#version 130";
	ImGui_ImplOpenGL3_Init(glsl_version);

	loadFonts();

	io.ConfigWindowsMoveFromTitleBarOnly = true;

	clearLog();

	// load icons
	byte* icon_data = NULL;
	uint w, h;

	lodepng_decode32(&icon_data, &w, &h, object_icon, sizeof(object_icon));
	objectIconTexture = new Texture(w, h, icon_data);
	objectIconTexture->upload(GL_RGBA);

	lodepng_decode32(&icon_data, &w, &h, face_icon, sizeof(face_icon));
	faceIconTexture = new Texture(w, h, icon_data);
	faceIconTexture->upload(GL_RGBA);
}

void Gui::draw() {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

#ifndef NDEBUG
	ImGui::ShowDemoWindow();
#endif

	drawMenuBar();
	drawPopups();

	drawToolbar();
	drawStatusMessage();

	if (showDebugWidget) {
		drawDebugWidget();
	}
	if (showKeyvalueWidget) {
		drawKeyvalueEditor();
	}
	if (showTransformWidget) {
		drawTransformWidget();
	}
	if (showLogWidget) {
		drawLog();
	}
	if (showSettingsWidget) {
		drawSettings();
	}
	if (showHelpWidget) {
		drawHelp();
	}
	if (showAboutWidget) {
		drawAbout();
	}
	if (showLimitsWidget) {
		drawLimits();
	}
	if (showTextureWidget) {
		drawTextureTool();
	}
	/*
	if (showLightmapEditorWidget) {
		drawLightMapTool();
	}
	*/
	if (showEntityReport) {
		drawEntityReport();
	}

	if (app->pickMode == PICK_OBJECT) {
		if (contextMenuEnt != -1) {
			ImGui::OpenPopup("ent_context");
			contextMenuEnt = -1;
		}
		if (emptyContextMenu) {
			emptyContextMenu = 0;
			ImGui::OpenPopup("empty_context");
		}
	}
	else {
		if (contextMenuEnt != -1 || emptyContextMenu) {
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup("face_context");
		}
	}


	draw3dContextMenus();

	drawStatusBar();

	// Rendering
	ImGui::Render();
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (shouldReloadFonts) {
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		shouldReloadFonts = false;

		ImGui_ImplOpenGL3_DestroyFontsTexture();
		io.Fonts->Clear();

		loadFonts();

		io.Fonts->Build();
		ImGui_ImplOpenGL3_CreateFontsTexture();
	}
}

void Gui::openContextMenu(int entIdx) {
	if (entIdx == -1) {
		emptyContextMenu = 1;
	}
	contextMenuEnt = entIdx;
}

void Gui::copyTexture() {
	BSPFACE* face = app->pickInfo.getFace();
	if (!face) {
		return;
	}

	Bsp* map = app->pickInfo.getMap();
	BSPTEXTUREINFO& texinfo = map->texinfos[face->iTextureInfo];
	copiedMiptex = texinfo.iMiptex;
}

void Gui::pasteTexture() {
	refreshSelectedFaces = true;
}

void Gui::copyLightmap() {
	if (!app->pickInfo.getFace()) {
		return;
	}

	Bsp* map = app->pickInfo.getMap();

	copiedLightmapFace = app->pickInfo.getFaceIndex();

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.getFaceIndex(), size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count(app->pickInfo.getFaceIndex());
	//copiedLightmap.luxelFlags = new byte[size[0] * size[1]];
	//qrad_get_lightmap_flags(map, app->pickInfo.faceIdx, copiedLightmap.luxelFlags);
}

void Gui::pasteLightmap() {
	if (!app->pickInfo.getFace()) {
		return;
	}

	Bsp* map = app->pickInfo.getMap();

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.getFaceIndex(), size);
	LIGHTMAP dstLightmap;
	dstLightmap.width = size[0];
	dstLightmap.height = size[1];
	dstLightmap.layers = map->lightmap_count(app->pickInfo.getFaceIndex());

	if (dstLightmap.width != copiedLightmap.width || dstLightmap.height != copiedLightmap.height) {
		logf("WARNING: lightmap sizes don't match (%dx%d != %d%d)",
			copiedLightmap.width,
			copiedLightmap.height,
			dstLightmap.width,
			dstLightmap.height);
		// TODO: resize the lightmap, or maybe just shift if the face is the same size
	}

	BSPFACE& src = map->faces[copiedLightmapFace];
	BSPFACE& dst = *app->pickInfo.getFace();
	dst.nLightmapOffset = src.nLightmapOffset;
	memcpy(dst.nStyles, src.nStyles, 4);

	app->mapRenderer->reloadLightmaps();
}

void Gui::draw3dContextMenus() {
	ImGuiContext& g = *GImGui;

	if (app->originHovered) {
		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context")) {
			if (ImGui::MenuItem("Center", "")) {
				app->transformedOrigin = app->getEntOrigin(app->pickInfo.getMap(), app->pickInfo.getEnt());
				app->applyTransform();
				app->pickCount++; // force gui refresh
			}

			if (app->pickInfo.getMap() && app->pickInfo.getEnt() && ImGui::BeginMenu("Align")) {
				BSPMODEL& model = *app->pickInfo.getModel();

				if (ImGui::MenuItem("Top")) {
					app->transformedOrigin.z = app->oldOrigin.z + model.nMaxs.z;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Bottom")) {
					app->transformedOrigin.z = app->oldOrigin.z + model.nMins.z;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Left")) {
					app->transformedOrigin.x = app->oldOrigin.x + model.nMins.x;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Right")) {
					app->transformedOrigin.x = app->oldOrigin.x + model.nMaxs.x;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Back")) {
					app->transformedOrigin.y = app->oldOrigin.y + model.nMins.y;
					app->applyTransform();
					app->pickCount++;
				}
				if (ImGui::MenuItem("Front")) {
					app->transformedOrigin.y = app->oldOrigin.y + model.nMaxs.y;
					app->applyTransform();
					app->pickCount++;
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		return;
	}

	if (app->pickMode == PICK_OBJECT) {

		if (ImGui::BeginPopup("ent_context"))
		{
			if (ImGui::MenuItem("Cut", "Ctrl+X")) {
				app->cutEnts();
			}
			if (ImGui::MenuItem("Copy", "Ctrl+C")) {
				app->copyEnts();
			}
			if (ImGui::MenuItem("Delete", "Del")) {
				app->deleteEnts();
			}
			ImGui::Separator();
			int modelIdx = app->pickInfo.getModelIndex();
			if (modelIdx > 0) {
				Bsp* map = app->pickInfo.getMap();
				BSPMODEL& model = *app->pickInfo.getModel();

				if (ImGui::BeginMenu("Create Hull", !app->invalidSolid && app->isTransformableSolid)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->regenerate_clipnodes(modelIdx, -1);
						checkValidHulls();
						logf("Regenerated hulls 1-3 on model %d\n", modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str())) {
							map->regenerate_clipnodes(modelIdx, i);
							checkValidHulls();
							logf("Regenerated hull %d on model %d\n", i, modelIdx);
						}
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Delete Hull", !app->isLoading)) {
					if (ImGui::MenuItem("All Hulls")) {
						map->delete_hull(0, modelIdx, -1);
						map->delete_hull(1, modelIdx, -1);
						map->delete_hull(2, modelIdx, -1);
						map->delete_hull(3, modelIdx, -1);
						app->mapRenderer->refreshModel(modelIdx);
						checkValidHulls();
						logf("Deleted all hulls on model %d\n", modelIdx);
					}
					if (ImGui::MenuItem("Clipnodes")) {
						map->delete_hull(1, modelIdx, -1);
						map->delete_hull(2, modelIdx, -1);
						map->delete_hull(3, modelIdx, -1);
						app->mapRenderer->refreshModelClipnodes(modelIdx);
						checkValidHulls();
						logf("Deleted hulls 1-3 on model %d\n", modelIdx);
					}

					ImGui::Separator();

					for (int i = 0; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, isHullValid)) {
							map->delete_hull(i, modelIdx, -1);
							checkValidHulls();
							if (i == 0)
								app->mapRenderer->refreshModel(modelIdx);
							else
								app->mapRenderer->refreshModelClipnodes(modelIdx);
							logf("Deleted hull %d on model %d\n", i, modelIdx);
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Simplify Hull", !app->isLoading)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->simplify_model_collision(modelIdx, 1);
						map->simplify_model_collision(modelIdx, 2);
						map->simplify_model_collision(modelIdx, 3);
						app->mapRenderer->refreshModelClipnodes(modelIdx);
						logf("Replaced hulls 1-3 on model %d with a box-shaped hull\n", modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, isHullValid)) {
							map->simplify_model_collision(modelIdx, 1);
							app->mapRenderer->refreshModelClipnodes(modelIdx);
							logf("Replaced hull %d on model %d with a box-shaped hull\n", i, modelIdx);
						}
					}

					ImGui::EndMenu();
				}

				bool canRedirect = model.iHeadnodes[1] != model.iHeadnodes[2] || model.iHeadnodes[1] != model.iHeadnodes[3];

				if (ImGui::BeginMenu("Redirect Hull", canRedirect && !app->isLoading)) {
					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						if (ImGui::BeginMenu(("Hull " + to_string(i)).c_str())) {

							for (int k = 1; k < MAX_MAP_HULLS; k++) {
								if (i == k)
									continue;

								bool isHullValid = model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] != model.iHeadnodes[i];

								if (ImGui::MenuItem(("Hull " + to_string(k)).c_str(), 0, false, isHullValid)) {
									model.iHeadnodes[i] = model.iHeadnodes[k];
									app->mapRenderer->refreshModelClipnodes(modelIdx);
									checkValidHulls();
									logf("Redirected hull %d to hull %d on model %d\n", i, k, modelIdx);
								}
							}

							ImGui::EndMenu();
						}
					}

					ImGui::EndMenu();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading)) {
					DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", app->pickInfo);
					command->execute();
					app->pushUndoCommand(command);
				}
				tooltip(g, "Create a copy of this BSP model and assign to this entity.\n\nThis lets you edit the model for this entity without affecting others.");
			
				/*
				if (ImGui::MenuItem("Merge BSP models", "", false, !app->isLoading)) {
					int idxA = 940;
					int idxB = 941;

					int numIdxA = 0;
					int numIdxB = 0;
					for (Entity* ent : map->ents) {
						int idx = ent->getBspModelIdx();
						if (idx == idxA) {
							numIdxA++;
						}
						else if (idx == idxB) {
							numIdxB++;
						}
					}

					if (numIdxA > 1 || numIdxB > 1) {
						logf("Merge aborted. Model(s) are shared by multiple entities.");
					}
					else {
						int newIndex = map->merge_models(idxA, idxB);
						logf("Created merged model *%d\n", newIndex);

						for (int i = 0; i < map->ents.size(); i++) {
							Entity* ent = map->ents[i];
							int idx = ent->getBspModelIdx();
							if (idx == idxA) {
								ent->setOrAddKeyvalue("model", "*" + to_string(newIndex));
							}
							else if (idx == idxB) {
								delete ent;
								map->ents.erase(map->ents.begin() + i);
								i--;
							}
						}

						map->remove_unused_model_structures();

						g_app->deselectObject();
						g_app->mapRenderers[0]->reload();
						refresh();
					}
				}
				*/
			}

			if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G")) {
				if (!app->movingEnt)
					app->grabEnts();
				else {
					app->ungrabEnts();
				}
			}
			bool shouldHide = app->pickInfo.shouldHideSelection();

			if (ImGui::MenuItem(shouldHide ? "Hide" : "Unhide", "H", false, app->pickInfo.ents.size() != 0)) {
				if (shouldHide) {
					app->hideSelectedEnts();
				}
				else {
					app->unhideSelectedEnts();
				}
			}
			if (ImGui::MenuItem("Transform", "Ctrl+M")) {
				showTransformWidget = !showTransformWidget;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Properties", "Alt+Enter")) {
				showKeyvalueWidget = !showKeyvalueWidget;
			}


			ImGui::EndPopup();
		}

		static bool emptyWasOpen = false; // prevent glfw error spam
		if (ImGui::BeginPopup("empty_context"))
		{
			static bool canPaste = false;
			if (!emptyWasOpen)
				canPaste = app->canPasteEnts();
			emptyWasOpen = true;

			if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
				app->pasteEnts(false);
			}
			if (ImGui::MenuItem("Paste at original origin", 0, false, canPaste)) {
				app->pasteEnts(true);
			}
			if (ImGui::MenuItem("Unhide All", 0, false, app->anyHiddenEnts)) {
				app->unhideEnts();
			}

			ImGui::EndPopup();
		}
		else {
			emptyWasOpen = false;
		}
	}
	else if (app->pickMode == PICK_FACE && app->pickInfo.faces.size()) {
		Bsp* map = app->pickInfo.getMap();

		if (ImGui::BeginPopup("face_context"))
		{
			if (ImGui::MenuItem("Copy texture", "Ctrl+C", false, app->pickInfo.faces.size() == 1)) {
				copyTexture();
			}
			if (ImGui::MenuItem("Paste texture", "Ctrl+V", false, copiedMiptex >= 0 && copiedMiptex < map->textureCount)) {
				pasteTexture();
			}
			if (ImGui::MenuItem("Select all of this texture", "", false, app->pickInfo.faces.size() == 1)) {
				Bsp* map = app->pickInfo.getMap();
				BSPTEXTUREINFO& texinfo = map->texinfos[app->pickInfo.getFace()->iTextureInfo];
				uint32_t selectedMiptex = texinfo.iMiptex;

				app->pickInfo.deselect();
				for (int i = 0; i < map->faceCount; i++) {
					BSPTEXTUREINFO& info = map->texinfos[map->faces[i].iTextureInfo];
					if (info.iMiptex == selectedMiptex) {
						app->pickInfo.selectFace(i);
					}
				}

				logf("Selected %d faces\n", app->pickInfo.faces.size());
				refreshSelectedFaces = true;
			}
			tooltip(g, "Select every face in the map which has this texture.");

			if (ImGui::MenuItem("Select connected planar faces of this texture", "", false, app->pickInfo.faces.size() == 1)) {
				Bsp* map = app->pickInfo.getMap();

				set<int> newSelect = map->selectConnectedTexture(app->pickInfo.getModelIndex(), app->pickInfo.getFaceIndex());
				app->pickInfo.deselect();

				for (int i : newSelect) {
					app->pickInfo.selectFace(i);
				}

				logf("Selected %d faces\n", app->pickInfo.faces.size());
				refreshSelectedFaces = true;
			}
			tooltip(g, "Selects faces connected to this one which lie on the same plane and use the same texture");

			Bsp* map = app->pickInfo.getMap();
			bool isEmbedded = false;
			if (map && app->pickInfo.getFace()) {
				BSPFACE& face = *app->pickInfo.getFace();
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
				if (info.iMiptex > 0 && info.iMiptex < map->textureCount) {
					int32_t texOffset = ((int32_t*)map->textures)[info.iMiptex + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					isEmbedded = tex.nOffsets[0] != 0;
				}
			}

			if (ImGui::MenuItem("Downscale texture", 0, false, !app->isLoading && isEmbedded)) {
				Bsp* map = app->pickInfo.getMap();

				set<int> downscaled;

				for (int i = 0; i < app->pickInfo.faces.size(); i++) {
					BSPFACE& face = map->faces[app->pickInfo.faces[i]];
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					
					if (downscaled.count(info.iMiptex))
						continue;
					
					int32_t texOffset = ((int32_t*)map->textures)[info.iMiptex + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

					int maxDim = max(tex.nWidth, tex.nHeight);

					int nextBestDim = 16;
					if (maxDim > 512) { nextBestDim = 512; }
					else if (maxDim > 256) { nextBestDim = 256; }
					else if (maxDim > 128) { nextBestDim = 128; }
					else if (maxDim > 64) { nextBestDim = 64; }
					else if (maxDim > 32) { nextBestDim = 32; }
					else if (maxDim > 32) { nextBestDim = 32; }

					downscaled.insert(info.iMiptex);
					map->downscale_texture(info.iMiptex, nextBestDim, true);
				}
				
				app->pickInfo.deselect();
				app->mapRenderer->reload();
				reloadLimits();
			}
			tooltip(g, "Reduces the dimensions of this texture down to the next power of 2\n\nThis will break lightmaps.");

			if (ImGui::MenuItem("Subdivide", 0, false, !app->isLoading)) {
				Bsp* map = app->pickInfo.getMap();

				for (int i = 0; i < app->pickInfo.faces.size(); i++) {
					map->subdivide_face(app->pickInfo.faces[i]);
				}
				app->pickInfo.deselect();
				app->mapRenderer->reload();
			}
			tooltip(g, "Split this face across the axis with the most texture pixels.");

			if (ImGui::MenuItem("Subdivide until valid", 0, false, !app->isLoading)) {
				Bsp* map = app->pickInfo.getMap();

				for (int i = 0; i < app->pickInfo.faces.size(); i++) {
					map->fix_bad_surface_extents_with_subdivide(app->pickInfo.faces[i]);
				}
				
				app->pickInfo.deselect();
				app->mapRenderer->reload();
			} 
			tooltip(g, "Subdivide this face until it has valid surface extents.");

			ImGui::Separator();

			if (ImGui::MenuItem("Copy lightmap", "(WIP)", false, app->pickInfo.faces.size() == 1)) {
				copyLightmap();
			}
			tooltip(g, "Only works for faces with matching sizes/extents,\nand the lightmap might get shifted.");

			if (ImGui::MenuItem("Paste lightmap", "", false, copiedLightmapFace >= 0 && copiedLightmapFace < map->faceCount)) {
				pasteLightmap();
			}

			ImGui::EndPopup();
		}
	}
}

void Gui::drawMenuBar() {
	ImGuiContext& g = *GImGui;

	ImGui::BeginMainMenuBar();

	static bool editWasOpen = false;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Open", "Ctrl+O", false, !app->isLoading)) {
			g_app->openMap(NULL);
		}

		ImGui::BeginDisabled(app->emptyMapLoaded);
		if (ImGui::MenuItem("Save", NULL)) {
			Bsp* map = app->mapRenderer->map;
			map->update_ent_lump();
			//map->write("yabma_move.bsp");
			//map->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
			map->write(map->path);
			app->setInitialLumpState();
		}
		if (ImGui::MenuItem("Save As...", "Ctrl+Alt+S")) {
			saveAs();
		}
		if (ImGui::MenuItem("Save a Copy As...", NULL)) {
			Bsp* map = app->mapRenderer->map;

			char* fname = tinyfd_saveFileDialog("Save a Copy As", map->path.c_str(),
				1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)");

			if (fname) {
				string oldFname = map->path;
				string oldName = map->name;

				map->update_ent_lump();
				map->path = fname;
				map->name = stripExt(basename(fname));
				map->write(map->path);

				map->path = oldFname;
				map->name = oldName;
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Merge", NULL, false, !app->isLoading)) {
			char* fname = tinyfd_openFileDialog("Merge Map", "",
				1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

			if (fname)
				g_app->merge(fname);
		}
		Bsp* map = g_app->mapRenderer->map;
		tooltip(g, ("Merge one other BSP into the current file.\n\n"
			"Equivalent CLI command:\nbspguy merge " + map->name + " -noscript -noripent -maps \""
			+ map->name + ",other_map\"\n\nUse the CLI for automatic arrangement and optimization of "
			"many maps. The CLI also offers ripent fixes and script setup which can "
			"generate a playable map without you having to make any manual edits (Sven Co-op only).").c_str());
		
		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
			refresh();
		}
		tooltip(g, "Discard all changes and reload the map.\n");

		if (ImGui::MenuItem("Validate")) {
			Bsp* map = app->mapRenderer->map;
			logf("Validating %s\n", map->name.c_str());
			map->validate();
		}
		tooltip(g, "Checks BSP data structures for invalid values and references. Trivial problems are fixed automatically. Results are output to the Log widget.");
		ImGui::EndDisabled();

		if (g_settings.recentFiles.size()) {
			ImGui::Separator();
			int idx = 1;
			for (int i = g_settings.recentFiles.size() - 1; i >= 0; i--) {
				if (ImGui::MenuItem((to_string(idx++) + ": " + g_settings.recentFiles[i]).c_str(), NULL)) {
					string path = g_settings.recentFiles[i];
					if (fileExists(path)) {
						g_app->openMap(path.c_str());
					}
					else {
						logf("BSP file does not exist: %s\n", path.c_str());
						g_settings.recentFiles.erase(g_settings.recentFiles.begin() + i);
						i--;
					}
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Clear Recent Files List", NULL)) {
				g_settings.recentFiles.clear();
				g_settings.save();
			}
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Exit", NULL)) {
			g_settings.save();
			glfwTerminate();
			std::exit(0);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		ImGui::BeginDisabled(app->emptyMapLoaded);
		Command* undoCmd = !app->undoHistory.empty() ? app->undoHistory[app->undoHistory.size() - 1] : NULL;
		Command* redoCmd = !app->redoHistory.empty() ? app->redoHistory[app->redoHistory.size() - 1] : NULL;
		string undoTitle = undoCmd ? "Undo " + undoCmd->desc : "Can't undo";
		string redoTitle = redoCmd ? "Redo " + redoCmd->desc : "Can't redo";
		bool canUndo = undoCmd && (!app->isLoading || undoCmd->allowedDuringLoad);
		bool canRedo = redoCmd && (!app->isLoading || redoCmd->allowedDuringLoad);
		bool entSelected = app->pickInfo.getEnt();
		bool nonWorldspawnEntSelected = entSelected && app->pickInfo.getEntIndex() != 0;

		if (ImGui::MenuItem(undoTitle.c_str(), "Ctrl+Z", false, canUndo)) {
			app->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), "Ctrl+Y", false, canRedo)) {
			app->redo();
		}

		ImGui::Separator();

		static bool canPaste = false;
		if (!editWasOpen)
			canPaste = app->canPasteEnts();
		editWasOpen = true;

		if (ImGui::MenuItem("Cut", "Ctrl+X", false, nonWorldspawnEntSelected)) {
			app->cutEnts();
		}
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, nonWorldspawnEntSelected)) {
			app->copyEnts();
		}
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {
			app->pasteEnts(false);
		}
		tooltip(g, "Creates entities from text data. You can use this to transfer entities "
			"from one bspguy window to another, or paste from .ent file text. Copy any entity "
			"in the viewer then paste to a text editor to see the format of the text data.");
		if (ImGui::MenuItem("Paste at original origin", 0, false, canPaste)) {
			app->pasteEnts(true);
		}
		tooltip(g, "Pastes entities at the locations they were copied from.");

		if (ImGui::MenuItem("Delete", "Del", false, nonWorldspawnEntSelected)) {
			app->deleteEnts();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading && nonWorldspawnEntSelected)) {
			DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", app->pickInfo);
			command->execute();
			app->pushUndoCommand(command);
		}
		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G", false, nonWorldspawnEntSelected)) {
			if (!app->movingEnt)
				app->grabEnts();
			else {
				app->ungrabEnts();
			}
		}

		bool shouldHide = app->pickInfo.shouldHideSelection();

		if (ImGui::MenuItem(shouldHide ? "Hide" : "Unhide", "H", false, app->pickInfo.ents.size() != 0)) {
			if (shouldHide) {
				app->hideSelectedEnts();
			}
			else {
				app->unhideSelectedEnts();
			}
		}
		if (ImGui::MenuItem("Unhide All", "", false, app->anyHiddenEnts)) {
			app->unhideEnts();
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", false, entSelected)) {
			showTransformWidget = !showTransformWidget;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Properties", "Alt+Enter", false, entSelected)) {
			showKeyvalueWidget = !showKeyvalueWidget;
		}

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
	else
		editWasOpen = false;

	if (ImGui::BeginMenu("View")) {
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
		if (ImGui::MenuItem("Textures", 0, g_render_flags & RENDER_TEXTURES)) {
			g_render_flags ^= RENDER_TEXTURES;
		}
		tooltip(g, "Render textures for all faces.");

		if (ImGui::MenuItem("Lightmaps", 0, g_render_flags & RENDER_LIGHTMAPS)) {
			g_render_flags ^= RENDER_LIGHTMAPS;
		}
		tooltip(g, "Render lighting textures for all faces.");

		if (ImGui::MenuItem("Wireframe", 0, g_render_flags & RENDER_WIREFRAME)) {
			g_render_flags ^= RENDER_WIREFRAME;
		}
		tooltip(g, "Outline all faces.");

		if (ImGui::MenuItem("Special World Faces", 0, g_render_flags & RENDER_SPECIAL)) {
			g_render_flags ^= RENDER_SPECIAL;
		}
		tooltip(g, "Render special faces that are normally invisible and/or have special rendering properties (e.g. the SKY texture).");

		if (ImGui::BeginMenu("Clipnodes")) {
			if (ImGui::MenuItem("Clipnodes (World)", 0, g_render_flags & RENDER_WORLD_CLIPNODES)) {
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			tooltip(g, "Render clipnode hulls for worldspawn");

			if (ImGui::MenuItem("Clipnodes (Entities)", 0, g_render_flags & RENDER_ENT_CLIPNODES)) {
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}
			tooltip(g, "Render clipnode hulls for solid entities");

			if (ImGui::MenuItem("Clipnode Transparency", 0, transparentClipnodes)) {
				transparentClipnodes = !transparentClipnodes;
				g_app->mapRenderer->updateClipnodeOpacity(transparentClipnodes ? 128 : 255);
			}
			tooltip(g, "Render clipnode meshes with transparency.");
			g_settings.render_flags = g_render_flags;

			ImGui::Separator();

			if (ImGui::MenuItem("Auto Hulls", 0, app->clipnodeRenderHull == -1)) {
				app->clipnodeRenderHull = -1;
			}
			tooltip(g, "Render collision hulls for things which would otherwise be invisible."
				"\n\nAn example of this is a trigger_once which had the NULL texture applied to it by the mapper. "
				"That entity would have no visible faces and so would be invisible if its collision hull were not rendered instead.");

			if (ImGui::MenuItem("Hull 0 (Point)", 0, app->clipnodeRenderHull == 0)) {
				app->clipnodeRenderHull = 0;
			}
			tooltip(g, "Renders hull 0 regardless of object visibility.\n\n"
				"This hull is used for point-sized object collision and mesh rendering.");

			if (ImGui::MenuItem("Hull 1 (Human)", 0, app->clipnodeRenderHull == 1)) {
				app->clipnodeRenderHull = 1;
			}
			tooltip(g, "Renders hull 1 regardless of object visibility."
				"\n\nThis is a collision hull used by standing players and human-sized monsters.");

			if (ImGui::MenuItem("Hull 2 (Large)", 0, app->clipnodeRenderHull == 2)) {
				app->clipnodeRenderHull = 2;
			}
			tooltip(g, "Renders hull 2 regardless of object visibility.\n\n"
				"This is a collision hull used by large monsters and pushable objects.");

			if (ImGui::MenuItem("Hull 3 (Head)", 0, app->clipnodeRenderHull == 3)) {
				app->clipnodeRenderHull = 3;
			}
			tooltip(g, "Renders hull 3 regardless of object visibility.\n\n"
				"This is a collision hull used by crouching players and small monsters.");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Point Entities", 0, g_render_flags & RENDER_POINT_ENTS)) {
			g_render_flags ^= RENDER_POINT_ENTS;
		}
		tooltip(g, "Render point-sized entities which either have no model or reference MDL/SPR files.");

		if (ImGui::MenuItem("Solid Entities", 0, g_render_flags & RENDER_ENTS)) {
			if (g_render_flags & RENDER_ENTS) {
				g_render_flags &= ~(RENDER_ENTS | RENDER_SPECIAL_ENTS);
			}
			else {
				g_render_flags |= RENDER_ENTS | RENDER_SPECIAL_ENTS;
			}
		}
		tooltip(g, "Render entities that reference BSP models.");

		if (ImGui::MenuItem("Entity Direction Vectors", 0, g_render_flags & RENDER_ENT_DIRECTIONS)) {
			g_render_flags ^= RENDER_ENT_DIRECTIONS;
			app->updateEntDirectionVectors();
		}
		tooltip(g, "Display direction vectors for selected entities.\n"
			"For point entities, vectors usually represent orientation.\n"
			"For solid entities, vectors usually represent movement direction.");

		if (ImGui::MenuItem("Entity Links", 0, g_render_flags & RENDER_ENT_CONNECTIONS)) {
			g_render_flags ^= RENDER_ENT_CONNECTIONS;
		}
		tooltip(g, "Show how entities connect to each other.\n\n"
			"Yellow line = Selected entity targets the connected entity.\n"
			"Blue line = Selected entity is targetted by the connected entity.\n"
			"Green line = Selected entity and connected entity target each other.\n\n"
			"Not all connections are displayed. You may still need to use the Entity Report "
			"to find connections depending on the game the map was compiled for."
		);

		if (ImGui::MenuItem("Models", 0, g_render_flags & RENDER_STUDIO_MDL)) {
			g_render_flags ^= RENDER_STUDIO_MDL;

			if (!(g_render_flags & RENDER_STUDIO_MDL)) {
				for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
					app->mapRenderer->map->ents[i]->didStudioDraw = false;
				}
			}
		}
		tooltip(g, "Display game models instead of colored cubes where available.");

		if (ImGui::MenuItem("Sprites", 0, g_render_flags & RENDER_SPRITES)) {
			g_render_flags ^= RENDER_SPRITES;

			if (!(g_render_flags & RENDER_SPRITES)) {
				for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
					app->mapRenderer->map->ents[i]->didStudioDraw = false;
				}
			}
		}
		tooltip(g, "Display sprites instead of colored cubes where available.");

		ImGui::Separator();

		if (ImGui::MenuItem("Map Boundaries", 0, g_render_flags & RENDER_MAP_BOUNDARY)) {
			g_render_flags ^= RENDER_MAP_BOUNDARY;
		}
		tooltip(g, "Renders map boundaries as a transparent box around the world. Entities which leave "
			"this box may have visual glitches depending on the engine this map runs in.");

		if (ImGui::MenuItem("Origin", 0, g_render_flags & RENDER_ORIGIN)) {
			g_render_flags ^= RENDER_ORIGIN;
		}
		tooltip(g, "Displays a colored cross at the world origin (0,0,0)");

		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Settings"))
	{
		if (ImGui::MenuItem("Editor Setup", NULL)) {
			if (!showSettingsWidget) {
				reloadSettings = true;
			}
			ImGui::SetWindowCollapsed("Editor Setup", false);
			showSettingsWidget = true;
		}

		ImGui::Separator();

		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		bool changed = false;
		ImGui::MenuItem("Engine", 0, false, false);

		if (ImGui::MenuItem("Half-Life", 0, g_settings.engine == ENGINE_HALF_LIFE, !app->isLoading)) {
			changed = g_settings.engine != ENGINE_HALF_LIFE;
			g_settings.engine = ENGINE_HALF_LIFE;
		}
		tooltip(g, "The standard GoldSrc engine. Assumes a +/-4096 map boundary.\n");

		if (ImGui::MenuItem("Sven Co-op", 0, g_settings.engine == ENGINE_SVEN_COOP, !app->isLoading)) {
			changed = g_settings.engine != ENGINE_SVEN_COOP;
			g_settings.engine = ENGINE_SVEN_COOP;
		}
		tooltip(g, "Sven Co-op has higher map limits than Half-Life.\n\nAttempting to run a "
			"Sven Co-op map in Half-Life may result in AllocBlock Full errors, Bad Surface Extents, "
			"crashes caused by large textures, and visual glitches caused by crossing the +/-4096 map boundary. "
			"See the Porting Tools menu for solutions to these problems.\n\n"
			"The map boundary for Sven Co-op is effectively +/-32768. Rendering glitches occur beyond that point.");

		if (changed) {
			g_limits = g_engine_limits[g_settings.engine];
			app->mapRenderer->reload();
			reloadLimits();
		}
		
		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Tools"))
	{
		ImGui::BeginDisabled(app->emptyMapLoaded);
		Bsp* map = app->mapRenderer->map;

		static vector<Wad*> emptyWads;
		vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;
		BspRenderer* renderer = app->mapRenderer;

		ImGui::MenuItem("Create", 0, false, false);
		if (ImGui::MenuItem("Point Entity", 0, false, true)) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");
			vector<Entity*> newEnts = { newEnt };

			CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Create Entity", newEnts);
			delete newEnt;
			createCommand->execute();
			app->pushUndoCommand(createCommand);
		}
		tooltip(g, "Create a point entity. This is a ripent-only operation which does not affect BSP structures.\n");

		if (ImGui::MenuItem("BSP Model", 0, false, !app->isLoading)) {
			vec3 origin = app->cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");

			float snapSize = pow(2.0, g_app->gridSnapLevel);
			if (snapSize < 16) {
				snapSize = 16;
			}

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", newEnt, snapSize);
			command->execute();
			delete newEnt;
			app->pushUndoCommand(command);
		}
		tooltip(g, "Create a BSP model and attach it to a new entity. This is not a ripent-only operation and will create new BSP structures.\n");

		if (ImGui::MenuItem("Cull Entity", 0, false, true)) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "cull");
			vector<Entity*> newEnts = { newEnt };

			CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Create Entity", newEnts);
			delete newEnt;
			createCommand->execute();
			app->pushUndoCommand(createCommand);
		}
		tooltip(g, "Create a point entity for use with the culling tool. 2 of these define the bounding box for structure culling operations.\n");

		ImGui::Separator();

		ImGui::MenuItem("Delete", 0, false, false);
		if (ImGui::MenuItem("Clean", 0, false, !app->isLoading)) {
			CleanMapCommand* command = new CleanMapCommand("Clean " + map->name, app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}
		tooltip(g, "Removes unreferenced structures in the BSP data.\n\nWhen you edit BSP models or delete"
			" references to them, the data is not deleted until you run this command. "
			"If you are close exceeding engine limits, you may need to run this regularly while creating "
			"and editing models. Watch the Limits and Messages widgets to see how many structures were removed.");

		if (ImGui::MenuItem("Optimize", 0, false, !app->isLoading)) {
			OptimizeMapCommand* command = new OptimizeMapCommand("Optimize " + map->name, app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}
		tooltip(g, "Removes unnecesary structures in the BSP data. Useful as a pre-processing step for "
			"merging maps together without exceeding engine limits.\n\n"

			"An example of commonly deleted structures would be the visible hull 0 for entities like "
			"trigger_once, which are invisible and so don't need textured faces. Entities "
			"like func_illusionary also don't need any clipnodes because they're not meant to be collidable.\n\n"
		
			"The drawback to using this command is that it's possible for entities to change state in "
			"such a way that the deleted hulls are needed later. In those cases it is possible that "
			"the game crashes or has broken collision.\n\n"
			
			"Check the Log widget to see which entities had their hulls deleted. You may want to delete "
			"them manually by yourself if you run into problems with the Optimize command.");

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::BeginMenu("Redirect Hull", hasAnyCollision && !app->isLoading)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::BeginMenu(("Hull " + to_string(i)).c_str())) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + to_string(k)).c_str(), "", false, anyHullValid[k])) {
							Bsp* map = app->mapRenderer->map;
							map->delete_hull(i, k);
							app->mapRenderer->reloadClipnodes();
							logf("Redirected hull %d to hull %d in map %s\n", i, k, map->name.c_str());
							checkValidHulls();
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}
		tooltip(g, "Redirects a BSP hull in all models including worldspawn. This frees up a large "
			"amount of clipnodes but reduces collision accuracy for entities that use the removed hulls. "
			"Run the Clean command after redirecting a hull to delete the unreferenced clipnodes.\n\n"
			"Use this if the Optimize command refused to remove/redirect a hull, and you're ok with the side effects of forcing its removal. "
			"Some entities may clip into walls, hover above the ground, be able/unable to enter certain areas.\n\n"
			"A common use case for this is redirecting Hull 2 -> Hull 1 (Large monsters hull -> Normal monsters hull). "
			"If the map doesn't have any large monsters or pushable objects then there are no side effects for removing the hull. "
			"Removing Hull 1 or Hull 3 always causes noticeable problems because players and most monsters use these hulls."
		);

		/*
		if (ImGui::BeginMenu("Delete Hull", hasAnyCollision && !app->isLoading)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), NULL, false, anyHullValid[i])) {
					Bsp* map = app->mapRenderer->map;
					map->delete_hull(i, -1);
					app->mapRenderer->reloadClipnodes();
					logf("Deleted hull %d in map %s\n", i, map->name.c_str());
					checkValidHulls();
				}
			}
			ImGui::EndMenu();
		}
		tooltip(g, "Deletes a BSP hull from all models including worldspawn. This saves a large "
			"amount of clipnodes but breaks collision for entities that use the hulls.\n\n"
			"Some entities may fall outside of the world when the map starts. In most cases it's "
			"better to use the Redirect Hull option or let the Optimize command decide which"
			"hulls to redirect.\n");
		*/

		if (ImGui::MenuItem("Deduplicate Models", 0, false, !app->isLoading)) {
			g_app->updateEntityLumpUndoState(map);
			DeduplicateModelsCommand* command = new DeduplicateModelsCommand("Deduplicate models",
				app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}
		tooltip(g, "Scans for duplicated BSP models and updates entity model keys to reference only one model in set of duplicated models. "
			"This lowers the model count and allows more game models to be precached.\n\n"
			"This does not delete BSP data structures unless you run the Clean command afterward.");

		if (ImGui::BeginMenu("Delete OOB Data", !app->isLoading)) {

			static const char* optionNames[10] = {
				"All Axes",
				"X Axis",
				"X Axis (positive only)",
				"X Axis (negative only)",
				"Y Axis",
				"Y Axis (positive only)",
				"Y Axis (negative only)",
				"Z Axis",
				"Z Axis (positive only)",
				"Z Axis (negative only)",
			};

			static int clipFlags[10] = {
				0xffffffff,
				OOB_CLIP_X | OOB_CLIP_X_NEG,
				OOB_CLIP_X,
				OOB_CLIP_X_NEG,
				OOB_CLIP_Y | OOB_CLIP_Y_NEG,
				OOB_CLIP_Y,
				OOB_CLIP_Y_NEG,
				OOB_CLIP_Z | OOB_CLIP_Z_NEG,
				OOB_CLIP_Z,
				OOB_CLIP_Z_NEG,
			};

			for (int i = 0; i < 10; i++) {
				if (ImGui::MenuItem(optionNames[i], 0, false, !app->isLoading)) {
					if (map->ents[0]->hasKey("origin")) {
						vec3 ori = map->ents[0]->getOrigin();
						logf("Moved worldspawn origin by %f %f %f\n", ori.x, ori.y, ori.z);
						map->move(ori);
						map->ents[0]->removeKeyvalue("origin");

					}

					g_app->updateEntityLumpUndoState(map);
					DeleteOobDataCommand* command = new DeleteOobDataCommand("Delete OOB Data",
						clipFlags[i], app->undoLumpState);
					g_app->saveLumpState(map, 0xffffffff, false);
					command->execute();
					app->pushUndoCommand(command);
				}
				tooltip(g, "Deletes out-of-bounds BSP data and entities.\n\n"
					"This is useful for splitting maps to run in an engine with stricter map limits.");
			}

			ImGui::EndMenu();
		}
		tooltip(g, "Deletes out-of-bounds BSP data and entities.\n\n"
			"This is useful for splitting maps to run in an engine with stricter map limits.");

		if (ImGui::MenuItem("Delete Boxed Data", 0, false, !app->isLoading)) {
			if (!g_app->hasCullbox) {
				logf("Create at least 2 entities with \"cull\" as a classname first!\n");
			}
			else {
				g_app->updateEntityLumpUndoState(map);
				DeleteBoxedDataCommand* command = new DeleteBoxedDataCommand("Delete Boxed Data",
					g_app->cullMins, g_app->cullMaxs, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
		}
		tooltip(g, "Deletes BSP data and entities inside of a box defined by 2 \"cull\" entities "
			"(for the min and max extent of the box). This is useful for getting maps to run in an "
			"engine with stricter map limits. Works best with enclosed areas. Trying to partially "
			"delete features in a room will likely result in broken collision detection.\n\n"
			"Create 2 cull entities to define the culling box. "
			"A transparent red box will form between them.");

		if (ImGui::MenuItem("Remove unused WADs", 0, false, !app->isLoading)) {
			map->remove_unused_wads(wads);
		}
		tooltip(g, "Removes unused WADs from the worldspawn 'wad' keyvalue.\n\nIn Half-Life, unused WADs cause crashes if they don't exist.\nIn Sven Co-op, missing WADs are ignored.\n");

		ImGui::Separator();

		ImGui::MenuItem("Porting", 0, false, false);
		if (ImGui::MenuItem("AllocBlock Reduction", 0, false, !app->isLoading)) {
			map->allocblock_reduction();

			BspRenderer* renderer = app->mapRenderer;
			if (renderer) {
				renderer->preRenderFaces();
				g_app->gui->refresh();
			}
		}
		tooltip(g, "Scales up textures on invisible models, if any exist. Manually increase texture scales or downscale large textures to reduce AllocBlocks further.\n");

		if (ImGui::MenuItem("Downscale Invalid Textures", 0, false, !app->isLoading)) {
			map->downscale_invalid_textures(wads);

			BspRenderer* renderer = app->mapRenderer;
			if (renderer) {
				renderer->preRenderFaces();
				g_app->gui->refresh();
				reloadLimits();
			}
		}
		tooltip(g, "Shrinks textures that exceed the max texture size and adjusts texture coordinates accordingly.\n\nIf a texture is stored in a WAD, it is first embedded into the BSP before being downscaled.\n");

		if (ImGui::BeginMenu("Fix Bad Surface Extents", !app->isLoading)) {
			if (ImGui::MenuItem("Shrink Textures (512)", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (512)",
					false, true, 512, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Downscales textures on bad faces to a max resolution of 512x512 pixels. "
				"This alone will likely not be enough to fix all faces with bad surface extents."
				"You may also have to apply the Subdivide or Scale methods.");

			if (ImGui::MenuItem("Shrink Textures (256)", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (256)",
					false, true, 256, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Downscales textures on bad faces to a max resolution of 256x256 pixels. "
				"This alone will likely not be enough to fix all faces with bad surface extents."
				"You may also have to apply the Subdivide or Scale methods.");

			if (ImGui::MenuItem("Shrink Textures (128)", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (128)",
					false, true, 128, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Downscales textures on bad faces to a max resolution of 128x128 pixels. "
				"This alone will likely not be enough to fix all faces with bad surface extents."
				"You may also have to apply the Subdivide or Scale methods.");

			if (ImGui::MenuItem("Shrink Textures (64)", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (64)",
					false, true, 64, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Downscales textures to a max resolution of 64x64 pixels. "
				"This alone will likely not be enough to fix all faces with bad surface extents."
				"You may also have to apply the Subdivide or Scale methods.");

			ImGui::Separator();

			if (ImGui::MenuItem("Scale", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Scale faces",
					true, false, 0, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Scales up face textures until they have valid extents. The drawback to this method is shifted texture coordinates and lower apparent texture quality.");

			if (ImGui::MenuItem("Subdivide", 0, false, !app->isLoading)) {
				FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Subdivide faces",
					false, false, 0, app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			tooltip(g, "Subdivides faces until they have valid extents. The drawback to this method is reduced in-game performace from higher poly counts.");

			ImGui::MenuItem("##", "WIP");
			tooltip(g, "Anything you choose here will break lightmaps. "
				"Run the map through a RAD compiler to fix, and pray that the mapper didn't "
				"customize compile settings much.");

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Zero Entity Origins", 0, false, !app->isLoading)) {
			int moveCount = 0;
			moveCount += map->zero_entity_origins("func_ladder");
			moveCount += map->zero_entity_origins("func_water"); // water is sometimes invisible after moving in sven
			moveCount += map->zero_entity_origins("func_mortar_field"); // mortars don't appear in sven

			BspRenderer* renderer = app->mapRenderer;

			if (moveCount) {
				if (renderer) {
					renderer->reload();
				}
				refresh();
			}
			else {
				logf("No entity origins need moving\n");
			}

			g_app->deselectObject();
		}
		tooltip(g, "Some entities break when their origin is non-zero (ladders, water, mortar fields).\nThis will move affected entity origins to (0,0,0), duplicating models if necessary.\n");

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Widgets"))
	{
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::MenuItem("Debug", NULL, showDebugWidget)) {
			showDebugWidget = !showDebugWidget;
			if (showDebugWidget)
				ImGui::SetWindowCollapsed("Debug", false);
		}
		tooltip(g, "For developers and those curious about BSP internals.");

		if (ImGui::MenuItem("Entity Report", "Ctrl+F")) {
			showEntityReport = !showEntityReport;
			if (showEntityReport)
				ImGui::SetWindowCollapsed("Entity Report", false);
		}
		tooltip(g, "Search for entities by name, class, and/or other properties.");

		if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", showKeyvalueWidget)) {
			showKeyvalueWidget = !showKeyvalueWidget;
			if (showKeyvalueWidget)
				ImGui::SetWindowCollapsed("Keyvalue Editor", false);
		}
		tooltip(g, "Edit entity properties.");

		if (ImGui::MenuItem("Map Limits", NULL, showLimitsWidget)) {
			showLimitsWidget = !showLimitsWidget;
			if (showLimitsWidget)
				ImGui::SetWindowCollapsed("Map Limits", false);
		}
		tooltip(g, "Shows how close the map is to exceeding engine limits.");

		if (ImGui::MenuItem("Transform", "Ctrl+M", showTransformWidget)) {
			showTransformWidget = !showTransformWidget;
			if (showTransformWidget)
				ImGui::SetWindowCollapsed("Transformation", false);
		}
		tooltip(g, "Move, rotate, and scale entities.");

		if (ImGui::MenuItem("Face Properties", "", showTextureWidget)) {
			showTextureWidget = !showTextureWidget;
			if (showTextureWidget)
				ImGui::SetWindowCollapsed("Face Properties", false);
		}
		tooltip(g, "Edit faces and textures.");
		/*
		if (ImGui::MenuItem("LightMap Editor (WIP)", "", showLightmapEditorWidget)) {
			showLightmapEditorWidget = !showLightmapEditorWidget;
			showLightmapEditorUpdate = true;
		}
		*/
		if (ImGui::MenuItem("Messages", "", showLogWidget)) {
			showLogWidget = !showLogWidget;
			if (showLogWidget)
				ImGui::SetWindowCollapsed("Messages", false);
		}
		tooltip(g, "Show program messages.");

		ImGui::Separator();

		ImGui::PopItemFlag();

		static string userLayout = getUserLayoutPath();

		if (ImGui::MenuItem("Save Widget Layout", NULL)) {
			ImGui::SaveIniSettingsToDisk(userLayout.c_str());
			app->getWindowSize(g_settings.autoload_layout_width, g_settings.autoload_layout_height);
			g_settings.save();
			logf("Layout saved to %s\n", userLayout.c_str());
		}
		tooltip(g, "Save the position and size of your widgets as they are now. For easing frustration when "
			"you accidentally resize the window while moving it and your right-aligned widgets move to the center of the screen.\n");

		if (ImGui::MenuItem("Load Widget Layout", NULL, false)) {
			if (!fileExists(userLayout)) {
				logf("No layout has been saved yet. Nothing to load.\n");
			}
			else {
				ImGui::LoadIniSettingsFromDisk(userLayout.c_str());
				ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
				logf("Layout loaded from %s\n", userLayout.c_str());
			}
		}
		tooltip(g, "Restore your previously saved layout. Widgets may move mostly off-screen if you saved "
			"at a larger window resolution than you're using now.\n");

		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::MenuItem("Auto-Load Layout", NULL, g_settings.autoload_layout)) {
			g_settings.autoload_layout = !g_settings.autoload_layout;
		}
		tooltip(g, "Automatically loads your saved widget layout whenever the window resizes to the same "
			" resolution you saved at.\n");

		ImGui::PopItemFlag();

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("View help")) {
			showHelpWidget = true;
		}
		if (ImGui::MenuItem("About")) {
			showAboutWidget = true;
		}
		ImGui::EndMenu();
	}


	string fpsText = to_string((int)ImGui::GetIO().Framerate) + " FPS";
	float fpsWidth = smallFont->CalcTextSizeA(fontSize * g_smallFontSizeMult, FLT_MAX, FLT_MAX, fpsText.c_str()).x;
	float rightAlignStart = ImGui::GetWindowWidth() - (fpsWidth + 20);

	ImGui::SameLine(rightAlignStart);
	ImGui::Text(fpsText.c_str());

	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("VSync", NULL, vsync)) {
			vsync = !vsync;
			glfwSwapInterval(vsync ? 1 : 0);
		}
		ImGui::EndPopup();
	}

	mainMenuBarHeight = ImGui::GetWindowHeight();

	ImGui::EndMainMenuBar();
}

void Gui::drawStatusBar() {
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	float height = mainMenuBarHeight; // Status bar height
	bool open = true; // Required for BeginViewportSideBar
	int flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

	float padding = 10.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, 0));

	if (ImGui::BeginViewportSideBar("##statusbar", viewport, ImGuiDir_Down, height, flags)) {
		string selectStr = "no selection";
		int entCount = app->pickInfo.ents.size();
		int faceCount = app->pickInfo.faces.size();
		if (entCount > 1) {
			selectStr = to_string(entCount) + " entities selected";
		}
		else if (entCount == 1) {

			string tname = app->pickInfo.getEnt()->getTargetname();
			string cname = app->pickInfo.getEnt()->getClassname();
			selectStr = tname.size() ? tname + " - " + cname : cname;
		}
		else if (faceCount == 1) {
			selectStr = "face #" + to_string(app->pickInfo.getFaceIndex()) + " selected";
		}
		else if (faceCount > 0) {
			selectStr = to_string(faceCount) + " faces selected";
		}
		
		static char cam_origin[32];
		static char cam_angles[32];
		static vec3 last_cam_origin = vec3(0.1f, 0, 0);
		static vec3 last_cam_angles = vec3(0.1f, 0, 0);
		float originWidth = smallFont->CalcTextSizeA(fontSize * g_smallFontSizeMult, FLT_MAX, FLT_MAX, cam_origin).x;
		float typicalOriginWidth = smallFont->CalcTextSizeA(fontSize * g_smallFontSizeMult, FLT_MAX, FLT_MAX, "-4096 -4096 -4096").x;
		originWidth = max(originWidth, typicalOriginWidth) + 10;
		float anglesWidth = smallFont->CalcTextSizeA(fontSize * g_smallFontSizeMult, FLT_MAX, FLT_MAX, cam_angles).x;
		float typicalAnglesWidth = smallFont->CalcTextSizeA(fontSize * g_smallFontSizeMult, FLT_MAX, FLT_MAX, "-90 -180 0").x;
		anglesWidth = max(anglesWidth, typicalAnglesWidth) + 10;
		
		float selectWidth = smallFont->CalcTextSizeA(fontSize*g_smallFontSizeMult, FLT_MAX, FLT_MAX, selectStr.c_str()).x;
		
		float rightAlignStart = ImGui::GetWindowWidth() - (selectWidth + padding);

		ImGui::Text("Origin:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(originWidth);
		if (ImGui::InputText("##Origin", cam_origin, 32)) {
			app->cameraOrigin = parseVector(cam_origin);
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(5, 0));
		ImGui::SameLine();
		ImGui::Text("Angles:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(anglesWidth);
		if (ImGui::InputText("##Angles", cam_angles, 32)) {
			vec3 editorCamAngles = parseVector(cam_angles);
			editorCamAngles = vec3(-editorCamAngles.x, editorCamAngles.z, 90 - editorCamAngles.y);
			app->cameraAngles = editorCamAngles;
		}

		ImGui::SameLine(rightAlignStart);
		ImGui::Text(selectStr.c_str());

		if (last_cam_origin != app->cameraOrigin) {
			last_cam_origin = app->cameraOrigin;
			snprintf(cam_origin, 32, "%d %d %d", (int)last_cam_origin.x, (int)last_cam_origin.y, (int)last_cam_origin.z);
		}

		vec3 gameCamAngles = app->cameraAngles;
		gameCamAngles = vec3(-gameCamAngles.x, -(gameCamAngles.z-90), gameCamAngles.y);
		gameCamAngles.y = normalizeRangef(gameCamAngles.y, -180, 180);

		if (last_cam_angles != gameCamAngles) {
			last_cam_angles = gameCamAngles;
			snprintf(cam_angles, 32, "%d %d %d", (int)last_cam_angles.x, (int)last_cam_angles.y, (int)last_cam_angles.z);
		}
		ImGui::End();
	}

	ImGui::PopStyleVar();
}

void Gui::drawPopups() {
	if (!g_app->mergeResult.map && !g_app->mergeResult.overflow && g_app->mergeResult.fpath.size())
		ImGui::OpenPopup("Merge Overlap");

	if (ImGui::BeginPopupModal("Merge Overlap", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		Bsp* thismap = g_app->mapRenderer->map;
		string name = stripExt(basename(g_app->mergeResult.fpath));
		vec3 mergeMove = g_app->mergeResult.moveFixes;
		vec3 mergeMove2 = g_app->mergeResult.moveFixes2;

		ImGui::Text((thismap->name + " overlaps " + name + " and must be moved before merging.").c_str());
		ImGui::Dummy(ImVec2(0.0f, 20.0f));
		ImGui::Text(("How do you want to move " + thismap->name + "?").c_str());

		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - padding) * 0.33f;

		ImGui::Columns(3, 0, false);

		string xmove = "Move X +" + to_string((int)mergeMove.x);
		string ymove = "Move Y +" + to_string((int)mergeMove.y);
		string zmove = "Move Z +" + to_string((int)mergeMove.z);

		string xmove2 = "Move X " + to_string((int)mergeMove2.x);
		string ymove2 = "Move Y " + to_string((int)mergeMove2.y);
		string zmove2 = "Move Z " + to_string((int)mergeMove2.z);

		vec3 adjustment;
		if (ImGui::Button(xmove.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(mergeMove.x, 0, 0);
		}

		ImGui::NextColumn();
		if (ImGui::Button(ymove.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(0, mergeMove.y, 0);
		}

		ImGui::NextColumn();
		if (ImGui::Button(zmove.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(0, 0, mergeMove.z);
		}

		ImGui::NextColumn();
		if (ImGui::Button(xmove2.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(mergeMove2.x, 0, 0);
		}

		ImGui::NextColumn();
		if (ImGui::Button(ymove2.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(0, mergeMove2.y, 0);
		}

		ImGui::NextColumn();
		if (ImGui::Button(zmove2.c_str(), ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			adjustment = vec3(0, 0, mergeMove2.z);
		}

		if (adjustment != vec3()) {
			vec3 newOri = thismap->ents[0]->getOrigin() + adjustment;
			thismap->ents[0]->setOrAddKeyvalue("origin", newOri.toKeyvalueString());
			g_app->merge(g_app->mergeResult.fpath);
		}

		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::NextColumn();
		ImGui::NextColumn();
		if (ImGui::Button("Cancel", ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			g_app->mergeResult.fpath = "";
		}
		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	if (g_app->mergeResult.overflow && g_app->mergeResult.fpath.size()) {
		ImGui::OpenPopup("Merge Failed");
		loadedStats = false;
	}

	if (ImGui::BeginPopupModal("Merge Failed", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		string engineName = g_settings.engine == ENGINE_HALF_LIFE ? "Half-Life" : "Sven Co-op";
		ImGui::Text(("Merging the selected maps would overflow \"" + engineName + "\" engine limits.\n"
			"Optimize the maps or manually remove unused structures before trying again.").c_str());

		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::Separator();

		drawLimitsSummary(g_app->mergeResult.map, true);

		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - padding) * 0.33f;

		ImGui::Dummy(ImVec2(0.0f, 20.0f));
		ImGui::Columns(3, 0, false);

		ImGui::NextColumn();
		if (ImGui::Button("OK", ImVec2(inputWidth, 0))) {
			ImGui::CloseCurrentPopup();
			g_app->mergeResult.fpath = "";
			delete g_app->mergeResult.map;
			g_app->mergeResult.map = NULL;
			loadedStats = false;
		}
		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}
}

void Gui::drawToolbar() {
	ImVec2 window_pos = ImVec2(10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("toolbar", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = (fontSize / 22.0f) * 32;
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec4 testColor = ImVec4(1, 0, 0, 1);
		selectColor.x *= selectColor.w;
		selectColor.y *= selectColor.w;
		selectColor.z *= selectColor.w;
		selectColor.w = 1;

		dimColor.x *= dimColor.w;
		dimColor.y *= dimColor.w;
		dimColor.z *= dimColor.w;
		dimColor.w = 1;

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_OBJECT ? selectColor : dimColor);
		if (ImGui::ImageButton("objpickicon", (ImTextureID)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1))) {
			app->deselectFaces();
			app->deselectObject();
			app->pickMode = PICK_OBJECT;
			showTextureWidget = false;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Object selection mode");
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE ? selectColor : dimColor);
		ImGui::SameLine();
		if (ImGui::ImageButton("facepickicon", (ImTextureID)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1))) {
			
			int modelIdx = app->pickInfo.getModelIndex();
			BSPMODEL* model = app->pickInfo.getModel();
			Bsp* map = app->pickInfo.getMap();
			BspRenderer* mapRenderer = app->mapRenderer;
			app->deselectObject();

			// don't select all worldspawn faces because it lags the program
			if (modelIdx > 0 && model) {
				for (int i = 0; i < model->nFaces; i++) {
					int faceIdx = model->iFirstFace + i;
					mapRenderer->highlightFace(faceIdx, true);
					app->pickInfo.selectFace(faceIdx);
				}
			}
			
			app->pickMode = PICK_FACE;
			app->pickCount++; // force texture tool refresh
			showTextureWidget = true;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Face selection mode");
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void Gui::drawStatusMessage() {
	static int windowWidth = 32;
	static int loadingWindowWidth = 32;
	static int loadingWindowHeight = 32;

	Entity* ent = app->pickInfo.getEnt();
	bool angleKey = ent && ent->hasKey("angle");
	bool sharedStructs = app->modelUsesSharedStructures && app->pickInfo.ents.size() == 1;
	bool concave = !app->isTransformableSolid && app->pickInfo.ents.size() == 1;
	bool invalidsolid = app->invalidSolid && app->pickInfo.ents.size() == 1;
	bool dutchAngle = app->cameraAngles.y != 0;
	bool showStatus = sharedStructs || concave || invalidsolid || badSurfaceExtents
		|| lightmapTooLarge || app->modelUsesSharedStructures || app->forceAngleRotation
		|| dutchAngle || angleKey;
	
	if (showStatus) {
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2, app->windowHeight - (10.0f+mainMenuBarHeight));
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (sharedStructs) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "SHARED DATA");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Model shares planes/clipnodes with other models.\n\nDuplicate the model to enable model editing.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (concave) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONCAVE SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Scaling and vertex manipulation don't work with concave solids yet\n";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (invalidsolid) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INVALID SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"The selected solid is not convex or has non-planar faces.\n\n"
						"Transformations will be reverted unless you fix this.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (badSurfaceExtents) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "BAD SURFACE EXTENTS");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels on some axis.\n\n"
						"This will crash the game. Increase texture scale or subdivide faces to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (lightmapTooLarge) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "LIGHTMAP TOO LARGE");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels.\n\n"
						"This will crash the game. Increase texture scale or subdivide faces to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (app->forceAngleRotation) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "FORCE ROTATE");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("The \"Force Rotate\" option is enabled in the Transformation widget.\n"
						"Many entities may be floating in space while this is enabled.");
				}
			}
			if (dutchAngle) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DUTCH ANGLE");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Your camera is tilted by the Z angle set in the bottom status bar.");
				}
			}
			if (angleKey) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "ANGLE KEY");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("The selected entity has an \"angle\" keyvalue set.\nThis key has special logic which overrides the \"angles\" keyvalue.");
				}
			}
			windowWidth = ImGui::GetWindowWidth();
		}
		ImGui::End();
	}

	if (app->isLoading) {
		ImVec2 window_pos = ImVec2((app->windowWidth - loadingWindowWidth) / 2,
			(app->windowHeight - loadingWindowHeight) / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

		if (ImGui::Begin("loader", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			static float lastTick = clock();
			static int loadTick = 0;

			if (float(clock() - lastTick) / (float)CLOCKS_PER_SEC > 0.05f) {
				loadTick = (loadTick + 1) % 8;
				lastTick = clock();
			}

			ImGui::PushFont(consoleFontLarge);
			switch (loadTick) {
			default:
			case 0: ImGui::Text("Loading |"); break;
			case 1: ImGui::Text("Loading /"); break;
			case 2: ImGui::Text("Loading -"); break;
			case 3: ImGui::Text("Loading \\"); break;
			case 4: ImGui::Text("Loading |"); break;
			case 5: ImGui::Text("Loading /"); break;
			case 6: ImGui::Text("Loading -"); break;
			case 7: ImGui::Text("Loading \\"); break;
			}
			ImGui::PopFont();

		}
		loadingWindowWidth = ImGui::GetWindowWidth();
		loadingWindowHeight = ImGui::GetWindowHeight();

		ImGui::End();
	}
}

void Gui::drawDebugWidget() {
	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, app->windowHeight));
	if (ImGui::Begin("Debug info", &showDebugWidget, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (app->pickInfo.getMap()) {
			Bsp* map = app->pickInfo.getMap();
			int modelIndex = app->pickInfo.getModelIndex();
			if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Entity ID: %d", modelIndex);

				if (modelIndex > 0) {
					ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
					ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

					ImGui::Checkbox("Debug nodes", &app->debugNodes);
					ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
				}

				if (app->pickInfo.getFaceIndex() != -1) {
					BSPMODEL& model = map->models[modelIndex];
					BSPFACE& face = *app->pickInfo.getFace();

					ImGui::Text("Model ID: %d", modelIndex);
					ImGui::Text("Model polies: %d", model.nFaces);

					ImGui::Text("Face ID: %d", app->pickInfo.getFaceIndex());
					ImGui::Text("Plane ID: %d", face.iPlane);

					if (face.iTextureInfo < map->texinfoCount) {
						BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
						int32_t texOffset = ((int32_t*)map->textures)[info.iMiptex + 1];
						BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
						ImGui::Text("Texinfo ID: %d", face.iTextureInfo);
						ImGui::Text("Texture ID: %d", info.iMiptex);
						ImGui::Text("Texture: %s (%dx%d)", tex.szName, tex.nWidth, tex.nHeight);
					}
					ImGui::Text("Lightmap Offset: %d", face.nLightmapOffset);
					ImGui::Text("Light Styles: [%d, %d, %d, %d]", face.nStyles[0], face.nStyles[1], face.nStyles[2], face.nStyles[3]);

					static int lastFaceIdx = -1;
					static string leafList;
					static int leafPick = 0;

					if (app->pickInfo.getFaceIndex() != lastFaceIdx) {
						lastFaceIdx = app->pickInfo.getFaceIndex();
						leafList = "";
						leafPick = -1;
						for (int i = 1; i < map->leafCount; i++) {
							BSPLEAF& leaf = map->leaves[i];
							for (int k = 0; k < leaf.nMarkSurfaces; k++) {
								if (map->marksurfs[leaf.iFirstMarkSurface + k] == app->pickInfo.getFaceIndex()) {
									leafList += " " + to_string(i);
									leafPick = i;
								}
							}
						}
					}

					const char* isVis = map->is_leaf_visible(leafPick, app->cameraOrigin) ? " (visible!)" : "";

					ImGui::Text("Leaf IDs:%s%s", leafList.c_str(), isVis);
				}
			}

			string bspTreeTitle = "BSP Tree";
			if (modelIndex >= 0) {
				bspTreeTitle += " (Model " + to_string(modelIndex) + ")";
			}
			if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

				if (app->pickInfo.getMap() && modelIndex >= 0 && modelIndex < app->pickInfo.getMap()->modelCount) {
					Bsp* map = app->pickInfo.getMap();

					vec3 localCamera = app->cameraOrigin - app->mapRenderer->mapOffset;

					static ImVec4 hullColors[] = {
						ImVec4(1, 1, 1, 1),
						ImVec4(0.3, 1, 1, 1),
						ImVec4(1, 0.3, 1, 1),
						ImVec4(1, 1, 0.3, 1),
					};

					for (int i = 0; i < MAX_MAP_HULLS; i++) {
						vector<int> nodeBranch;
						int leafIdx;
						int childIdx = -1;
						int headNode = map->models[modelIndex].iHeadnodes[i];
						int contents = map->pointContents(headNode, localCamera, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text("Contents: %s", map->getLeafContentsName(contents));
							if (i == 0) {
								ImGui::Text("Leaf: %d", leafIdx);
							}
							else if (i == NAV_HULL && g_app->debugLeafNavMesh) {
								int leafNavIdx = app->debugLeafNavMesh->getNodeIdx(map, localCamera);

								ImGui::Text("Nav ID: %d", leafNavIdx);
							}
							ImGui::Text("Parent Node: %d (child %d)",
								nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode,
								childIdx);
							ImGui::Text("Head Node: %d", headNode);
							ImGui::Text("Depth: %d", nodeBranch.size());

							ImGui::Unindent();
							ImGui::TreePop();
						}
						ImGui::PopStyleColor();
					}
				}
				else {
					ImGui::Text("No model selected");
				}

			}
		}
		else {
			ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen);
		}

		if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("DebugVec0 %6.2f %6.2f %6.2f", app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
			ImGui::Text("DebugVec1 %6.2f %6.2f %6.2f", app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text("DebugVec2 %6.2f %6.2f %6.2f", app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text("DebugVec3 %6.2f %6.2f %6.2f", app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			float mb = app->undoMemoryUsage / (1024.0f * 1024.0f);
			ImGui::Text("Undo Memory Usage: %.2f MB\n", mb);
		}
	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor() {
	//ImGui::SetNextWindowBgAlpha(0.75f);

	static int selectedFgdIdx = -1;

	ImGui::SetNextWindowSize(ImVec2(610, 610), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin("Keyvalue Editor", &showKeyvalueWidget, 0)) {
		if (app->pickInfo.ents.size() > 0) {
			Bsp* map = app->pickInfo.getMap();
			Entity* ent = app->pickInfo.getEnt();
			BSPMODEL& model = map->models[app->pickInfo.getModelIndex()];
			BSPFACE& face = *app->pickInfo.getFace();
			string cname = ent->getClassname();
			
			Fgd* fgd = selectedFgdIdx >= 0 && selectedFgdIdx < app->fgds.size() ? app->fgds[selectedFgdIdx] : NULL;
			FgdClass* fgdClass = fgd ? fgd->getFgdClass(cname) : NULL;

			if (!fgdClass) {
				for (int i = 0; i < app->fgds.size(); i++) {
					if (app->fgds[i]->getFgdClass(cname)) {
						fgd = app->fgds[i];
						fgdClass = app->mergedFgd->getFgdClass(cname);
						break;
					}
				}
			}

			bool sameClassesSelected = true;
			vector<Entity*> pickEnts = app->pickInfo.getEnts();
			for (Entity* ent : pickEnts) {
				if (ent->getClassname() != cname) {
					sameClassesSelected = false;
					break;
				}
			}

			ImGui::Columns(2, "smartcolumns", false);
			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Class:");
			ImGui::SameLine();
			if (cname == "worldspawn") {
				ImGui::Text(cname.c_str());
			}
			else if (ImGui::Button(sameClassesSelected ? (" " + cname + " ").c_str() : "<multiple>")) {
				ImGui::OpenPopup("classname_popup");
			}
			ImGui::PopFont();

			if (fgdClass != NULL) {
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip((fgdClass->description).c_str());
				}
			}

			if (fgd) {
				ImGui::NextColumn();
				ImGui::PushFont(largeFont);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("FGD:");
				if (ImGui::IsItemHovered()) {
					ImGui::PopFont();
					ImGui::SetTooltip("The Game Definition File determines which keyvalues/flags to display.\n\n"
						"This will change automatically if an entity definition isn't found\n"
						"in the FGD you previously selected.\n");
					ImGui::PushFont(largeFont);
				}
				ImGui::SameLine();
				if (ImGui::Button((" " + fgd->name + " ").c_str()))
					ImGui::OpenPopup("fgd_popup");
				ImGui::PopFont();
			}
			ImGui::Columns(1);

			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text("Change Class");
				ImGui::Separator();

				vector<FgdGroup>* targetGroup = &app->mergedFgd->pointEntGroups;
				if (ent->getBspModelIdx() != -1) {
					targetGroup = &app->mergedFgd->solidEntGroups;
				}

				for (int i = 0; i < targetGroup->size(); i++) {
					FgdGroup& group = targetGroup->at(i);

					if (ImGui::BeginMenu(group.groupName.c_str())) {
						for (int k = 0; k < group.classes.size(); k++) {
							if (ImGui::MenuItem(group.classes[k]->name.c_str())) {
								for (Entity* ent : pickEnts) {
									ent->setOrAddKeyvalue("classname", group.classes[k]->name);
									app->mapRenderer->refreshEnt(app->pickInfo.getEntIndex());
								}
								app->pushEntityUndoState("Change Class");
								entityReportFilterNeeded = true;
							}
						}

						ImGui::EndMenu();
					}
				}

				ImGui::EndPopup();
			}

			if (ImGui::BeginPopup("fgd_popup"))
			{
				ImGui::Text("Change FGD");
				ImGui::Separator();
				for (int k = 0; k < app->fgds.size(); k++) {
					Fgd* menuFgd = app->fgds[k];
					bool canSelect = menuFgd->getFgdClass(cname) != NULL;
					if (ImGui::MenuItem(menuFgd->name.c_str(), "", k == selectedFgdIdx, canSelect)) {
						selectedFgdIdx = k;
					}
				}

				ImGui::EndPopup();
			}

			ImGui::Dummy(ImVec2(0, 10));

			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Attributes")) {
					ImGui::Dummy(ImVec2(0, 10));
					if (!sameClassesSelected) {
						ImGui::Text("Multiple entity classes selected.");
						ImGui::Text("Use the Raw Edit tab instead.");
					}
					else {
						drawKeyvalueEditor_SmartEditTab(fgd);
					}
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Flags")) {
					ImGui::Dummy(ImVec2(0, 10));
					if (!sameClassesSelected) {
						ImGui::Text("Multiple entity classes selected.");
						ImGui::Text("Use the Raw Edit tab instead.");
					}
					else {
						drawKeyvalueEditor_FlagsTab(fgd);
					}
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Raw Edit")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab();
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();

		}
		else {
			if (app->pickInfo.ents.size() > 1)
				ImGui::Text("Multiple entities selected");
			else if (!app->pickInfo.getEnt())
				ImGui::Text("No entity selected");
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(Fgd* fgd) {
	Entity* ent = g_app->pickInfo.getEnt();
	if (!fgd) {
		ImGui::Text("No entity definition found for %s.", ent->getClassname().c_str());
		return;
	}
	if (app->fgds.empty()) {
		ImGui::Text("No FGD files loaded");
		ImGui::Text("Add an FGD in Settings or use the Raw Edit tab instead.");
		return;
	}

	string cname = ent->getClassname();
	string lowerClass = toLowerCase(cname);
	FgdClass* fgdClass = fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild("SmartEditWindow");

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	static int lastPickCount = 0;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	if (fgdClass != NULL) {
		struct KeyGroup {
			string name;
			vector<KeyvalueDef> keys;
			COLOR3 color;

			static uint32_t hash(const char* str) {
				uint64 hash = 14695981039346656037ULL;
				uint32_t c;

				while ((c = *str++)) {
					hash = (hash * 1099511628211) ^ c;
				}

				return hash;
			}
		};

		static vector<KeyGroup> groups;

		groups.clear();
		string currentGroup;
		KeyGroup tempGroup;

		for (int i = 0; i < fgdClass->keyvalues.size() && i < MAX_KEYS_PER_ENT; i++) {
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			string key = keyvalue.name;
			if (key == "spawnflags") {
				continue;
			}
			string niceName = keyvalue.description;

			if (currentGroup != keyvalue.fgdSource) {
				if (i != 0) {
					tempGroup.name;
					groups.push_back(tempGroup);
				}

				currentGroup = keyvalue.fgdSource;
				tempGroup.name = currentGroup;
				tempGroup.keys.clear();
				tempGroup.color = keyvalue.color;
			}
			tempGroup.keys.push_back(keyvalue);
		}
		if (tempGroup.keys.size())
			groups.push_back(tempGroup);

		int keyOffset = 0;
		for (int k = 0; k < groups.size(); k++) {
			COLOR3 c2 = groups[k].color;
			ImVec4 c = ImVec4(c2.r / 255.0f, c2.g / 255.0f, c2.b / 255.0f, 1.0f);

			bool isSelf = toLowerCase(groups[k].name) == lowerClass;

			if (groups[k].keys.size() > 3 && !isSelf) {
				ImGui::Columns(1);

				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(c.x, c.y, c.z, 0.3f)); // Set background color (blue)
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(c.x, c.y, c.z, 0.4f)); // Set hovered color (lighter blue)
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(c.x, c.y, c.z, 0.2f)); // Set active color (darker blue)

				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(c.x, c.y, c.z, 0.05f));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f));  // Set InputText background to fully opaque (white)
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f)); // Hovered state
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f));

				static bool isExpanded = true;

				ImGui::BeginChild(("ChildArea" + groups[k].name).c_str(), ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY);
				string title = groups[k].name + " (" + to_string(groups[k].keys.size()) + ")";
				if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Dummy(ImVec2(0, 2));
					drawKeyvalueEditor_SmartEditTab_GroupKeys(groups[k].keys, inputWidth, true, keyOffset);
					ImGui::Dummy(ImVec2(0, 2));
				}

				ImGui::EndChild();
				//ImGui::PopStyleColor(3);
				ImGui::PopStyleColor(7);
			}
			else {
				drawKeyvalueEditor_SmartEditTab_GroupKeys(groups[k].keys, inputWidth, false, keyOffset);
			}
			//ImGui::PopStyleColor(3);

			keyOffset += groups[k].keys.size();
		}

		lastPickCount = app->pickCount;
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_SmartEditTab_GroupKeys(vector<KeyvalueDef>& keys, float inputWidth, bool isGrouped, int keyOffset) {
	static char keyNames[MAX_KEYS_PER_ENT][MAX_KEY_LEN];
	static char keyValues[MAX_KEYS_PER_ENT][MAX_VAL_LEN];

	struct InputData {
		int idx;
		string key;
		string defaultValue;
		bool matchingValues;
		BspRenderer* bspRenderer;
	};
	
	static InputData inputData[MAX_KEYS_PER_ENT];

	ImGui::Columns(2, "smartcolumns", false); // 4-ways, with border
	
	vector<Entity*> pickEnts = app->pickInfo.getEnts();

	for (int i = 0; i < keys.size(); i++) {
		KeyvalueDef& keyvalue = keys[i];
		string key = keyvalue.name;
		if (key == "spawnflags") {
			continue;
		}

		bool matchingValues = true;
		string matchValue = pickEnts[0]->getKeyvalue(key);
		for (Entity* ent : pickEnts) {
			if (ent->hasKey(key)) {
				if (matchValue != ent->getKeyvalue(key)) {
					matchingValues = false;
					break;
				}
			}
		}

		string value = matchingValues ? matchValue : "(no change)";
		string niceName = keyvalue.description;

		// TODO: ImGui doesn't have placeholder text like in HTML forms,
		// but it would be nice to show an example/default value here somehow.
		// Forcing the default value is bad because that can change entity behavior
		// in unexpected ways. The default should always be an empty string or 0 when
		// you don't care about the key. I think I remember there being strange problems
		// when JACK would autofill default values for every possible key in an entity.
		// 
		//if (value.empty() && keyvalue.defaultValue.length()) {
		//	value = keyvalue.defaultValue;
		//}

		int bufferIdx = keyOffset + i;
		strcpy(keyNames[bufferIdx], niceName.c_str());
		strcpy(keyValues[bufferIdx], value.c_str());

		inputData[bufferIdx].key = key;
		inputData[bufferIdx].defaultValue = keyvalue.defaultValue;
		inputData[bufferIdx].bspRenderer = app->mapRenderer;
		inputData[bufferIdx].matchingValues = matchingValues;
		inputData[bufferIdx].idx = bufferIdx;

		ImGui::SetNextItemWidth(inputWidth);
		ImGui::AlignTextToFramePadding();
		if (isGrouped) {
			ImGui::Dummy(ImVec2(20, 0));
			ImGui::SameLine();
		}
		ImGui::Text(keyNames[bufferIdx]);
		if (ImGui::IsItemHovered()) {
			//ImGui::SetTooltip((key + "(" + keyvalue.valueType + ") : " + niceName).c_str());
			ImGui::SetTooltip((key + " : " + niceName).c_str());
		}
		ImGui::NextColumn();

		ImGui::SetNextItemWidth(inputWidth);

		bool colorChanged = true;
		if (!matchingValues) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.5f, 0.0f, 0.7f));
		}
		else {
			colorChanged = false;
		}

		if (keyvalue.iType == FGD_KEY_CHOICES && keyvalue.choices.size() > 0) {
			string selectedValue = keyvalue.choices[0].name;
			int ikey = atoi(value.c_str());

			for (int k = 0; k < keyvalue.choices.size(); k++) {
				KeyvalueChoice& choice = keyvalue.choices[k];

				if ((choice.isInteger && ikey == choice.ivalue) ||
					(!choice.isInteger && value == choice.svalue)) {
					selectedValue = choice.name;
				}
			}

			if (ImGui::BeginCombo(("##val" + to_string(bufferIdx)).c_str(), selectedValue.c_str()))
			{
				for (int k = 0; k < keyvalue.choices.size(); k++) {
					KeyvalueChoice& choice = keyvalue.choices[k];
					bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);

					if (ImGui::Selectable(choice.name.c_str(), selected)) {
						for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
							int idx = g_app->pickInfo.ents[i];
							Entity* ent = g_app->pickInfo.getMap()->ents[idx];
							ent->setOrAddKeyvalue(key, choice.svalue);
							app->mapRenderer->refreshEnt(idx);
						}
						
						app->updateEntConnections();
						app->pushEntityUndoState("Edit Keyvalue");
					}
				}

				ImGui::EndCombo();
			}
		}
		else {
			struct InputChangeCallback {
				static int keyValueChanged(ImGuiInputTextCallbackData* data) {
					if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
						if (data->EventChar < 256) {
							if (strchr("-0123456789", (char)data->EventChar))
								return 0;
						}
						return 1;
					}

					InputData* dat = (InputData*)data->UserData;

					if (!dat->matchingValues) {
						keyValues[dat->idx][0] = 0; // clear the "(no change)" text
						data->Buf[0] = 0;
						data->BufTextLen = 0;
						data->BufDirty = true;
						data->CursorPos = 0;
					}
					
					for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
						int idx = g_app->pickInfo.ents[i];
						Entity* ent = g_app->pickInfo.getMap()->ents[idx];

						string newVal = data->Buf;
						if (newVal.empty()) {
							ent->removeKeyvalue(dat->key);
						}
						else {
							ent->setOrAddKeyvalue(dat->key, newVal);
						}
						dat->bspRenderer->refreshEnt(idx);
					}
					
					if (g_app->pickInfo.ents.size() < 100)
						g_app->updateEntConnections();
					g_app->forceRefreshTransformWindow = true;
					return 1;
				}
			};

			if (keyvalue.iType == FGD_KEY_INTEGER) {
				ImGui::InputText(("##val" + to_string(bufferIdx) + "_" + to_string(app->pickCount)).c_str(), keyValues[bufferIdx], 64,
					ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
					InputChangeCallback::keyValueChanged, &inputData[bufferIdx]);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					app->updateEntConnections();
				}
			}
			else {
				ImGui::InputText(("##val" + to_string(bufferIdx) + "_" + to_string(app->pickCount)).c_str(), keyValues[bufferIdx], MAX_VAL_LEN,
					ImGuiInputTextFlags_CallbackAlways, InputChangeCallback::keyValueChanged, &inputData[bufferIdx]);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					app->updateEntConnections();
				}
			}
		}
		if (ImGui::IsItemHovered() && ImGui::GetItemRectSize().x < ImGui::CalcTextSize(keyValues[bufferIdx]).x) {
			ImGui::SetTooltip(keyValues[bufferIdx]);
		}

		if (ImGui::IsItemHovered()) {
			if (!matchingValues) {
				ImGui::SetTooltip("This value differs between the selected entities");
			}
		}

		if (colorChanged) {
			ImGui::PopStyleColor(2);
		}

		ImGui::NextColumn();
	}
}

void Gui::drawKeyvalueEditor_FlagsTab(Fgd* fgd) {
	if (!fgd) {
		ImGui::Text("No FGD loaded.");
		ImGui::Text("Add an FGD in Settings or use the Raw Edit tab instead.");
		return;
	}

	ImGui::BeginChild("FlagsWindow");
	vector<Entity*> pickEnts = app->pickInfo.getEnts();

	uint combinedSpawnFlags = 0;
	for (Entity* ent : pickEnts) {
		combinedSpawnFlags |= strtoul(ent->getKeyvalue("spawnflags").c_str(), NULL, 10);
	}

	FgdClass* fgdClass = fgd->getFgdClass(pickEnts[0]->getClassname());

	ImGui::Columns(2, "keyvalcols", true);

	static bool checkboxEnabled[32];

	for (int i = 0; i < 32; i++) {
		if (i == 16) {
			ImGui::NextColumn();
		}
		string name;
		if (fgdClass != NULL) {
			name = fgdClass->spawnFlagNames[i];
		}

		bool matchingFlags = true;

		checkboxEnabled[i] = combinedSpawnFlags & (1 << i);

		bool flagsDiffer = false;
		for (Entity* ent : pickEnts) {
			uint spawnflags = strtoul(ent->getKeyvalue("spawnflags").c_str(), NULL, 10);
			if ((spawnflags & (1 << i)) != (combinedSpawnFlags & (1 << i))) {
				flagsDiffer = true;
				break;
			}
		}

		bool colorChanged = true;
		if (flagsDiffer) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.5f, 0.0f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 0.5f, 0.0f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
		}
		else {
			colorChanged = false;
		}

		if (ImGui::Checkbox((name + "##flag" + to_string(i)).c_str(), &checkboxEnabled[i])) {
			for (Entity* ent : pickEnts) {
				uint spawnflags = strtoul(ent->getKeyvalue("spawnflags").c_str(), NULL, 10);

				if (!checkboxEnabled[i]) {
					spawnflags &= ~(1U << i);
				}
				else {
					spawnflags |= (1U << i);
				}

				if (spawnflags != 0)
					ent->setOrAddKeyvalue("spawnflags", to_string(spawnflags));
				else
					ent->removeKeyvalue("spawnflags");
			}
			app->updateEntConnections();

			app->pushEntityUndoState(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag");
		}
		if (ImGui::IsItemHovered() && flagsDiffer) {
			ImGui::PopStyleColor(1);
			ImGui::SetTooltip("This flag is not enabled on all selected entities");
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
		}

		if (colorChanged) {
			ImGui::PopStyleColor(5);
		}
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab() {
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, "keyvalcols", false);

	float butColWidth = smallFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2) * 0.5f;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text("  Key"); ImGui::NextColumn();
	ImGui::Text("Value"); ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild("RawValuesWindow");

	ImGui::Columns(4, "keyvalcols2", false);

	textColWidth -= style.ScrollbarSize; // double space to prevent accidental deletes

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	static char keyNames[MAX_KEYS_PER_ENT][MAX_KEY_LEN];
	static char keyValues[MAX_KEYS_PER_ENT][MAX_VAL_LEN];

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - paddingx * 2) * 0.5f;

	unordered_set<string> addedKeys;
	static vector<string> combinedKeys;
	vector<string> oldCombinedKeys = combinedKeys;
	vector<Entity*> pickEnts = app->pickInfo.getEnts();
	combinedKeys.clear();
	bool multiedit = pickEnts.size() > 1;
	Bsp* map = app->pickInfo.getMap();
	bool fullRefreshNeeded = false;

	if (multiedit) {
		for (int i = 0; i < app->pickInfo.ents.size(); i++) {
			Entity* ent = map->ents[app->pickInfo.ents[i]];

			for (int k = 0; k < ent->keyOrder.size(); k++) {
				string key = ent->keyOrder[k];
				if (!addedKeys.count(key)) {
					addedKeys.insert(key);
					combinedKeys.push_back(key);
				}
			}
		}

		bool keysMoved = combinedKeys.size() != oldCombinedKeys.size();
		for (int i = 0; i < combinedKeys.size() && !keysMoved; i++) {
			if (combinedKeys[i] != oldCombinedKeys[i]) {
				keysMoved = true;
			}
		}
		fullRefreshNeeded = keysMoved;
	}
	else {
		combinedKeys = app->pickInfo.getEnt()->keyOrder;
	}

	struct InputData {
		int idx;
		bool matchingValues;
		string commonValue;
		BspRenderer* bspRenderer;
		Gui* gui;
	};

	struct TextChangeCallback {
		static int keyNameChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			string oldKey = combinedKeys[inputData->idx];
			combinedKeys[inputData->idx] = data->Buf;

			bool anyUpdate = false;
			bool modelUpdate = false;
			for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
				int entidx = g_app->pickInfo.ents[i];
				Entity* ent = g_app->pickInfo.getMap()->ents[entidx];

				if (!ent->hasKey(oldKey)) {
					ent->setOrAddKeyvalue(data->Buf, inputData->matchingValues ? inputData->commonValue : "");
				}
				else {
					if (!ent->renameKey(oldKey, data->Buf)) {
						continue;
					}
				}
				inputData->bspRenderer->refreshEnt(entidx);
				if (oldKey == "model" || string(data->Buf) == "model") {
					modelUpdate = true;
				}
				anyUpdate = true;
				inputData->gui->entityReportFilterNeeded = true;
			}

			if (modelUpdate) {
				inputData->bspRenderer->preRenderEnts();
				g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
			}
			if (anyUpdate && g_app->pickInfo.ents.size() < 100) {
				g_app->updateEntConnections();
			}
			g_app->forceRefreshTransformWindow = true;
			return 1;
		}

		static int keyValueChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			string key = combinedKeys[inputData->idx];

			if (key == data->Buf) {
				return 1;
			}

			if (!inputData->matchingValues) {
				keyValues[inputData->idx][0] = 0; // clear the "(no change)" text
				data->Buf[0] = 0;
				data->BufTextLen = 0;
				data->BufDirty = true;
				data->CursorPos = 0;
			}

			bool anyUpdate = false;
			bool modelUpdate = false;
			for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
				int entidx = g_app->pickInfo.ents[i];
				Entity* ent = g_app->pickInfo.getMap()->ents[entidx];

				if (!ent->hasKey(key) || ent->getKeyvalue(key) != data->Buf) {
					ent->setOrAddKeyvalue(key, data->Buf);
					inputData->bspRenderer->refreshEnt(entidx);
					
					if (key == "model") {
						modelUpdate = true;
					}
					anyUpdate = true;
					inputData->gui->entityReportFilterNeeded = true;
				}
			}

			if (modelUpdate) {
				inputData->bspRenderer->preRenderEnts();
				g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
			}
			if (anyUpdate && g_app->pickInfo.ents.size() < 100) {
				g_app->updateEntConnections();
			}
			g_app->forceRefreshTransformWindow = true;
			return 1;
		}
	};

	static InputData keyIds[MAX_KEYS_PER_ENT];
	static InputData valueIds[MAX_KEYS_PER_ENT];
	static int lastPickCount = -1;
	static string dragNames[MAX_KEYS_PER_ENT];
	static const char* dragIds[MAX_KEYS_PER_ENT];

	if (dragNames[0].empty()) {
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++) {
			string name = "::##drag" + to_string(i);
			dragNames[i] = name;
		}
	}

	if (lastPickCount != app->pickCount) {
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++) {
			dragIds[i] = dragNames[i].c_str();
		}
	}

	ImVec4 dragColor = style.Colors[ImGuiCol_FrameBg];
	dragColor.x *= 2;
	dragColor.y *= 2;
	dragColor.z *= 2;

	ImVec4 dragButColor = style.Colors[ImGuiCol_Header];

	static bool hoveredDrag[MAX_KEYS_PER_ENT];
	static int ignoreErrors = 0;

	static bool wasKeyDragging = false;
	bool keyDragging = false;

	if (fullRefreshNeeded) {
		ImGui::ClearActiveID();
	}

	float startY = 0;
	for (int i = 0; i < combinedKeys.size() && i < MAX_KEYS_PER_ENT; i++) {
		const char* item = dragIds[i];

		// drag buttons
		{
			ImGui::BeginDisabled(multiedit);
			style.SelectableTextAlign.x = 0.5f;
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Header, hoveredDrag[i] ? dragColor : dragButColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, dragColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, dragColor);
			ImGui::Selectable(item, true);
			ImGui::PopStyleColor(3);
			style.SelectableTextAlign.x = 0.0f;

			hoveredDrag[i] = ImGui::IsItemActive();
			if (hoveredDrag[i]) {
				keyDragging = true;
			}
			if (i == 0) {
				startY = ImGui::GetItemRectMin().y;
			}

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				Entity* ent = app->pickInfo.getEnt();
				int n_next = (ImGui::GetMousePos().y - startY) / (ImGui::GetItemRectSize().y + style.FramePadding.y * 2);
				if (n_next >= 0 && n_next < ent->keyOrder.size() && n_next < MAX_KEYS_PER_ENT)
				{
					dragIds[i] = dragIds[n_next];
					dragIds[n_next] = item;

					string temp = ent->keyOrder[i];
					ent->keyOrder[i] = ent->keyOrder[n_next];
					ent->keyOrder[n_next] = temp;

					// fix false-positive error highlight
					ignoreErrors = 2;

					ImGui::ResetMouseDragDelta();
				}
			}
			ImGui::EndDisabled();
			ImGui::NextColumn();
		}
		

		string key = combinedKeys[i];

		bool sharedKey = true;
		for (int k = 0; k < app->pickInfo.ents.size(); k++) {
			Entity* ent = map->ents[app->pickInfo.ents[k]];
			if (!ent->hasKey(key)) {
				sharedKey = false;
				break;
			}
		}
		
		bool matchingValues = true;
		string matchValue = pickEnts[0]->getKeyvalue(key);
		for (Entity* ent : pickEnts) {
			if (ent->hasKey(key)) {
				if (matchValue != ent->getKeyvalue(key)) {
					matchingValues = false;
					break;
				}
			}
		}

		string value = matchingValues ? matchValue : "(no change)";
		
		// key column
		{
			bool invalidKey = ignoreErrors == 0 && lastPickCount == app->pickCount && key != keyNames[i];

			strcpy(keyNames[i], key.c_str());

			keyIds[i].idx = i;
			keyIds[i].bspRenderer = app->mapRenderer;
			keyIds[i].gui = this;
			keyIds[i].matchingValues = matchingValues;
			keyIds[i].commonValue = matchValue;

			bool coloredKey = true;
			if (invalidKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (!sharedKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			else {
				coloredKey = false;
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##key" + to_string(i) + "_" + to_string(app->pickCount)).c_str(), keyNames[i], MAX_KEY_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyNameChanged, &keyIds[i]);
			
			if (ImGui::IsItemHovered()) {
				if (invalidKey) {
					ImGui::SetTooltip("Key already exists");
				}
				else if (!sharedKey) {
					ImGui::SetTooltip("This key does not exist in all selected entities");
				}
				else if (ImGui::GetItemRectSize().x - 50 < ImGui::CalcTextSize(keyNames[i]).x) {
					ImGui::SetTooltip(keyNames[i]);
				}
			}

			if (ImGui::IsItemDeactivatedAfterEdit()) {
				app->updateEntConnections();
			}

			if (coloredKey) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}

		// value column
		{
			strcpy(keyValues[i], value.c_str());

			valueIds[i].idx = i;
			valueIds[i].bspRenderer = app->mapRenderer;
			valueIds[i].gui = this;
			valueIds[i].matchingValues = matchingValues;
			valueIds[i].commonValue = matchValue;

			bool colorChanged = true;
			if (!matchingValues) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			else {
				colorChanged = false;
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + to_string(i) + to_string(app->pickCount)).c_str(), keyValues[i], MAX_VAL_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (ImGui::IsItemHovered()) {
				if (!matchingValues) {
					ImGui::SetTooltip("This value differs between the selected entities");
				}
				else if (ImGui::GetItemRectSize().x - 50 < ImGui::CalcTextSize(keyValues[i]).x) {
					ImGui::SetTooltip(keyValues[i]);
				}
			}
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				app->updateEntConnections();
			}

			if (colorChanged) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{

			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
			if (ImGui::Button((" X ##del" + to_string(i)).c_str())) {
				for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
					int entidx = g_app->pickInfo.ents[i];
					Entity* ent = g_app->pickInfo.getMap()->ents[entidx];
					ent->removeKeyvalue(key);
					app->mapRenderer->refreshEnt(entidx);
				}
				
				if (key == "model")
					app->mapRenderer->preRenderEnts();
				ignoreErrors = 2;
				g_app->updateEntConnections();
				g_app->pushEntityUndoState("Delete Keyvalue");
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

	if (!keyDragging && wasKeyDragging) {
		app->pushEntityUndoState("Move Keyvalue");
	}
	wasKeyDragging = keyDragging;
	lastPickCount = app->pickCount;

	ImGui::Columns(1);

	ImGui::Dummy(ImVec2(0, style.FramePadding.y));
	ImGui::Dummy(ImVec2(butColWidth, 0)); ImGui::SameLine();
	if (ImGui::Button(" Add ")) {
		for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
			int entidx = g_app->pickInfo.ents[i];
			Entity* ent = g_app->pickInfo.getMap()->ents[entidx];
			string baseKeyName = "NewKey";
			string keyName = "NewKey";
			for (int i = 0; i < 128; i++) {
				if (!ent->hasKey(keyName)) {
					break;
				}
				keyName = baseKeyName + "#" + to_string(i + 2);
			}
			ent->addKeyvalue(keyName, "");
		}
		
		app->mapRenderer->refreshEnt(app->pickInfo.getEntIndex());
		app->updateEntConnections();
		ignoreErrors = 2;
		app->pushEntityUndoState("Add Keyvalue");
	}

	if (ignoreErrors > 0) {
		ignoreErrors--;
	}

	ImGui::EndChild();
}

void Gui::drawTransformWidget() {
	bool transformingEnt = app->pickInfo.getEntIndex() >= 0;

	BspRenderer* bspRenderer = app->mapRenderer;
	Bsp* map = app->pickInfo.getMap();

	ImGui::SetNextWindowSize(ImVec2(430, 380), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(340, 140), ImVec2(FLT_MAX, app->windowHeight - 40));


	static int x, y, z; // grid snapped origin
	static float fx, fy, fz; // raw origin
	static float last_fx, last_fy, last_fz;
	static float sx, sy, sz; // scaling
	static float frx, fry, frz; // raw rotation
	static int rx, ry, rz; // grid snapped rotation

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static int oldSnappingEnabled = app->gridSnappingEnabled;
	static int oldTransformTarget = -1;
	static int oldMultiselect;
	static vector<vec3> multiselectOrigins; // reference point for multiselect transforms
	static vector<vec3> multiselectAngles; // reference point for multiselect transforms

	if (ImGui::Begin("Transformation", &showTransformWidget, 0)) {
		ImGuiStyle& style = ImGui::GetStyle();

		int multiSelect = app->pickInfo.ents.size();

		bool shouldUpdateUi = lastPickCount != app->pickCount ||
			app->draggingAxis != -1 ||
			app->movingEnt ||
			oldSnappingEnabled != app->gridSnappingEnabled ||
			lastVertPickCount != app->vertPickCount ||
			oldTransformTarget != app->transformTarget ||
			oldMultiselect != multiSelect ||
			g_app->forceRefreshTransformWindow;

		TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_SCALE ? &app->scaleAxes : &app->moveAxes);

		if (shouldUpdateUi) {
			if (transformingEnt) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					x = fx = last_fx = activeAxes.origin.x;
					y = fy = last_fy = activeAxes.origin.y;
					z = fz = last_fz = activeAxes.origin.z;
				}
				else {
					if (multiSelect > 1) {
						if (multiSelect != oldMultiselect || lastPickCount != app->pickCount || g_app->forceRefreshTransformWindow) {
							multiselectOrigins.clear();
							multiselectAngles.clear();
							for (int i = 0; i < app->pickInfo.ents.size(); i++) {
								Entity* ent = map->ents[app->pickInfo.ents[i]];
								vec3 ori = ent->getOrigin();
								vec3 angles = ent->getAngles();
								multiselectOrigins.push_back(ori);
								multiselectAngles.push_back(angles);
							}
							frx = rx = x = fx = 0;
							fry = ry = y = fy = 0;
							frz = rz = z = fz = 0;
						}
					}
					else {
						Entity* ent = app->pickInfo.getEnt();
						vec3 ori = ent->getOrigin();
						vec3 angles = ent->getAngles();
						if (app->originSelected) {
							ori = app->transformedOrigin;
						}
						x = fx = ori.x;
						y = fy = ori.y;
						z = fz = ori.z;
						frx = rx = angles.x;
						fry = ry = angles.y;
						frz = rz = angles.z;
					}
				}

			}
			else {
				frx = rx = x = fx = 0;
				fry = ry = y = fy = 0;
				frz = rz = z = fz = 0;
			}
			sx = sy = sz = 1;

			g_app->forceRefreshTransformWindow = false;
		}

		oldMultiselect = multiSelect;
		oldTransformTarget = app->transformTarget;
		oldSnappingEnabled = app->gridSnappingEnabled;
		lastVertPickCount = app->vertPickCount;
		lastPickCount = app->pickCount;

		bool scaled = false;
		bool originChanged = false;
		bool anglesChanged = false;
		guiHoverAxis = -1;

		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

		static bool inputsWereDragged = false;
		bool inputsAreDragging = false;
		bool canEditBspModel = app->pickInfo.getModel() && !app->modelUsesSharedStructures && app->isTransformableSolid;

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));
		if (ImGui::BeginTable("TransformTable", 5, ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed, 5);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGui::BeginDisabled(app->pickInfo.ents.empty());
			ImGui::Text("Move");
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##xpos", &x, 0.1f, 0, 0, "X: %d")) { originChanged = true; }
			}
			else {
				if (ImGui::DragFloat("##xpos2", &fx, 0.1f, 0, 0, "X: %.2f")) { originChanged = true; }
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##ypos", &y, 0.1f, 0, 0, "Y: %d")) { originChanged = true; }
			}
			else {
				if (ImGui::DragFloat("##ypos2", &fy, 0.1f, 0, 0, "Y: %.2f")) { originChanged = true; }
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##zpos", &z, 0.1f, 0, 0, "Z: %d")) { originChanged = true; }
			}
			else {
				if (ImGui::DragFloat("##zpos2", &fz, 0.1f, 0, 0, "Z: %.2f")) { originChanged = true; }
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::EndDisabled();

			ImGui::BeginDisabled(app->transformTarget != TRANSFORM_OBJECT || g_app->pickInfo.getEntIndex() == 0 || app->pickInfo.ents.empty());
			ImGui::Text("Rotate");
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##xrot", &rx, 0.1f, 0, 0, "X: %d")) {
					rx = rx > 0 ? (rx % 360) : -(-rx % 360);
					anglesChanged = true;
				}
			}
			else {
				if (ImGui::DragFloat("##xrot2", &frx, 0.1f, 0, 0, "X: %.2f")) {
					frx = normalizeRangef(frx, -360, 360);
					anglesChanged = true;
				}
			}
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##yrot", &ry, 0.1f, 0, 0, "Y: %d")) {
					ry = ry > 0 ? (ry % 360) : -(-ry % 360);
					anglesChanged = true;
				}
			}
			else {
				if (ImGui::DragFloat("##yrot2", &fry, 0.1f, 0, 0, "Y: %.2f")) {
					fry = normalizeRangef(fry, -360, 360);
					anglesChanged = true;
				}
			}
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();
			
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (app->gridSnappingEnabled) {
				if (ImGui::DragInt("##zrot", &rz, 0.1f, 0, 0, "Z: %d")) {
					rz = rz > 0 ? (rz % 360) : -(-rz % 360);
					anglesChanged = true;
				}
			}
			else {
				if (ImGui::DragFloat("##zrot2", &frz, 0.1f, 0, 0, "Z: %.2f")) {
					frz = normalizeRangef(frz, -360, 360);
					anglesChanged = true;
				}
			}
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::EndDisabled();

			ImGui::TableNextColumn();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGui::BeginDisabled(!canEditBspModel || app->transformTarget != TRANSFORM_OBJECT || app->pickInfo.ents.empty());
			ImGui::Text("Scale");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);

			if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f")) { scaled = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::EndDisabled();

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		//ImGui::Columns(1);
		//ImGui::PopStyleVar();

		if (inputsWereDragged && !inputsAreDragging) {
			app->pushEntityUndoState("Move Entity");

			if (transformingEnt) {
				app->applyTransform(true);

				if (app->gridSnappingEnabled) {
					fx = last_fx = x;
					fy = last_fy = y;
					fz = last_fz = z;
				}
				else {
					x = last_fx = fx;
					y = last_fy = fy;
					z = last_fz = fz;
				}
			}
		}

		ImGui::Dummy(ImVec2(0, style.FramePadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));

		const int grid_snap_modes = 11;
		const char* element_names[grid_snap_modes] = { "0", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
		static int current_element = app->gridSnapLevel + 1;

		ImGui::Columns(2, 0, false);
		ImGui::SetColumnWidth(0, inputWidth4);
		ImGui::SetColumnWidth(1, inputWidth4 * 3);
		ImGui::Text("Grid Snap:"); ImGui::NextColumn();
		ImGui::SetNextItemWidth(inputWidth4 * 3);
		if (ImGui::SliderInt("##gridsnap", &current_element, 0, grid_snap_modes - 1, element_names[current_element])) {
			app->gridSnapLevel = current_element - 1;
			app->gridSnappingEnabled = current_element != 0;
			originChanged = true;
		}
		ImGui::Columns(1);

		ImGui::Columns(4, 0, false);
		ImGui::SetColumnWidth(0, inputWidth4);
		ImGui::SetColumnWidth(1, inputWidth4);
		ImGui::SetColumnWidth(2, inputWidth4);
		ImGui::SetColumnWidth(3, inputWidth4);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Target: "); ImGui::NextColumn();

		ImGui::RadioButton("Entity", &app->transformTarget, TRANSFORM_OBJECT); ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Apply transformation to an entity origin and angles keyvalues.");
		}

		ImGui::BeginDisabled(!canEditBspModel);
		ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX); ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Apply transformation to BSP model vertices.");
		}
		
		ImGui::RadioButton("Origin", &app->transformTarget, TRANSFORM_ORIGIN); ImGui::NextColumn();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Apply transformation to a BSP model's origin (not the origin keyvalue).\nOrigins are used as a point of reference for rotation.");
		}

		ImGui::Text("3D Axes: "); ImGui::NextColumn();
		if (ImGui::RadioButton("Hide", &app->transformMode, TRANSFORM_NONE))
			app->showDragAxes = false;

		ImGui::NextColumn();
		if (ImGui::RadioButton("Move", &app->transformMode, TRANSFORM_MOVE))
			app->showDragAxes = true;

		ImGui::NextColumn();
		ImGui::BeginDisabled(!canEditBspModel || app->transformTarget != TRANSFORM_OBJECT);
		if (ImGui::RadioButton("Scale", &app->transformMode, TRANSFORM_SCALE))
			app->showDragAxes = true;
		ImGui::EndDisabled();
		ImGui::NextColumn();

		ImGui::Columns(2, "checkboxes", false);

		if (ImGui::Checkbox("Force Rotate", &app->forceAngleRotation)) {
			app->updateEntConnectionPositions();
		}
		ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Force solid entities to rotate by their angles keyvalue, even if they may not appear rotated in-game.\nPoint entities that don't use angles will show directional vectors.\n\nBy default, the program checks the entity class and FGDs to decide if an entity should appear rotated or display vectors.");
		}

		ImGui::PushItemWidth(inputWidth);
		ImGui::BeginDisabled(!canEditBspModel);
		ImGui::Checkbox("Texture lock", &app->textureLock);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Stretches/compresses textures to fit the object while scaling.\nDoes not work with angled faces.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopItemWidth();

		ImGui::Columns(1);

		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
		string w = to_string((int)app->selectionSize.x) + "w ";
		string h = to_string((int)app->selectionSize.y) + "h ";
		string l = to_string((int)app->selectionSize.z) + "l";
		ImGui::Text(("Size: " + w + h + l).c_str());

		if (app->pickInfo.getEntIndex() == 0 && map->ents[0]->getOrigin() != vec3()) {
			ImGui::SameLine();
			const char* butLabel = "Apply BSP Move";
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (ImGui::CalcTextSize(butLabel).x + ImGui::GetStyle().ItemSpacing.x + 30));
			if (ImGui::Button(butLabel)) {
				MoveMapCommand* command = new MoveMapCommand("Apply Worldspawn Transform",
					map->ents[0]->getOrigin(), app->undoLumpState);
				g_app->saveLumpState(map, 0xffffffff, false);
				command->execute();
				app->pushUndoCommand(command);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Moves all BSP data by the amount set in the worldspawn origin keyvalue.\n"
					"Useful for aligning maps before merging and moving areas inside the map boundaries.");
			}
		}

		if (transformingEnt) {
			if (originChanged) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					vec3 delta;
					if (app->gridSnappingEnabled) {
						delta = vec3(x - last_fx, y - last_fy, z - last_fz);
					}
					else {
						delta = vec3(fx - last_fx, fy - last_fy, fz - last_fz);
					}

					app->moveSelectedVerts(delta);
				}
				else if (app->transformTarget == TRANSFORM_OBJECT) {
					vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
					newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

					if (app->gridSnappingEnabled) {
						fx = x;
						fy = y;
						fz = z;
					}
					else {
						x = fx;
						y = fy;
						z = fz;
					}

					if (multiSelect > 1) {
						for (int i = 0; i < app->pickInfo.ents.size(); i++) {
							int entidx = app->pickInfo.ents[i];
							Entity* ent = map->ents[entidx];
							vec3 ori = multiselectOrigins[i] + newOrigin;
							ori = app->gridSnappingEnabled ? app->snapToGrid(ori) : ori;
							ent->setOrAddKeyvalue("origin", ori.toKeyvalueString(!app->gridSnappingEnabled));
							bspRenderer->refreshEnt(entidx);
						}
						app->updateEntConnectionPositions();
					}
					else {
						Entity* ent = app->pickInfo.getEnt();
						ent->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString(!app->gridSnappingEnabled));
						bspRenderer->refreshEnt(app->pickInfo.getEntIndex());
						app->updateEntConnectionPositions();
					}
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN) {
					vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
					newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

					app->transformedOrigin = newOrigin;
				}
			}
			if (scaled && app->pickInfo.getEnt()->isBspModel() && app->isTransformableSolid && !app->modelUsesSharedStructures) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					app->scaleSelectedVerts(sx, sy, sz);
				}
				else if (app->transformTarget == TRANSFORM_OBJECT) {
					int modelIdx = app->pickInfo.getModelIndex();
					app->scaleSelectedObject(sx, sy, sz);
					app->mapRenderer->refreshModel(app->pickInfo.getModelIndex());
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN) {
					logf("Scaling has no effect on origins\n");
				}
			}
			if (anglesChanged) {
				if (app->transformTarget == TRANSFORM_OBJECT) {
					vec3 newAngles = app->gridSnappingEnabled ? vec3(rx, ry, rz) : vec3(frx, fry, frz);

					if (multiSelect > 1) {
						for (int i = 0; i < app->pickInfo.ents.size(); i++) {
							int entidx = app->pickInfo.ents[i];
							Entity* ent = map->ents[entidx];
							vec3 angles = multiselectAngles[i] + newAngles;
							ent->setOrAddKeyvalue("angles", angles.toKeyvalueString(true));
							bspRenderer->refreshEnt(entidx);
						}
						app->updateEntConnectionPositions();
						app->updateEntDirectionVectors();
					}
					else {
						Entity* ent = app->pickInfo.getEnt();
						ent->setOrAddKeyvalue("angles", newAngles.toKeyvalueString(true));
						bspRenderer->refreshEnt(app->pickInfo.getEntIndex());
						if (ent->getBspModelIdx() != -1)
							app->updateEntConnectionPositions();
						else 
							app->updateEntDirectionVectors();
					}
				}
			}
		}

		inputsWereDragged = inputsAreDragging;
	}
	ImGui::End();
}

void Gui::clearLog()
{
	Buf.clear();
	LineOffsets.clear();
	LineOffsets.push_back(0);
}

void Gui::addLog(const char* s)
{
	int old_size = Buf.size();
	Buf.append(s);
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
		if (Buf[old_size] == '\n')
			LineOffsets.push_back(old_size + 1);
}

void Gui::loadFonts() {
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	static ImVector<ImWchar> ranges;
	static ImFontGlyphRangesBuilder builder;
	static const ImWchar allLatinRange[] = // covers all Latin languages
	{
		0x0001, 0x007F,    // Basic Latin
		0x0080, 0x00FF,    // Latin-1 Supplement
		0x0100, 0x017F,    // Latin Extended-A
		0x0180, 0x024F,    // Latin Extended-B
		0x0250, 0x02AF,    // IPA Extensions
		0x2C60, 0x2C7F,    // Latin Extended-C
		0xA720, 0xA7FF,    // Latin Extended-D
		0xAB30, 0xAB6F,    // Latin Extended-E
		0x1E00, 0x1EFF,    // Latin Additional
		0,
	};
	builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
	builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
	builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
	builder.AddRanges(&allLatinRange[0]);
	builder.BuildRanges(&ranges);
	

	static bool loggedAlready = true;
	if (!loggedAlready) {
		bool activeChars[IM_UNICODE_CODEPOINT_MAX + 1];
		memset(activeChars, 0, sizeof(activeChars));
		for (int i = 0; i < ranges.size() - 1; i += 2) {
			if (ranges[i + 1] == 0)
				break;
			for (int k = ranges[i]; k <= ranges[i + 1]; k++)
				activeChars[k] = true;
		}

		int totalUsed = 0;
		logf("selection = fontforge.activeFont().selection\n\n");
		logf("selection.select(");
		for (int i = 0; i <= IM_UNICODE_CODEPOINT_MAX; i++) {
			if (activeChars[i]) {
				logf("%d, ", i);
				totalUsed++;
			}
		}
		logf(")\n\n");
		logf("selection.invert()\n");

		logf("Using %d / %d glyphs\n", totalUsed, IM_UNICODE_CODEPOINT_MAX+1);
		loggedAlready = true;

		// To generate a unicode font that isn't 20 MB:
		// - download NotoSans, NotoSansJP, NotoSaKR, NotoSansSC (all medium weights)
		// - merge them all in FontForge with Element -> Merge (choose No for kerning dialog)
		// - File -> Generate fonts (in case you mess up the next part)
		// - copy this script output to FontFoge -> File -> Execute Script, then execute
		// - Encoding -> Detach and Remove glyphs.
		// - File -> generate fonts
	}

	vector<uint8_t> decompressed;
	// data copied to new array so that ImGui doesn't delete static data
	byte* smallFontData = NULL;
	int notosans_unicode_sz = 0;
	if (lzmaDecompress((uint8_t*)notosans_unicode, sizeof(notosans_unicode), decompressed)) {
		notosans_unicode_sz = decompressed.size();
		smallFontData = new byte[notosans_unicode_sz];
		memcpy(smallFontData, &decompressed[0], notosans_unicode_sz);
	}

	byte* largeFontData = NULL;
	int notosans_sz = 0;
	if (lzmaDecompress((uint8_t*)notosans, sizeof(notosans), decompressed)) {
		notosans_sz = decompressed.size();
		largeFontData = new byte[notosans_sz];
		memcpy(largeFontData, &decompressed[0], notosans_sz);
	}

	decompressed.clear();
	byte* consoleFontData = NULL;
	byte* consoleFontLargeData = NULL;
	int notosans_mono_sz = 0;
	if (lzmaDecompress((uint8_t*)notosans_mono, sizeof(notosans_mono), decompressed)) {
		notosans_mono_sz = decompressed.size();
		consoleFontData = new byte[notosans_mono_sz];
		consoleFontLargeData = new byte[notosans_mono_sz];
		memcpy(consoleFontData, &decompressed[0], notosans_mono_sz);
		memcpy(consoleFontLargeData, &decompressed[0], notosans_mono_sz);
	}

	// TODO: ImGui is getting updates to font scaling, so there won't be a need for a separate
	// largeFont, which I'm using now for quality reasons (font scaling breaks anti-aliasing
	// and makes the font look worse if scaled up). It will also improve startup time as
	// glyphs are loaded on-demand instead of 16k all at once!!! Should be ready early 2025.
	smallFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, notosans_unicode_sz, fontSize*g_smallFontSizeMult, NULL, ranges.Data);
	largeFont = io.Fonts->AddFontFromMemoryTTF((void*)largeFontData, notosans_sz, fontSize*1.25f, NULL, ranges.Data);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, notosans_mono_sz, fontSize, NULL, ranges.Data);
	consoleFontLarge = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontLargeData, notosans_mono_sz, fontSize*1.1f, NULL, ranges.Data);
}

void Gui::drawLog() {

	ImGui::SetNextWindowSize(ImVec2(750, 300), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	if (!ImGui::Begin("Messages", &showLogWidget))
	{
		ImGui::End();
		return;
	}

	g_log_mutex.lock();
	for (int i = 0; i < g_log_buffer.size(); i++) {
		addLog(g_log_buffer[i].c_str());
	}
	g_log_buffer.clear();
	g_log_mutex.unlock();

	static int i = 0;

	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
	bool toggledAutoScroll = false;
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem("Copy")) {
			copy = true;
		}
		if (ImGui::MenuItem("Clear")) {
			clearLog();
		}
		if (ImGui::MenuItem("Auto-scroll", NULL, &AutoScroll)) {
			toggledAutoScroll = true;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();

	if (copy) ImGui::LogBegin(ImGuiLogFlags_OutputClipboard, 0);

	ImGuiListClipper clipper;
	clipper.Begin(LineOffsets.Size);
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + LineOffsets[line_no];
			const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
			ImGui::TextUnformatted(line_start, line_end);
		}
	}
	clipper.End();

	if (copy) ImGui::LogFinish();

	ImGui::PopFont();
	ImGui::PopStyleVar();

	if (AutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() || toggledAutoScroll))
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::End();

}

void Gui::drawSettings() {

	ImGui::SetNextWindowSize(ImVec2(790, 350), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(740, 200), ImVec2(FLT_MAX, app->windowHeight - 40));

	if (ImGui::Begin("Editor Setup", &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 3;
		static const char* tab_titles[settings_tabs] = {
			"General",
			"Asset Paths",
			"FGDs",
		};

		// left
		ImGui::BeginChild("left pane", ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++) {
			if (ImGui::Selectable(tab_titles[i], settingsTab == i))
				settingsTab = i;
		}

		ImGui::EndChild();
		ImGui::SameLine();

		// right

		ImGui::BeginGroup();
		int footerHeight = settingsTab <= 2 ? ImGui::GetFrameHeightWithSpacing() + 4 : 0;
		ImGui::BeginChild("item view", ImVec2(0, -footerHeight)); // Leave room for 1 line below us
		ImGui::Text(tab_titles[settingsTab]);
		ImGui::Separator();

		static char gamedir[256];
		static char workingdir[256];
		static int numFgds = 0;
		static int numRes = 0;

		static std::vector<std::string> tmpFgdPaths;
		static std::vector<std::string> tmpResPaths;

		if (reloadSettings) {
			strncpy(gamedir, g_settings.gamedir.c_str(), 256);
			tmpFgdPaths = g_settings.fgdPaths;
			tmpResPaths = g_settings.resPaths;

			numFgds = tmpFgdPaths.size();
			numRes = tmpResPaths.size();

			reloadSettings = false;
		}

		int pathWidth = ImGui::GetWindowWidth() - 60;
		int delWidth = 50;

		ImGui::BeginChild("right pane content");
		if (settingsTab == 0) {
			ImGui::DragFloat("Movement Speed", &app->moveSpeed, 0.1f, 0.1f, 1000, "%.1f");
			ImGui::DragFloat("Rotation Speed", &app->rotationSpeed, 0.01f, 0.1f, 100, "%.1f");
			if (ImGui::DragInt("Font Size", &fontSize, 0.1f, 8, 48, "%d pixels")) {
				shouldReloadFonts = true;
			}
			ImGui::DragInt("Undo Levels", &app->undoLevels, 0.05f, 0, 64);
			ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
			ImGui::DragFloat("Back Clipping Plane", &app->zFar, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::DragFloat("Model Render Distance", &app->zFarMdl, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);

			ImGui::Columns(2);
			ImGui::Checkbox("Verbose Logging", &g_verbose);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("For troubleshooting problems with the program or specific commands");
			}
			ImGui::NextColumn();

			ImGui::Checkbox("Confirm Close", &g_settings.confirm_exit);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Show a warning dialog if closing the map without saving changes.\n");
			}
			ImGui::NextColumn();

			ImGui::Checkbox("Texture Filtering", &g_settings.texture_filtering);
			tooltip(g, "Rubs vaseline on your textures\n");
			ImGui::NextColumn();

			if (ImGui::Checkbox("VSync", &vsync)) {
				glfwSwapInterval(vsync ? 1 : 0);
			}
		}
		else if (settingsTab == 1) {
			ImGui::InputText("##GameDir", gamedir, 256, ImGuiInputTextFlags_ElideLeft);

			ImGui::SameLine();
			ImGui::Text("Game Directory");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Path to the folder holding your game executable (hl.exe, svencoop.exe)."
					"\nAsset Paths are relative to this folder.\n\nExample path:\n"
					"C:\\Steam\\steamapps\\common\\Half-Life\n\n"
					"This path isn't required. You can use absolute paths for Assets and FGDs if you want.");
			}
			ImGui::Dummy(ImVec2(0, 10));

			for (int i = 0; i < numRes; i++) {
				ImGui::SetNextItemWidth(pathWidth);
				tmpResPaths[i].resize(256);
				ImGui::InputText(("##res" + to_string(i)).c_str(), &tmpResPaths[i][0], 256, ImGuiInputTextFlags_ElideLeft);
				ImGui::SameLine();
				if (ImGui::IsItemHovered()) {
					string paths = tmpResPaths[i];
					if (!isAbsolutePath(tmpResPaths[i])) {
						paths = joinPaths(getAbsolutePath(""), tmpResPaths[i]);
						if (!string(gamedir).empty())
							paths += "\n" + joinPaths(gamedir, tmpResPaths[i]);
					}
					ImGui::SetTooltip(("This asset path adds the following search paths:\n" + paths).c_str());
				}

				ImGui::SameLine();
				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_res" + to_string(i)).c_str())) {
					tmpResPaths.erase(tmpResPaths.begin() + i);
					numRes--;
				}
				ImGui::PopStyleColor(3);

			}

			if (ImGui::Button("Add Asset Path")) {
				numRes++;
				tmpResPaths.push_back(std::string());
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Asset Paths are used to find textures and models.\n"
					"Filling this out will fix missing textures (pink and black checkerboards)\n\n"
					"You can use paths relative to your Game Directory or absolute paths."
					"\nFor example, you would add \"valve\" here for Half-Life.");
			}
		}
		else if (settingsTab == 2) {
			for (int i = 0; i < numFgds; i++) {
				ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 100);
				tmpFgdPaths[i].resize(256);

				bool isFound = !tmpFgdPaths[i].empty() && !findAsset(tmpFgdPaths[i]).empty();
				if (!isFound) {
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.0f, 0.0f, 0.5f));
					ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 0.0f, 0.0f, 0.7f));
				}

				ImGui::InputText(("##fgd" + to_string(i)).c_str(), &tmpFgdPaths[i][0], 256, ImGuiInputTextFlags_ElideLeft);

				if (ImGui::IsItemHovered() && !isFound) {
					if (isAbsolutePath(tmpFgdPaths[i])) {
						ImGui::SetTooltip("File not found.");
					}
					else {
						vector<string> paths = getAssetPaths();
						sort(paths.begin(), paths.end());
						string searched;
						for (int k = 0; k < paths.size(); k++) {
							string basePath = isAbsolutePath(paths[k]) ? paths[k] : getAbsolutePath(paths[k]);
							searched += "\n" + joinPaths(basePath, tmpFgdPaths[i]);
						}
						ImGui::SetTooltip(("File not found. The following paths were checked according "
							"to your configured Asset Paths:\n" + searched).c_str());
					}
				}
				if (!isFound)
					ImGui::PopStyleColor(2);	

				ImGui::SameLine();
				ImGui::SetNextItemWidth(delWidth);
				if (ImGui::Button((" ... ##open_fgd" + to_string(i)).c_str())) {
					char const* fgdFilterPatterns[2] = { "*.fgd" };
					char* fgd = tinyfd_openFileDialog("Open Game Definition File", "", 1, fgdFilterPatterns, "Game Definition File (*.fgd)", 1);
					if (fgd) {
						tmpFgdPaths[i] = string(fgd);
					}
				}

				ImGui::SameLine();
				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_fgd" + to_string(i)).c_str())) {
					tmpFgdPaths.erase(tmpFgdPaths.begin() + i);
					numFgds--;
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button("Add FGD Path")) {
				numFgds++;
				tmpFgdPaths.push_back(std::string());
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Add a path to a Game Definition File (.fgd)."
					"\n\nFGD files define entity configurations. Without FGDs you will see pink\n"
					"cubes and be unable to use the Attributes tab in the Keyvalue Editor.\n\n"
					"You can use paths relative to your Asset Paths or absolute paths."
					"\nFor example, you would add \"sven-coop.fgd\" here for Sven Co-op.");
			}
		}


		ImGui::EndChild();

		ImGui::EndChild();

		if (settingsTab <= 2) {
			ImGui::Separator();

			ImGui::BeginDisabled(app->isLoading);
			if (ImGui::Button("Apply Changes")) {
				g_settings.gamedir = string(gamedir);
				g_settings.fgdPaths = tmpFgdPaths;
				g_settings.resPaths = tmpResPaths;

				app->loadFgds();
				app->postLoadFgds();
				app->mapRenderer->reload();
				g_settings.save();
			}
			ImGui::EndDisabled();
		}

		ImGui::EndGroup();
	}
	ImGui::End();
}

void Gui::drawHelp() {
	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Help", &showHelpWidget)) {

		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem("UI Controls")) {
				ImGui::Dummy(ImVec2(0, 10));

				// user guide from the demo
				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("Click and drag on lower corner to resize window\n(double-click to auto fit window to its contents).");
				ImGui::BulletText("While adjusting numeric inputs:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/ALT for faster/slower edit.");
				ImGui::BulletText("Double-click or CTRL+click to input value.");
				ImGui::Unindent();
				ImGui::BulletText("While inputing text:\n");
				ImGui::Indent();
				ImGui::BulletText("CTRL+A or double-click to select all.");
				ImGui::BulletText("CTRL+X/C/V to use clipboard cut/copy/paste.");
				ImGui::BulletText("CTRL+Z,CTRL+Y to undo/redo.");
				ImGui::BulletText("You can apply arithmetic operators +,*,/ on numerical values.\nUse +- to subtract.");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("3D Controls")) {
				ImGui::Dummy(ImVec2(0, 10));

				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("WASD to move (hold SHIFT/CTRL for faster/slower movement).");
				ImGui::BulletText("Hold right mouse button to rotate view.");
				ImGui::BulletText("Left click to select objects/entities. Right click for options.");
				ImGui::BulletText("While grabbing an entity:\n");
				ImGui::Indent();
				ImGui::BulletText("Mouse wheel to push/pull (hold SHIFT/CTRL for faster/slower).");
				ImGui::BulletText("Click outside of the entity or press G to let go.");
				ImGui::Unindent();
				ImGui::BulletText("While grabbing 3D transform axes:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/CTRL for faster/slower adjustments");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Vertex Manipulation")) {
				ImGui::Dummy(ImVec2(0, 10));

				ImGuiIO& io = ImGui::GetIO();
				ImGui::BulletText("Press F to split a face while 2 edges are selected.");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void Gui::drawAbout() {
	ImGui::SetNextWindowSize(ImVec2(500, 140), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("About", &showAboutWidget)) {
		ImGui::InputText("Version", (char*)g_version_string, strlen(g_version_string), ImGuiInputTextFlags_ReadOnly);

		static char* author = "w00tguy";
		ImGui::InputText("Author", author, strlen(author), ImGuiInputTextFlags_ReadOnly);

		static char* url = "https://github.com/wootguy/bspguy";
		ImGui::InputText("Contact", url, strlen(url), ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::End();
}

void Gui::drawLimitsSummary(Bsp* map, bool modalMode) {
	if (!loadedStats) {
		stats.clear();
		stats.push_back(calcStat("AllocBlock", map->calc_allocblock_usage(), g_limits.max_allocblocks, false));
		stats.push_back(calcStat("clipnodes", map->clipnodeCount, g_limits.max_clipnodes, false));
		stats.push_back(calcStat("nodes", map->nodeCount, g_limits.max_nodes, false));
		stats.push_back(calcStat("leaves", map->leafCount, g_limits.max_leaves, false));
		stats.push_back(calcStat("models", map->modelCount, g_limits.max_models, false));
		stats.push_back(calcStat("faces", map->faceCount, g_limits.max_faces, false));
		stats.push_back(calcStat("texinfos", map->texinfoCount, g_limits.max_texinfos, false));
		stats.push_back(calcStat("textures", map->textureCount, g_limits.max_textures, false));
		stats.push_back(calcStat("planes", map->planeCount, g_limits.max_planes, false));
		stats.push_back(calcStat("vertexes", map->vertCount, g_limits.max_vertexes, false));
		stats.push_back(calcStat("edges", map->edgeCount, g_limits.max_edges, false));
		stats.push_back(calcStat("surfedges", map->surfedgeCount, g_limits.max_surfedges, false));
		stats.push_back(calcStat("marksurfaces", map->marksurfCount, g_limits.max_marksurfaces, false));
		stats.push_back(calcStat("lightstyles", map->lightstyle_count(), g_limits.max_lightstyles, false));
		stats.push_back(calcStat("lightdata", map->lightDataLength, g_limits.max_lightdata, true));
		stats.push_back(calcStat("entdata", map->header.lump[LUMP_ENTITIES].nLength, g_limits.max_entdata, true));
		stats.push_back(calcStat("visdata", map->visDataLength, g_limits.max_visdata, true));
		loadedStats = true;
	}

	if (!modalMode)
		ImGui::BeginChild("##content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFontLarge);

	int midWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
	int nameWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "marksurfaces").x;

	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 1.0f));
	if (ImGui::BeginTable("StatsTable", 3, ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("Data Type", ImGuiTableColumnFlags_WidthFixed, nameWidth);
		ImGui::TableSetupColumn(" Current / Max", ImGuiTableColumnFlags_WidthFixed, midWidth);
		ImGui::TableSetupColumn("Fullness", ImGuiTableColumnFlags_WidthStretch);
		
		ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
		ImGui::TableHeadersRow();
		ImGui::PopStyleColor(3);

		// manually create the bottom border
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 0.0f));
		ImGui::TableNextRow(ImGuiTableRowFlags_None, 1.0f);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Border));
		ImGui::PopStyleVar();

		for (int i = 0; i < stats.size(); i++) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(stats[i].color, stats[i].name.c_str());

			ImGui::TableNextColumn();
			string val = stats[i].val + " / " + stats[i].max;
			ImGui::TextColored(stats[i].color, val.c_str());

			ImGui::TableNextColumn();
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.4f, 0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			ImGui::ProgressBar(stats[i].progress, ImVec2(-1, 0), stats[i].fullness.c_str());
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(1);
			ImGui::TableNextColumn();
		}

		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	ImGui::PopFont();
	if (!modalMode)
		ImGui::EndChild();
}

void Gui::drawLimits() {
	ImGui::SetNextWindowSize(ImVec2(550, 630), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(450, 200), ImVec2(FLT_MAX, app->windowHeight - 40));

	Bsp* map = app->mapRenderer->map;
	string title = "Map Limits";

	if (ImGui::Begin((title + "###limits").c_str(), &showLimitsWidget)) {

		if (map == NULL) {
			ImGui::Text("No map selected");
		}
		else {
			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("All Datatypes")) {
					drawLimitsSummary(map, false);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Clipnodes")) {
					loadedStats = false;
					drawLimitTab(map, SORT_CLIPNODES);
					ImGui::EndTabItem();
				}

				/*
				if (ImGui::BeginTabItem("Nodes")) {
					loadedStats = false;
					drawLimitTab(map, SORT_NODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Faces")) {
					loadedStats = false;
					drawLimitTab(map, SORT_FACES);
					ImGui::EndTabItem();
				}
				*/

				if (ImGui::BeginTabItem("Vertices")) {
					loadedStats = false;
					drawLimitTab(map, SORT_VERTS);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("AllocBlock")) {
					loadedStats = false;
					drawAllocBlockLimitTab(map);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
	}

	ImGui::End();
}

void Gui::drawLimitTab(Bsp* map, int sortMode) {

	int maxCount;
	const char* countName;
	switch (sortMode) {
	case SORT_VERTS:		maxCount = map->vertCount; countName = "Vertexes";  break;
	case SORT_NODES:		maxCount = map->nodeCount; countName = "Nodes";  break;
	case SORT_CLIPNODES:	maxCount = map->clipnodeCount; countName = "Clipnodes";  break;
	case SORT_FACES:		maxCount = map->faceCount; countName = "Faces";  break;
	}

	if (!loadedLimit[sortMode]) {
		vector<STRUCTUSAGE*> modelInfos = map->get_sorted_model_infos(sortMode);

		limitModels[sortMode].clear();
		for (int i = 0; i < modelInfos.size(); i++) {

			int val;
			switch (sortMode) {
			case SORT_VERTS:		val = modelInfos[i]->sum.verts; break;
			case SORT_NODES:		val = modelInfos[i]->sum.nodes; break;
			case SORT_CLIPNODES:	val = modelInfos[i]->sum.clipnodes; break;
			case SORT_FACES:		val = modelInfos[i]->sum.faces; break;
			}

			ModelInfo stat = calcModelStat(map, modelInfos[i], val, maxCount, false);
			limitModels[sortMode].push_back(stat);
			delete modelInfos[i];
		}
		loadedLimit[sortMode] = true;
	}
	vector<ModelInfo>& modelInfos = limitModels[sortMode];

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFontLarge);

	int valWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Model ").x;
	int bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth);
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	ImGui::Text("Classname"); ImGui::NextColumn();
	ImGui::Text("Model"); ImGui::NextColumn();
	ImGui::Text(countName); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	int selected = app->pickInfo.getEntIndex();

	for (int i = 0; i < limitModels[sortMode].size(); i++) {

		if (modelInfos[i].val == "0") {
			break;
		}

		string cname = modelInfos[i].classname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(cname.c_str(), selected == modelInfos[i].entIdx, flags)) {
			selected = i;
			int entIdx = modelInfos[i].entIdx;
			if (entIdx < map->ents.size()) {
				Entity* ent = map->ents[entIdx];
				app->pickInfo.deselect();
				app->pickInfo.selectEnt(entIdx);
				app->postSelectEnt();
				// map should already be valid if limits are showing

				if (ImGui::IsMouseDoubleClicked(0)) {
					app->goToEnt(map, entIdx);
				}
			}
		}
		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].model.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].model.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].val.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].val.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].usage.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].usage.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}

bool sortAllocInfos(const AllocInfo& a, const AllocInfo& b) {
	return a.sort > b.sort;
}

void Gui::drawAllocBlockLimitTab(Bsp* map) {

	int maxCount;
	const int allocBlockSize = 128 * 128;

	if (!loadedLimit[SORT_ALLOCBLOCK]) {
		limitAllocs.clear();

		struct AllocInfoInt {
			int faceCount = 0;
			int val = 0;
			int faceIdx = -1;
		};

		unordered_map<string, AllocInfoInt> infos;

		for (int i = 0; i < map->faceCount; i++) {
			int size[2];
			GetFaceLightmapSize(map, i, size);

			BSPFACE& f = map->faces[i];
			BSPTEXTUREINFO& tinfo = map->texinfos[f.iTextureInfo];
			int32_t texOffset = ((int32_t*)map->textures)[tinfo.iMiptex + 1];
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

			string texname = tex.szName;
			AllocInfoInt& info = infos[texname];
			info.faceCount++;
			info.val += size[0] * size[1];
			info.faceIdx = i;
		}

		static char tmp[256];

		for (auto item : infos) {
			AllocInfo info;
			info.texname = item.first;
			info.faceCount = to_string(item.second.faceCount);
			
			sprintf(tmp, "%.1f", item.second.val / (float)allocBlockSize);
			info.val = tmp;

			sprintf(tmp, "%.1f", (item.second.val / (float)(64 * allocBlockSize))*100);
			info.usage = std::string(tmp) + "%%";
			
			info.sort = item.second.val;
			info.faceIdx = item.second.faceIdx;
			
			limitAllocs.push_back(info);
		}

		sort(limitAllocs.begin(), limitAllocs.end(), sortAllocInfos);

		loadedLimit[SORT_ALLOCBLOCK] = true;
	}
	vector<AllocInfo>& allocInfos = limitAllocs;

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFontLarge);

	int valWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, " Model ").x;
	int bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth);
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	ImGui::Text("Texture"); ImGui::NextColumn();
	ImGui::Text("Faces"); ImGui::NextColumn();
	ImGui::Text("Blocks"); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	int selected = app->pickInfo.getFaceIndex();

	for (int i = 0; i < limitAllocs.size(); i++) {

		if (limitAllocs[i].val == "0.0") {
			break;
		}

		string texname = limitAllocs[i].texname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(texname.c_str(), false, flags)) {
			selected = i;

			int faceIdx = limitAllocs[i].faceIdx;
			int modelIdx = 0;
			for (int i = 0; i < map->modelCount; i++) {
				BSPMODEL& model = map->models[i];
				if (model.iFirstFace <= faceIdx && model.iFirstFace + model.nFaces > faceIdx) {
					modelIdx = i;
					break;
				}
			}

			app->deselectFaces();
			app->pickInfo.selectFace(faceIdx);
			app->pickMode = PICK_FACE;
			showTextureWidget = true;
			app->pickCount++;
			refreshSelectedFaces = true;
			app->goToFace(map, faceIdx);
		}
		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].faceCount.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].faceCount.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].val.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].val.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitAllocs[i].usage.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitAllocs[i].usage.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}


void Gui::drawEntityReport() {
	ImGui::SetNextWindowSize(ImVec2(550, 630), ImGuiCond_FirstUseEver);

	struct ReportEnt {
		int idx;
		bool selected;
		bool hasFgd;
		string cname;
	};

	static vector<ReportEnt> filteredEnts;

	Bsp* map = app->mapRenderer->map;
	string title = "Entity Report  (" + to_string(filteredEnts.size()) + " results)";

	if (ImGui::Begin((title + "###entreport").c_str(), &showEntityReport)) {
		if (map == NULL) {
			ImGui::Text("No map selected");
		}
		else {
			ImGui::BeginGroup();

			const int MAX_FILTERS = 1;
			static char keyFilter[MAX_FILTERS][MAX_KEY_LEN];
			static char valueFilter[MAX_FILTERS][MAX_VAL_LEN];
			static int lastSelect = -1;
			static string classFilter = "(none)";
			static bool partialMatches = true;

			ImGuiIO& io = ImGui::GetIO();
			const ImGuiKeyChord expected_key_mod_flags = io.KeyMods;

			int footerHeight = ImGui::GetFrameHeightWithSpacing() * 5 + 16;
			ImGui::BeginChild("entlist", ImVec2(0, -footerHeight));

			if (entityReportFilterNeeded) {
				filteredEnts.clear();
				for (int i = 1; i < map->ents.size(); i++) {
					Entity* ent = map->ents[i];
					string cname = ent->getClassname();

					bool visible = true;

					if (!classFilter.empty() && classFilter != "(none)") {
						if (toLowerCase(cname) != toLowerCase(classFilter)) {
							visible = false;
						}
					}

					for (int k = 0; k < MAX_FILTERS; k++) {
						if (strlen(keyFilter[k]) > 0) {
							string searchKey = trimSpaces(toLowerCase(keyFilter[k]));

							bool foundKey = false;
							string actualKey;
							for (int c = 0; c < ent->keyOrder.size(); c++) {
								string key = toLowerCase(ent->keyOrder[c]);
								if (key == searchKey || (partialMatches && key.find(searchKey) != string::npos)) {
									foundKey = true;
									actualKey = key;
									break;
								}
							}
							if (!foundKey) {
								visible = false;
								break;
							}

							string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							if (!searchValue.empty()) {
								if ((partialMatches && ent->getKeyvalue(actualKey).find(searchValue) == string::npos) ||
									(!partialMatches && ent->getKeyvalue(actualKey) != searchValue)) {
									visible = false;
									break;
								}
							}
						}
						else if (strlen(valueFilter[k]) > 0) {
							string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							bool foundMatch = false;
							for (int c = 0; c < ent->keyOrder.size(); c++) {
								string val = toLowerCase(ent->getKeyvalue(ent->keyOrder[c]));
								if (val == searchValue || (partialMatches && val.find(searchValue) != string::npos)) {
									foundMatch = true;
									break;
								}
							}
							if (!foundMatch) {
								visible = false;
								break;
							}
						}
					}
					if (visible) {
						ReportEnt rpent;
						rpent.idx = i;
						rpent.selected = false;
						rpent.hasFgd = app->entityHasFgd(cname);
						rpent.cname = cname;
						filteredEnts.push_back(rpent);
					}
				}
			}
			entityReportFilterNeeded = false;

			if (entityReportReselectNeeded) {
				unordered_set<int> selection;
				for (int i = 0; i < app->pickInfo.ents.size(); i++) {
					selection.insert(app->pickInfo.ents[i]);
				}

				for (int i = 0; i < filteredEnts.size(); i++) {
					filteredEnts[i].selected = selection.count(filteredEnts[i].idx);
				}
			}
			entityReportReselectNeeded = false;

			ImGuiListClipper clipper;
			clipper.Begin(filteredEnts.size());

			while (clipper.Step())
			{
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd && line < filteredEnts.size() && filteredEnts[line].idx < map->ents.size(); line++)
				{
					int i = line;
					int entIdx = filteredEnts[i].idx;
					Entity* ent = map->ents[entIdx];
					string cname = filteredEnts[i].cname;

					bool pushedColor = true;
					if (!filteredEnts[i].hasFgd) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
					}
					else if (ent->hidden) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
					}
					else {
						pushedColor = false;
					}

					if (ImGui::Selectable((cname + "##ent" + to_string(i)).c_str(), filteredEnts[i].selected, ImGuiSelectableFlags_AllowDoubleClick)) {
						if (expected_key_mod_flags & ImGuiMod_Ctrl) {
							filteredEnts[i].selected = !filteredEnts[i].selected;
							lastSelect = i;
						}
						else if (expected_key_mod_flags & ImGuiMod_Shift) {
							if (lastSelect >= 0) {
								int begin = i > lastSelect ? lastSelect : i;
								int end = i > lastSelect ? i : lastSelect;
								for (int k = 0; k < filteredEnts.size(); k++)
									filteredEnts[k].selected = false;
								for (int k = begin; k < end; k++)
									filteredEnts[k].selected = true;
								filteredEnts[lastSelect].selected = true;
								filteredEnts[i].selected = true;
							}
						}
						else {
							for (int k = 0; k < filteredEnts.size(); k++)
								filteredEnts[k].selected = false;
							filteredEnts[i].selected = true;
							lastSelect = i;
						}

						if (ImGui::IsMouseDoubleClicked(0)) {
							app->goToEnt(map, entIdx);
						}

						app->deselectObject();
						for (int k = 0; k < filteredEnts.size(); k++) {
							if (filteredEnts[k].selected)
								app->pickInfo.selectEnt(filteredEnts[k].idx);
						}
						app->postSelectEnt();
					}

					if (pushedColor) {
						ImGui::PopStyleColor();
					}

					if (ImGui::IsItemHovered()) {
						if (!filteredEnts[i].hasFgd) {
							ImGui::SetTooltip("%s is not defined in any of your FGDs.\n", cname.c_str());
						}
						else if (ent->hidden) {
							ImGui::SetTooltip("This entity is hidden.\n", cname.c_str());
						}
					}

					if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(1)) {
						//ImGui::OpenPopup("ent_report_context");
						openContextMenu(app->pickInfo.getEntIndex());
					}
				}
			}
			clipper.End();

			ImGui::EndChild();

			ImGui::BeginChild("filters");

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));

			static vector<string> usedClasses;
			static set<string> uniqueClasses;

			static bool comboWasOpen = false;

			ImGui::Text("Classname Filter");
			if (ImGui::BeginCombo("##classfilter", classFilter.c_str()))
			{
				if (!comboWasOpen) {
					comboWasOpen = true;

					usedClasses.clear();
					uniqueClasses.clear();
					usedClasses.push_back("(none)");

					for (int i = 1; i < map->ents.size(); i++) {
						Entity* ent = map->ents[i];
						string cname = ent->getClassname();

						if (uniqueClasses.find(cname) == uniqueClasses.end()) {
							usedClasses.push_back(cname);
							uniqueClasses.insert(cname);
						}
					}
					sort(usedClasses.begin(), usedClasses.end());

				}
				for (int k = 0; k < usedClasses.size(); k++) {
					bool selected = usedClasses[k] == classFilter;

					bool hasFgd = k == 0 || app->entityHasFgd(usedClasses[k]);
					if (!hasFgd) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
					}

					if (ImGui::Selectable(usedClasses[k].c_str(), selected)) {
						classFilter = usedClasses[k];
						entityReportFilterNeeded = true;
					}

					if (!hasFgd) {
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s is not defined in any of your FGDs.\n", usedClasses[k].c_str());
						}
					}
				}

				ImGui::EndCombo();
			}
			else {
				comboWasOpen = false;
			}

			ImGui::Dummy(ImVec2(0, 8));
			ImGui::Text("Keyvalue Filter");

			ImGuiStyle& style = ImGui::GetStyle();
			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;
			inputWidth -= smallFont->CalcTextSizeA(fontSize*g_smallFontSizeMult, FLT_MAX, FLT_MAX, " = ").x;

			for (int i = 0; i < MAX_FILTERS; i++) {
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Key" + to_string(i)).c_str(), keyFilter[i], 64)) {
					entityReportFilterNeeded = true;
				}
				ImGui::SameLine();
				ImGui::Text(" = "); ImGui::SameLine();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Value" + to_string(i)).c_str(), valueFilter[i], 64)) {
					entityReportFilterNeeded = true;
				}
			}

			if (ImGui::Checkbox("Partial Matching", &partialMatches)) {
				entityReportFilterNeeded = true;
			}

			ImGui::EndChild();

			ImGui::EndGroup();
		}
	}

	if (ImGui::IsItemActive() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
		logf("Ctrl + A detected!\n");
	}

	ImGui::End();
}


static bool ColorPicker(float* col, bool alphabar)
{
	const int    EDGE_SIZE = 200; // = int( ImGui::GetWindowWidth() * 0.75f );
	const ImVec2 SV_PICKER_SIZE = ImVec2(EDGE_SIZE, EDGE_SIZE);
	const float  SPACING = ImGui::GetStyle().ItemInnerSpacing.x;
	const float  HUE_PICKER_WIDTH = 20.f;
	const float  CROSSHAIR_SIZE = 7.0f;

	ImColor color(col[0], col[1], col[2]);
	bool value_changed = false;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	// setup

	ImVec2 picker_pos = ImGui::GetCursorScreenPos();

	float hue, saturation, value;
	ImGui::ColorConvertRGBtoHSV(
		color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);

	// draw hue bar

	ImColor colors[] = { ImColor(255, 0, 0),
		ImColor(255, 255, 0),
		ImColor(0, 255, 0),
		ImColor(0, 255, 255),
		ImColor(0, 0, 255),
		ImColor(255, 0, 255),
		ImColor(255, 0, 0) };

	for (int i = 0; i < 6; ++i)
	{
		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING, picker_pos.y + i * (SV_PICKER_SIZE.y / 6)),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH,
				picker_pos.y + (i + 1) * (SV_PICKER_SIZE.y / 6)),
			colors[i],
			colors[i],
			colors[i + 1],
			colors[i + 1]);
	}

	draw_list->AddLine(
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING - 2, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + 2 + HUE_PICKER_WIDTH, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImColor(255, 255, 255));

	// draw alpha bar

	if (alphabar) {
		float alpha = col[3];

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + HUE_PICKER_WIDTH, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + 2 * HUE_PICKER_WIDTH, picker_pos.y + SV_PICKER_SIZE.y),
			ImColor(0, 0, 0), ImColor(0, 0, 0), ImColor(255, 255, 255), ImColor(255, 255, 255));

		draw_list->AddLine(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING - 2) + HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING + 2) + 2 * HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImColor(255.f - alpha, 255.f, 255.f));
	}

	// draw color matrix

	{
		const ImU32 c_oColorBlack = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 1.f));
		const ImU32 c_oColorBlackTransparent = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 0.f));
		const ImU32 c_oColorWhite = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f));

		ImVec4 cHueValue(1, 1, 1, 1);
		ImGui::ColorConvertHSVtoRGB(hue, 1, 1, cHueValue.x, cHueValue.y, cHueValue.z);
		ImU32 oHueColor = ImGui::ColorConvertFloat4ToU32(cHueValue);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorWhite,
			oHueColor,
			oHueColor,
			c_oColorWhite
		);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorBlackTransparent,
			c_oColorBlackTransparent,
			c_oColorBlack,
			c_oColorBlack
		);
	}

	// draw cross-hair

	float x = saturation * SV_PICKER_SIZE.x;
	float y = (1 - value) * SV_PICKER_SIZE.y;
	ImVec2 p(picker_pos.x + x, picker_pos.y + y);
	draw_list->AddLine(ImVec2(p.x - CROSSHAIR_SIZE, p.y), ImVec2(p.x - 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x + CROSSHAIR_SIZE, p.y), ImVec2(p.x + 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y + CROSSHAIR_SIZE), ImVec2(p.x, p.y + 2), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y - CROSSHAIR_SIZE), ImVec2(p.x, p.y - 2), ImColor(255, 255, 255));

	// color matrix logic

	ImGui::InvisibleButton("saturation_value_selector", SV_PICKER_SIZE);

	if (ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0])
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.x < 0) mouse_pos_in_canvas.x = 0;
		else if (mouse_pos_in_canvas.x >= SV_PICKER_SIZE.x - 1) mouse_pos_in_canvas.x = SV_PICKER_SIZE.x - 1;

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		value = 1 - (mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1));
		saturation = mouse_pos_in_canvas.x / (SV_PICKER_SIZE.x - 1);
		value_changed = true;
	}

	// hue bar logic

	ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING + SV_PICKER_SIZE.x, picker_pos.y));
	ImGui::InvisibleButton("hue_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

	if (ImGui::GetIO().MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		hue = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
		value_changed = true;
	}

	// alpha bar logic

	if (alphabar) {

		ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING * 2 + HUE_PICKER_WIDTH + SV_PICKER_SIZE.x, picker_pos.y));
		ImGui::InvisibleButton("alpha_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

		if (ImGui::GetIO().MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
		{
			ImVec2 mouse_pos_in_canvas = ImVec2(
				ImGui::GetIO().MousePos.x - picker_pos.x, ImGui::GetIO().MousePos.y - picker_pos.y);

			/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
			else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

			float alpha = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
			col[3] = alpha;
			value_changed = true;
		}

	}

	// R,G,B or H,S,V color editor

	color = ImColor::HSV(hue >= 1 ? hue - 10 * 1e-6 : hue, saturation > 0 ? saturation : 10 * 1e-6, value > 0 ? value : 1e-6);
	col[0] = color.Value.x;
	col[1] = color.Value.y;
	col[2] = color.Value.z;

	bool widget_used;
	ImGui::PushItemWidth((alphabar ? SPACING + HUE_PICKER_WIDTH : 0) +
		SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH - 2 * ImGui::GetStyle().FramePadding.x);
	widget_used = alphabar ? ImGui::ColorEdit4("", col) : ImGui::ColorEdit3("", col);
	ImGui::PopItemWidth();

	// try to cancel hue wrap (after ColorEdit), if any
	{
		float new_hue, new_sat, new_val;
		ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], new_hue, new_sat, new_val);
		if (new_hue <= 0 && hue > 0) {
			if (new_val <= 0 && value != new_val) {
				color = ImColor::HSV(hue, saturation, new_val <= 0 ? value * 0.5f : new_val);
				col[0] = color.Value.x;
				col[1] = color.Value.y;
				col[2] = color.Value.z;
			}
			else
				if (new_sat <= 0) {
					color = ImColor::HSV(hue, new_sat <= 0 ? saturation * 0.5f : new_sat, new_val);
					col[0] = color.Value.x;
					col[1] = color.Value.y;
					col[2] = color.Value.z;
				}
		}
	}
	return value_changed | widget_used;
}

bool ColorPicker3(float col[3]) {
	return ColorPicker(col, false);
}

bool ColorPicker4(float col[4]) {
	return ColorPicker(col, true);
}

static Texture* currentlightMap[MAXLIGHTMAPS] = { nullptr };

void UpdateLightmaps(BSPFACE face, int size[2], Bsp * map, int& lightmaps)
{
	for (int i = 0; i < MAXLIGHTMAPS; i++)
	{
		if (currentlightMap[i] != nullptr)
			delete currentlightMap[i];
		currentlightMap[i] = nullptr;
	}

	for (int i = 0; i < MAXLIGHTMAPS; i++) {
		if (face.nStyles[i] == 255)
			continue;
		currentlightMap[i] = new Texture(size[0], size[1]);
		int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		int offset = face.nLightmapOffset + i * lightmapSz;
		memcpy(currentlightMap[i]->data, map->lightdata + offset, lightmapSz);
		currentlightMap[i]->upload(GL_RGB, true);
		lightmaps++;
		//logf("upload %d style at offset %d\n", i, offset);
	}
}

void Gui::drawLightMapTool() {
	static float colourPatch[3];

	
	static int windowWidth = 550;
	static int windowHeight = 520;
	static int lightmaps = 0;
	const char* light_names[] =
	{
		"OFF",
		"Light 0",
		"Light 1",
		"Light 2",
		"Light 3"
	};
	static int type = 0;

	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, windowHeight), ImVec2(windowWidth, FLT_MAX));

	if (ImGui::Begin("LightMap Editor (WIP)", &showLightmapEditorWidget)) {
		ImGui::Dummy(ImVec2(windowWidth / 2.45f, 10.0f));
		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Not easy to use and changes aren't displayed in real time.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		Bsp* map = app->pickInfo.getMap();
		if (map && app->pickInfo.faces.size() == 1)
		{
			int faceIdx = app->pickInfo.getFaceIndex();
			BSPFACE& face = *app->pickInfo.getFace();
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);
			if (showLightmapEditorUpdate)
			{
				lightmaps = 0;
				UpdateLightmaps(face, size, map, lightmaps);
				windowWidth = lightmaps > 1 ? 550 : 250;
				showLightmapEditorUpdate = false;
			}
			ImVec2 imgSize = ImVec2(200, 200);
			for (int i = 0; i < lightmaps; i++)
			{
				if (i == 0)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[1]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(120, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[2]);
				}

				if (i == 2)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[3]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(150, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[4]);
				}

				if (i == 1 || i > 2)
				{
					ImGui::SameLine();
				}
				else if (i == 2)
				{
					ImGui::Separator();
				}

				if (currentlightMap[i] == nullptr)
				{
					ImGui::Dummy(ImVec2(200, 200));
					continue;
				}

				if (ImGui::ImageButton(("lightmap" + to_string(currentlightMap[i]->id)).c_str(), (ImTextureID)currentlightMap[i]->id, imgSize, ImVec2(0, 0), ImVec2(1, 1))) {
					ImVec2 picker_pos = ImGui::GetCursorScreenPos();
					if (i == 1 || i == 3)
					{
						picker_pos.x += 208;
					}
					ImVec2 mouse_pos_in_canvas = ImVec2(ImGui::GetIO().MousePos.x - picker_pos.x, 205 + ImGui::GetIO().MousePos.y - picker_pos.y);


					int image_x = currentlightMap[i]->width / 200.0 * (ImGui::GetIO().MousePos.x - picker_pos.x);
					int image_y = currentlightMap[i]->height / 200.0 * (205 + ImGui::GetIO().MousePos.y - picker_pos.y);
					if (image_x < 0)
					{
						image_x = 0;
					}
					if (image_y < 0)
					{
						image_y = 0;
					}
					if (image_x > currentlightMap[i]->width)
					{
						image_x = currentlightMap[i]->width;
					}
					if (image_y > currentlightMap[i]->height)
					{
						image_y = currentlightMap[i]->height;
					}

					int offset = (currentlightMap[i]->width * sizeof(COLOR3) * image_y) + (image_x * sizeof(COLOR3));
					if (offset >= currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3))
						offset = (currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3)) - 1;
					if (offset < 0)
						offset = 0;

					currentlightMap[i]->data[offset + 0] = colourPatch[0] * 255;
					currentlightMap[i]->data[offset + 1] = colourPatch[1] * 255;
					currentlightMap[i]->data[offset + 2] = colourPatch[2] * 255;
					currentlightMap[i]->upload(GL_RGB, true);
					//logf("%f %f %f %f %d %d = %d \n", picker_pos.x, picker_pos.y, mouse_pos_in_canvas.x, mouse_pos_in_canvas.y, image_x, image_y, i);
				}
			}
			ImGui::Separator();
			ImGui::Text("Lightmap width:%d height:%d", size[0], size[1]);
			ImGui::Separator();
			ColorPicker3(colourPatch);
			ImGui::Separator();
			ImGui::SetNextItemWidth(100.f);
			ImGui::Combo(" Disable light", &type, light_names, IM_ARRAYSIZE(light_names));
			app->mapRenderer->showLightFlag = type - 1;
			ImGui::Separator();
			if (ImGui::Button("Apply", ImVec2(120, 0)))
			{
				for (int i = 0; i < MAXLIGHTMAPS; i++) {
					if (face.nStyles[i] == 255 || currentlightMap[i] == nullptr)
						continue;
					int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
					int offset = face.nLightmapOffset + i * lightmapSz;
					memcpy(map->lightdata + offset, currentlightMap[i]->data, lightmapSz);
				}
				app->mapRenderer->reloadLightmaps();
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert", ImVec2(120, 0)))
			{
				showLightmapEditorUpdate = true;
			}
		}
		else if (app->pickInfo.faces.size() > 1)
		{
			ImGui::Text("Multiple faces selected");
		}
		else
		{
			ImGui::Text("No face selected");
		}

	}
	ImGui::End();
}
void Gui::drawTextureTool() {
	ImGui::SetNextWindowSize(ImVec2(300, 570), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 420), ImVec2(FLT_MAX, app->windowHeight));

	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	if (ImGui::Begin("Face Editor", &showTextureWidget)) {
		static float scaleX, scaleY, shiftX, shiftY, rotate;
		static bool isSpecial;
		static int width, height;
		static ImTextureID textureId = NULL; // OpenGL ID
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

		if (mapRenderer == NULL || map == NULL || app->pickMode != PICK_FACE || app->pickInfo.faces.size() == 0)
		{
			ImGui::Text("No face selected");
			ImGui::End();
			return;
		}

		if (lastPickCount != app->pickCount && app->pickMode == PICK_FACE) {
			if (app->pickInfo.faces.size() && mapRenderer != NULL) {
				int faceIdx = app->pickInfo.faces[0];
				Bsp* map = app->pickInfo.getMap();
				BSPFACE& face = map->faces[faceIdx];
				BSPPLANE& plane = map->planes[face.iPlane];
				BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
				int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];

				width = height = 0;
				if (texOffset != -1) {
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					width = tex.nWidth;
					height = tex.nHeight;
					strncpy(textureName, tex.szName, MAXTEXTURENAME);
					isEmbedded = tex.nOffsets[0] != 0;

					int w = tex.nWidth;
					int h = tex.nHeight;
					int sz = w * h;	   // miptex 0
					int sz2 = sz / 4;  // miptex 1
					int sz3 = sz2 / 4; // miptex 2
					int sz4 = sz3 / 4; // miptex 3
					int szAll = sizeof(BSPMIPTEX) + sz + sz2 + sz3 + sz4 + 2 + 256 * 3 + 2;
					tex_size_kb = (szAll + 512) / 1024;
				}
				else {
					textureName[0] = '\0';
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

				textureId = (ImTextureID)mapRenderer->getFaceTextureId(faceIdx);
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
						textureName[0] = '\0';
					}
				}
			}
			else {
				scaleX = scaleY = shiftX = shiftY = width = height = 0;
				textureId = NULL;
				textureName[0] = '\0';
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

		

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));
		if (ImGui::BeginTable("TransformTexTable", 4, ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthFixed, 5);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGui::Text("Shift");
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##shiftx", &shiftX, 0.1f, 0, 0, "X: %.2f")) { shiftedX = true; }
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##shifty", &shiftY, 0.1f, 0, 0, "Y: %.2f")) { shiftedY = true; }
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
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGui::Text("Scale");
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##scalex", &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0) { scaledX = true; }
			ImGui::TableNextColumn();

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::DragFloat("##scaley", &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0) { scaledY = true; }
			ImGui::TableNextColumn();

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();

		if (ImGui::Checkbox("Special", &isSpecial)) {
			toggledFlags = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Used with invisible faces to bypass the surface extent limit."
				"\nLightmaps may break in strange ways if this is used on a normal face.");
			ImGui::EndTooltip();
		}

		ImGui::SameLine(0, 20);
		
		if (g_app->isLoading) {
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		}
		if (ImGui::Checkbox("Embedded", &isEmbedded)) {
			Bsp* map = app->pickInfo.getMap();
			bool isActuallyEmbedded = false;

			if (map && app->pickInfo.getFace()) {
				BSPFACE& face = *app->pickInfo.getFace();
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
				if (info.iMiptex > 0 && info.iMiptex < map->textureCount) {
					int32_t texOffset = ((int32_t*)map->textures)[info.iMiptex + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					isActuallyEmbedded = tex.nOffsets[0] != 0;
				}
			}

			BSPFACE& face = *app->pickInfo.getFace();
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
			if (isActuallyEmbedded) {
				isEmbedded = !map->unembed_texture(info.iMiptex, wads);
			}
			else {
				isEmbedded = map->embed_texture(info.iMiptex, wads);
			}

			// refresh texture source
			app->pickCount++;
			last_texture_name = "";
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

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Texture");
		ImGui::SetNextItemWidth(inputWidth + 40);
		if (!validTexture) {
			ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		}
		if (ImGui::InputText("##texname", textureName, MAXTEXTURENAME)) {
			textureChanged = true;
		}
		if (refreshSelectedFaces) {
			textureChanged = true;
			refreshSelectedFaces = false;
			int32_t texOffset = ((int32_t*)map->textures)[copiedMiptex + 1];
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			strncpy(textureName, tex.szName, MAXTEXTURENAME);
		}
		if (!validTexture) {
			ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		ImGui::Text("%dx%d", width, height);

		if ((scaledX || scaledY || shiftedX || shiftedY || rotated || textureChanged || refreshSelectedFaces || toggledFlags)) {
			uint32_t newMiptex = 0;
			if (textureChanged) {
				validTexture = false;

				int32_t totalTextures = ((int32_t*)map->textures)[0];

				for (uint i = 0; i < totalTextures; i++) {
					int32_t texOffset = ((int32_t*)map->textures)[i + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					if (!strcasecmp(tex.szName, textureName)) {
						validTexture = true;
						newMiptex = i;
						// force matching case of real texture reference
						strncpy(textureName, tex.szName, MAXTEXTURENAME);
						textureName[MAXTEXTURENAME-1] = 0;
						break;
					}
				}

				if (!validTexture) {
					for (int i = 0; i < wads.size(); i++) {
						if (wads[i]->hasTexture(textureName)) {
							validTexture = true;
							WADTEX* tex = wads[i]->readTexture(textureName);
							newMiptex = map->add_texture_from_wad(tex);
							
							// Note: texture offset must match lump texture index, it's lucky that this just works
							mapRenderer->loadTexture(tex);

							delete tex;
							break;
						}
					}
				}
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
				if ((textureChanged || toggledFlags) && validTexture) {
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
				textureId = (ImTextureID)mapRenderer->getFaceTextureId(app->pickInfo.faces[0]);
				for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++) {
					mapRenderer->refreshModel(*it, false);
				}
				for (int i = 0; i < app->pickInfo.faces.size(); i++) {
					mapRenderer->highlightFace(app->pickInfo.faces[i], true);
				}
			}

			checkFaceErrors();
			g_app->updateTextureAxes();
		}

		refreshSelectedFaces = false;

		float imgWidth = min(256.0f, inputWidth * 2 - 2);
		ImVec2 imgSize = ImVec2(imgWidth, imgWidth);
		if (ImGui::ImageButton("texicon", textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1))) {
			logf("Open browser!\n");

			ImGui::OpenPopup("Not Implemented");
		}

		if (ImGui::BeginPopupModal("Not Implemented", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("TODO: Texture browser\n\n");
			ImGui::Separator();

			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::EndPopup();
		}

		ImGui::Text(("Source: " + texture_src).c_str());
		ImGui::Text(("Size: " + to_string(tex_size_kb) + " KB").c_str());
	}

	ImGui::End();
}

StatInfo Gui::calcStat(string name, uint val, uint max, bool isMem) {
	StatInfo stat;
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	ImVec4 color;

	if (val > max) {
		color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	}
	else if (percent >= 90) {
		color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
	}
	else if (percent >= 75) {
		color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	}
	else {
		color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	static char tmp[256];

	string out;

	stat.name = name;

	if (isMem) {
		sprintf(tmp, "%8.2f", val / meg);
		stat.val = string(tmp);

		sprintf(tmp, "%-5.2f MB", max / meg);
		stat.max = string(tmp);
	}
	else {
		sprintf(tmp, "%8u", val);
		stat.val = string(tmp);

		sprintf(tmp, "%-8u", max);
		stat.max = string(tmp);
	}
	sprintf(tmp, "%3.1f%%", percent);
	stat.fullness = string(tmp);
	stat.color = color;

	stat.progress = (float)val / (float)max;

	return stat;
}

ModelInfo Gui::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, uint val, uint max, bool isMem) {
	ModelInfo stat;

	string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < map->ents.size(); k++) {
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx) {
			targetname = map->ents[k]->getTargetname();
			classname = map->ents[k]->getClassname();
			stat.entIdx = k;
		}
	}

	stat.classname = classname;
	stat.targetname = targetname;

	static char tmp[256];

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	string out;

	if (isMem) {
		sprintf(tmp, "%8.1f", val / meg);
		stat.val = to_string(val);

		sprintf(tmp, "%-5.1f MB", max / meg);
		stat.usage = tmp;
	}
	else {
		stat.model = "*" + to_string(modelInfo->modelIdx);
		stat.val = to_string(val);
	}
	if (percent >= 0.1f) {
		sprintf(tmp, "%6.1f%%%%", percent);
		stat.usage = string(tmp);
	}

	return stat;
}

void Gui::reloadLimits() {
	for (int i = 0; i < SORT_MODES; i++) {
		loadedLimit[i] = false;
	}
	loadedStats = false;
}

void Gui::checkValidHulls() {
	for (int i = 0; i < MAX_MAP_HULLS; i++) {
		anyHullValid[i] = false;
		Bsp* map = app->mapRenderer->map;

		for (int m = 0; m < map->modelCount; m++) {
			if (map->models[m].iHeadnodes[i] >= 0) {
				anyHullValid[i] = true;
				break;
			}
		}
	}
}

void Gui::checkFaceErrors() {
	lightmapTooLarge = badSurfaceExtents = false;

	if (!app->pickInfo.getFace())
		return;

	Bsp* map = app->pickInfo.getMap();

	for (int i = 0; i < app->pickInfo.faces.size(); i++) {
		int size[2];
		if (!GetFaceLightmapSize(map, app->pickInfo.faces[i], size)) {
			badSurfaceExtents = true;
		}
		if (size[0] * size[1] > MAX_LUXELS) {
			lightmapTooLarge = true;
		}
	}
}

void Gui::refresh() {
	reloadLimits();
	checkValidHulls();
	entityReportFilterNeeded = true;
}

void Gui::saveAs() {
	Bsp* map = app->mapRenderer->map;

	char* fname = tinyfd_saveFileDialog("Save As", map->path.c_str(),
		1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)");

	if (fname) {
		map->update_ent_lump();
		map->path = fname;
		map->name = stripExt(basename(fname));
		map->write(map->path);
		app->updateWindowTitle();
		app->setInitialLumpState();
	}
}

const char* Gui::openMap() {
	return tinyfd_openFileDialog("Open Map", "", 1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);
}

void Gui::windowResized(int width, int height) {
	if (!g_settings.autoload_layout)
		return;

	if (width == g_settings.autoload_layout_width && height == g_settings.autoload_layout_height) {
		logf("Loading saved widget layout for resolution %dx%d\n", width, height);
		string userLayout = getUserLayoutPath();
		ImGui::LoadIniSettingsFromDisk(userLayout.c_str());
		ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
		logf("Layout loaded from %s\n", userLayout.c_str());
	}
}


string Gui::getUserLayoutPath() {
	return getFolderPath(ImGui::GetIO().IniFilename) + "imgui_user.ini";
}