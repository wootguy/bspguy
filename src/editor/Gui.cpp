#include "Gui.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Renderer.h"
#include <lodepng.h>
#include <algorithm>

// embedded binary data
#include "fonts/robotomono.h"
#include "fonts/robotomedium.h"
#include "icons/object.h"
#include "icons/face.h"

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

string iniPath = getConfigDir() + "imgui.ini";

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

	drawFpsOverlay();
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
	if (showLightmapEditorWidget) {
		drawLightMapTool();
	}
	if (showEntityReport) {
		drawEntityReport();
	}
	if (showGOTOWidget) {
		drawGOTOWidget();
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
	if (!app->pickInfo.valid) {
		return;
	}
	Bsp* map = app->pickInfo.map;
	BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[app->pickInfo.faceIdx].iTextureInfo];
	copiedMiptex = texinfo.iMiptex;
}

void Gui::pasteTexture() {
	refreshSelectedFaces = true;
}

void Gui::copyLightmap() {
	if (!app->pickInfo.valid) {
		return;
	}

	Bsp* map = app->pickInfo.map;

	copiedLightmapFace = app->pickInfo.faceIdx;

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.faceIdx, size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count(app->pickInfo.faceIdx);
	//copiedLightmap.luxelFlags = new byte[size[0] * size[1]];
	//qrad_get_lightmap_flags(map, app->pickInfo.faceIdx, copiedLightmap.luxelFlags);
}

void Gui::pasteLightmap() {
	if (!app->pickInfo.valid) {
		return;
	}

	Bsp* map = app->pickInfo.map;

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.faceIdx, size);
	LIGHTMAP dstLightmap;
	dstLightmap.width = size[0];
	dstLightmap.height = size[1];
	dstLightmap.layers = map->lightmap_count(app->pickInfo.faceIdx);

	if (dstLightmap.width != copiedLightmap.width || dstLightmap.height != copiedLightmap.height) {
		logf("WARNING: lightmap sizes don't match (%dx%d != %d%d)",
			copiedLightmap.width,
			copiedLightmap.height,
			dstLightmap.width,
			dstLightmap.height);
		// TODO: resize the lightmap, or maybe just shift if the face is the same size
	}

	BSPFACE& src = map->faces[copiedLightmapFace];
	BSPFACE& dst = map->faces[app->pickInfo.faceIdx];
	dst.nLightmapOffset = src.nLightmapOffset;
	memcpy(dst.nStyles, src.nStyles, 4);

	app->mapRenderers[app->pickInfo.mapIdx]->reloadLightmaps();
}

