#include "MenuBar.h"
#include "Entity.h"
#include "Widget.h"

void MenuBar::draw() {
	ImGui::BeginMainMenuBar();
	bool mergeMode = g_app->mapArrangeMode;

	if (g_app->mapArrangeMode) {
		if (ImGui::BeginMenu("Merge Multiple"))
		{
			if (ImGui::MenuItem("Cancel Merge")) {
				g_app->openMap(g_app->openMapAfterMergeCancel.c_str());
				g_app->openMapAfterMergeCancel = "";
			}
			if (ImGui::MenuItem("Merge")) {
				vector<Bsp*> bsps;
				for (BspRenderer* renderer : app->arrangeBsps) {
					bsps.push_back(renderer->map);
				}
				vector<MapMergeOp> mergeOps;
				if (BspMerger::solveMerge(bsps, mergeOps) == 0) {
					Bsp* result = BspMerger::createMergedMap(bsps, mergeOps, app->mergeOptimize, app->mergeNohull2, app->mergeRipentMode);
					if (result) {
						app->openMap(result);
					}
					else {
						logf("Merge failed!\n");
					}
				}
				else {
					logf("Merger unable to solve\n");
				}
			}

			ImGui::EndMenu();
		}
	}
	else {
		drawFileMenu();
		drawEditMenu();
		drawViewMenu();
		drawSettingsMenu();
		drawCreateMenu();
		drawToolsMenu();
		drawWidgetsMenu();
		drawHelpMenu();
	}


	string fpsText = to_string((int)ImGui::GetIO().Framerate) + " FPS";
	string statsText = fpsText;
	string wpolyText;
	ImVec4 wpolyColor = ImVec4(1, 1, 1, 1);

	if (g_app->mapRenderer->pvsDat && g_settings.show_wpoly) {
		int wpoly = g_app->mapRenderer->pvsDat->wpoly;
		wpolyText = to_string(wpoly);
		fpsText = fpsText + ",  ";
		statsText = fpsText + wpolyText + " wpoly";

		if (wpoly >= 2000) {
			wpolyColor = ImVec4(1, 0, 0, 1);
		}
		else if (wpoly >= 1000) {
			wpolyColor = ImVec4(1, 0.6f, 0, 1);
		}
	}

	float fpsW = ImGui::CalcTextSize(fpsText.c_str()).x;
	float wpolyW = ImGui::CalcTextSize(wpolyText.c_str()).x;
	float labelW = ImGui::CalcTextSize(" wpoly").x;
	float totalW = fpsW;
	if (g_settings.show_wpoly)
		totalW += wpolyW + labelW;

	float avail = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW);

	ImGui::Text(fpsText.c_str());

	if (g_settings.show_wpoly) {
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextColored(wpolyColor, wpolyText.c_str());
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Text(" wpoly");
	}

	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("VSync", NULL, gui->vsync)) {
			gui->vsync = !gui->vsync;
			glfwSwapInterval(gui->vsync ? 1 : 0);
		}
		if (ImGui::MenuItem("wpoly", NULL, g_settings.show_wpoly)) {
			g_settings.show_wpoly = !g_settings.show_wpoly;
		}
		tooltip("Display the number of polygons that would be rendered in-game. Enabling this reduces FPS when wpoly counts are high.");

		ImGui::EndPopup();
	}

	height = ImGui::GetWindowHeight();

	ImGui::EndMainMenuBar();
}



void MenuBar::drawFileMenu() {
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Open", "Ctrl+O", false, !app->isLoading)) {
			g_app->openMap((char*)NULL);
		}

		if (ImGui::BeginMenu("Recent Files", !app->isLoading)) {
			if (g_settings.recentFiles.size()) {
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
			}

			if (ImGui::MenuItem("Clear", NULL, false, g_settings.recentFiles.size())) {
				g_settings.recentFiles.clear();
				g_settings.save();
			}

			ImGui::EndMenu();
		}

		Bsp* map = app->mapRenderer->map;

		ImGui::BeginDisabled(app->emptyMapLoaded);
		if (ImGui::MenuItem("Save", NULL)) {
			map->update_ent_lump();
			//map->write("yabma_move.bsp");
			//map->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
			map->write(map->path);
		}
		if (ImGui::MenuItem("Save As...", "Ctrl+Alt+S")) {
			saveAs();
		}
		if (ImGui::MenuItem("Save a Copy As...", NULL)) {
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

		if (ImGui::BeginMenu("Export"))
		{
			string defaultPath = map->path;
			int lastDot = defaultPath.find_last_of(".");
			if (lastDot != -1) {
				defaultPath = defaultPath.substr(0, lastDot);
			}

			if (ImGui::MenuItem("Entities (.ent)", "")) {
				char* fname = tinyfd_saveFileDialog("Export Entities", defaultPath.c_str(),
					1, entFilterPatterns, "Entity File (*.ent)");

				if (fname) {
					map->update_ent_lump();
					FILE* outfile = fopen(fname, "w");
					fwrite(map->lumps[LUMP_ENTITIES], map->header.lump[LUMP_ENTITIES].nLength, 1, outfile);
					fclose(outfile);
				}
			}
			tooltip("Save entity definitions to a file.");

			if (ImGui::MenuItem("Embedded Textures (.wad)", "")) {
				char* fname = tinyfd_saveFileDialog("Export Embedded Textures", defaultPath.c_str(),
					1, wadFilterPatterns, "Half-Life Package (*.wad)");

				if (fname) {
					vector<WADTEX> wadTextures = map->get_embedded_textures();

					Wad outWad = Wad();
					outWad.write(fname, &wadTextures[0], wadTextures.size());
					logf("Exported %d embedded textures to: %s\n", wadTextures.size(), fname);

					for (WADTEX& tex : wadTextures) {
						delete[] tex.data;
					}
				}
			}
			tooltip("Saves embedded textures to a WAD file. This does not unembed any textures.");

			if (ImGui::MenuItem("Leaf Portals (.prt)", "", false, !app->isLoading)) {
				char* fname = tinyfd_saveFileDialog("Export Leaf Portals", defaultPath.c_str(),
					1, prtFilterPatterns, "Leaf Portals (*.prt)");

				if (fname) {
					if (!app->mapRenderer->leafNavMesh) {
						app->mapRenderer->reloadLeaves(true);
					}

					map->write_portal_file(app->mapRenderer->leafNavMesh, fname);
				}
			}
			tooltip("Generate a leaf portal file for use with a VIS compiler.\n\n"
				"Leaf portals are the polygonal areas where leaves are touching. They indicate where you can move from one leaf to another.");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Import"))
		{
			if (ImGui::MenuItem("Entities (.ent)", "")) {
				string defaultPath = map->path;
				int lastDot = defaultPath.find_last_of(".");
				if (lastDot != -1) {
					defaultPath = defaultPath.substr(0, lastDot);
				}
				defaultPath = defaultPath + ".ent";

				char* fname = tinyfd_openFileDialog("Import Entities", defaultPath.c_str(),
					1, entFilterPatterns, "Entity File (*.ent)", false);

				if (fname) {
					int len;
					char* data = loadFile(fname, len);
					std::string entString = std::string(data, len);

					LumpReplaceCommand* undoCommand = new LumpReplaceCommand("Import Entities");

					CreateEntityFromTextCommand* command =
						new CreateEntityFromTextCommand("Import Entities", entString);

					map->ents.clear();
					command->execute();

					logf("Imported %d entities\n", command->createdEnts);

					int worldspawnIdx = -1;
					int worldspawnCount = 0;
					for (int i = 0; i < map->ents.size(); i++) {
						if (map->ents[i]->getClassname() == "worldspawn") {
							if (worldspawnIdx == -1)
								worldspawnIdx = i;
							worldspawnCount++;
						}
					}

					if (worldspawnCount > 1) {
						logf("WARNING: Multiple worldspawn entities were defined. The first definition was used.\n");
					}

					if (worldspawnIdx == -1) {
						logf("A worldspawn entity was not defined in the .ent file. A default will be created.\n");
						Entity* defaultWorldspawn = new Entity();
						defaultWorldspawn->setOrAddKeyvalue("classname", "worldspawn");
						defaultWorldspawn->setOrAddKeyvalue("MaxRange", "32768");
						defaultWorldspawn->setOrAddKeyvalue("skyname", "desert");
						map->ents.insert(map->ents.begin(), defaultWorldspawn);
					}
					else if (worldspawnIdx != 0) {
						logf("The worldspawn entity was moved to the first entity index.\n");
						Entity* temp = map->ents[0];
						map->ents[0] = map->ents[worldspawnIdx];
						map->ents[worldspawnIdx] = temp;
					}

					delete command;
					undoCommand->pushUndoState();
					gui->entityReportFilterNeeded = true;
					app->pickInfo.deselect();
					app->postSelectEnt();
				}
			}
			tooltip("Delete all map entities and load new ones from a file."
				"\n\nTo add additional entities instead of replacing them, copy entity text from your .ent file and select Paste here.");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (!g_settings.ripent_safe_mode) {
			if (ImGui::MenuItem("Merge", NULL, false, !app->isLoading)) {
				char* fname = tinyfd_openFileDialog("Merge Map", "",
					1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

				if (fname)
					g_app->merge(fname);
			}
			tooltip(("Merge one other BSP into the current file.\n\n"
				"Equivalent CLI command:\nbspguy merge " + map->name + " -noripent -maps \""
				+ map->name + ",other_map\"").c_str());

			if (ImGui::MenuItem("Merge Multiple", NULL, false, !app->isLoading) && g_app->confirmMapExit()) {
				gui->showWidget(WIDGET_MERGE_MULTI, true);
			}
			tooltip("Merge multiple BSPs into a new file.");
		}

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
			gui->refresh();
		}
		tooltip("Discard all changes and reload the map.\n");

		if (ImGui::MenuItem("Validate")) {
			logf("\n-------- Validating %s --------\n", map->name.c_str());
			map->validate();
			logf("-----------------------------------------\n", map->name.c_str());
			ImGui::SetWindowCollapsed("Messages", false);
			gui->widgets[WIDGET_MESSAGES]->widgetVisible = true;
		}
		tooltip("Checks BSP data structures for invalid values and references. Trivial problems are fixed automatically. Results are output to the Messages widget.");
		ImGui::EndDisabled();

		ImGui::Separator();
		if (ImGui::MenuItem("Exit", NULL)) {
			app->exit();
			std::exit(0);
		}
		ImGui::EndMenu();
	}

}

void MenuBar::drawEditOptions(bool isMainMenu) {
	ImGuiContext& g = *GImGui;
	ImGuiIO& io = ImGui::GetIO();

	bool entSelected = app->pickInfo.getEnt();
	bool nonWorldspawnEntSelected = entSelected && app->pickInfo.getEntIndex() != 0;

	if (ImGui::MenuItem("Cut", "Ctrl+X", false, nonWorldspawnEntSelected)) {
		app->cutEnts();
	}
	if (ImGui::MenuItem("Copy", "Ctrl+C", false, nonWorldspawnEntSelected)) {
		app->copyEnts(false);
	}

	if (isMainMenu) {
		static bool canPaste = false;
		if (!editWasOpen)
			canPaste = app->canPasteEnts();
		editWasOpen = true;

		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste && !app->isLoading)) {
			app->pasteEnts(false);
		}
		tooltip("Paste entities from your clipboard. Entity data is stored as text which you "
			"can transfer to text editors or other bspguy windows.");
		if (ImGui::MenuItem("Paste at original origin", 0, false, canPaste && !app->isLoading)) {
			app->pasteEnts(true);
		}
		tooltip("Pastes entities at the locations they were copied from.");
	}

	if (ImGui::MenuItem("Delete", "Del", false, nonWorldspawnEntSelected)) {
		app->deleteEnts();
	}

	Bsp* map = app->pickInfo.getMap();
	bool anyBspModelSelected = false;
	bool anyValidHeadnode[MAX_MAP_HULLS] = { false };
	bool anyRedirectValid[MAX_MAP_HULLS][MAX_MAP_HULLS] = { false };
	bool anyCanRedirect = false;
	vector<Entity*> pickEnts = app->pickInfo.getEnts();
	vector<int> modelIndexes;
	for (Entity* ent : pickEnts) {
		int modelIdx = ent->getBspModelIdx();

		if (modelIdx < 0)
			continue;

		BSPMODEL& model = map->models[modelIdx];
		anyBspModelSelected = true;
		modelIndexes.push_back(modelIdx);
		for (int i = 0; i < MAX_MAP_HULLS; i++) {
			if (model.iHeadnodes[i] >= 0) {
				anyValidHeadnode[i] = true;
			}
		}
		if (model.iHeadnodes[1] != model.iHeadnodes[2] || model.iHeadnodes[1] != model.iHeadnodes[3]) {
			anyCanRedirect = true;
		}
		for (int i = 1; i < MAX_MAP_HULLS; i++) {
			for (int k = 1; k < MAX_MAP_HULLS; k++) {
				if (model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] != model.iHeadnodes[i]) {
					anyRedirectValid[i][k] = true;
				}
			}
		}
	}

	if (anyBspModelSelected) {
		ImGui::Separator();

		bool anySolidSelected = false;
		int numSolidsSelected = 0;
		vector<Entity*> pickEnts = app->pickInfo.getEnts();
		if (app->pickInfo.getEntIndex() > 0) {

			for (Entity* ent : pickEnts) {
				if (ent->getBspModelIdx() != -1 && app->pickInfo.getEntIndex() != 0) {
					anySolidSelected = true;
					numSolidsSelected++;
				}
			}
		}

		bool plural = numSolidsSelected > 1;
		if (ImGui::BeginMenu(plural ? "BSP Models" : "BSP Model")) {
			if (!g_settings.ripent_safe_mode) {
				if (ImGui::MenuItem("Copy", 0, false, !app->isLoading && anySolidSelected)) {
					app->copyEnts(true);
				}
				tooltip("Stores the entity and BSP model to the clipboard. Used to transfer BSP models between maps.\n\n"
					"Textures are included and embedded after pasting, except for textures that already exist in the map. ");
			}

			if (ImGui::MenuItem("Deduplicate", 0, false, !app->isLoading && anySolidSelected && app->pickInfo.ents.size() > 1)) {
				app->updateEntityLumpUndoState(map);

				int firstModel = pickEnts[0]->getBspModelIdx();

				vec3 minsA, maxsA, centerA;
				map->get_model_vertex_bounds(firstModel, minsA, maxsA);
				centerA = minsA + (maxsA - minsA) * 0.5f;

				for (Entity* ent : pickEnts) {
					int oldModelIdx = ent->getBspModelIdx();
					if (oldModelIdx != firstModel) {
						vec3 minsB, maxsB, centerB;
						map->get_model_vertex_bounds(oldModelIdx, minsB, maxsB);
						centerB = minsB + (maxsB - minsB) * 0.5f;
						vec3 offset = centerB - centerA;
						ent->setOrAddKeyvalue("origin", (ent->getOrigin() + offset).toKeyvalueString(true));
						ent->setOrAddKeyvalue("model", "*" + to_string(firstModel));
					}
				}

				app->pushEntityUndoState("Deduplicate Models");
				app->mapRenderer->preRenderEnts();
			}
			tooltip("Force entities to use the same BSP model.\n\n"
				"The model used by the first entity you selected will be applied to all other selected entities."
			);

			if (!g_settings.ripent_safe_mode) {
				if (ImGui::MenuItem("Duplicate", 0, false, !app->isLoading && anySolidSelected)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Duplicate BSP Model");

					for (Entity* ent : pickEnts) {
						int oldModelIdx = ent->getBspModelIdx();
						int newModelIdx = map->duplicate_model(oldModelIdx);
						ent->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));
					}

					command->pushUndoState();
				}
				tooltip("Create a copy of this BSP model and assign it to this entity.\n\n"
					"In most cases you need to do this before you can use the scale/vertex/origin features in the Transformation widget. "
					"This also prevents model edits from affecting multiple entities at once.");

				if (ImGui::MenuItem("Merge", "", false, !app->isLoading && app->pickInfo.ents.size() > 1)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Merge Models");

					// remove origins from models so that they merge at offsets seen in the editor
					int newIndex = map->merge_models(app->pickInfo.getEnts(), false);

					if (newIndex >= 0 || newIndex == -3) {
						command->pushUndoState();
					}
					else {
						delete command;

						if (newIndex == -2) {
							gui->showWidget(WIDGET_MODEL_MERGE_CONFIRM, true);
						}
					}
				}
				tooltip("Merge solid entity models together.");
			}

			if (ImGui::MenuItem("Select Shared")) {
				int oldSelection = app->pickInfo.ents.size();
				vector<int> modelIndexes = app->pickInfo.getModelIndexes();

				for (int i = 0; i < map->ents.size(); i++) {
					int modelIdx = map->ents[i]->getBspModelIdx();
					if (modelIdx <= 0) {
						continue;
					}

					for (int k = 0; k < modelIndexes.size(); k++) {
						if (modelIdx == modelIndexes[k]) {
							app->pickInfo.selectEnt(i);
							break;
						}
					}
				}

				logf("Selected %d additional entities\n", app->pickInfo.ents.size() - oldSelection);

				app->postSelectEnt();
			}
			tooltip("Select entities that share the selected BSP model(s).");

			ImGui::EndMenu();
		}

		if (!g_settings.ripent_safe_mode && ImGui::BeginMenu("Edit BSP Hulls", !app->isLoading)) {
			if (ImGui::BeginMenu("Create", !app->invalidSolid && app->isTransformableSolid && anyValidHeadnode[0])) {
				if (ImGui::MenuItem("Clipnodes")) {
					ModelEditCommand* command = new ModelEditCommand("Create Model Clipnodes", modelIndexes);

					for (int idx : modelIndexes) {
						BSPMODEL& model = map->models[idx];
						if (model.iHeadnodes[0] >= 0) {
							map->regenerate_clipnodes(idx, -1);
							logf("Regenerated hulls 1-3 on model %d\n", idx);
						}
						else {
							logf("Model %d has no nodes. Skipping.\n", idx);
						}
					}
					gui->checkValidHulls();

					// don't delete models so that indexes don't shift which would require a full refresh
					map->remove_unused_model_structures(false).print_delete_stats(1);
					gui->reloadLimits();

					command->pushUndoState();
				}
				tooltip("Creates clipnode hulls for the selected model by extending the planes of Hull 0.\nClipnodes are used for entity collision detection.");

				ImGui::Separator();

				for (int i = 1; i < MAX_MAP_HULLS; i++) {
					if (ImGui::MenuItem(("Hull " + to_string(i)).c_str())) {
						ModelEditCommand* command = new ModelEditCommand("Create Model Hull", modelIndexes);

						for (int idx : modelIndexes) {
							BSPMODEL& model = map->models[idx];
							if (model.iHeadnodes[0] >= 0) {
								map->regenerate_clipnodes(idx, i);
								logf("Regenerated hull %d on model %d\n", i, idx);
							}
							else {
								logf("Model %d has no nodes. Skipping.\n", idx);
							}
						}
						gui->checkValidHulls();

						map->remove_unused_model_structures(false).print_delete_stats(1);
						gui->reloadLimits();

						command->pushUndoState();
					}
					tooltip("Creates a clipnode hull for the selected model by extending the planes of Hull 0.\nClipnodes are used for entity collision detection.");
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Delete", !app->isLoading)) {
				if (ImGui::MenuItem("All Hulls")) {
					ModelEditCommand* command = new ModelEditCommand("Delete Model Hulls", modelIndexes);

					for (int idx : modelIndexes) {
						BSPMODEL& model = map->models[idx];
						if (model.iHeadnodes[0] >= 0 || model.iHeadnodes[1] >= 0 || model.iHeadnodes[2] >= 0 || model.iHeadnodes[3] >= 0) {
							map->delete_hull(0, idx, -1);
							map->delete_hull(1, idx, -1);
							map->delete_hull(2, idx, -1);
							map->delete_hull(3, idx, -1);
							logf("Deleted all hulls on model %d\n", idx);
						}
						else {
							logf("Model %d has no hulls. Skipping.\n", idx);
						}
					}
					gui->checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					gui->reloadLimits();

					command->pushUndoState();
				}
				tooltip("Deletes all hulls from the selected model. Be careful using this as it can cause crashes if the entity needs a deleted hull.");

				if (ImGui::MenuItem("Clipnodes")) {
					ModelEditCommand* command = new ModelEditCommand("Delete Model Clipnodes", modelIndexes);

					for (int idx : modelIndexes) {
						BSPMODEL& model = map->models[idx];
						if (model.iHeadnodes[1] >= 0 || model.iHeadnodes[2] >= 0 || model.iHeadnodes[3] >= 0) {
							map->delete_hull(1, idx, -1);
							map->delete_hull(2, idx, -1);
							map->delete_hull(3, idx, -1);
							logf("Deleted hulls 1-3 on model %d\n", idx);
						}
						else {
							logf("Model %d has no clipnodes. Skipping.\n", idx);
						}
					}
					gui->checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					gui->reloadLimits();

					command->pushUndoState();
				}
				tooltip("Deletes all clipnode hulls from the selected model. Be careful using this as it can cause crashes if the entity needs a deleted hull.");

				ImGui::Separator();

				for (int i = 0; i < MAX_MAP_HULLS; i++) {

					if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, anyValidHeadnode[i])) {
						ModelEditCommand* command = new ModelEditCommand("Delete Model Hull", modelIndexes);

						for (int idx : modelIndexes) {
							BSPMODEL& model = map->models[idx];
							if (model.iHeadnodes[i] >= 0) {
								map->delete_hull(i, idx, -1);
								logf("Deleted hull %d on model %d\n", i, idx);
							}
							else {
								logf("Model %d has no hull %d. Skipping.\n", idx, i);
							}
						}
						gui->checkValidHulls();

						logf("Cleaning %s\n", map->name.c_str());
						map->remove_unused_model_structures(false).print_delete_stats(1);
						gui->reloadLimits();

						command->pushUndoState();
					}
					tooltip("Deletes a hull from the selected model. Be careful using this as it can cause crashes if the entity needs the deleted hull.");
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Simplify", !app->isLoading && (anyValidHeadnode[1] || anyValidHeadnode[2] || anyValidHeadnode[3]))) {
				if (ImGui::MenuItem("Clipnodes")) {
					ModelEditCommand* command = new ModelEditCommand("Simplify Model Clipnodes", modelIndexes);

					for (int idx : modelIndexes) {
						BSPMODEL& model = map->models[idx];
						map->simplify_model_collision(idx, 1);
						map->simplify_model_collision(idx, 2);
						map->simplify_model_collision(idx, 3);
						logf("Replaced hulls 1-3 on model %d with a box-shaped hull.\n", idx);
					}

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					gui->reloadLimits();

					command->pushUndoState();
				}
				tooltip("Replaces all clipnode hulls with a simple box.");

				ImGui::Separator();

				for (int i = 1; i < MAX_MAP_HULLS; i++) {
					if (ImGui::MenuItem(("Hull " + to_string(i)).c_str(), 0, false, anyValidHeadnode[i])) {
						ModelEditCommand* command = new ModelEditCommand("Simplify Model Hull", modelIndexes);

						for (int idx : modelIndexes) {
							BSPMODEL& model = map->models[idx];
							if (model.iHeadnodes[i] >= 0) {
								map->simplify_model_collision(idx, 1);
								logf("Replaced hull %d on model %d with a box-shaped hull\n", i, idx);
							}
							else {
								logf("Model %d has no hull %d. Skipping.\n", idx, i);
							}
						}

						logf("Cleaning %s\n", map->name.c_str());
						map->remove_unused_model_structures(false).print_delete_stats(1);
						gui->reloadLimits();

						command->pushUndoState();
					}
					tooltip("Replaces a clipnode hull with a simple box.");
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Redirect", anyCanRedirect && !app->isLoading)) {
				for (int i = 1; i < MAX_MAP_HULLS; i++) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;

						if (ImGui::MenuItem(("Hull " + to_string(i) + " --> Hull " + to_string(k)).c_str(), 0, false, anyRedirectValid[i][k])) {
							ModelEditCommand* command = new ModelEditCommand("Redirect Model Hull", modelIndexes);

							for (int idx : modelIndexes) {
								BSPMODEL& model = map->models[idx];
								bool canRedirect = model.iHeadnodes[k] >= 0 && model.iHeadnodes[k] != model.iHeadnodes[i];
								if (canRedirect) {
									model.iHeadnodes[i] = model.iHeadnodes[k];
									logf("Redirected hull %d to hull %d on model %d\n", i, k, idx);
								}
								else {
									logf("Model %d has no hull %d or is already linked to hull %d. Skipping.\n", idx, k, i);
								}
							}
							gui->checkValidHulls();

							logf("Cleaning %s\n", map->name.c_str());
							map->remove_unused_model_structures(false).print_delete_stats(1);
							gui->reloadLimits();

							command->pushUndoState();
						}
						tooltip("Redirect a clipnode hull to another clipnode hull. This is safer than deleting but can make collision detection less accurate.");
					}

					if (i != MAX_MAP_HULLS - 1)
						ImGui::Separator();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}
	}

	ImGui::Separator();

	if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G", false, nonWorldspawnEntSelected)) {
		if (!app->movingEnt)
			app->grabEnts();
		else {
			app->ungrabEnts();
		}
	}
	tooltip("Attach the entity to your camera for easy movement.\n"
		"Mouse wheel scrolling controls the distance from the camera."
		"\nHold Shift/Ctrl for faster/slower distance adjustments.");

	bool shouldHide = app->pickInfo.shouldHideSelection();

	if (ImGui::MenuItem(shouldHide ? "Hide" : "Unhide", "H", false, nonWorldspawnEntSelected)) {
		if (shouldHide) {
			app->hideSelectedEnts();
		}
		else {
			app->unhideSelectedEnts();
		}
	}
	if (isMainMenu) {
		if (ImGui::MenuItem("Unhide All", 0, false, app->anyHiddenEnts || app->hiddenLeaves.size() || app->hiddenFaces.size())) {
			app->unhideEnts();
			app->unhideLeaves();
			app->unhideFaces();
		}
	}
	if (ImGui::MenuItem("Transform", "Ctrl+M")) {
		gui->widgets[WIDGET_TRANSFORM]->widgetVisible = !gui->widgets[WIDGET_TRANSFORM]->widgetVisible;
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Properties", "Alt+Enter")) {
		gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = !gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
	}
}