void Gui::draw3dContextMenus() {
	ImGuiContext& g = *GImGui;

	if (app->originHovered) {
		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context")) {
			if (ImGui::MenuItem("Center", "")) {
				app->transformedOrigin = app->getEntOrigin(app->pickInfo.map, app->pickInfo.ent);
				app->applyTransform();
				app->pickCount++; // force gui refresh
			}

			if (app->pickInfo.map && app->pickInfo.ent && ImGui::BeginMenu("Align")) {
				BSPMODEL& model = app->pickInfo.map->models[app->pickInfo.ent->getBspModelIdx()];

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
				app->cutEnt();
			}
			if (ImGui::MenuItem("Copy", "Ctrl+C")) {
				app->copyEnt();
			}
			if (ImGui::MenuItem("Delete", "Del")) {
				app->deleteEnt();
			}
			ImGui::Separator();
			if (app->pickInfo.modelIdx > 0) {
				Bsp* map = app->pickInfo.map;
				BSPMODEL& model = app->pickInfo.map->models[app->pickInfo.modelIdx];

				if (ImGui::BeginMenu("Create Hull", !app->invalidSolid && app->isTransformableSolid)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->regenerate_clipnodes(app->pickInfo.modelIdx, -1);
						checkValidHulls();
						logf("Regenerated hulls 1-3 on model %d\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str())) {
							map->regenerate_clipnodes(app->pickInfo.modelIdx, i);
							checkValidHulls();
							logf("Regenerated hull %d on model %d\n", i, app->pickInfo.modelIdx);
						}
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Delete Hull", !app->isLoading)) {
					if (ImGui::MenuItem("All Hulls")) {
						map->delete_hull(0, app->pickInfo.modelIdx, -1);
						map->delete_hull(1, app->pickInfo.modelIdx, -1);
						map->delete_hull(2, app->pickInfo.modelIdx, -1);
						map->delete_hull(3, app->pickInfo.modelIdx, -1);
						app->mapRenderers[app->pickInfo.mapIdx]->refreshModel(app->pickInfo.modelIdx);
						checkValidHulls();
						logf("Deleted all hulls on model %d\n", app->pickInfo.modelIdx);
					}
					if (ImGui::MenuItem("Clipnodes")) {
						map->delete_hull(1, app->pickInfo.modelIdx, -1);
						map->delete_hull(2, app->pickInfo.modelIdx, -1);
						map->delete_hull(3, app->pickInfo.modelIdx, -1);
						app->mapRenderers[app->pickInfo.mapIdx]->refreshModelClipnodes(app->pickInfo.modelIdx);
						checkValidHulls();
						logf("Deleted hulls 1-3 on model %d\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 0; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, isHullValid)) {
							map->delete_hull(i, app->pickInfo.modelIdx, -1);
							checkValidHulls();
							if (i == 0)
								app->mapRenderers[app->pickInfo.mapIdx]->refreshModel(app->pickInfo.modelIdx);
							else
								app->mapRenderers[app->pickInfo.mapIdx]->refreshModelClipnodes(app->pickInfo.modelIdx);
							logf("Deleted hull %d on model %d\n", i, app->pickInfo.modelIdx);
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Simplify Hull", !app->isLoading)) {
					if (ImGui::MenuItem("Clipnodes")) {
						map->simplify_model_collision(app->pickInfo.modelIdx, 1);
						map->simplify_model_collision(app->pickInfo.modelIdx, 2);
						map->simplify_model_collision(app->pickInfo.modelIdx, 3);
						app->mapRenderers[app->pickInfo.mapIdx]->refreshModelClipnodes(app->pickInfo.modelIdx);
						logf("Replaced hulls 1-3 on model %d with a box-shaped hull\n", app->pickInfo.modelIdx);
					}

					ImGui::Separator();

					for (int i = 1; i < MAX_MAP_HULLS; i++) {
						bool isHullValid = model.iHeadnodes[i] >= 0;

						if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, isHullValid)) {
							map->simplify_model_collision(app->pickInfo.modelIdx, 1);
							app->mapRenderers[app->pickInfo.mapIdx]->refreshModelClipnodes(app->pickInfo.modelIdx);
							logf("Replaced hull %d on model %d with a box-shaped hull\n", i, app->pickInfo.modelIdx);
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
									app->mapRenderers[app->pickInfo.mapIdx]->refreshModelClipnodes(app->pickInfo.modelIdx);
									checkValidHulls();
									logf("Redirected hull %d to hull %d on model %d\n", i, k, app->pickInfo.modelIdx);
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
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Create a copy of this BSP model and assign to this entity.\n\nThis lets you edit the model for this entity without affecting others.");
					ImGui::EndTooltip();
				}
			}

			if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G")) {
				if (!app->movingEnt)
					app->grabEnt();
				else {
					app->ungrabEnt();
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

		if (app->pickInfo.valid && ImGui::BeginPopup("empty_context"))
		{
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, app->copiedEnt != NULL)) {
				app->pasteEnt(false);
			}
			if (ImGui::MenuItem("Paste at original origin", 0, false, app->copiedEnt != NULL)) {
				app->pasteEnt(true);
			}

			ImGui::EndPopup();
		}
	}
	else if (app->pickMode == PICK_FACE && app->pickInfo.valid) {
		Bsp* map = app->pickInfo.map;

		if (ImGui::BeginPopup("face_context"))
		{
			if (ImGui::MenuItem("Copy texture", "Ctrl+C")) {
				copyTexture();
			}
			if (ImGui::MenuItem("Paste texture", "Ctrl+V", false, copiedMiptex >= 0 && copiedMiptex < map->textureCount)) {
				pasteTexture();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Copy lightmap", "(WIP)")) {
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Only works for faces with matching sizes/extents,\nand the lightmap might get shifted.");
				ImGui::EndTooltip();
			}

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

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", NULL)) {
			Bsp* map = app->getMapContainingCamera()->map;
			map->update_ent_lump();
			//map->write("yabma_move.bsp");
			//map->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
			map->write(map->path);
		}
		if (ImGui::MenuItem("Test", NULL)) {
			Bsp* map = app->getMapContainingCamera()->map;

			string mapPath = g_settings.gamedir + "/svencoop_addon/maps/" + map->name + ".bsp";
			string entPath = g_settings.gamedir + "/svencoop_addon/scripts/maps/bspguy/maps/" + map->name + ".ent";

			map->update_ent_lump(true); // strip nodes before writing (to skip slow node graph generation)
			map->write(mapPath);
			map->update_ent_lump(false); // add the nodes back in for conditional loading in the ent file

			ofstream entFile(entPath, ios::out | ios::trunc);
			if (entFile.is_open()) {
				logf("Writing %s\n", entPath.c_str());
				entFile.write((const char*)map->lumps[LUMP_ENTITIES], map->header.lump[LUMP_ENTITIES].nLength - 1);
			}
			else {
				logf("Failed to open ent file for writing:\n%s\n", entPath.c_str());
				logf("Check that the directories in the path exist, and that you have permission to write in them.\n");
			}
		}
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Saves the .bsp and .ent file to your svencoop_addon folder.\n\nAI nodes will be stripped to skip node graph generation.\n");
			ImGui::EndTooltip();
		}

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
		}
		if (ImGui::MenuItem("Validate")) {
			for (int i = 0; i < app->mapRenderers.size(); i++) {
				Bsp* map = app->mapRenderers[i]->map;
				logf("Validating %s\n", map->name.c_str());
				map->validate();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Settings", NULL)) {
			if (!showSettingsWidget) {
				reloadSettings = true;
			}
			showSettingsWidget = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", NULL)) {
			g_settings.save();
			//glfwTerminate();
			if (dirExists(g_settings.gamedir + g_settings.workingdir))
				removeDir(g_settings.gamedir + g_settings.workingdir);
			//std::exit(0);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit")) {
		Command* undoCmd = !app->undoHistory.empty() ? app->undoHistory[app->undoHistory.size() - 1] : NULL;
		Command* redoCmd = !app->redoHistory.empty() ? app->redoHistory[app->redoHistory.size() - 1] : NULL;
		string undoTitle = undoCmd ? "Undo " + undoCmd->desc : "Can't undo";
		string redoTitle = redoCmd ? "Redo " + redoCmd->desc : "Can't redo";
		bool canUndo = undoCmd && (!app->isLoading || undoCmd->allowedDuringLoad);
		bool canRedo = redoCmd && (!app->isLoading || redoCmd->allowedDuringLoad);
		bool entSelected = app->pickInfo.valid && app->pickInfo.ent;
		bool mapSelected = app->pickInfo.valid && app->pickInfo.map;
		bool nonWorldspawnEntSelected = entSelected && app->pickInfo.entIdx != 0;

		if (ImGui::MenuItem(undoTitle.c_str(), "Ctrl+Z", false, canUndo)) {
			app->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), "Ctrl+Y", false, canRedo)) {
			app->redo();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cut", "Ctrl+X", false, nonWorldspawnEntSelected)) {
			app->cutEnt();
		}
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, nonWorldspawnEntSelected)) {
			app->copyEnt();
		}
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, mapSelected && app->copiedEnt != NULL)) {
			app->pasteEnt(false);
		}
		if (ImGui::MenuItem("Paste at original origin", 0, false, entSelected && app->copiedEnt != NULL)) {
			app->pasteEnt(true);
		}
		if (ImGui::MenuItem("Delete", "Del", false, nonWorldspawnEntSelected)) {
			app->deleteEnt();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading && nonWorldspawnEntSelected)) {
			DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", app->pickInfo);
			command->execute();
			app->pushUndoCommand(command);
		}
		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G", false, nonWorldspawnEntSelected)) {
			if (!app->movingEnt)
				app->grabEnt();
			else {
				app->ungrabEnt();
			}
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", false, entSelected)) {
			showTransformWidget = !showTransformWidget;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Properties", "Alt+Enter", false, entSelected)) {
			showKeyvalueWidget = !showKeyvalueWidget;
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Map"))
	{
		if (ImGui::MenuItem("Entity Report", NULL)) {
			showEntityReport = true;
		}

		if (ImGui::MenuItem("Show Limits", NULL)) {
			showLimitsWidget = true;
		}

		ImGui::Separator();

		bool mapSelected = app->pickInfo.valid && app->pickInfo.map;
		Bsp* map = mapSelected ? app->pickInfo.map : NULL;

		if (ImGui::MenuItem("Clean", 0, false, !app->isLoading && mapSelected)) {
			CleanMapCommand* command = new CleanMapCommand("Clean " + map->name, app->pickInfo.mapIdx, app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}

		if (ImGui::MenuItem("Optimize", 0, false, !app->isLoading && mapSelected)) {
			OptimizeMapCommand* command = new OptimizeMapCommand("Optimize " + map->name, app->pickInfo.mapIdx, app->undoLumpState);
			g_app->saveLumpState(map, 0xffffffff, false);
			command->execute();
			app->pushUndoCommand(command);
		}

		ImGui::Separator();

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::BeginMenu("Delete Hull", hasAnyCollision && !app->isLoading)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), NULL, false, anyHullValid[i])) {
					for (int k = 0; k < app->mapRenderers.size(); k++) {
						Bsp* map = app->mapRenderers[k]->map;
						map->delete_hull(i, -1);
						app->mapRenderers[k]->reloadClipnodes();
						logf("Deleted hull %d in map %s\n", i, map->name.c_str());
					}
					checkValidHulls();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Redirect Hull", hasAnyCollision && !app->isLoading)) {
			for (int i = 1; i < MAX_MAP_HULLS; i++) {
				if (ImGui::BeginMenu(("Hull " + to_string(i)).c_str())) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + to_string(k)).c_str(), "", false, anyHullValid[k])) {
							for (int j = 0; j < app->mapRenderers.size(); j++) {
								Bsp* map = app->mapRenderers[j]->map;
								map->delete_hull(i, k);
								app->mapRenderers[j]->reloadClipnodes();
								logf("Redirected hull %d to hull %d in map %s\n", i, k, map->name.c_str());
							}
							checkValidHulls();
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Create"))
	{
		bool mapSelected = app->pickInfo.valid && app->pickInfo.map;
		Bsp* map = mapSelected ? app->mapRenderers[app->pickInfo.mapIdx]->map : NULL;
		BspRenderer* renderer = mapSelected ? app->mapRenderers[app->pickInfo.mapIdx] : NULL;

		if (ImGui::MenuItem("Entity", 0, false, mapSelected)) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");

			CreateEntityCommand* createCommand = new CreateEntityCommand("Create Entity", app->pickInfo.mapIdx, newEnt);
			delete newEnt;
			createCommand->execute();
			app->pushUndoCommand(createCommand);
		}

		if (ImGui::MenuItem("BSP Model", 0, false, !app->isLoading && mapSelected)) {
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

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->pickInfo.mapIdx, newEnt, snapSize);
			command->execute();
			delete newEnt;
			app->pushUndoCommand(command);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Widgets"))
	{
		if (ImGui::MenuItem("Debug", NULL, showDebugWidget)) {
			showDebugWidget = !showDebugWidget;
		}
		if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", showKeyvalueWidget)) {
			showKeyvalueWidget = !showKeyvalueWidget;
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", showTransformWidget)) {
			showTransformWidget = !showTransformWidget;
		}
		if (ImGui::MenuItem("Go to", "Ctrl+G", showGOTOWidget)) {
			showGOTOWidget = !showGOTOWidget;
			showGOTOWidget_update = true;
		}

		if (ImGui::MenuItem("Face Properties", "", showTextureWidget)) {
			showTextureWidget = !showTextureWidget;
		}
		if (ImGui::MenuItem("LightMap Editor (WIP)", "", showLightmapEditorWidget)) {
			showLightmapEditorWidget = !showLightmapEditorWidget;
			showLightmapEditorUpdate = true;
		}
		if (ImGui::MenuItem("Log", "", showLogWidget)) {
			showLogWidget = !showLogWidget;
		}
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

	ImGui::EndMainMenuBar();
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
		if (ImGui::ImageButton((void*)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4)) {
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
		if (ImGui::ImageButton((void*)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4)) {
			if (app->pickInfo.valid && app->pickInfo.modelIdx >= 0) {
				Bsp* map = app->pickInfo.map;
				BspRenderer* mapRenderer = app->mapRenderers[app->pickInfo.mapIdx];
				BSPMODEL& model = map->models[app->pickInfo.modelIdx];
				for (int i = 0; i < model.nFaces; i++) {
					int faceIdx = model.iFirstFace + i;
					mapRenderer->highlightFace(faceIdx, true);
					app->selectedFaces.push_back(faceIdx);
				}
			}
			app->selectMapIdx = app->pickInfo.mapIdx;
			app->deselectObject();
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

void Gui::drawFpsOverlay() {
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 window_pos = ImVec2(io.DisplaySize.x - 10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("Overlay", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::MenuItem("VSync", NULL, vsync)) {
				vsync = !vsync;
				glfwSwapInterval(vsync ? 1 : 0);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void Gui::drawStatusMessage() {
	static int windowWidth = 32;
	static int loadingWindowWidth = 32;
	static int loadingWindowHeight = 32;

	bool showStatus = app->invalidSolid || !app->isTransformableSolid || badSurfaceExtents || lightmapTooLarge || app->modelUsesSharedStructures;
	if (showStatus) {
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2, app->windowHeight - 10.0f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (app->modelUsesSharedStructures) {
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
			if (!app->isTransformableSolid) {
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
			if (app->invalidSolid) {
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
						"This will crash the game. Increase texture scale to fix.";
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
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
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

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Origin: %d %d %d", (int)app->cameraOrigin.x, (int)app->cameraOrigin.y, (int)app->cameraOrigin.z);
			ImGui::Text("Angles: %d %d %d", (int)app->cameraAngles.x, (int)app->cameraAngles.y, (int)app->cameraAngles.z);
		}

		if (app->pickInfo.valid) {
			Bsp* map = app->pickInfo.map;
			Entity* ent = app->pickInfo.ent;

			if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Name: %s", map->name.c_str());
			}

			if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Entity ID: %d", app->pickInfo.entIdx);

				if (app->pickInfo.modelIdx > 0) {
					ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
					ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

					ImGui::Checkbox("Debug nodes", &app->debugNodes);
					ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
				}

				if (app->pickInfo.faceIdx != -1) {
					BSPMODEL& model = map->models[app->pickInfo.modelIdx];
					BSPFACE& face = map->faces[app->pickInfo.faceIdx];

					ImGui::Text("Model ID: %d", app->pickInfo.modelIdx);
					ImGui::Text("Model polies: %d", model.nFaces);

					ImGui::Text("Face ID: %d", app->pickInfo.faceIdx);
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
				}
			}

			string bspTreeTitle = "BSP Tree";
			if (app->pickInfo.modelIdx >= 0) {
				bspTreeTitle += " (Model " + to_string(app->pickInfo.modelIdx) + ")";
			}
			if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

				if (app->pickInfo.modelIdx >= 0) {
					Bsp* map = app->pickInfo.map;

					vec3 localCamera = app->cameraOrigin - app->mapRenderers[app->pickInfo.mapIdx]->mapOffset;

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
						int headNode = map->models[app->pickInfo.modelIdx].iHeadnodes[i];
						int contents = map->pointContents(headNode, localCamera, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text("Contents: %s", map->getLeafContentsName(contents));
							if (i == 0) {
								ImGui::Text("Leaf: %d", leafIdx);
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

	ImGui::SetNextWindowSize(ImVec2(610, 610), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin("Keyvalue Editor", &showKeyvalueWidget, 0)) {
		if (app->pickInfo.valid && app->pickInfo.ent && app->fgd) {
			Bsp* map = app->pickInfo.map;
			Entity* ent = app->pickInfo.ent;
			BSPMODEL& model = map->models[app->pickInfo.modelIdx];
			BSPFACE& face = map->faces[app->pickInfo.faceIdx];
			string cname = ent->keyvalues["classname"];
			FgdClass* fgdClass = app->fgd->getFgdClass(cname);

			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Class:"); ImGui::SameLine();
			if (cname != "worldspawn") {
				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup("classname_popup");
			}
			else {
				ImGui::Text(cname.c_str());
			}
			ImGui::PopFont();

			if (fgdClass != NULL) {
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted((fgdClass->description).c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}


			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text("Change Class");
				ImGui::Separator();

				vector<FgdGroup>* targetGroup = &app->fgd->pointEntGroups;
				if (ent->getBspModelIdx() != -1) {
					targetGroup = &app->fgd->solidEntGroups;
				}

				for (int i = 0; i < targetGroup->size(); i++) {
					FgdGroup& group = targetGroup->at(i);

					if (ImGui::BeginMenu(group.groupName.c_str())) {
						for (int k = 0; k < group.classes.size(); k++) {
							if (ImGui::MenuItem(group.classes[k]->name.c_str())) {
								ent->setOrAddKeyvalue("classname", group.classes[k]->name);
								app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
								app->pushEntityUndoState("Change Class");
							}
						}

						ImGui::EndMenu();
					}
				}

				ImGui::EndPopup();
			}

			ImGui::Dummy(ImVec2(0, 10));

			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Attributes")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_SmartEditTab(ent);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Flags")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab(ent);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Raw Edit")) {
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab(ent);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();

		}
		else {
			ImGui::Text("No entity selected");
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(Entity* ent) {
	string cname = ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild("SmartEditWindow");

	ImGui::Columns(2, "smartcolumns", false); // 4-ways, with border

	static char keyNames[128][64];
	static char keyValues[128][64];

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	struct InputData {
		string key;
		string defaultValue;
		Entity* entRef;
		int entIdx;
		BspRenderer* bspRenderer;
	};

	if (fgdClass != NULL) {

		static InputData inputData[128];
		static int lastPickCount = 0;

		for (int i = 0; i < fgdClass->keyvalues.size() && i < 128; i++) {
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			string key = keyvalue.name;
			if (key == "spawnflags") {
				continue;
			}
			string value = ent->keyvalues[key];
			string niceName = keyvalue.description;

			if (value.empty() && keyvalue.defaultValue.length()) {
				value = keyvalue.defaultValue;
			}

			strcpy(keyNames[i], niceName.c_str());
			strcpy(keyValues[i], value.c_str());

			inputData[i].key = key;
			inputData[i].defaultValue = keyvalue.defaultValue;
			inputData[i].entIdx = app->pickInfo.entIdx;
			inputData[i].entRef = ent;
			inputData[i].bspRenderer = app->mapRenderers[app->pickInfo.mapIdx];

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(keyNames[i]); ImGui::NextColumn();

			ImGui::SetNextItemWidth(inputWidth);

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

				if (ImGui::BeginCombo(("##val" + to_string(i)).c_str(), selectedValue.c_str()))
				{
					for (int k = 0; k < keyvalue.choices.size(); k++) {
						KeyvalueChoice& choice = keyvalue.choices[k];
						bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);

						if (ImGui::Selectable(choice.name.c_str(), selected)) {
							ent->setOrAddKeyvalue(key, choice.svalue);
							app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
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

						InputData* inputData = (InputData*)data->UserData;
						Entity* ent = inputData->entRef;

						string newVal = data->Buf;
						if (newVal.empty()) {
							if (inputData->defaultValue.length()) {
								ent->setOrAddKeyvalue(inputData->key, inputData->defaultValue);
							}
							else {
								ent->removeKeyvalue(inputData->key);
							}

						}
						else {
							ent->setOrAddKeyvalue(inputData->key, newVal);
						}
						inputData->bspRenderer->refreshEnt(inputData->entIdx);
						g_app->updateEntConnections();
						return 1;
					}
				};

				if (keyvalue.iType == FGD_KEY_INTEGER) {
					ImGui::InputText(("##val" + to_string(i) + "_" + to_string(app->pickCount)).c_str(), keyValues[i], 64,
						ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
						InputChangeCallback::keyValueChanged, &inputData[i]);
				}
				else {
					ImGui::InputText(("##val" + to_string(i) + "_" + to_string(app->pickCount)).c_str(), keyValues[i], 64,
						ImGuiInputTextFlags_CallbackAlways, InputChangeCallback::keyValueChanged, &inputData[i]);
				}


			}

			ImGui::NextColumn();
		}

		lastPickCount = app->pickCount;
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_FlagsTab(Entity* ent) {
	ImGui::BeginChild("FlagsWindow");

	uint spawnflags = strtoul(ent->keyvalues["spawnflags"].c_str(), NULL, 10);
	FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);

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

		checkboxEnabled[i] = spawnflags & (1 << i);

		if (ImGui::Checkbox((name + "##flag" + to_string(i)).c_str(), &checkboxEnabled[i])) {
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

			app->pushEntityUndoState(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag");
		}
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab(Entity* ent) {
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

	struct InputData {
		int idx;
		Entity* entRef;
		int entIdx;
		BspRenderer* bspRenderer;
	};

	struct TextChangeCallback {
		static int keyNameChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;

			string key = ent->keyOrder[inputData->idx];
			if (key != data->Buf) {
				ent->renameKey(inputData->idx, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				if (key == "model" || string(data->Buf) == "model") {
					inputData->bspRenderer->preRenderEnts();
					g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
				}
				g_app->updateEntConnections();
			}

			return 1;
		}

		static int keyValueChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;
			string key = ent->keyOrder[inputData->idx];

			if (ent->keyvalues[key] != data->Buf) {
				ent->setOrAddKeyvalue(key, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				if (key == "model") {
					inputData->bspRenderer->preRenderEnts();
					g_app->saveLumpState(inputData->bspRenderer->map, 0xffffffff, false);
				}
				g_app->updateEntConnections();
			}

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

	float startY = 0;
	for (int i = 0; i < ent->keyOrder.size() && i < MAX_KEYS_PER_ENT; i++) {
		const char* item = dragIds[i];

		{
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

			ImGui::NextColumn();
		}

		string key = ent->keyOrder[i];
		string value = ent->keyvalues[key];

		{
			bool invalidKey = ignoreErrors == 0 && lastPickCount == app->pickCount && key != keyNames[i];

			strcpy(keyNames[i], key.c_str());

			keyIds[i].idx = i;
			keyIds[i].entIdx = app->pickInfo.entIdx;
			keyIds[i].entRef = ent;
			keyIds[i].bspRenderer = app->mapRenderers[app->pickInfo.mapIdx];

			if (invalidKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##key" + to_string(i) + "_" + to_string(app->pickCount)).c_str(), keyNames[i], MAX_KEY_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyNameChanged, &keyIds[i]);

			if (invalidKey || hoveredDrag[i]) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			strcpy(keyValues[i], value.c_str());

			valueIds[i].idx = i;
			valueIds[i].entIdx = app->pickInfo.entIdx;
			valueIds[i].entRef = ent;
			valueIds[i].bspRenderer = app->mapRenderers[app->pickInfo.mapIdx];

			if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + to_string(i) + to_string(app->pickCount)).c_str(), keyValues[i], MAX_VAL_LEN, ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (hoveredDrag[i]) {
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{

			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
			if (ImGui::Button((" X ##del" + to_string(i)).c_str())) {
				ent->removeKeyvalue(key);
				app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
				if (key == "model")
					app->mapRenderers[app->pickInfo.mapIdx]->preRenderEnts();
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
		string baseKeyName = "NewKey";
		string keyName = "NewKey";
		for (int i = 0; i < 128; i++) {
			if (!ent->hasKey(keyName)) {
				break;
			}
			keyName = baseKeyName + "#" + to_string(i + 2);
		}
		ent->addKeyvalue(keyName, "");
		app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
		app->updateEntConnections();
		ignoreErrors = 2;
		app->pushEntityUndoState("Add Keyvalue");
	}

	if (ignoreErrors > 0) {
		ignoreErrors--;
	}

	ImGui::EndChild();
}

void Gui::drawGOTOWidget() {
	ImGui::SetNextWindowSize(ImVec2(410, 200), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410, 200), ImVec2(410, 200));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	if (ImGui::Begin("Go to coordinates:", &showGOTOWidget, 0)) {
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			coordinates = app->cameraOrigin;
			angles = app->cameraAngles;
			showGOTOWidget_update = false;
		}
		ImGui::Text("Coordinates");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xpos", &coordinates.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##ypos", &coordinates.y, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zpos", &coordinates.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();
		ImGui::Text("Angles");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xangles", &angles.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##yangles", &angles_y, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zangles", &angles.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();

		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
		if (ImGui::Button("Go to"))
		{
			app->cameraOrigin = coordinates;
			app->cameraAngles = angles;
			makeVectors(angles, app->cameraForward, app->cameraRight, app->cameraUp);
		}
		ImGui::PopStyleColor(3);

	}

	ImGui::End();
}
void Gui::drawTransformWidget() {
	bool transformingEnt = app->pickInfo.valid && app->pickInfo.entIdx > 0;

	Entity* ent = NULL;
	BspRenderer* bspRenderer = NULL;
	if (transformingEnt) {
		bspRenderer = app->mapRenderers[app->pickInfo.mapIdx];
		Bsp* map = app->pickInfo.map;
		ent = app->pickInfo.ent;
	}

	ImGui::SetNextWindowSize(ImVec2(430, 380), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, app->windowHeight - 40));


	static int x, y, z;
	static float fx, fy, fz;
	static float last_fx, last_fy, last_fz;
	static float sx, sy, sz;

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static int oldSnappingEnabled = app->gridSnappingEnabled;
	static int oldTransformTarget = -1;


	if (ImGui::Begin("Transformation", &showTransformWidget, 0)) {
		ImGuiStyle& style = ImGui::GetStyle();

		bool shouldUpdateUi = lastPickCount != app->pickCount ||
			app->draggingAxis != -1 ||
			app->movingEnt ||
			oldSnappingEnabled != app->gridSnappingEnabled ||
			lastVertPickCount != app->vertPickCount ||
			oldTransformTarget != app->transformTarget;

		TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_SCALE ? &app->scaleAxes : &app->moveAxes);

		if (shouldUpdateUi) {
			if (transformingEnt) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					x = fx = last_fx = activeAxes.origin.x;
					y = fy = last_fy = activeAxes.origin.y;
					z = fz = last_fz = activeAxes.origin.z;
				}
				else {
					vec3 ori = ent->hasKey("origin") ? parseVector(ent->keyvalues["origin"]) : vec3();
					if (app->originSelected) {
						ori = app->transformedOrigin;
					}
					x = fx = ori.x;
					y = fy = ori.y;
					z = fz = ori.z;
				}

			}
			else {
				x = fx = 0;
				y = fy = 0;
				z = fz = 0;
			}
			sx = sy = sz = 1;
		}

		oldTransformTarget = app->transformTarget;
		oldSnappingEnabled = app->gridSnappingEnabled;
		lastVertPickCount = app->vertPickCount;
		lastPickCount = app->pickCount;

		bool scaled = false;
		bool originChanged = false;
		guiHoverAxis = -1;

		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

		static bool inputsWereDragged = false;
		bool inputsAreDragging = false;

		ImGui::Text("Move");
		ImGui::PushItemWidth(inputWidth);

		if (app->gridSnappingEnabled) {
			if (ImGui::DragInt("##xpos", &x, 0.1f, 0, 0, "X: %d")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragInt("##ypos", &y, 0.1f, 0, 0, "Y: %d")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragInt("##zpos", &z, 0.1f, 0, 0, "Z: %d")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
		}
		else {
			if (ImGui::DragFloat("##xpos", &fx, 0.1f, 0, 0, "X: %.2f")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##ypos", &fy, 0.1f, 0, 0, "Y: %.2f")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##zpos", &fz, 0.1f, 0, 0, "Z: %.2f")) { originChanged = true; }
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
		}

		ImGui::PopItemWidth();

		ImGui::Dummy(ImVec2(0, style.FramePadding.y));

		ImGui::Text("Scale");
		ImGui::PushItemWidth(inputWidth);

		if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			guiHoverAxis = 0;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::SameLine();

		if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			guiHoverAxis = 1;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;
		ImGui::SameLine();

		if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f")) { scaled = true; }
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			guiHoverAxis = 2;
		if (ImGui::IsItemActive())
			inputsAreDragging = true;

		if (inputsWereDragged && !inputsAreDragging) {
			if (app->undoEntityState->getOrigin() != app->pickInfo.ent->getOrigin()) {
				app->pushEntityUndoState("Move Entity");
			}

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

		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 3));
		ImGui::PopItemWidth();

		ImGui::Dummy(ImVec2(0, style.FramePadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));


		ImGui::Columns(4, 0, false);
		ImGui::SetColumnWidth(0, inputWidth4);
		ImGui::SetColumnWidth(1, inputWidth4);
		ImGui::SetColumnWidth(2, inputWidth4);
		ImGui::SetColumnWidth(3, inputWidth4);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Target: "); ImGui::NextColumn();

		ImGui::RadioButton("Object", &app->transformTarget, TRANSFORM_OBJECT); ImGui::NextColumn();
		ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX); ImGui::NextColumn();
		ImGui::RadioButton("Origin", &app->transformTarget, TRANSFORM_ORIGIN); ImGui::NextColumn();

		ImGui::Text("3D Axes: "); ImGui::NextColumn();
		if (ImGui::RadioButton("Hide", &app->transformMode, TRANSFORM_NONE))
			app->showDragAxes = false;

		ImGui::NextColumn();
		if (ImGui::RadioButton("Move", &app->transformMode, TRANSFORM_MOVE))
			app->showDragAxes = true;

		ImGui::NextColumn();
		if (ImGui::RadioButton("Scale", &app->transformMode, TRANSFORM_SCALE))
			app->showDragAxes = true;

		ImGui::Columns(1);

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

		ImGui::PushItemWidth(inputWidth);
		ImGui::Checkbox("Texture lock", &app->textureLock);
		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Doesn't work for angled faces yet. Applies to scaling only.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
		ImGui::PopItemWidth();

		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
		ImGui::Text(("Size: " + app->selectionSize.toKeyvalueString(false, "w ", "l ", "h")).c_str());

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

					ent->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString(!app->gridSnappingEnabled));
					bspRenderer->refreshEnt(app->pickInfo.entIdx);
					app->updateEntConnectionPositions();
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN) {
					vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
					newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

					app->transformedOrigin = newOrigin;
				}
			}
			if (scaled && ent->isBspModel() && app->isTransformableSolid && !app->modelUsesSharedStructures) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					app->scaleSelectedVerts(sx, sy, sz);
				}
				else if (app->transformTarget == TRANSFORM_OBJECT) {
					int modelIdx = ent->getBspModelIdx();
					app->scaleSelectedObject(sx, sy, sz);
					app->mapRenderers[app->pickInfo.mapIdx]->refreshModel(ent->getBspModelIdx());
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN) {
					logf("Scaling has no effect on origins\n");
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
	// data copied to new array so that ImGui doesn't delete static data
	byte* smallFontData = new byte[sizeof(robotomedium)];
	byte* largeFontData = new byte[sizeof(robotomedium)];
	byte* consoleFontData = new byte[sizeof(robotomono)];
	byte* consoleFontLargeData = new byte[sizeof(robotomono)];
	memcpy(smallFontData, robotomedium, sizeof(robotomedium));
	memcpy(largeFontData, robotomedium, sizeof(robotomedium));
	memcpy(consoleFontData, robotomono, sizeof(robotomono));
	memcpy(consoleFontLargeData, robotomono, sizeof(robotomono));

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	smallFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, sizeof(robotomedium), fontSize);
	largeFont = io.Fonts->AddFontFromMemoryTTF((void*)largeFontData, sizeof(robotomedium), fontSize * 1.1f);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, sizeof(robotomono), fontSize);
	consoleFontLarge = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontLargeData, sizeof(robotomono), fontSize * 1.1f);
}

void Gui::drawLog() {

	ImGui::SetNextWindowSize(ImVec2(750, 300), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	if (!ImGui::Begin("Log", &showLogWidget))
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

	if (copy) ImGui::LogBegin(ImGuiLogType_Clipboard, 0);

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
	if (ImGui::Begin("Settings", &showSettingsWidget))
	{
		const int settings_tabs = 5;
		static const char* tab_titles[settings_tabs] = {
			"General",
			"FGDs",
			"Asset Paths",
			"Rendering",
			"Controls"
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
			strncpy(workingdir, g_settings.workingdir.c_str(), 256);
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
			ImGui::InputText("Game Directory", gamedir, 256);
			ImGui::Text("Working Directory:");
			ImGui::InputText("(Relative to Game dir)", workingdir, 256);
			if (ImGui::DragInt("Font Size", &fontSize, 0.1f, 8, 48, "%d pixels")) {
				shouldReloadFonts = true;
			}
			ImGui::DragInt("Undo Levels", &app->undoLevels, 0.05f, 0, 64);
			ImGui::Checkbox("Verbose Logging", &g_verbose);
		}
		else if (settingsTab == 1) {
			for (int i = 0; i < numFgds; i++) {
				ImGui::SetNextItemWidth(pathWidth);
				tmpFgdPaths[i].resize(256);
				ImGui::InputText(("##fgd" + to_string(i)).c_str(), &tmpFgdPaths[i][0], 256);
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

			if (ImGui::Button("Add fgd path")) {
				numFgds++;
				if (numFgds > 64) {
					numFgds = 64;
				}
				else
				{
					tmpFgdPaths.push_back(std::string());
				}
			}
		}
		else if (settingsTab == 2) {
			for (int i = 0; i < numRes; i++) {
				ImGui::SetNextItemWidth(pathWidth);
				tmpResPaths[i].resize(256);
				ImGui::InputText(("##res" + to_string(i)).c_str(), &tmpResPaths[i][0], 256);
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

			if (ImGui::Button("Add res path")) {
				numRes++;
				if (numRes > 64) {
					numRes = 64;
				}
				else
				{
					tmpResPaths.push_back(std::string());
				}
			}
		}
		else if (settingsTab == 3) {
			ImGui::Text("Viewport:");
			if (ImGui::Checkbox("VSync", &vsync)) {
				glfwSwapInterval(vsync ? 1 : 0);
			}
			ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
			ImGui::DragFloat("Back Clipping plane", &app->zFar, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Separator();

			bool renderTextures = g_render_flags & RENDER_TEXTURES;
			bool renderLightmaps = g_render_flags & RENDER_LIGHTMAPS;
			bool renderWireframe = g_render_flags & RENDER_WIREFRAME;
			bool renderEntities = g_render_flags & RENDER_ENTS;
			bool renderSpecial = g_render_flags & RENDER_SPECIAL;
			bool renderSpecialEnts = g_render_flags & RENDER_SPECIAL_ENTS;
			bool renderPointEnts = g_render_flags & RENDER_POINT_ENTS;
			bool renderOrigin = g_render_flags & RENDER_ORIGIN;
			bool renderWorldClipnodes = g_render_flags & RENDER_WORLD_CLIPNODES;
			bool renderEntClipnodes = g_render_flags & RENDER_ENT_CLIPNODES;
			bool renderEntConnections = g_render_flags & RENDER_ENT_CONNECTIONS;

			ImGui::Text("Render Flags:");

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox("Textures", &renderTextures)) {
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox("Lightmaps", &renderLightmaps)) {
				g_render_flags ^= RENDER_LIGHTMAPS;
				for (int i = 0; i < app->mapRenderers.size(); i++)
					app->mapRenderers[i]->updateModelShaders();
			}
			if (ImGui::Checkbox("Wireframe", &renderWireframe)) {
				g_render_flags ^= RENDER_WIREFRAME;
			}
			if (ImGui::Checkbox("Origin", &renderOrigin)) {
				g_render_flags ^= RENDER_ORIGIN;
			}
			if (ImGui::Checkbox("Entity Links", &renderEntConnections)) {
				g_render_flags ^= RENDER_ENT_CONNECTIONS;
				if (g_render_flags & RENDER_ENT_CONNECTIONS) {
					app->updateEntConnections();
				}
			}

			ImGui::NextColumn();

			if (ImGui::Checkbox("Point Entities", &renderPointEnts)) {
				g_render_flags ^= RENDER_POINT_ENTS;
			}
			if (ImGui::Checkbox("Normal Solid Entities", &renderEntities)) {
				g_render_flags ^= RENDER_ENTS;
			}
			if (ImGui::Checkbox("Special Solid Entities", &renderSpecialEnts)) {
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}
			if (ImGui::Checkbox("Special World Faces", &renderSpecial)) {
				g_render_flags ^= RENDER_SPECIAL;
			}


			ImGui::Columns(1);

			ImGui::Separator();

			ImGui::Text("Clipnode Rendering:");

			ImGui::Columns(2, 0, false);
			ImGui::RadioButton("Auto", &app->clipnodeRenderHull, -1);
			ImGui::RadioButton("0 - Point", &app->clipnodeRenderHull, 0);
			ImGui::RadioButton("1 - Human", &app->clipnodeRenderHull, 1);
			ImGui::RadioButton("2 - Large", &app->clipnodeRenderHull, 2);
			ImGui::RadioButton("3 - Head", &app->clipnodeRenderHull, 3);

			ImGui::NextColumn();

			if (ImGui::Checkbox("World Leaves", &renderWorldClipnodes)) {
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			if (ImGui::Checkbox("Entity Leaves", &renderEntClipnodes)) {
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}
			static bool transparentNodes = true;
			if (ImGui::Checkbox("Transparency", &transparentNodes)) {
				for (int i = 0; i < g_app->mapRenderers.size(); i++) {
					g_app->mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}

			ImGui::Columns(1);
		}
		else if (settingsTab == 4) {
			ImGui::DragFloat("Movement speed", &app->moveSpeed, 0.1f, 0.1f, 1000, "%.1f");
			ImGui::DragFloat("Rotation speed", &app->rotationSpeed, 0.01f, 0.1f, 100, "%.1f");
		}


		ImGui::EndChild();

		ImGui::EndChild();

		if (settingsTab <= 2) {
			ImGui::Separator();

			if (ImGui::Button("Apply Changes")) {
				g_settings.gamedir = string(gamedir);
				/* fixup gamedir */
				if (g_settings.gamedir.size() && (g_settings.gamedir[g_settings.gamedir.size() - 1] == '/'
					|| g_settings.gamedir[g_settings.gamedir.size() - 1] == '\\'))
				{
					g_settings.gamedir.pop_back();
					sprintf(gamedir, "%s", g_settings.gamedir.c_str());
				}

				g_settings.fgdPaths.clear();
				for (auto const& s : tmpFgdPaths)
				{
					if (s[0] != '\0')
						g_settings.fgdPaths.push_back(s.c_str()); // c_str for remove NULL characters
				}
				g_settings.resPaths.clear();
				for (auto const& s : tmpResPaths)
				{
					if (s[0] != '\0')
						g_settings.resPaths.push_back(s.c_str());
				}
				app->reloadFgdsAndTextures();
			}
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

void Gui::drawLimits() {
	ImGui::SetNextWindowSize(ImVec2(550, 630), ImGuiCond_FirstUseEver);

	Bsp* map = app->pickInfo.valid ? app->mapRenderers[app->pickInfo.mapIdx]->map : NULL;
	string title = map ? "Limits - " + map->name : "Limits";

	if (ImGui::Begin((title + "###limits").c_str(), &showLimitsWidget)) {

		if (map == NULL) {
			ImGui::Text("No map selected");
		}
		else {
			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Summary")) {

					if (!loadedStats) {
						stats.clear();
						stats.push_back(calcStat("models", map->modelCount, MAX_MAP_MODELS, false));
						stats.push_back(calcStat("planes", map->planeCount, MAX_MAP_PLANES, false));
						stats.push_back(calcStat("vertexes", map->vertCount, MAX_MAP_VERTS, false));
						stats.push_back(calcStat("nodes", map->nodeCount, MAX_MAP_NODES, false));
						stats.push_back(calcStat("texinfos", map->texinfoCount, MAX_MAP_TEXINFOS, false));
						stats.push_back(calcStat("faces", map->faceCount, MAX_MAP_FACES, false));
						stats.push_back(calcStat("clipnodes", map->clipnodeCount, MAX_MAP_CLIPNODES, false));
						stats.push_back(calcStat("leaves", map->leafCount, MAX_MAP_LEAVES, false));
						stats.push_back(calcStat("marksurfaces", map->marksurfCount, MAX_MAP_MARKSURFS, false));
						stats.push_back(calcStat("surfedges", map->surfedgeCount, MAX_MAP_SURFEDGES, false));
						stats.push_back(calcStat("edges", map->edgeCount, MAX_MAP_EDGES, false));
						stats.push_back(calcStat("textures", map->textureCount, MAX_MAP_TEXTURES, false));
						stats.push_back(calcStat("lightdata", map->lightDataLength, MAX_MAP_LIGHTDATA, true));
						stats.push_back(calcStat("visdata", map->visDataLength, MAX_MAP_VISDATA, true));
						stats.push_back(calcStat("entities", map->ents.size(), MAX_MAP_ENTS, false));
						loadedStats = true;
					}

					ImGui::BeginChild("content");
					ImGui::Dummy(ImVec2(0, 10));
					ImGui::PushFont(consoleFontLarge);

					int midWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
					int otherWidth = (ImGui::GetWindowWidth() - midWidth) / 2;
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					ImGui::Text("Data Type"); ImGui::NextColumn();
					ImGui::Text(" Current / Max"); ImGui::NextColumn();
					ImGui::Text("Fullness"); ImGui::NextColumn();

					ImGui::Columns(1);
					ImGui::Separator();
					ImGui::BeginChild("chart");
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					for (int i = 0; i < stats.size(); i++) {
						ImGui::TextColored(stats[i].color, stats[i].name.c_str()); ImGui::NextColumn();

						string val = stats[i].val + " / " + stats[i].max;
						ImGui::TextColored(stats[i].color, val.c_str());
						ImGui::NextColumn();

						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.4f, 0, 1));
						ImGui::ProgressBar(stats[i].progress, ImVec2(-1, 0), stats[i].fullness.c_str());
						ImGui::PopStyleColor(1);
						ImGui::NextColumn();
					}

					ImGui::Columns(1);
					ImGui::EndChild();
					ImGui::PopFont();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Clipnodes")) {
					drawLimitTab(map, SORT_CLIPNODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Nodes")) {
					drawLimitTab(map, SORT_NODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Faces")) {
					drawLimitTab(map, SORT_FACES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Vertices")) {
					drawLimitTab(map, SORT_VERTS);
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

	int selected = app->pickInfo.valid ? app->pickInfo.entIdx : -1;

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
				app->pickInfo.ent = ent;
				app->pickInfo.entIdx = entIdx;
				app->pickInfo.modelIdx = map->ents[entIdx]->getBspModelIdx();
				app->pickInfo.valid = true;
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

void Gui::drawEntityReport() {
	ImGui::SetNextWindowSize(ImVec2(550, 630), ImGuiCond_FirstUseEver);

	Bsp* map = app->pickInfo.valid ? app->mapRenderers[app->pickInfo.mapIdx]->map : NULL;
	string title = map ? "Entity Report - " + map->name : "Entity Report";

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
			static bool filterNeeded = true;
			static vector<int> visibleEnts;
			static vector<bool> selectedItems;

			const ImGuiKeyModFlags expected_key_mod_flags = ImGui::GetMergedKeyModFlags();

			int footerHeight = ImGui::GetFrameHeightWithSpacing() * 5 + 16;
			ImGui::BeginChild("entlist", ImVec2(0, -footerHeight));

			if (filterNeeded) {
				visibleEnts.clear();
				for (int i = 1; i < map->ents.size(); i++) {
					Entity* ent = map->ents[i];
					string cname = ent->keyvalues["classname"];

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
								if ((partialMatches && ent->keyvalues[actualKey].find(searchValue) == string::npos) ||
									(!partialMatches && ent->keyvalues[actualKey] != searchValue)) {
									visible = false;
									break;
								}
							}
						}
						else if (strlen(valueFilter[k]) > 0) {
							string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							bool foundMatch = false;
							for (int c = 0; c < ent->keyOrder.size(); c++) {
								string val = toLowerCase(ent->keyvalues[ent->keyOrder[c]]);
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
						visibleEnts.push_back(i);
					}
				}

				selectedItems.clear();
				selectedItems.resize(visibleEnts.size());
				for (int k = 0; k < selectedItems.size(); k++)
					selectedItems[k] = false;
			}
			filterNeeded = false;

			ImGuiListClipper clipper;
			clipper.Begin(visibleEnts.size());

			while (clipper.Step())
			{
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd && line < visibleEnts.size(); line++)
				{
					int i = line;
					int entIdx = visibleEnts[i];
					Entity* ent = map->ents[entIdx];
					string cname = ent->keyvalues["classname"];

					if (ImGui::Selectable((cname + "##ent" + to_string(i)).c_str(), selectedItems[i], ImGuiSelectableFlags_AllowDoubleClick)) {
						if (expected_key_mod_flags & ImGuiKeyModFlags_Ctrl) {
							selectedItems[i] = !selectedItems[i];
							lastSelect = i;
						}
						else if (expected_key_mod_flags & ImGuiKeyModFlags_Shift) {
							if (lastSelect >= 0) {
								int begin = i > lastSelect ? lastSelect : i;
								int end = i > lastSelect ? i : lastSelect;
								for (int k = 0; k < selectedItems.size(); k++)
									selectedItems[k] = false;
								for (int k = begin; k < end; k++)
									selectedItems[k] = true;
								selectedItems[lastSelect] = true;
								selectedItems[i] = true;
							}
						}
						else {
							for (int k = 0; k < selectedItems.size(); k++)
								selectedItems[k] = false;
							selectedItems[i] = true;
							lastSelect = i;
						}

						if (ImGui::IsMouseDoubleClicked(0)) {
							app->goToEnt(map, entIdx);
						}

						g_app->selectEnt(map, entIdx);
					}

					if (selectedItems[i] && ImGui::IsItemHovered() && ImGui::IsMouseReleased(1)) {
						ImGui::OpenPopup("ent_report_context");
					}
				}
			}

			if (ImGui::BeginPopup("ent_report_context"))
			{
				if (ImGui::MenuItem("Delete")) {
					vector<Entity*> newEnts;

					set<int> selectedEnts;
					for (int i = 0; i < selectedItems.size(); i++) {
						if (selectedItems[i])
							selectedEnts.insert(visibleEnts[i]);
					}

					for (int i = 0; i < map->ents.size(); i++) {
						if (selectedEnts.find(i) != selectedEnts.end()) {
							delete map->ents[i];
						}
						else {
							newEnts.push_back(map->ents[i]);
						}
					}
					map->ents = newEnts;
					app->deselectObject();
					app->mapRenderers[app->pickInfo.mapIdx]->preRenderEnts();
					reloadLimits();
					filterNeeded = true;
				}

				ImGui::EndPopup();
			}

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
						string cname = ent->keyvalues["classname"];

						if (uniqueClasses.find(cname) == uniqueClasses.end()) {
							usedClasses.push_back(cname);
							uniqueClasses.insert(cname);
						}
					}
					sort(usedClasses.begin(), usedClasses.end());

				}
				for (int k = 0; k < usedClasses.size(); k++) {
					bool selected = usedClasses[k] == classFilter;
					if (ImGui::Selectable(usedClasses[k].c_str(), selected)) {
						classFilter = usedClasses[k];
						filterNeeded = true;
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
			inputWidth -= smallFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " = ").x;

			for (int i = 0; i < MAX_FILTERS; i++) {
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Key" + to_string(i)).c_str(), keyFilter[i], 64)) {
					filterNeeded = true;
				}
				ImGui::SameLine();
				ImGui::Text(" = "); ImGui::SameLine();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Value" + to_string(i)).c_str(), valueFilter[i], 64)) {
					filterNeeded = true;
				}
			}

			if (ImGui::Checkbox("Partial Matching", &partialMatches)) {
				filterNeeded = true;
			}

			ImGui::EndChild();

			ImGui::EndGroup();
		}
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


void Gui::drawLightMapTool() {
	static Texture* currentlightMap[MAXLIGHTMAPS] = { nullptr };
	static float colourPatch[3];

	static int windowWidth = 550;
	static int windowHeight = 520;
	static int lightmaps = 0;
	const char* light_names[] =
	{
		"OFF",
		"Main light",
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
			ImGui::TextUnformatted("Almost always breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		Bsp* map = app->pickInfo.valid ? app->pickInfo.map : NULL;
		if (map && app->selectedFaces.size() && app->pickInfo.mapIdx != -1)
		{
			int faceIdx = app->selectedFaces[0];
			BSPFACE& face = map->faces[faceIdx];
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);


			if (showLightmapEditorUpdate)
			{
				lightmaps = 0;
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

				if (ImGui::ImageButton((void*)currentlightMap[i]->id, imgSize, ImVec2(0, 0), ImVec2(1, 1), 0)) {
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
			app->mapRenderers[app->pickInfo.mapIdx]->showLightFlag = type - 1;
			ImGui::Separator();
			if (ImGui::Button("Save", ImVec2(120, 0)))
			{
				for (int i = 0; i < MAXLIGHTMAPS; i++) {
					if (face.nStyles[i] == 255 || currentlightMap[i] == nullptr)
						continue;
					int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
					int offset = face.nLightmapOffset + i * lightmapSz;
					memcpy(map->lightdata + offset, currentlightMap[i]->data, lightmapSz);
				}

				app->mapRenderers[app->pickInfo.mapIdx]->reloadLightmaps();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload", ImVec2(120, 0)))
			{
				showLightmapEditorUpdate = true;
			}
			/*
					TODO: Export and import from file
				ImGui::Separator();
				if (ImGui::Button("Export", ImVec2(120, 0)))
				{

				}
				ImGui::SameLine();
				if (ImGui::Button("Import", ImVec2(120, 0)))
				{
					showLightmapEditorUpdate = true;
				}
					TODO: Export/import all lightmaps from files
				ImGui::Separator();
				if (ImGui::Button("Export ALL", ImVec2(120, 0)))
				{

				}
				ImGui::SameLine();
				if (ImGui::Button("Import ALL", ImVec2(120, 0)))
				{

				}
			*/
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
	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	if (ImGui::Begin("Face Editor", &showTextureWidget)) {
		static float scaleX, scaleY, shiftX, shiftY;
		static bool isSpecial;
		static int width, height;
		static ImTextureID textureId = NULL; // OpenGL ID
		static char textureName[16];
		static int lastPickCount = -1;
		static bool validTexture = true;
		BspRenderer* mapRenderer = app->selectMapIdx >= 0 && app->selectMapIdx < app->mapRenderers.size() ? app->mapRenderers[app->selectMapIdx] : NULL;
		Bsp* map = app->pickInfo.valid ? app->pickInfo.map : NULL;
		if (mapRenderer == NULL || map == NULL || app->pickMode != PICK_FACE || app->selectedFaces.size() == 0)
		{
			ImGui::Text("No face selected");
			ImGui::End();
			return;
		}
		if (lastPickCount != app->pickCount && app->pickMode == PICK_FACE) {
			if (app->selectedFaces.size() && app->pickInfo.valid && mapRenderer != NULL) {
				int faceIdx = app->selectedFaces[0];
				BSPFACE& face = map->faces[faceIdx];
				BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
				int32_t texOffset = ((int32_t*)map->textures)[texinfo.iMiptex + 1];

				width = height = 0;
				if (texOffset != -1) {
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					width = tex.nWidth;
					height = tex.nHeight;
					strncpy(textureName, tex.szName, MAXTEXTURENAME);
				}
				else {
					textureName[0] = '\0';
				}

				int miptex = texinfo.iMiptex;

				scaleX = 1.0f / texinfo.vS.length();
				scaleY = 1.0f / texinfo.vT.length();
				shiftX = texinfo.shiftS;
				shiftY = texinfo.shiftT;
				isSpecial = texinfo.nFlags & TEX_SPECIAL;

				textureId = (void*)mapRenderer->getFaceTextureId(faceIdx);
				validTexture = true;

				// show default values if not all faces share the same values
				for (int i = 1; i < app->selectedFaces.size(); i++) {
					int faceIdx2 = app->selectedFaces[i];
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
		bool textureChanged = false;
		bool toggledFlags = false;

		ImGui::PushItemWidth(inputWidth);
		ImGui::Text("Scale");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Almost always breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##scalex", &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0) {
			scaledX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##scaley", &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0) {
			scaledY = true;
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Shift");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Sometimes breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##shiftx", &shiftX, 0.1f, 0, 0, "X: %.3f")) {
			shiftedX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##shifty", &shiftY, 0.1f, 0, 0, "Y: %.3f")) {
			shiftedY = true;
		}
		ImGui::PopItemWidth();

		ImGui::Text("Flags");
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

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Texture");
		ImGui::SetNextItemWidth(inputWidth);
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

		if ((scaledX || scaledY || shiftedX || shiftedY || textureChanged || refreshSelectedFaces || toggledFlags)) {
			uint32_t newMiptex = 0;
			if (textureChanged) {
				validTexture = false;

				int32_t totalTextures = ((int32_t*)map->textures)[0];

				for (uint i = 0; i < totalTextures; i++) {
					int32_t texOffset = ((int32_t*)map->textures)[i + 1];
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					if (strcmp(tex.szName, textureName) == 0) {
						validTexture = true;
						newMiptex = i;
						break;
					}
				}
			}
			set<int> modelRefreshes;
			for (int i = 0; i < app->selectedFaces.size(); i++) {
				int faceIdx = app->selectedFaces[i];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);

				if (scaledX) {
					texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
				}
				if (scaledY) {
					texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}
				if (shiftedX) {
					texinfo->shiftS = shiftX;
				}
				if (shiftedY) {
					texinfo->shiftT = shiftY;
				}
				if (toggledFlags) {
					texinfo->nFlags = isSpecial ? TEX_SPECIAL : 0;
				}
				if ((textureChanged || toggledFlags) && validTexture) {
					if (textureChanged)
						texinfo->iMiptex = newMiptex;
					modelRefreshes.insert(map->get_model_from_face(faceIdx));
				}
				mapRenderer->updateFaceUVs(faceIdx);
			}
			if (textureChanged || toggledFlags) {
				textureId = (void*)mapRenderer->getFaceTextureId(app->selectedFaces[0]);
				for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++) {
					mapRenderer->refreshModel(*it);
				}
				for (int i = 0; i < app->selectedFaces.size(); i++) {
					mapRenderer->highlightFace(app->selectedFaces[i], true);
				}
			}

			checkFaceErrors();
		}

		refreshSelectedFaces = false;

		ImVec2 imgSize = ImVec2(inputWidth * 2 - 2, inputWidth * 2 - 2);
		if (ImGui::ImageButton(textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1), 1)) {
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
			targetname = map->ents[k]->keyvalues["targetname"];
			classname = map->ents[k]->keyvalues["classname"];
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
		for (int k = 0; k < app->mapRenderers.size() && !anyHullValid[i]; k++) {
			Bsp* map = app->mapRenderers[k]->map;

			for (int m = 0; m < map->modelCount; m++) {
				if (map->models[m].iHeadnodes[i] >= 0) {
					anyHullValid[i] = true;
					break;
				}
			}
		}
	}
}

void Gui::checkFaceErrors() {
	lightmapTooLarge = badSurfaceExtents = false;

	if (!app->pickInfo.valid)
		return;

	Bsp* map = app->pickInfo.map;

	for (int i = 0; i < app->selectedFaces.size(); i++) {
		int size[2];
		if (!GetFaceLightmapSize(map, app->selectedFaces[i], size)) {
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
}