void MenuBar::drawEditMenu() {
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
		Bsp* map = app->mapRenderer->map;

		if (ImGui::MenuItem(undoTitle.c_str(), "Ctrl+Z", false, canUndo)) {
			app->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), "Ctrl+Y", false, canRedo)) {
			app->redo();
		}

		ImGui::Separator();

		drawEditOptions(true);

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}
	else
		editWasOpen = false;
}

void MenuBar::drawViewMenu() {
	if (ImGui::BeginMenu("View")) {
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::BeginMenu("Clipnodes")) {
			ImGui::BeginDisabled(app->isLoading);

			if (ImGui::MenuItem("World", 0, g_settings.render_flags & RENDER_WORLD_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_WORLD_CLIPNODES;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Render clipnode hulls for worldspawn. These may take a long time to generate, "
				"which slows startup time if you leave this enabled.");

			if (ImGui::MenuItem("Entities", 0, g_settings.render_flags & RENDER_ENT_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_ENT_CLIPNODES;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Render clipnode hulls for solid entities");

			if (ImGui::MenuItem("Transparency", 0, !(g_settings.render_flags & RENDER_CLIPNODE_OPAQUE))) {
				g_settings.render_flags ^= RENDER_CLIPNODE_OPAQUE;
			}
			tooltip("Render clipnode meshes with transparency.");
			g_settings.render_flags = g_settings.render_flags;

			ImGui::Separator();

			if (ImGui::MenuItem("Auto Hulls", 0, app->clipnodeRenderHull == -1)) {
				app->clipnodeRenderHull = -1;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Render collision hulls for things which would otherwise be invisible."
				"\n\nAn example of this is a trigger_once which had the NULL texture applied to it by the mapper. "
				"That entity would have no visible faces and so would be invisible if its collision hull were not rendered instead.");

			if (ImGui::MenuItem("Hull 0 (Point)", 0, app->clipnodeRenderHull == 0)) {
				app->clipnodeRenderHull = 0;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Renders hull 0 regardless of object visibility.\n\n"
				"This hull is used for point-sized object collision and mesh rendering.");

			if (ImGui::MenuItem("Hull 1 (Human)", 0, app->clipnodeRenderHull == 1)) {
				app->clipnodeRenderHull = 1;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Renders hull 1 regardless of object visibility."
				"\n\nThis is a collision hull used by standing players and human-sized monsters.");

			if (ImGui::MenuItem("Hull 2 (Large)", 0, app->clipnodeRenderHull == 2)) {
				app->clipnodeRenderHull = 2;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Renders hull 2 regardless of object visibility.\n\n"
				"This is a collision hull used by large monsters and pushable objects.");

			if (ImGui::MenuItem("Hull 3 (Head)", 0, app->clipnodeRenderHull == 3)) {
				app->clipnodeRenderHull = 3;
				app->mapRenderer->loadMoreClipnodes();
			}
			tooltip("Renders hull 3 regardless of object visibility.\n\n"
				"This is a collision hull used by crouching players and small monsters.");
			ImGui::EndDisabled();

			ImGui::EndMenu();
		}
		

		if (ImGui::BeginMenu("Entities")) {
			if (ImGui::MenuItem("Point Entities", 0, g_settings.render_flags & RENDER_POINT_ENTS)) {
				g_settings.render_flags ^= RENDER_POINT_ENTS;
			}
			tooltip("Render point-sized entities which either have no model or reference MDL/SPR files.");

			if (ImGui::MenuItem("Solid Entities", 0, g_settings.render_flags & RENDER_ENTS)) {
				if (g_settings.render_flags & RENDER_ENTS) {
					g_settings.render_flags &= ~(RENDER_ENTS | RENDER_SPECIAL_ENTS);
				}
				else {
					g_settings.render_flags |= RENDER_ENTS | RENDER_SPECIAL_ENTS;
				}
			}
			tooltip("Render entities that reference BSP models.");

			ImGui::Separator();

			if (ImGui::MenuItem("Direction Vectors", 0, g_settings.render_flags & RENDER_ENT_DIRECTIONS)) {
				g_settings.render_flags ^= RENDER_ENT_DIRECTIONS;
				app->updateEntDirectionVectors();
			}
			tooltip("Display direction vectors for selected entities.\n"
				"For point entities, vectors usually represent orientation.\n"
				"For solid entities, vectors usually represent movement direction.");

			if (ImGui::MenuItem("Links", 0, g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
				g_settings.render_flags ^= RENDER_ENT_CONNECTIONS;
				g_app->updateEntConnections();
			}
			tooltip("Show how entities connect to each other.\n\n"
				"Yellow line = Selected entity targets the connected entity.\n"
				"Blue line = Selected entity is targetted by the connected entity.\n"
				"Green line = Selected entity and connected entity target each other.\n\n"
				"Not all connections are displayed. You may still need to use the Entity Report "
				"to find connections depending on the game the map was compiled for."
			);

			if (ImGui::MenuItem("All Name Tags", 0, g_settings.render_flags & RENDER_NAME_TAGS)) {
				g_settings.render_flags ^= RENDER_NAME_TAGS;
			}
			tooltip("Display entity target names for all entities, not just what is selected. "
				"This may lag you hard and clutter your screen with an unreadable mess of text. "
				"It may also save you some clicking.");

			if (ImGui::MenuItem("Render Modes", 0, g_settings.render_flags & RENDER_RENDER_MODES)) {
				g_settings.render_flags ^= RENDER_RENDER_MODES;
				app->mapRenderer->reloadMegaBuffers();
			}
			tooltip("Models, sprites, and brushes render as they would in-game. "
				"Entity rendermode, renderamt, and rendercolor keys are respected.\n\n"
				"In some cases this makes entities completely invisible and difficult to select. "
				"This can also reduce FPS a good amount."
			);

			ImGui::Separator();

			if (ImGui::MenuItem("Models", 0, g_settings.render_flags & RENDER_STUDIO_MDL)) {
				g_settings.render_flags ^= RENDER_STUDIO_MDL;

				for (Entity* ent : app->ents()) {
					ent->clearCache();
				}
			}
			tooltip("Display game models instead of colored cubes where available.");

			if (ImGui::MenuItem("Sprites", 0, g_settings.render_flags & RENDER_SPRITES)) {
				g_settings.render_flags ^= RENDER_SPRITES;

				for (Entity* ent : app->ents()) {
					ent->clearCache();
				}
			}
			tooltip("Display sprites instead of colored cubes where available.");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Lightmaps")) {
			bool lightEnabled = g_settings.render_flags & RENDER_LIGHTMAPS;
			int* scnt = app->mapRenderer->lightStyleCount;

			if (ImGui::MenuItem("Enabled", 0, lightEnabled)) {
				g_settings.render_flags ^= RENDER_LIGHTMAPS;
			}
			tooltip("Render lighting textures for all faces. Disable for full brightness.");

			ImGui::BeginDisabled(!lightEnabled || app->isLoading);
			if (ImGui::MenuItem("Monochrome", 0, g_app->monochromeLight)) {
				g_app->monochromeLight = !g_app->monochromeLight;
				app->mapRenderer->reloadLightmaps();
			}
			tooltip("Render lightmaps in greyscale. Use this to preview how the map will look in "
				"Quake 1.\n\nSome Quake 1 source ports support colored lighting via external "
				".lit files. These files are created whenever you save the map in a Quake 1 format.");
			ImGui::EndDisabled();

			ImGui::Separator();

			if (ImGui::MenuItem("Layer 0", 0, app->lightStylesEnabled[0], lightEnabled)) {
				app->lightStylesEnabled[0] = !app->lightStylesEnabled[0];
			}
			tooltip(cstrf("Render layer 0 lightmaps. Most faces have this unless they're pitch black.\n\n"
				"%d faces in this map have layer 0 lightmaps.", scnt[0]));

			if (ImGui::MenuItem("Layer 1", 0, scnt[1] > 0 && app->lightStylesEnabled[1], lightEnabled && scnt[1] > 0)) {
				app->lightStylesEnabled[1] = !app->lightStylesEnabled[1];
			}
			tooltip(cstrf("Render layer 1 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 1 lightmaps.", scnt[1]));

			if (ImGui::MenuItem("Layer 2", 0, scnt[2] > 0 && app->lightStylesEnabled[2], lightEnabled && scnt[2] > 0)) {
				app->lightStylesEnabled[2] = !app->lightStylesEnabled[2];
			}
			tooltip(cstrf("Render layer 2 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 2 lightmaps.", scnt[2]));

			if (ImGui::MenuItem("Layer 3", 0, scnt[3] > 0 && app->lightStylesEnabled[3], lightEnabled && scnt[3] > 0)) {
				app->lightStylesEnabled[3] = !app->lightStylesEnabled[3];
			}
			tooltip(cstrf("Render layer 3 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 3 lightmaps.", scnt[3]));

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Skybox", 0, g_settings.render_flags & RENDER_SKYBOX)) {
			g_settings.render_flags ^= RENDER_SKYBOX;
		}
		tooltip("Render the skybox. Reload the map to load a new sky after editing skyname in worldspawn.");

		if (ImGui::MenuItem("Special World Faces", 0, g_settings.render_flags & RENDER_SPECIAL)) {
			g_settings.render_flags ^= RENDER_SPECIAL;
		}
		tooltip("Render special faces that are normally invisible and/or have special rendering properties (e.g. the SKY texture).");

		if (ImGui::MenuItem("Textures", 0, g_settings.render_flags & RENDER_TEXTURES)) {
			g_settings.render_flags ^= RENDER_TEXTURES;
		}
		tooltip("Render textures for all faces.");

		if (ImGui::MenuItem("Wireframe", 0, g_settings.render_flags & RENDER_WIREFRAME)) {
			g_settings.render_flags ^= RENDER_WIREFRAME;
		}
		tooltip("Outline all faces. Greatly reduces FPS on fill-rate limited hardware (old iGPUs)");

		ImGui::Separator();

		if (ImGui::MenuItem("Leaf Links", 0, g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			g_settings.render_flags ^= RENDER_LEAF_GRAPH;
		}
		tooltip("Display leaf connectivity graph when in leaf selection mode. Leaf numbers don't match VIS compiler numbering.\n\n"
			"Green box = Leaf origin\n"
			"Yellow box = Portal origin\n"
			"Yellow outline = Portal\n"
			"Orange line = Link between leaves"
		);

		if (ImGui::MenuItem("PVS Wireframe", 0, g_settings.render_flags & RENDER_PVS)) {
			g_settings.render_flags ^= RENDER_PVS;
			if (g_app->mapRenderer->pvsDat && (g_settings.render_flags & RENDER_PVS))
				g_app->mapRenderer->pvsDat->leaf = -1; // force update
		}
		tooltip("Render wireframe for polygons in the potentially visible set.\n");

		ImGui::Separator();

		if (ImGui::MenuItem("Map Boundaries", 0, g_settings.render_flags & RENDER_MAP_BOUNDARY)) {
			g_settings.render_flags ^= RENDER_MAP_BOUNDARY;
		}
		tooltip("Renders map boundaries as a transparent box around the world. Entities which leave "
			"this box may have visual glitches depending on the engine this map runs in.");

		if (ImGui::MenuItem("Origin", 0, g_settings.render_flags & RENDER_ORIGIN)) {
			g_settings.render_flags ^= RENDER_ORIGIN;
		}
		tooltip("Displays a colored cross at the world origin (0,0,0)");

		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}

}

void MenuBar::drawSettingsMenu() {
	if (ImGui::BeginMenu("Settings"))
	{
		if (ImGui::MenuItem("Editor Setup", NULL)) {
			if (!gui->widgets[WIDGET_SETTINGS]->widgetVisible) {
				gui->reloadSettings = true;
			}
			ImGui::SetWindowCollapsed("Editor Setup", false);
			gui->widgets[WIDGET_SETTINGS]->widgetVisible = true;
		}

		ImGui::Separator();

		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		bool changed = false;
		if (ImGui::BeginMenu("Engine")) {
			ImGui::BeginDisabled(g_settings.ripent_safe_mode);

			if (ImGui::MenuItem("Auto", 0, g_settings.auto_engine_select, !app->isLoading)) {
				g_settings.auto_engine_select = !g_settings.auto_engine_select;

				if (g_settings.auto_engine_select) {
					changed = app->autoSelectEngine(app->mapRenderer->map, false);
				}
			}
			tooltip("Automatically select the engine best suited for the BSP file that was loaded. "
				"Disable this to convert between BSP formats.");

			ImGui::Separator();

			ImGui::BeginDisabled(app->isLoading || g_settings.auto_engine_select);

			if (ImGui::MenuItem("Half-Life", 0, g_settings.engine == ENGINE_HALF_LIFE)) {
				changed = g_settings.engine != ENGINE_HALF_LIFE;
				g_settings.engine = ENGINE_HALF_LIFE;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
			}
			tooltip("The original GoldSrc engine.\n\nThis engine uses the BSP30 format. BSP30 adds "
				"true color lightmaps, unique color palettes for each texture, and the ability to load "
				"textures from WAD files. Due to a buffer overflow bug in the renderer, the max leaves "
				"allowed in the world model is reduced to 8192 from 32760.\n");

			if (ImGui::MenuItem("Half-Life: Blue Shift", 0, g_settings.engine == ENGINE_BLUE_SHIFT)) {
				changed = g_settings.engine != ENGINE_BLUE_SHIFT;
				g_settings.engine = ENGINE_BLUE_SHIFT;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
			}
			tooltip("A slightly modified version of the Half-Life engine.\n\nThis engine uses the BSP30 "
				"format with different ordering of data. No limits are changed.\n");

			if (ImGui::MenuItem("Sven Co-op 5.0", 0, g_settings.engine == ENGINE_SVEN_COOP)) {
				changed = g_settings.engine != ENGINE_SVEN_COOP;
				g_settings.engine = ENGINE_SVEN_COOP;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -32768;
					g_settings.mapsize_max = 32768;
				}
			}
			tooltip("Sven Co-op 5.0 uses a modified version of the Half-Life engine. It enables higher map limits "
				"without modifying the BSP30 file format. Some maps need this selected to display correctly in the editor.\n\n"

				"Sven Co-op 5.0 increases the following limits:\n"
				"    AllocBlocks: 64 -> 1024\n"
				"    Leaves (world model): 8192 -> 32768\n"
				"    Light Data: 48 MB -> 64 MB\n"
				"    Light Styles: 32 -> 224\n"
				"    Models: 512 -> 4096\n"
				"    Planes: 32768 -> 65535\n"
				"    Surface Extents: 16 -> 64 (lightmap size)\n"
				"    Texture Pixels: 262144 -> 104856 (512x512 -> 1024x1024)\n"
				"    VIS Data: 8 MB -> 64 MB\n\n"

				"Attempting to run a "
				"Sven Co-op map in Half-Life may result in AllocBlock Full errors, Bad Surface Extents, "
				"crashes caused by large textures, and visual glitches caused by crossing the +/-4096 map boundary. "
				"See the Tools menu for solutions to these problems.");

			ImGui::Separator();

			if (ImGui::MenuItem("Quake 1", 0, g_settings.engine == ENGINE_QUAKE_1)) {
				changed = g_settings.engine != ENGINE_QUAKE_1;
				g_settings.engine = ENGINE_QUAKE_1;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
			}
			tooltip("The original Quake engine from 1997.\n\nThis engine uses the BSP29 file format. "
				"BSP29 is limited to greyscale lightmaps "
				"and a global texture palette which all textures share.\n\n"
				"External QLIT files (.lit) are automatically imported and generated for source ports that "
				"support colored lightmaps.\n");

			if (ImGui::MenuItem("Quake 1 (BSP2)", 0, g_settings.engine == ENGINE_QUAKE_1_BSP2)) {
				changed = g_settings.engine != ENGINE_QUAKE_1_BSP2;
				g_settings.engine = ENGINE_QUAKE_1_BSP2;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
			}
			tooltip("An upgraded Quake 1 engine with support for the BSP2 file format.\n\n"
				"BSP2 increases map limits so much that they no longer matter. Lighting and "
				"texture palette limitations still apply. External QLIT files (.lit) are automatically imported and generated for source ports that "
				"support colored lightmaps.\n\n"
				"Some source ports that can load BSP2 files:\n"
				"  - Darkplaces\n"
				"  - ezQuake\n"
				"  - FTE\n"
				"  - Mark V\n"
				"  - Quakeforge\n"
				"  - Quakespasm\n"
				"  - Super8\n"
				"  - TyrQuake\n"
				"  - Xash3D\n"
			);
			ImGui::EndDisabled();
			ImGui::EndDisabled();

			ImGui::EndMenu();
		}
		if (g_settings.ripent_safe_mode) {
			tooltip("The engine determines the file format for saving and limits for the map.\n\n"
				"In Ripent Safe Mode, the engine is selected automatically based on the format of the BSP");
		}
		else {
			tooltip("The engine determines the file format for saving and limits for the map.\n");
		}

		if (ImGui::BeginMenu("Map Size")) {
			if (ImGui::MenuItem("Auto", 0, g_settings.mapsize_auto)) {
				if (g_settings.engine == ENGINE_HALF_LIFE) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
				else if (g_settings.engine == ENGINE_SVEN_COOP) {
					g_settings.mapsize_min = -32768;
					g_settings.mapsize_max = 32768;
				}
				g_settings.mapsize_auto = true;
			}
			tooltip("The map size will be set according to the Engine you choose.");

			if (ImGui::MenuItem("+/-4096 (Half-Life)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -4096 && g_settings.mapsize_max == 4096)) {
				g_settings.mapsize_min = -4096;
				g_settings.mapsize_max = 4096;
				g_settings.mapsize_auto = false;
			}
			tooltip("The default map size for Half-Life and most of its mods.");

			if (ImGui::MenuItem("+/-32768 (Sven Co-op)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -32768 && g_settings.mapsize_max == 32768)) {
				g_settings.mapsize_min = -32768;
				g_settings.mapsize_max = 32768;
				g_settings.mapsize_auto = false;
			}
			tooltip("The practical map size for Sven Co-op.");

			if (ImGui::MenuItem("+/-131072 (Sven Co-op)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -131072 && g_settings.mapsize_max == 131072)) {
				g_settings.mapsize_min = -131072;
				g_settings.mapsize_max = 131072;
				g_settings.mapsize_auto = false;
			}
			tooltip("The technically correct map size for Sven Co-op. Players can run around in this giant area but the game becomes a buggy mess once you pass the +/-32768 boundary.");

			for (int i = 0; i < g_app->fgds.size(); i++) {
				Fgd* fgd = g_app->fgds[i];
				int min = fgd->mapSizeMin;
				int max = fgd->mapSizeMax;

				if (min == 0 && max == 0) {
					continue;
				}

				string name;
				if (min != -max)
					name = "(" + to_string(min) + ", " + to_string(max) + ") " + fgd->name + ".fgd";
				else
					name = "+/-" + to_string(max) + " (" + fgd->name + ".fgd)";

				if (ImGui::MenuItem(name.c_str(), 0, g_settings.mapsize_min == min && g_settings.mapsize_max == max)) {
					g_settings.mapsize_min = min;
					g_settings.mapsize_max = max;
				}
				tooltip(("The @mapsize loaded from " + fgd->name + ".fgd.").c_str());
			}
			ImGui::EndMenu();
		}

		if (changed) {
			g_limits = g_engine_limits[g_settings.engine];
			app->mapRenderer->reload();
			gui->reloadLimits();
		}

		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}

}

void MenuBar::drawCreateMenu() {
	if (ImGui::BeginMenu("Create"))
	{
		Bsp* map = app->mapRenderer->map;

		ImGui::BeginDisabled(app->emptyMapLoaded);

		if (ImGui::MenuItem("Point Entity", 0, false, true)) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->setOrAddKeyvalue("origin", origin.toKeyvalueString());
			newEnt->setOrAddKeyvalue("classname", "info_player_deathmatch");
			vector<Entity*> newEnts = { newEnt };

			CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Create Entity", newEnts);
			delete newEnt;
			createCommand->execute();
			app->pushUndoCommand(createCommand);
		}
		tooltip("Create a point entity.\n");

		if (!g_settings.ripent_safe_mode) {
			if (ImGui::MenuItem("BSP Model", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Create Model");

				vec3 origin = app->cameraOrigin + app->cameraForward * 100;
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);

				Entity* newEnt = new Entity();
				newEnt->setOrAddKeyvalue("origin", origin.toKeyvalueString());
				newEnt->setOrAddKeyvalue("classname", "func_wall");

				float size = pow(2.0, g_app->gridSnapLevel);
				if (size < 16) {
					size = 16;
				}

				int aaatriggerIdx = map->get_default_texture_idx();
				vec3 mins = vec3(-size, -size, -size);
				vec3 maxs = vec3(size, size, size);
				int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx);
				newEnt->setOrAddKeyvalue("model", "*" + to_string(modelIdx));
				map->ents.push_back(newEnt);

				command->pushUndoState();
			}
			tooltip("Create a BSP model and attach it to a new entity.\n");

			if (ImGui::MenuItem("Cull Entity", 0, false, true)) {
				Entity* newEnt = new Entity();
				vec3 origin = (app->cameraOrigin + app->cameraForward * 100) - app->mapRenderer->mapOffset;
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);
				newEnt->setOrAddKeyvalue("origin", origin.toKeyvalueString());
				newEnt->setOrAddKeyvalue("classname", "cull");
				vector<Entity*> newEnts = { newEnt };

				CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Create Entity", newEnts);
				delete newEnt;
				createCommand->execute();
				app->pushUndoCommand(createCommand);
			}
		}
		tooltip("Create a point entity for use with the culling tool. 2 of these define the bounding box for the Cull Box deletion tool.\n");

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

}

void MenuBar::drawToolsMenu() {
	if (ImGui::BeginMenu("Tools"))
	{
		ImGui::BeginDisabled(app->emptyMapLoaded);
		Bsp* map = app->mapRenderer->map;

		static vector<Wad*> emptyWads;
		vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;
		BspRenderer* renderer = app->mapRenderer;

		bool hasAnyCollision = gui->anyHullValid[1] || gui->anyHullValid[2] || gui->anyHullValid[3];

		if (!g_settings.ripent_safe_mode && ImGui::BeginMenu("Delete BSP Data", !app->isLoading)) {
			if (ImGui::MenuItem("Clean", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Clean " + map->name);

				logf("Cleaning %s\n", map->name.c_str());
				map->remove_unused_model_structures().print_delete_stats(1);

				command->pushUndoState();
			}
			tooltip("Removes unreferenced structures in the BSP data. Run this after editing BSP models.\n\nWhen you edit BSP models or delete"
				" references to them, the data is not deleted until you run this command. "
				"Watch the Limits and Messages widgets to see how many structures were removed.");

			if (ImGui::MenuItem("Optimize", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Optimize " + map->name);

				logf("Optimizing %s\n", map->name.c_str());
				if (!map->has_hull2_ents()) {
					logf("    Redirecting hull 2 to hull 1 because there are no large monsters/pushables\n");
					map->delete_hull(2, 1);
				}

				bool oldVerbose = g_verbose;
				g_verbose = true;
				map->delete_unused_hulls(true).print_delete_stats(1);
				g_verbose = oldVerbose;

				command->pushUndoState();
			}
			tooltip(g_optimize_tip);

			ImGui::Separator();

			if (ImGui::BeginMenu("Clipnode Hull", hasAnyCollision && !app->isLoading)) {
				for (int i = 1; i < MAX_MAP_HULLS; i++) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + to_string(i) + " --> Hull " + to_string(k)).c_str(), "", false, gui->anyHullValid[k])) {
							LumpReplaceCommand* command = new LumpReplaceCommand("Redirect Hull " + to_string(i));

							Bsp* map = app->mapRenderer->map;
							map->delete_hull(i, k);
							logf("Redirected hull %d to hull %d in map %s\n", i, k, map->name.c_str());
							gui->checkValidHulls();

							logf("Cleaning %s\n", map->name.c_str());
							map->remove_unused_model_structures().print_delete_stats(1);

							command->pushUndoState();
						}
						tooltip("Redirects a clipnode hull in all models including worldspawn. This frees up a large "
							"amount of clipnodes but reduces collision accuracy for entities that use the removed hulls.\n\n"
							"Use this if the Optimize command refused to remove/redirect a hull, and you're ok with the side effects of forcing its removal. "
							"Some entities may clip into walls, hover above the ground, be able/unable to enter certain areas.\n\n"
							"A common use case for this is redirecting Hull 2 -> Hull 1 (Large monsters hull -> Normal monsters hull). "
							"If the map doesn't have any large monsters or pushable objects then there are no side effects for removing the hull. "
							"Removing Hull 1 or Hull 3 always causes noticeable problems because players and most monsters use these hulls."
						);
					}
					if (i != MAX_MAP_HULLS - 1)
						ImGui::Separator();
				}
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Cull Box", 0, false, !app->isLoading)) {
				if (!g_app->hasCullbox) {
					logf("Create at least 2 entities with \"cull\" as a classname first!\n");
				}
				else {
					LumpReplaceCommand* command = new LumpReplaceCommand("Delete Boxed Data");
					map->delete_box_data(g_app->cullMins, g_app->cullMaxs);
					command->pushUndoState();
				}
			}
			tooltip("Deletes BSP data and entities inside of a box defined by 2 \"cull\" entities "
				"(for the min and max extent of the box). Works best with fully enclosed areas. "
				"Partially deleting features in a room will likely result in holes and broken clipnodes.\n\n"
				"Create 2 cull entities to define the culling box. "
				"A transparent red box will form between them.");

			ImGui::Separator();

			static const char* optionNames[7] = {
				"OOB All Axes",
				//"OOB X Axis",
				"OOB X+ Axis",
				"OOB X- Axis",
				//"OOB Y Axis",
				"OOB Y+ Axis",
				"OOB Y- Axis",
				//"OOB Z Axis",
				"OOB Z+ Axis",
				"OOB Z- Axis",
			};
			static const char* optionDesc[7] = {
				"on all axes",
				//"on the X axis",
				"on the positive X axis",
				"on the negative X axis",
				//"on the Y axis"
				"on the positive Y axis",
				"on the negative Y Axis",
				//"on the Y Axis",
				"on the positive Z Axis",
				"on the negative Z Axis",
			};

			static int clipFlags[7] = {
				(int)0xffffffff,
				//OOB_CLIP_X | OOB_CLIP_X_NEG,
				OOB_CLIP_X,
				OOB_CLIP_X_NEG,
				//OOB_CLIP_Y | OOB_CLIP_Y_NEG,
				OOB_CLIP_Y,
				OOB_CLIP_Y_NEG,
				//OOB_CLIP_Z | OOB_CLIP_Z_NEG,
				OOB_CLIP_Z,
				OOB_CLIP_Z_NEG,
			};

			for (int i = 0; i < 7; i++) {
				if (ImGui::MenuItem(optionNames[i], 0, false, !app->isLoading)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Delete OOB Data");

					if (map->ents[0]->hasKey("origin")) {
						vec3 ori = map->ents[0]->getOrigin();
						logf("Moved worldspawn origin by %f %f %f\n", ori.x, ori.y, ori.z);
						map->move(ori);
						map->ents[0]->removeKeyvalue("origin");

					}

					map->delete_oob_data(clipFlags[i]);
					command->pushUndoState();
				}
				if (ImGui::IsItemHovered()) {
					gui->hoveredOOB = i;
				}
				tooltip(("Deletes out-of-bounds BSP structures and entities " + string(optionDesc[i]) + ".\n\n"
					"Enable the Map Boundary setting in the View menu to see what will be deleted.").c_str());
			}

			ImGui::EndMenu();
		}

		if (!g_settings.ripent_safe_mode && ImGui::BeginMenu("Faces")) {
			if (ImGui::MenuItem("Fix Bad Surface Extents", 0, false, !app->isLoading)) {
				gui->showWidget(WIDGET_FIX_EXTENTS, true);
			}
			tooltip("Fix all faces with bad surface extents.");

			if (ImGui::MenuItem("Scale Invisible Faces", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("AllocBlock Reduction");
				if (map->allocblock_reduction() == 0) {
					delete command;
				}
				else {
					command->pushUndoState();
				}
			}
			tooltip("Scales up textures on invisible model faces to reduce AllocBlock size. "
				"Manually increase texture scales or downscale large textures to reduce "
				"AllocBlocks further.");

			ImGui::EndMenu();
		}
		
		if (!g_settings.ripent_safe_mode && ImGui::BeginMenu("Leaves")) {
			if (ImGui::MenuItem("World Leaf Reduction", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("World Leaf Reduction");
				
				map->merge_simple_leaf_chains();
				map->remove_unused_model_structures(false).print_delete_stats(1);

				command->pushUndoState();
			}
			tooltip("Merges simple leaf chains which don't split out into different branches in the BSP "
				"tree. World leaf count will be reduced and more faces will be visible on average after doing this.");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Models")) {
			if (ImGui::MenuItem("Deduplicate Models", 0, false, !app->isLoading)) {
				gui->showWidget(WIDGET_DEDUP_MODELS, true);
			}
			tooltip("Scans for duplicated BSP models and updates entity model keys to reference only one model from set of duplicated models. "
				"This lowers the model count and allows more game models to be precached. Lightmaps are ignored during the scan, so this might "
				"make some entities appear too bright in dark areas, or too dark in lit areas.\n\n"
				"This does not delete BSP data unless you run the Clean command afterward. Cut/hide problematic entities before "
				"deduplicating if you don't want their models swapped.");

			if (ImGui::MenuItem("Recover Oprhaned Models", 0, false, !app->isLoading)) {
				unordered_set<int> usedModels;

				for (Entity* ent : map->ents) {
					usedModels.insert(ent->getBspModelIdx());
				}

				vec3 ori;
				ori.z = map->models[0].nMaxs.z + 256;
				vec3 lastSz;

				vector<Entity*> newEnts;

				for (int i = 1; i < map->modelCount; i++) {
					if (usedModels.count(i))
						continue;

					BSPMODEL& model = map->models[i];
					vec3 offset = model.nMins + (model.nMaxs - model.nMins) * 0.5f;
					ori.x += lastSz.x;
					ori.x += (model.nMaxs - model.nMins).x;

					Entity* ent = new Entity();
					ent->setOrAddKeyvalue("origin", (ori - offset).toKeyvalueString());
					ent->setOrAddKeyvalue("model", cstrf("*%d", i));
					ent->setOrAddKeyvalue("classname", "func_illusionary");
					newEnts.push_back(ent);

					lastSz = model.nMaxs - model.nMins;
				}

				if (newEnts.size()) {
					logf("Recovered %d orphaned BSP models.\n", newEnts.size());
					CreateEntitiesCommand* createCommand = new CreateEntitiesCommand("Recover Orphaned Models", newEnts);
					createCommand->execute();
					app->pushUndoCommand(createCommand);

					app->pickInfo.deselect();
					app->hiddenLeaves.clear();
					app->pickMode = PICK_OBJECT;
					for (int i = 0; i < newEnts.size(); i++) {
						app->pickInfo.selectEnt(map->ents.size() - (1 + i));
					}

					// focus the camera on the new ents
					app->cameraOrigin = ori + vec3(lastSz.x + 128, 0, 128);
					app->cameraAngles = vec3(10, 0, -90);
				}
				else {
					logf("No BSP models are orphaned.\n");
				}
			}
			tooltip("Create entities for all BSP models that no longer have an entity assigned to them.");

			if (!g_settings.ripent_safe_mode) {
				if (ImGui::MenuItem("Zero Model Origins", 0, false, !app->isLoading)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Zero Model Origins");

					int moveCount = 0;
					moveCount += map->zero_entity_origins("func_ladder");
					moveCount += map->zero_entity_origins("func_water"); // water is sometimes invisible after moving in sven
					moveCount += map->zero_entity_origins("func_mortar_field"); // mortars don't appear in sven

					BspRenderer* renderer = app->mapRenderer;

					if (moveCount) {
						command->pushUndoState();
					}
					else {
						delete command;
						logf("No entity origins need moving\n");
					}
				}
				tooltip("Some BSP models break when used in an entity with a non-zero origin (ladders, "
					"water, mortar fields). This will move affected entity origins to (0,0,0) while keeping "
					"models in the same place, duplicating them if necessary.\n");
			}

			ImGui::EndMenu();
		}

		if (!g_settings.ripent_safe_mode && ImGui::BeginMenu("Textures")) {
			if (ImGui::MenuItem("Create Series WAD", "")) {
				createSeriesWad();
			}
			tooltip("Creates a WAD file containing the embedded textures from all maps in a series. "
				"Textures with the same name but different appearance will not be exported.\n\n"
				"Textures that are exported will also be unembedded from their BSPs, and included "
				"via the newly created WAD file instead. The point of this tool is to reduce disk "
				"space used by campaigns that duplicate textures across many maps.");

			if (ImGui::MenuItem("Embed All", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Embed Textures");

				int count = map->embed_all_textures();
				logf("Embedded %d textures\n", count);

				command->pushUndoState();
			}
			tooltip("Embeds all externally referenced textures, forcing them to be loaded from the BSP instead of WADs.");

			if (ImGui::MenuItem("Unembed All", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Unembed Textures");

				vector<Wad*> wads = g_app->mapRenderer ? g_app->mapRenderer->wads : vector<Wad*>();

				int count = 0;
				int fail = 0;
				for (int i = 0; i < map->textureCount; i++) {
					BSPMIPTEX* tex = map->get_texture(i);

					if (!tex || tex->nOffsets[0] == 0) {
						continue;
					}

					if (map->unembed_texture(i, wads, true) > 0) {
						count++;
					}
				}
				logf("Unembedded %d textures\n", count);

				command->pushUndoState();
			}
			tooltip("Deletes all embedded texture data, forcing textures to be loaded from WADs referenced "
				"in the worldspawn entity.\n\nIf an embedded texture cannot be found in a WAD, it will become "
				"a missing texture. You may want to export embedded textures first to avoid losing data.");

			ImGui::Separator();

			if (ImGui::MenuItem("Downscale Invalid", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Downscale Textures");
				if (map->downscale_invalid_textures(wads) == 0) {
					delete command;
				}
				else {
					command->pushUndoState();
				}
			}
			tooltip("Downscales textures that exceed the max texture size for the selected engine "
				"and adjusts texture coordinates accordingly.\n\nIf a texture is stored in a WAD, "
				"it is first embedded into the BSP before being downscaled.\n");

			if (ImGui::MenuItem("Remove Unused WADs", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Remove Unused WADs", true);
				map->remove_unused_wads(wads);
				command->pushUndoState();
			}
			tooltip("Removes unused WADs from the worldspawn 'wad' keyvalue and strips folder paths."
				"\n\nDespite being a keyvalue edit, this is not a ripent-safe operation. Clients "
				"don't load wad paths from the server, they use whatever is stored in their local "
				"copy of the BSP.");

			ImGui::EndMenu();
		}

		if (!g_settings.ripent_safe_mode && ImGui::MenuItem("RAD Preparation", 0, false, !app->isLoading)) {
			gui->showWidget(WIDGET_RAD_PREP, true);
		}
		tooltip("Prepare the map for light recompilation with VHLT.");

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

}

void MenuBar::drawWidgetsMenu() {
	if (ImGui::BeginMenu("Widgets"))
	{
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::MenuItem("Debug", NULL, gui->widgets[WIDGET_DEBUG]->widgetVisible)) {
			gui->widgets[WIDGET_DEBUG]->widgetVisible = !gui->widgets[WIDGET_DEBUG]->widgetVisible;
			if (gui->widgets[WIDGET_DEBUG]->widgetVisible)
				ImGui::SetWindowCollapsed(gui->widgets[WIDGET_DEBUG]->widgetName, false);
		}
		tooltip("For developers and those curious about BSP internals.");

		if (ImGui::MenuItem("Entity Report", "Ctrl+F", gui->widgets[WIDGET_ENT_REPORT]->widgetVisible)) {
			gui->widgets[WIDGET_ENT_REPORT]->widgetVisible = !gui->widgets[WIDGET_ENT_REPORT]->widgetVisible;
			if (gui->widgets[WIDGET_ENT_REPORT]->widgetVisible)
				ImGui::SetWindowCollapsed("###entreport", false);
		}
		tooltip("Search for entities by name, class, and/or other properties.");

		if (!g_settings.ripent_safe_mode) {
			if (ImGui::MenuItem("Face Editor", "", gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible)) {
				gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible = !gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible;
				if (gui->widgets[WIDGET_FACE_EDITOR]->widgetVisible)
					ImGui::SetWindowCollapsed("Face Editor", false);
			}
			tooltip("Edit faces and textures.");
		}

		if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible)) {
			gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = !gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
			if (gui->widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible)
				ImGui::SetWindowCollapsed("Keyvalue Editor", false);
		}
		tooltip("Edit entity properties.");

		if (!g_settings.ripent_safe_mode) {
			if (ImGui::MenuItem("Leaf Graph", "", gui->widgets[WIDGET_LEAF]->widgetVisible)) {
				gui->widgets[WIDGET_LEAF]->widgetVisible = !gui->widgets[WIDGET_LEAF]->widgetVisible;
				if (gui->widgets[WIDGET_LEAF]->widgetVisible)
					ImGui::SetWindowCollapsed("Leaf Graph", false);
			}
			tooltip("View BSP tree graph.");
		}

		if (ImGui::MenuItem("Map Limits", NULL, gui->widgets[WIDGET_LIMITS]->widgetVisible)) {
			gui->widgets[WIDGET_LIMITS]->widgetVisible = !gui->widgets[WIDGET_LIMITS]->widgetVisible;
			if (gui->widgets[WIDGET_LIMITS]->widgetVisible)
				ImGui::SetWindowCollapsed("Map Limits", false);
		}
		tooltip("Shows how close the map is to exceeding engine limits.");

		if (ImGui::MenuItem("Messages", "", gui->widgets[WIDGET_MESSAGES]->widgetVisible)) {
			gui->widgets[WIDGET_MESSAGES]->widgetVisible = !gui->widgets[WIDGET_MESSAGES]->widgetVisible;
			if (gui->widgets[WIDGET_MESSAGES]->widgetVisible)
				ImGui::SetWindowCollapsed("Messages", false);
		}
		tooltip("Show program messages.");

		if (ImGui::MenuItem("Transform", "Ctrl+M", gui->widgets[WIDGET_TRANSFORM]->widgetVisible)) {
			gui->widgets[WIDGET_TRANSFORM]->widgetVisible = !gui->widgets[WIDGET_TRANSFORM]->widgetVisible;
			if (gui->widgets[WIDGET_TRANSFORM]->widgetVisible)
				ImGui::SetWindowCollapsed("Transformation", false);
		}
		tooltip("Move, rotate, and scale entities.");

		ImGui::Separator();

		ImGui::PopItemFlag();

		static string userLayout = gui->getUserLayoutPath();

		if (ImGui::MenuItem("Save Widget Layout", NULL)) {
			ImGui::SaveIniSettingsToDisk(userLayout.c_str());
			app->getWindowSize(g_settings.autoload_layout_width, g_settings.autoload_layout_height);
			g_settings.save();
			logf("Layout saved to %s\n", userLayout.c_str());
		}
		tooltip("Save the position and size of your widgets as they are now.");

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
		tooltip("Restore your previously saved layout. Widgets may move mostly off-screen if you saved "
			"at a larger window resolution than you're using now.\n");

		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::MenuItem("Auto-Load Layout", NULL, g_settings.autoload_layout)) {
			g_settings.autoload_layout = !g_settings.autoload_layout;
		}
		tooltip("Automatically loads your saved widget layout whenever the window resizes to the same "
			" resolution you saved at.\n");

		if (ImGui::MenuItem("Reset Widget Sizes")) {
			gui->resetWidgetSizes();
		}
		tooltip("Reset widget sizes to their defaults.\n");

		ImGui::ClearIniSettings();

		ImGui::PopItemFlag();

		ImGui::EndMenu();
	}

}

void MenuBar::drawHelpMenu() {
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("View help")) {
			gui->showWidget(WIDGET_HELP, true);
		}
		if (ImGui::MenuItem("About")) {
			gui->showWidget(WIDGET_ABOUT, true);
		}
		ImGui::EndMenu();
	}
}



void MenuBar::saveAs() {
	Bsp* map = app->mapRenderer->map;

	char* fname = tinyfd_saveFileDialog("Save As", map->path.c_str(),
		1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)");

	if (fname) {
		map->update_ent_lump();
		map->path = fname;
		map->name = stripExt(basename(fname));
		map->write(map->path);
		app->updateWindowTitle();
	}
}

void MenuBar::createSeriesWad() {
	char* fnamestr = tinyfd_openFileDialog("Select Series Maps", "",
		1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

	if (!fnamestr)
		return;

	vector<string> fnames;

	fnames = splitString(fnamestr, "|");

	string defaultPath = getFolderPath(getFolderPath(fnames[0]));

	char* sharedWadName = tinyfd_saveFileDialog("Series WAD Name", defaultPath.c_str(),
		1, wadFilterPatterns, "Half-Life Package (*.wad)");

	if (!fnames.size() || !sharedWadName) {
		return;
	}


	vector<WADTEX> sharedTex;
	vector<Wad*> emptyWadList;

	for (int i = 0; i < fnames.size(); i++) {
		Bsp* temp = new Bsp(fnames[i]);
		if (!temp->valid) {
			delete temp;
			continue;
		}

		vector<WADTEX> embedded = temp->get_embedded_textures();

		if (embedded.empty()) {
			logf("No embedded textures found in %s\n", temp->name);
			delete temp;
			continue;
		}

		int numExport = 0;
		bool anyUnembed = false;

		for (int j = 0; j < embedded.size(); j++) {
			WADTEX& bsptex = embedded[j];

			bool isConflicted = false;
			bool isAlreadyWritten = false;
			for (int k = 0; k < sharedTex.size(); k++) {
				WADTEX& wadtex = sharedTex[k];

				if (strcasecmp(wadtex.szName, bsptex.szName)) {
					continue;
				}

				// names match, but do the contents?
				if (wadtex.nHeight != bsptex.nHeight || wadtex.nWidth != bsptex.nWidth) {
					logf("Texture %s in %s has different dimensions than in the WAD\n",
						bsptex.szName, temp->name);
					isConflicted = true;
					break;
				}

				if (memcmp(wadtex.data, bsptex.data, bsptex.getDataSize())) {
					logf("Texture %s in %s has different data than in the WAD\n",
						bsptex.szName, temp->name);
					isConflicted = true;
					break;
				}

				isAlreadyWritten = true;
				break;
			}

			if (!isConflicted) {
				int id = temp->get_texture_id(bsptex.szName);
				temp->unembed_texture(id, emptyWadList, true, true);
				anyUnembed = true;

				if (!isAlreadyWritten) {
					sharedTex.push_back(bsptex);
					numExport++;
				}
				else {
					delete[] bsptex.data;
				}
			}
			else
				delete[] bsptex.data;
		}

		if (anyUnembed) {
			bool didUpdate = false;
			for (Entity* ent : temp->ents) {
				if (ent->getClassname() == "worldspawn") {
					string wadlist = ent->getKeyvalue("wad");
					if (wadlist.size() && wadlist[wadlist.size() - 1] != ';') {
						wadlist += ";";
					}
					wadlist += basename(sharedWadName);
					ent->setOrAddKeyvalue("wad", wadlist);
					didUpdate = true;
					break;
				}
			}
			if (!didUpdate) {
				errorf("ERROR: %s does not have a worldspawn entity to update.\n", temp->name);
			}
			temp->update_ent_lump();
		}

		logf("Added %d / %d embedded textures from %s\n",
			numExport, embedded.size(), temp->name.c_str());

		temp->write(fnames[i]);
		delete temp;
	}

	Wad outWad = Wad();
	outWad.write(sharedWadName, &sharedTex[0], sharedTex.size());
	logf("Exported %d embedded textures to %s\n", sharedTex.size(), sharedWadName);

	for (WADTEX& tex : sharedTex) {
		delete[] tex.data;
	}
}
