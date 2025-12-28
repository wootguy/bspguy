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
#include "BaseRenderer.h"
#include <unordered_set>
#include "bmp.h"

// embedded binary data
#include "fonts/notosans_mono.h"
#include "fonts/notosans_unicode.h"
#include "icons/object.h"
#include "icons/face.h"
#include "icons/leaf.h"

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

string iniPath = getConfigDir() + "imgui.ini";

char const* bspFilterPatterns[1] = { "*.bsp" };
char const* entFilterPatterns[1] = { "*.ent" };
char const* wadFilterPatterns[1] = { "*.wad" };
char const* prtFilterPatterns[1] = { "*.prt" };
char const* radFilterPatterns[1] = { "*.rad" };
char const* imgFilterPatterns[2] = { "*.bmp", "*.png" };
char const* pngFilterPatterns[1] = { "*.png" };
char const* bmpFilterPatterns[1] = { "*.bmp" };

bool editWasOpen = false;

const char* g_optimize_tip =
"Removes \"unnecesary\" structures in the BSP data. Potentially unsafe.\n\n"

"What the program considers unnecesary for Half-Life may become a fatal error for another game."
"In most cases mods do not significantly change default entity behavior, but there is a risk.\n\n"

"An example of commonly deleted structures would be the visible hull 0 for entities like "
"trigger_once, which are invisible and so don't need textured faces. Entities "
"like func_illusionary also don't need any clipnodes because they're not meant to be collidable.\n\n"

"Check the Messages widget to see which entities had their hulls deleted. You may want to selectively "
"delete hulls yourself if you run into problems.";

void tooltip(ImGuiContext& g, const char* text, float hoverDelay=g_tooltip_delay) {
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > hoverDelay) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(min(ImGui::GetFontSize() * 35.0f, (float)g_app->windowWidth));
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

	glCheckError("Creating ImGui context");

	io.IniFilename = iniPath.c_str();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	ImGui_ImplOpenGL2_Init();

	glCheckError("ImGui init");

	loadFonts();

	glCheckError("ImGui font load");

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

	lodepng_decode32(&icon_data, &w, &h, leaf_icon, sizeof(leaf_icon));
	leafIconTexture = new Texture(w, h, icon_data);
	leafIconTexture->upload(GL_RGBA);

	glCheckError("icon uploads");
}

void Gui::draw() {
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE0);

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

#ifdef DEBUG_MODE
	ImGui::ShowDemoWindow();
#endif

	hoveredOOB = -1;

	drawMenuBar();

	if (!app->mapArrangeMode)
		drawToolbar();

	drawStatusMessage();

	if (showDebugWidget) {
		drawDebugWidget();
	}
	if (showKeyvalueWidget && !g_app->mapArrangeMode) {
		drawKeyvalueEditor();
	}
	if (showTransformWidget) {
		drawTransformWidget();
	}
	if (showLogWidget) {
		drawLog();
	}
	if (showSettingsWidget && !g_app->mapArrangeMode) {
		drawSettings();
	}
	if (showHelpWidget) {
		drawHelp();
	}
	if (showAboutWidget) {
		drawAbout();
	}
	if (showLimitsWidget && !g_app->mapArrangeMode) {
		drawLimits();
	}
	if (showFaceEditorWidget && !g_app->mapArrangeMode) {
		drawFaceEditor();
	}
	if (showEntityReport && !g_app->mapArrangeMode) {
		drawEntityReport();
	}
	if (g_settings.first_load) {
		drawWelcomePopup();
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
	else if (app->pickMode == PICK_FACE) {
		if (contextMenuEnt != -1 || emptyContextMenu) {
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup("face_context");
		}
	}
	else if (app->pickMode == PICK_LEAF) {
		if (contextMenuEnt != -1 || emptyContextMenu) {
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup("leaf_context");
		}
	}


	draw3dContextMenus();

	drawStatusBar();

	drawPopups();

	drawDebugText();

	// Rendering
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE0);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	ImGui::Render();
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	glDisable(GL_SCISSOR_TEST);
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
	refreshAfterFacePaste = true;
}

void Gui::copyLightmap(int faceIdx, int layer) {
	Bsp* map = app->pickInfo.getMap();

	if (faceIdx < 0 || faceIdx >= map->faceCount || layer < 0 || layer >= MAXLIGHTMAPS) {
		logf("Failed to copy lightmap face %d layer %d\n", faceIdx, layer);
		return;
	}

	copiedLightmapFace = faceIdx;
	copiedLightmapLayer = layer;
}

void Gui::pasteLightmap(int faceIdx, int layer) {
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
	COLOR3* srcData = (COLOR3*)(map->lightdata + srcFace.nLightmapOffset + lightmapSz*copiedLightmapLayer);

	map->apply_lightmap(faceIdx, layer, srcData, size[0], size[1]);

	command->pushUndoState();
}

void Gui::draw3dContextMenus() {
	ImGuiContext& g = *GImGui;
	ImGuiIO& io = ImGui::GetIO();
	
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
			drawEditOptions(false);

			ImGui::EndPopup();
		}

		static bool emptyWasOpen = false; // prevent glfw error spam
		if (ImGui::BeginPopup("empty_context"))
		{
			static bool canPaste = false;
			if (!emptyWasOpen)
				canPaste = app->canPasteEnts();
			emptyWasOpen = true;

			if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste && !app->isLoading)) {
				app->pasteEnts(false);
			}
			if (ImGui::MenuItem("Paste at original origin", 0, false, canPaste && !app->isLoading)) {
				app->pasteEnts(true);
			}
			if (ImGui::MenuItem("Unhide All", 0, false, app->anyHiddenEnts)) {
				app->unhideEnts();
			}
			tooltip(g, "Unhides entities you previously marked as hidden.");

			ImGui::EndPopup();
		}
		else {
			emptyWasOpen = false;
		}
	}
	else if (app->pickMode == PICK_FACE) {
		Bsp* map = app->pickInfo.getMap();

		if (ImGui::BeginPopup("face_context"))
		{
			if (app->pickInfo.faces.empty()) {
				if (ImGui::MenuItem("Select all", 0, false)) {

					for (int i = 0; i < map->faceCount; i++) {
						app->pickInfo.selectFace(i);
					}
					g_app->mapRenderer->highlightPickedFaces(true);
					g_app->updateTextureAxes();
				}
				tooltip(g, "Select every face in the map.");

				if (ImGui::MenuItem("Select visible", 0, false)) {
					unordered_set<int> visibleModels;

					if (g_settings.render_flags & RENDER_ENTS) {
						for (int i = 0; i < map->ents.size(); i++) {
							if (map->ents[i]->hidden) {
								continue;
							}
							int modelIdx = i == 0 ? 0 : map->ents[i]->getBspModelIdx();
							if (modelIdx != -1)
								visibleModels.insert(modelIdx);
						}
					}
					else if (map->ents.size() && !map->ents[0]->hidden) {
						visibleModels.insert(0);
					}

					for (int i = 0; i < map->modelCount; i++) {
						BSPMODEL& model = map->models[i];
						
						if (!visibleModels.count(i))
							continue;

						for (int k = model.iFirstFace; k < model.iFirstFace + model.nFaces; k++) {
							if (app->hiddenFaces.count(k))
								continue;
							app->pickInfo.selectFace(k);
						}
					}

					g_app->mapRenderer->highlightPickedFaces(true);
					g_app->updateTextureAxes();
				}
				tooltip(g, "Select every visible face in the map (excludes hidden models).");

				ImGui::Separator();

				if (ImGui::MenuItem("Unhide All", 0, false, app->hiddenFaces.size())) {
					app->unhideFaces();
				}
				tooltip(g, "Unhides faces you previously marked as hidden.");
			}
			else {
				if (ImGui::MenuItem("Copy texture", "Ctrl+C", false, app->pickInfo.faces.size() == 1)) {
					copyTexture();
				}
				if (ImGui::MenuItem("Paste texture", "Ctrl+V", false, copiedMiptex >= 0 && copiedMiptex < map->textureCount)) {
					pasteTexture();
				}

				ImGui::Separator();

				if (ImGui::BeginMenu("Select", !app->isLoading)) {
					if (ImGui::MenuItem("Connected", "", false)) {
						Bsp* map = app->pickInfo.getMap();

						int oldSelectSz = app->pickInfo.faces.size();
						unordered_set<int> newSelect = map->select_connected_faces(app->pickInfo.faces, app->hiddenFaces, false, false);

						g_app->mapRenderer->highlightPickedFaces(false);

						app->pickInfo.deselect();
						for (int i : newSelect) {
							app->pickInfo.selectFace(i);
						}
						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size() - oldSelectSz);
						g_app->pickCount++;
					}
					tooltip(g, "Recursively select faces connected by vertices.");

					if (ImGui::MenuItem("Connected texture", "", false)) {
						Bsp* map = app->pickInfo.getMap();

						int oldSelectSz = app->pickInfo.faces.size();
						unordered_set<int> newSelect = map->select_connected_faces(app->pickInfo.faces, app->hiddenFaces, false, true);

						g_app->mapRenderer->highlightPickedFaces(false);

						app->pickInfo.deselect();
						for (int i : newSelect) {
							app->pickInfo.selectFace(i);
						}
						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size() - oldSelectSz);
						g_app->pickCount++;
					}
					tooltip(g, "Recursively select faces connected by vertices which share the selected textures.");

					if (ImGui::MenuItem("Connected planar texture", "", false)) {
						Bsp* map = app->pickInfo.getMap();

						int oldSelectSz = app->pickInfo.faces.size();
						unordered_set<int> newSelect = map->select_connected_faces(app->pickInfo.faces, app->hiddenFaces, true, true);

						g_app->mapRenderer->highlightPickedFaces(false);

						app->pickInfo.deselect();
						for (int i : newSelect) {
							app->pickInfo.selectFace(i);
						}
						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size() - oldSelectSz);
						g_app->pickCount++;
					}
					tooltip(g, "Selects faces connected to this one which lie on the same plane and use the same texture");

					ImGui::Separator();

					if (ImGui::MenuItem("Leaves", "", false, app->pickInfo.faces.size())) {
						switchToLeafSelectMode(true, false);
					}
					tooltip(g, "Select all leaves which mark the selected faces.");

					if (ImGui::MenuItem("Leaves (strict)", "", false, app->pickInfo.faces.size())) {
						switchToLeafSelectMode(true, true);
					}
					tooltip(g, "Select leaves which mark only the selected faces, and nothing "
						"more. Some faces may be deselected to accomplish this.");

					ImGui::Separator();

					if (ImGui::MenuItem("Texture", "", false, app->pickInfo.faces.size() == 1)) {
						Bsp* map = app->pickInfo.getMap();
						BSPTEXTUREINFO& texinfo = map->texinfos[app->pickInfo.getFace()->iTextureInfo];
						uint32_t selectedMiptex = texinfo.iMiptex;

						g_app->mapRenderer->highlightPickedFaces(false);

						app->pickInfo.deselect();
						for (int i = 0; i < map->faceCount; i++) {
							BSPTEXTUREINFO& info = map->texinfos[map->faces[i].iTextureInfo];
							if (info.iMiptex == selectedMiptex) {
								app->pickInfo.selectFace(i);
							}
						}
						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size());
						g_app->pickCount++;
					}
					tooltip(g, "Select every face in the map which has this texture.");

					if (ImGui::MenuItem("Texture (bad extents)", "", false, app->pickInfo.faces.size() == 1)) {
						Bsp* map = app->pickInfo.getMap();
						BSPTEXTUREINFO& texinfo = map->texinfos[app->pickInfo.getFace()->iTextureInfo];
						uint32_t selectedMiptex = texinfo.iMiptex;

						g_app->mapRenderer->highlightPickedFaces(false);

						app->pickInfo.deselect();
						for (int i = 0; i < map->faceCount; i++) {
							BSPTEXTUREINFO& info = map->texinfos[map->faces[i].iTextureInfo];
							if (info.iMiptex == selectedMiptex) {

								int size[2];
								if (GetFaceLightmapSize(map, i, size)) {
									continue;
								}

								app->pickInfo.selectFace(i);
							}
						}
						g_app->mapRenderer->highlightPickedFaces(true);
						g_app->updateTextureAxes();

						logf("Selected %d faces\n", app->pickInfo.faces.size());
						g_app->pickCount++;
					}
					tooltip(g, "Select faces with bad surface extents that use this texture.");

					ImGui::EndMenu();
				}

				Bsp* map = app->pickInfo.getMap();
				bool isEmbedded = false;
				if (map && app->pickInfo.getFace()) {
					BSPFACE& face = *app->pickInfo.getFace();
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					BSPMIPTEX* tex = map->get_texture(info.iMiptex);
					if (tex) {
						isEmbedded = tex->nOffsets[0] != 0;
					}
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Subdivide", 0, false, !app->isLoading)) {
					bool plural = app->pickInfo.faces.size() > 1;
					LumpReplaceCommand* command = new LumpReplaceCommand(plural ? "Subdivide Faces" : "Subdivide Face");

					Bsp* map = app->pickInfo.getMap();

					// subdividing changes faces indexes so must be done in the right order
					sort(app->pickInfo.faces.begin(), app->pickInfo.faces.end(), [](const int& a, const int& b) {
						return a > b;
						});

					for (int i = 0; i < app->pickInfo.faces.size(); i++) {
						map->subdivide_face(app->pickInfo.faces[i]);
					}

					command->pushUndoState();
				}
				tooltip(g, "Split selected faces across the axis with the most texture pixels.");

				if (ImGui::MenuItem("Subdivide until valid", 0, false, !app->isLoading)) {
					bool plural = app->pickInfo.faces.size() > 1;
					LumpReplaceCommand* command = new LumpReplaceCommand(plural ? "Subdivide Faces" : "Subdivide Face");

					Bsp* map = app->pickInfo.getMap();

					// subdividing changes faces indexes so must be done in the right order
					sort(app->pickInfo.faces.begin(), app->pickInfo.faces.end(), [](const int& a, const int& b) {
						return a > b;
						});

					int totalSub = 0;
					for (int i = 0; i < app->pickInfo.faces.size(); i++) {
						totalSub += map->fix_bad_surface_extents_with_subdivide(app->pickInfo.faces[i]);
					}
					if (totalSub == 0) {
						logf("No faces were subdivided (failed or extents are already valid)\n");
						delete command;
					}
					else {
						command->pushUndoState();
					}
				}
				tooltip(g, "Subdivide selected faces until they have valid surface extents.");

				if (ImGui::MenuItem("Scale until valid", 0, false, !app->isLoading)) {
					bool plural = app->pickInfo.faces.size() > 1;
					LumpReplaceCommand* command = new LumpReplaceCommand(plural ? "Scale Faces" : "Scale Face");

					Bsp* map = app->pickInfo.getMap();

					int totalScale = 0;
					for (int i = 0; i < app->pickInfo.faces.size(); i++) {
						totalScale += map->fix_bad_surface_extents_with_scale(app->pickInfo.faces[i]);
					}
					if (totalScale == 0) {
						logf("No faces were scaled (failed or extents are already valid)\n");
						delete command;
					}
					else {
						command->pushUndoState();
					}
				}
				tooltip(g, "Scale selected faces until they have valid surface extents.");

				ImGui::Separator();

				if (ImGui::MenuItem("Hide", "H", false, !app->isLoading)) {
					app->hideSelectedFaces();
				}
			}

			ImGui::EndPopup();
		}
	}
	else if (app->pickMode == PICK_LEAF) {
		Bsp* map = app->pickInfo.getMap();

		if (ImGui::BeginPopup("leaf_context"))
		{
			if (app->pickInfo.leaves.empty()) {
				/*
				if (ImGui::MenuItem("Select Degenerates", "", false, !app->isLoading)) {
					app->pickInfo.deselect();

					for (int i = 0; i < map->models[0].nVisLeafs; i++) {
						BSPLEAF& leaf = map->leaves[i];

						if (leaf.nContents == CONTENTS_SOLID)
							continue;

						if (app->mapRenderer->leafNavMesh->leafMap[i] == NAV_INVALID_IDX) {
							logf("Select leaf %d @ %d %d %d (%d contents, %d faces, %d %d %d size)\n",
								i,
								(int)(leaf.nMins[0] + (leaf.nMaxs[0] - leaf.nMins[0])*0.5f),
								(int)(leaf.nMins[1] + (leaf.nMaxs[1] - leaf.nMins[1])*0.5f),
								(int)(leaf.nMins[2] + (leaf.nMaxs[2] - leaf.nMins[2])*0.5f),
								leaf.nContents, leaf.nMarkSurfaces,
								(int)(leaf.nMaxs[0] - leaf.nMins[0]),
								(int)(leaf.nMaxs[1] - leaf.nMins[1]),
								(int)(leaf.nMaxs[2] - leaf.nMins[2]));
							app->pickInfo.selectLeaf(i);
						}
					}

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Select all leaves that the program failed to generate a mesh for.");

				if (ImGui::MenuItem("Select Sky", "", false, !app->isLoading)) {
					app->pickInfo.deselect();

					for (int i = 0; i < map->models[0].nVisLeafs; i++) {
						BSPLEAF& leaf = map->leaves[i];

						if (leaf.nContents == CONTENTS_SKY)
							app->pickInfo.selectLeaf(i);
					}

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Select all leaves with SKY contents.");

				if (ImGui::MenuItem("Merge Unreachable", "", false, !app->isLoading)) {
					
					int noface = 0;
					for (int i = 1; i < map->models[0].nVisLeafs; i++) {
						BSPLEAF& leaf = map->leaves[i];

						if (leaf.nMarkSurfaces == 0) {
							noface++;
						}
					}
					logf("%d leaves with no faces\n", noface);

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Merge leaves that are unreachable by the player and contain no faces.");
				*/

				if (ImGui::MenuItem("Select all", 0, false, !app->isLoading)) {
					for (int i = 1; i < map->models[0].nVisLeafs; i++) {
						app->pickInfo.selectLeaf(i);
					}

					logf("Selected %d leaves\n", (int)app->pickInfo.leaves.size());

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Select all world leaves in the map (excluding the shared solid leaf 0).");

				if (ImGui::MenuItem("Select visible", 0, false, !app->isLoading)) {
					for (int i = 1; i < map->models[0].nVisLeafs; i++) {
						if (app->hiddenLeaves.count(i))
							continue;
						app->pickInfo.selectLeaf(i);
					}

					logf("Selected %d leaves\n", (int)app->pickInfo.leaves.size());

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Select all world leaves that haven't been marked as hidden.");

				ImGui::Separator();

				if (ImGui::MenuItem("Unhide All", 0, false, app->hiddenLeaves.size())) {
					app->unhideLeaves();
				}
				tooltip(g, "Unhides leaves you previously marked as hidden.");
			}
			else {
				if (ImGui::MenuItem("Select Connected", "", false, app->pickInfo.leaves.size() >= 1 && !app->isLoading)) {
					vector<int> pickLeaves = app->pickInfo.leaves;
					vector<int> connected = map->get_connected_leaves(app->mapRenderer->leafNavMesh, pickLeaves, app->hiddenLeaves);

					for (int idx : connected) {
						app->pickInfo.selectLeaf(idx);
					}

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
				}
				tooltip(g, "Recursively select all leaves that are touching the selected leaf(s). Hidden leaves are not connected through.");

				if (ImGui::MenuItem("Select PVS", "P", false, app->pickInfo.leaves.size() >= 1)) {
					selectLeafPvs();
				}
				tooltip(g, "Select all leaves in the potentially visible set (PVS) of the selected leaf(s).");

				ImGui::Separator();

				if (ImGui::MenuItem("Copy as PVS", "", false, app->pickInfo.leaves.size() >= 1)) {
					app->pvsCopyLeaves = app->pickInfo.leaves;
				}
				tooltip(g, "Copy the selected leaves as a potentially visible set (PVS).");

				if (ImGui::BeginMenu("Apply PVS", app->pickInfo.leaves.size() >= 1 && app->pvsCopyLeaves.size())) {
					if (ImGui::MenuItem("Add", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, 1);
					}
					tooltip(g, "Add copied leaves to the target leaf PVS.");

					if (ImGui::MenuItem("Replace", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, 0);
					}
					tooltip(g, "Replace target leaf PVS with the copied leaves.");
					
					if (ImGui::MenuItem("Subtract", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, -1);
					}
					tooltip(g, "Remove copied leaves from the target leaf PVS.");

					ImGui::EndMenu();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Convert to Model", "", false, app->pickInfo.leaves.size() > 1)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Convert to Model");

					int modelIdx = map->convert_leaves_to_model(app->pickInfo.leaves);

					Entity* newEnt = new Entity();
					newEnt->setOrAddKeyvalue("origin", "0 0 0");
					newEnt->setOrAddKeyvalue("classname", "func_illusionary");
					newEnt->setOrAddKeyvalue("model", "*" + to_string(modelIdx));
					map->ents.push_back(newEnt);

					map->remove_unused_model_structures(false).print_delete_stats(1);

					command->pushUndoState();
					app->mapRenderer->reloadLeaves();
				}
				tooltip(g, "Merges the selected leaves and converts leaf faces to a fully solid BSP model. "
					"The new model is then attached to a func_illusionary entity which overlaps "
					"the original faces."
					"\n\nThis reduces world leaf count, but has these drawbacks:\n"
					"- Decals won't work on the new model faces (the entity must be non-solid).\n"
					"- Performance is reduced due to merged faces being visible from more areas.\n"
					"- Performance is reduced inside the merged leaf area due to its combined PVS.\n"
					"- Performance is reduced globally if the new model is so big that it is never culled.\n\n"
					"Best used in unreachable areas, or nooks and crannies where players are "
					"unlikely to shoot.");

				ImGui::Separator();

				if (ImGui::MenuItem("Hide", "H", false, app->pickInfo.leaves.size() > 0)) {
					app->hideSelectedLeaves();
				}
				tooltip(g, "Hide selected leaves from view. This can be used to disconnect clusters of "
					"leaves for controllable flood selection.");
			}

			ImGui::EndPopup();
		}
	
	}
}

void Gui::drawEditOptions(bool isMainMenu) {
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
		tooltip(g, "Paste entities from your clipboard. Entity data is stored as text which you "
			"can transfer to text editors or other bspguy windows.");
		if (ImGui::MenuItem("Paste at original origin", 0, false, canPaste && !app->isLoading)) {
			app->pasteEnts(true);
		}
		tooltip(g, "Pastes entities at the locations they were copied from.");
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
			if (ImGui::MenuItem("Copy", 0, false, !app->isLoading && anySolidSelected)) {
				app->copyEnts(true);
			}
			tooltip(g, "Stores the entity and BSP model to the clipboard. Used to transfer BSP models between maps.\n\n"
				"Textures are included and embedded after pasting, except for textures that already exist in the map. ");

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
			tooltip(g, "Force entities to use the same BSP model.\n\n"
				"The model used by the first entity you selected will be applied to all other selected entities."
			);

			if (ImGui::MenuItem("Duplicate", 0, false, !app->isLoading && anySolidSelected)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Duplicate BSP Model");

				for (Entity* ent : pickEnts) {
					int oldModelIdx = ent->getBspModelIdx();
					int newModelIdx = map->duplicate_model(oldModelIdx);
					ent->setOrAddKeyvalue("model", "*" + to_string(newModelIdx));
				}

				command->pushUndoState();
			}
			tooltip(g, "Create a copy of this BSP model and assign it to this entity.\n\n"
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
						confirmMerge = 2;
					}
				}
			}
			tooltip(g, "Merge solid entity models together.");

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
			tooltip(g, "Select entities that share the selected BSP model(s).");

			ImGui::EndMenu();
		}		

		if (ImGui::BeginMenu("Edit BSP Hulls", !app->isLoading)) {
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
					checkValidHulls();

					// don't delete models so that indexes don't shift which would require a full refresh
					map->remove_unused_model_structures(false).print_delete_stats(1);
					reloadLimits();

					command->pushUndoState();
				}
				tooltip(g, "Creates clipnode hulls for the selected model by extending the planes of Hull 0.\nClipnodes are used for entity collision detection.");

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
						checkValidHulls();

						map->remove_unused_model_structures(false).print_delete_stats(1);
						reloadLimits();

						command->pushUndoState();
					}
					tooltip(g, "Creates a clipnode hull for the selected model by extending the planes of Hull 0.\nClipnodes are used for entity collision detection.");
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
					checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					reloadLimits();

					command->pushUndoState();
				}
				tooltip(g, "Deletes all hulls from the selected model. Be careful using this as it can cause crashes if the entity needs a deleted hull.");

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
					checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					reloadLimits();

					command->pushUndoState();
				}
				tooltip(g, "Deletes all clipnode hulls from the selected model. Be careful using this as it can cause crashes if the entity needs a deleted hull.");

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
						checkValidHulls();

						logf("Cleaning %s\n", map->name.c_str());
						map->remove_unused_model_structures(false).print_delete_stats(1);
						reloadLimits();

						command->pushUndoState();
					}
					tooltip(g, "Deletes a hull from the selected model. Be careful using this as it can cause crashes if the entity needs the deleted hull.");
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
					reloadLimits();

					command->pushUndoState();
				}
				tooltip(g, "Replaces all clipnode hulls with a simple box.");

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
						reloadLimits();

						command->pushUndoState();
					}
					tooltip(g, "Replaces a clipnode hull with a simple box.");
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
							checkValidHulls();

							logf("Cleaning %s\n", map->name.c_str());
							map->remove_unused_model_structures(false).print_delete_stats(1);
							reloadLimits();

							command->pushUndoState();
						}
						tooltip(g, "Redirect a clipnode hull to another clipnode hull. This is safer than deleting but can make collision detection less accurate.");
					}

					if (i != MAX_MAP_HULLS-1)
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
	tooltip(g, "Attach the entity to your camera for easy movement.\n"
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
		showTransformWidget = !showTransformWidget;
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Properties", "Alt+Enter")) {
		showKeyvalueWidget = !showKeyvalueWidget;
	}
}

void Gui::drawStandardMenuBar() {
	ImGuiContext& g = *GImGui;

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
			app->setInitialLumpState();
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
			tooltip(g, "Save entity definitions to a file.");

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
			tooltip(g, "Saves embedded textures to a WAD file. This does not unembed any textures.");

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
			tooltip(g, "Generate a leaf portal file for use with a VIS compiler.\n\n"
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
					entityReportFilterNeeded = true;
					app->pickInfo.deselect();
					app->postSelectEnt();
				}
			}
			tooltip(g, "Delete all map entities and load new ones from a file."
				"\n\nTo add additional entities instead of replacing them, copy entity text from your .ent file and select Paste here.");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Merge", NULL, false, !app->isLoading)) {
			char* fname = tinyfd_openFileDialog("Merge Map", "",
				1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

			if (fname)
				g_app->merge(fname);
		}
		tooltip(g, ("Merge one other BSP into the current file.\n\n"
			"Equivalent CLI command:\nbspguy merge " + map->name + " -noripent -maps \""
			+ map->name + ",other_map\"").c_str());

		if (ImGui::MenuItem("Merge Multiple", NULL, false, !app->isLoading) && g_app->confirmMapExit()) {
			showMergePopup = true;
		}
		tooltip(g, "Merge multiple BSPs into a new file.");

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
			refresh();
		}
		tooltip(g, "Discard all changes and reload the map.\n");

		if (ImGui::MenuItem("Validate")) {
			logf("\n-------- Validating %s --------\n", map->name.c_str());
			map->validate();
			logf("-----------------------------------------\n", map->name.c_str());
			ImGui::SetWindowCollapsed("Messages", false);
			showLogWidget = true;
		}
		tooltip(g, "Checks BSP data structures for invalid values and references. Trivial problems are fixed automatically. Results are output to the Messages widget.");
		ImGui::EndDisabled();

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

	if (ImGui::BeginMenu("View")) {
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
		if (ImGui::MenuItem("Textures", 0, g_settings.render_flags & RENDER_TEXTURES)) {
			g_settings.render_flags ^= RENDER_TEXTURES;
		}
		tooltip(g, "Render textures for all faces.");

		if (ImGui::BeginMenu("Lightmaps")) {
			bool lightEnabled = g_settings.render_flags & RENDER_LIGHTMAPS;
			int* scnt = app->mapRenderer->lightStyleCount;

			if (ImGui::MenuItem("Enabled", 0, lightEnabled)) {
				g_settings.render_flags ^= RENDER_LIGHTMAPS;
			}
			tooltip(g, "Render lighting textures for all faces. Disable for full brightness.");

			ImGui::Separator();

			if (ImGui::MenuItem("Layer 0", 0, app->lightStylesEnabled[0], lightEnabled)) {
				app->lightStylesEnabled[0] = !app->lightStylesEnabled[0];
			}
			tooltip(g, cstrf("Render layer 0 lightmaps. Most faces have this unless they're pitch black.\n\n"
				"%d faces in this map have layer 0 lightmaps.", scnt[0]));

			if (ImGui::MenuItem("Layer 1", 0, scnt[1] > 0 && app->lightStylesEnabled[1], lightEnabled && scnt[1] > 0)) {
				app->lightStylesEnabled[1] = !app->lightStylesEnabled[1];
			}
			tooltip(g, cstrf("Render layer 1 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 1 lightmaps.", scnt[1]));

			if (ImGui::MenuItem("Layer 2", 0, scnt[2] > 0 && app->lightStylesEnabled[2], lightEnabled && scnt[2] > 0)) {
				app->lightStylesEnabled[2] = !app->lightStylesEnabled[2];
			}
			tooltip(g, cstrf("Render layer 2 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 2 lightmaps.", scnt[2]));

			if (ImGui::MenuItem("Layer 3", 0, scnt[3] > 0 && app->lightStylesEnabled[3], lightEnabled && scnt[3] > 0)) {
				app->lightStylesEnabled[3] = !app->lightStylesEnabled[3];
			}
			tooltip(g, cstrf("Render layer 3 lightmaps. Used with toggled/animated lights.\n\n"
				"%d faces in this map have layer 3 lightmaps.", scnt[3]));

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Wireframe", 0, g_settings.render_flags & RENDER_WIREFRAME)) {
			g_settings.render_flags ^= RENDER_WIREFRAME;
		}
		tooltip(g, "Outline all faces.");

		if (ImGui::MenuItem("Special World Faces", 0, g_settings.render_flags & RENDER_SPECIAL)) {
			g_settings.render_flags ^= RENDER_SPECIAL;
		}
		tooltip(g, "Render special faces that are normally invisible and/or have special rendering properties (e.g. the SKY texture).");

		if (ImGui::BeginMenu("Clipnodes")) {
			if (ImGui::MenuItem("World", 0, g_settings.render_flags & RENDER_WORLD_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			tooltip(g, "Render clipnode hulls for worldspawn");

			if (ImGui::MenuItem("Entities", 0, g_settings.render_flags & RENDER_ENT_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_ENT_CLIPNODES;
			}
			tooltip(g, "Render clipnode hulls for solid entities");

			if (ImGui::MenuItem("Transparency", 0, transparentClipnodes)) {
				transparentClipnodes = !transparentClipnodes;
				g_app->mapRenderer->updateClipnodeOpacity(transparentClipnodes ? 128 : 255);
			}
			tooltip(g, "Render clipnode meshes with transparency.");
			g_settings.render_flags = g_settings.render_flags;

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

		if (ImGui::MenuItem("Point Entities", 0, g_settings.render_flags & RENDER_POINT_ENTS)) {
			g_settings.render_flags ^= RENDER_POINT_ENTS;
		}
		tooltip(g, "Render point-sized entities which either have no model or reference MDL/SPR files.");

		if (ImGui::MenuItem("Solid Entities", 0, g_settings.render_flags & RENDER_ENTS)) {
			if (g_settings.render_flags & RENDER_ENTS) {
				g_settings.render_flags &= ~(RENDER_ENTS | RENDER_SPECIAL_ENTS);
			}
			else {
				g_settings.render_flags |= RENDER_ENTS | RENDER_SPECIAL_ENTS;
			}
		}
		tooltip(g, "Render entities that reference BSP models.");

		if (ImGui::MenuItem("Entity Direction Vectors", 0, g_settings.render_flags & RENDER_ENT_DIRECTIONS)) {
			g_settings.render_flags ^= RENDER_ENT_DIRECTIONS;
			app->updateEntDirectionVectors();
		}
		tooltip(g, "Display direction vectors for selected entities.\n"
			"For point entities, vectors usually represent orientation.\n"
			"For solid entities, vectors usually represent movement direction.");

		if (ImGui::MenuItem("Entity Links", 0, g_settings.render_flags & RENDER_ENT_CONNECTIONS)) {
			g_settings.render_flags ^= RENDER_ENT_CONNECTIONS;
		}
		tooltip(g, "Show how entities connect to each other.\n\n"
			"Yellow line = Selected entity targets the connected entity.\n"
			"Blue line = Selected entity is targetted by the connected entity.\n"
			"Green line = Selected entity and connected entity target each other.\n\n"
			"Not all connections are displayed. You may still need to use the Entity Report "
			"to find connections depending on the game the map was compiled for."
		);

		if (ImGui::MenuItem("Entity Render Modes", 0, g_settings.render_flags & RENDER_RENDER_MODES)) {
			g_settings.render_flags ^= RENDER_RENDER_MODES;
		}
		tooltip(g, "Respect entity rendermode, renderamt, and rendercolor keys.\n\n"
			"This enables transparent textures and models. In some cases it makes "
			"entities completely invisible which can make selection difficult."
		);

		if (ImGui::MenuItem("Models", 0, g_settings.render_flags & RENDER_STUDIO_MDL)) {
			g_settings.render_flags ^= RENDER_STUDIO_MDL;

			if (!(g_settings.render_flags & RENDER_STUDIO_MDL)) {
				for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
					app->mapRenderer->map->ents[i]->didStudioDraw = false;
				}
			}
		}
		tooltip(g, "Display game models instead of colored cubes where available.");

		if (ImGui::MenuItem("Sprites", 0, g_settings.render_flags & RENDER_SPRITES)) {
			g_settings.render_flags ^= RENDER_SPRITES;

			if (!(g_settings.render_flags & RENDER_SPRITES)) {
				for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
					app->mapRenderer->map->ents[i]->didStudioDraw = false;
				}
			}
		}
		tooltip(g, "Display sprites instead of colored cubes where available.");

		ImGui::Separator();

		if (ImGui::MenuItem("Leaf Links", 0, g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			g_settings.render_flags ^= RENDER_LEAF_GRAPH;
		}
		tooltip(g, "Display leaf connectivity graph when in leaf selection mode.\n\n"
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
		tooltip(g, "Render wireframe for polygons in the potentially visible set.\n");

		ImGui::Separator();

		if (ImGui::MenuItem("Map Boundaries", 0, g_settings.render_flags & RENDER_MAP_BOUNDARY)) {
			g_settings.render_flags ^= RENDER_MAP_BOUNDARY;
		}
		tooltip(g, "Renders map boundaries as a transparent box around the world. Entities which leave "
			"this box may have visual glitches depending on the engine this map runs in.");

		if (ImGui::MenuItem("Origin", 0, g_settings.render_flags & RENDER_ORIGIN)) {
			g_settings.render_flags ^= RENDER_ORIGIN;
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
		if (ImGui::BeginMenu("Engine")) {
			if (ImGui::MenuItem("Half-Life", 0, g_settings.engine == ENGINE_HALF_LIFE, !app->isLoading)) {
				changed = g_settings.engine != ENGINE_HALF_LIFE;
				g_settings.engine = ENGINE_HALF_LIFE;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -4096;
					g_settings.mapsize_max = 4096;
				}
			}
			tooltip(g, "The standard GoldSrc engine.\n");

			if (ImGui::MenuItem("Sven Co-op", 0, g_settings.engine == ENGINE_SVEN_COOP, !app->isLoading)) {
				changed = g_settings.engine != ENGINE_SVEN_COOP;
				g_settings.engine = ENGINE_SVEN_COOP;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -32768;
					g_settings.mapsize_max = 32768;
				}
			}
			tooltip(g, "Sven Co-op has higher map limits than Half-Life. Some maps need this selected to display correctly in the editor."
				"\n\nAttempting to run a "
				"Sven Co-op map in Half-Life may result in AllocBlock Full errors, Bad Surface Extents, "
				"crashes caused by large textures, and visual glitches caused by crossing the +/-4096 map boundary. "
				"See the Tools menu for solutions to these problems.");

			ImGui::EndMenu();
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
			tooltip(g, "The map size will be set according to the Engine you choose.");

			if (ImGui::MenuItem("+/-4096 (Half-Life)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -4096 && g_settings.mapsize_max == 4096)) {
				g_settings.mapsize_min = -4096;
				g_settings.mapsize_max = 4096;
				g_settings.mapsize_auto = false;
			}
			tooltip(g, "The default map size for Half-Life and most of its mods.");
			
			if (ImGui::MenuItem("+/-32768 (Sven Co-op)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -32768 && g_settings.mapsize_max == 32768)) {
				g_settings.mapsize_min = -32768;
				g_settings.mapsize_max = 32768;
				g_settings.mapsize_auto = false;
			}
			tooltip(g, "The practical map size for Sven Co-op.");
			
			if (ImGui::MenuItem("+/-131072 (Sven Co-op)", 0, !g_settings.mapsize_auto && g_settings.mapsize_min == -131072 && g_settings.mapsize_max == 131072)) {
				g_settings.mapsize_min = -131072;
				g_settings.mapsize_max = 131072;
				g_settings.mapsize_auto = false;
			}
			tooltip(g, "The technically correct map size for Sven Co-op. Players can run around in this giant area but the game becomes a buggy mess once you pass the +/-32768 boundary.");
			
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
				tooltip(g, ("The @mapsize loaded from " + fgd->name + ".fgd.").c_str());
			}
			ImGui::EndMenu();
		}

		if (changed) {
			g_limits = g_engine_limits[g_settings.engine];
			app->mapRenderer->reload();
			reloadLimits();
		}
		
		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}

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
		tooltip(g, "Create a point entity. This is a ripent-only operation which does not affect BSP structures.\n");

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
		tooltip(g, "Create a BSP model and attach it to a new entity. This is not a ripent-only operation and will create new BSP structures.\n");

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
		tooltip(g, "Create a point entity for use with the culling tool. 2 of these define the bounding box for the Cull Box deletion tool.\n");

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Tools"))
	{
		ImGui::BeginDisabled(app->emptyMapLoaded);
		Bsp* map = app->mapRenderer->map;

		static vector<Wad*> emptyWads;
		vector<Wad*>& wads = g_app->mapRenderer ? g_app->mapRenderer->wads : emptyWads;
		BspRenderer* renderer = app->mapRenderer;

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

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
			deduplicateOpen = 1;
		}
		tooltip(g, "Scans for duplicated BSP models and updates entity model keys to reference only one model from set of duplicated models. "
			"This lowers the model count and allows more game models to be precached. Lightmaps are ignored during the scan, so this might "
			"make some entities appear too bright in dark areas, or too dark in lit areas.\n\n"
			"This does not delete BSP data unless you run the Clean command afterward. Cut/hide problematic entities before "
			"deduplicating if you don't want their models swapped.");

		if (ImGui::BeginMenu("Delete BSP Data", !app->isLoading)) {
			if (ImGui::MenuItem("Clean", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Clean " + map->name);

				logf("Cleaning %s\n", map->name.c_str());
				map->remove_unused_model_structures().print_delete_stats(1);

				command->pushUndoState();
			}
			tooltip(g, "Removes unreferenced structures in the BSP data. Run this after editing BSP models.\n\nWhen you edit BSP models or delete"
				" references to them, the data is not deleted until you run this command. "
				"If you are close exceeding engine limits, you may need to run this regularly while creating "
				"and editing models. Watch the Limits and Messages widgets to see how many structures were removed.");

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
			tooltip(g, g_optimize_tip);

			ImGui::Separator();

			if (ImGui::BeginMenu("Clipnode Hull", hasAnyCollision && !app->isLoading)) {
				for (int i = 1; i < MAX_MAP_HULLS; i++) {
					for (int k = 1; k < MAX_MAP_HULLS; k++) {
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + to_string(i) + " --> Hull " + to_string(k)).c_str(), "", false, anyHullValid[k])) {
							LumpReplaceCommand* command = new LumpReplaceCommand("Redirect Hull " + to_string(i));

							Bsp* map = app->mapRenderer->map;
							map->delete_hull(i, k);
							logf("Redirected hull %d to hull %d in map %s\n", i, k, map->name.c_str());
							checkValidHulls();

							logf("Cleaning %s\n", map->name.c_str());
							map->remove_unused_model_structures().print_delete_stats(1);

							command->pushUndoState();
						}
						tooltip(g, "Redirects a clipnode hull in all models including worldspawn. This frees up a large "
							"amount of clipnodes but reduces collision accuracy for entities that use the removed hulls.\n\n"
							"Use this if the Optimize command refused to remove/redirect a hull, and you're ok with the side effects of forcing its removal. "
							"Some entities may clip into walls, hover above the ground, be able/unable to enter certain areas.\n\n"
							"A common use case for this is redirecting Hull 2 -> Hull 1 (Large monsters hull -> Normal monsters hull). "
							"If the map doesn't have any large monsters or pushable objects then there are no side effects for removing the hull. "
							"Removing Hull 1 or Hull 3 always causes noticeable problems because players and most monsters use these hulls."
						);
					}
					if (i != MAX_MAP_HULLS-1)
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
			tooltip(g, "Deletes BSP data and entities inside of a box defined by 2 \"cull\" entities "
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
					hoveredOOB = i;
				}
				tooltip(g, ("Deletes out-of-bounds BSP structures and entities " + string(optionDesc[i]) + ".\n\n"
					"Enable the Map Boundary setting in the View menu to see what will be deleted.").c_str());
			}

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Fix Bad Surface Extents", 0, false, !app->isLoading)) {
			showDownscalePopup = true;
		}
		tooltip(g, "Fix all faces with bad surface extents.");

		if (ImGui::MenuItem("RAD Preparation", 0, false, !app->isLoading)) {
			showRadPrepPopup = true;
			refreshTexlightList = true;
			texlights = map->get_tex_lights();
		}
		tooltip(g, "Prepare the map for light recompilation with VHLT.");

		if (ImGui::MenuItem("Scale Invisible Faces", 0, false, !app->isLoading)) {
			LumpReplaceCommand* command = new LumpReplaceCommand("AllocBlock Reduction");
			if (map->allocblock_reduction() == 0) {
				delete command;
			}
			else {
				command->pushUndoState();
			}
		}
		tooltip(g, "Scales up textures on invisible model faces to reduce AllocBlock size. "
			"Manually increase texture scales or downscale large textures to reduce "
			"AllocBlocks further.");

		if (ImGui::BeginMenu("Textures")) {
			if (ImGui::MenuItem("Create Series WAD", "")) {
				createSeriesWad();
			}
			tooltip(g, "Creates a WAD file containing the embedded textures from all maps in a series. "
				"Textures with the same name but different appearance will not be exported.\n\n"
				"Textures that are exported will also be unembedded from their BSPs, and included "
				"via the newly created WAD file instead. The point of this tool is to reduce disk "
				"space used by campaigns that duplicate textures across many maps.");

			if (ImGui::MenuItem("Embed All", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Embed Textures");

				vector<Wad*> wads = g_app->mapRenderer ? g_app->mapRenderer->wads : vector<Wad*>();

				int count = 0;
				int fail = 0;
				for (int i = 0; i < map->textureCount; i++) {
					BSPMIPTEX* tex = map->get_texture(i);
					if (!tex)
						continue;

					if (tex->nOffsets[0] != 0) {
						continue;
					}

					if (map->embed_texture(i, wads)) {
						count++;
					}
					else {
						fail++;
					}
				}
				logf("Embedded %d textures\n", count);

				command->pushUndoState();
			}
			tooltip(g, "Embeds all externally referenced textures, forcing them to be loaded from the BSP instead of WADs.");

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
			tooltip(g, "Deletes all embedded texture data, forcing textures to be loaded from WADs referenced "
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
			tooltip(g, "Downscales textures that exceed the max texture size for the selected engine "
				"and adjusts texture coordinates accordingly.\n\nIf a texture is stored in a WAD, "
				"it is first embedded into the BSP before being downscaled.\n");

			if (ImGui::MenuItem("Remove Unused WADs", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Remove Unused WADs", true);
				map->remove_unused_wads(wads);
				command->pushUndoState();
			}
			tooltip(g, "Removes unused WADs from the worldspawn 'wad' keyvalue and strips folder paths.");

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Zero Entity Origins", 0, false, !app->isLoading)) {
			LumpReplaceCommand* command = new LumpReplaceCommand("Zero Entity Origins");

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

		if (ImGui::MenuItem("Face Editor", "", showFaceEditorWidget)) {
			showFaceEditorWidget = !showFaceEditorWidget;
			if (showFaceEditorWidget)
				ImGui::SetWindowCollapsed("Face Editor", false);
		}
		tooltip(g, "Edit faces and textures.");

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

		if (ImGui::MenuItem("Messages", "", showLogWidget)) {
			showLogWidget = !showLogWidget;
			if (showLogWidget)
				ImGui::SetWindowCollapsed("Messages", false);
		}
		tooltip(g, "Show program messages.");

		if (ImGui::MenuItem("Transform", "Ctrl+M", showTransformWidget)) {
			showTransformWidget = !showTransformWidget;
			if (showTransformWidget)
				ImGui::SetWindowCollapsed("Transformation", false);
		}
		tooltip(g, "Move, rotate, and scale entities.");

		ImGui::Separator();

		ImGui::PopItemFlag();

		static string userLayout = getUserLayoutPath();

		if (ImGui::MenuItem("Save Widget Layout", NULL)) {
			ImGui::SaveIniSettingsToDisk(userLayout.c_str());
			app->getWindowSize(g_settings.autoload_layout_width, g_settings.autoload_layout_height);
			g_settings.save();
			logf("Layout saved to %s\n", userLayout.c_str());
		}
		tooltip(g, "Save the position and size of your widgets as they are now.");

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
}

void Gui::drawMenuBar() {
	ImGuiContext& g = *GImGui;

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
		drawStandardMenuBar();
	}

	
	string fpsText = to_string((int)ImGui::GetIO().Framerate) + " FPS";
	string statsText = fpsText;
	string wpolyText;
	ImVec4 wpolyColor = ImVec4(1,1,1,1);

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

	float fpsWidth = defaultFont->CalcTextSizeA(g_settings.fontSize, FLT_MAX, FLT_MAX, statsText.c_str()).x;
	float rightAlignStart = ImGui::GetWindowWidth() - (fpsWidth + 20);

	ImGui::SameLine(rightAlignStart);
	ImGui::Text(fpsText.c_str());

	if (g_settings.show_wpoly) {
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextColored(wpolyColor, wpolyText.c_str());
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Text(" wpoly");
	}

	if (ImGui::BeginPopupContextWindow()) {
		if (ImGui::MenuItem("VSync", NULL, vsync)) {
			vsync = !vsync;
			glfwSwapInterval(vsync ? 1 : 0);
		}
		if (ImGui::MenuItem("wpoly", NULL, g_settings.show_wpoly)) {
			g_settings.show_wpoly = !g_settings.show_wpoly;
		}
		tooltip(g, "Display the number of polygons that would be rendered in-game. Enabling this reduces FPS when wpoly counts are high.");

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
		int leafCount = app->pickInfo.leaves.size();

		if (entCount > 1) {
			selectStr = to_string(entCount) + " entities selected";
		}
		else if (entCount == 1) {

			string tname = app->pickInfo.getEnt()->getTargetname();
			string cname = app->pickInfo.getEnt()->getClassname();
			selectStr = tname.size() ? tname + " - " + cname : cname;
		}
		else if (leafCount == 1) {
			selectStr = "leaf #" + to_string(app->pickInfo.getLeafIndex()) + " selected";
		}
		else if (leafCount > 0) {
			selectStr = to_string(leafCount) + " leaves selected";
		}
		else if (faceCount == 1) {
			selectStr = "face #" + to_string(app->pickInfo.getFaceIndex()) + " selected";
		}
		else if (faceCount > 0) {
			selectStr = to_string(faceCount) + " faces selected";
		}
		
		static char cam_origin[32];
		static char cam_angles[32];
		int fontSize = g_settings.fontSize;
		static vec3 last_cam_origin = vec3(0.1f, 0, 0);
		static vec3 last_cam_angles = vec3(0.1f, 0, 0);
		float originWidth = defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, cam_origin).x;
		float typicalOriginWidth = defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "-4096 -4096 -4096").x;
		originWidth = max(originWidth, typicalOriginWidth) + 10;
		float anglesWidth = defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, cam_angles).x;
		float typicalAnglesWidth = defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "-90 -180 0").x;
		anglesWidth = max(anglesWidth, typicalAnglesWidth) + 10;
		
		float selectWidth = defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, selectStr.c_str()).x;
		
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
	ImGuiContext& g = *GImGui;
	ImGuiIO& io = ImGui::GetIO();
	Bsp* map = app->mapRenderer->map;

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
			
			if (showMergePopupAfterFailPopup) {
				showMergePopupAfterFailPopup = false;
				showMergePopup = true;
			}
		}
		ImGui::Dummy(ImVec2(0.0f, 20.0f));

		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}

	if (confirmMerge == 2) {
		ImGui::OpenPopup("Confirm Merge");
	}

	ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Confirm Merge", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextWrapped("The selected models have clipnodes that are overlapping or can't be merged simply.\n\n"
			"Inspect clipnodes after merging. They will likely be broken.");

		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Button("Merge")) {
			LumpReplaceCommand* command = new LumpReplaceCommand("Merge Models");
			Bsp* map = app->mapRenderer->map;
			int newIndex = map->merge_models(app->pickInfo.getEnts(), true);

			if (newIndex >= 0 || newIndex == -3) {
				command->pushUndoState();
			}
			else {
				delete command;
			}

			confirmMerge = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			confirmMerge = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (deduplicateOpen) {
		ImGui::OpenPopup("Deduplicate Models");
	}

	ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Deduplicate Models", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		static bool allowShift = false;
		static int dedupEstimate = -1;

		ImGui::Text("Model count will be lowered by: %d", dedupEstimate);

		if (dedupEstimate == -1) {
			dedupEstimate = map->deduplicate_models(allowShift, true);
		}

		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Checkbox("Allow Shifted Textures", &allowShift)) {
			dedupEstimate = -1;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Ignore texture shifts when deduplicating. "
				"Textures must still match for every face but the shift values don't need to be exactly the same.");
		}

		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Button("Deduplicate")) {
			LumpReplaceCommand* command = new LumpReplaceCommand("Deduplicate models");
			map->deduplicate_models(allowShift, false);
			command->pushUndoState();
			ImGui::CloseCurrentPopup();
			deduplicateOpen = 0;
			dedupEstimate = -1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Select Unique")) {
			app->deselectObject();

			for (int m = 1; m < map->modelCount; m++) {
				for (int i = 0; i < map->ents.size(); i++) {
					if (map->ents[i]->getBspModelIdx() == m) {
						app->pickInfo.selectEnt(i);
						break;
					}
				}
			}
			
			app->postSelectEnt();
			deduplicateOpen = 0;
			dedupEstimate = -1;
			ImGui::CloseCurrentPopup();
		}
		tooltip(g, "Don't deduplicate models, just select entities that use a unique model.\n\nUse this then drag the entities out to space to see which models need merging (for when deduplication isn't enough).");

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			deduplicateOpen = 0;
			dedupEstimate = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (showDownscalePopup) {
		ImGui::OpenPopup("Fix Bad Surface Extents");
	}

	ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Fix Bad Surface Extents", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

		static int minDim = 224;
		static int step = 16;
		static int subdivideMax = 20;
		static bool shouldDownscale = true;
		static bool shouldSubdivide = false;
		static bool shouldScale = false;

		ImGui::TextWrapped("Fixes are applied in order.");

		ImVec2 cellPadding(5.0f, 10.0f); // x = horizontal, y = vertical
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

		if (ImGui::BeginTable("MyTable", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Fixed1", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Fixed2", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Fixed3", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Auto");

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGui::Text("1)");
			ImGui::TableNextColumn();
			ImGui::Checkbox("Subdivide Faces", &shouldSubdivide);
			tooltip(g, "Subdivide faces for a texture if additional faces would be below the Subdivide Limit. The drawback to this method is reduced in-game performace from higher poly counts.");

			ImGui::TableNextColumn();
			ImGui::Dummy(ImVec2(20, 0));

			ImGui::TableNextColumn();
			ImGui::BeginDisabled(!shouldSubdivide);
			ImGui::SetNextItemWidth(200);
			ImGui::DragInt("Face Limit", &subdivideMax, 0.1f, 0, g_limits.max_faces);
			tooltip(g, "This limit is applied per texture, not globally. All faces for a texture will be subdivided only if the newly created face count is less than this limit.\n");
			ImGui::EndDisabled();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("2)");

			ImGui::TableNextColumn();
			ImGui::Checkbox("Downscale Textures", &shouldDownscale);
			tooltip(g, "Downscale a texture if subdivision would create more faces than desired.\n");

			ImGui::TableNextColumn();
			ImGui::Dummy(ImVec2(20, 0));

			ImGui::TableNextColumn();
			ImGui::BeginDisabled(!shouldDownscale);
			ImGui::SetNextItemWidth(200);
			if (ImGui::InputScalar("Size Limit", ImGuiDataType_U32, (void*)&minDim, &step)) {
				minDim = max(16, (minDim / 16) * 16);
			}
			tooltip(g, "Textures will be downscaled no lower than the limit you set here. Increase for higher texture quality. Decrease to fix more errors."
				"\n\nThis applies only to the largest dimension of the texture. For example, setting 128 "
				"here will allow downscaling a texture from 256x128 to 128x64, and no lower than that.\n\n"
				"For Sven Co-op maps compiled with -subdivide 528:\n"
				"224 fixes extents for texture sizes 512 and up.\n"
				"112 fixes extents for texture sizes 256 and up.\n"
				"48 fixes extents for texture sizes 128 and up.\n"
				"16 fixes as many errors as possible.");
			ImGui::EndDisabled();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("3)");

			ImGui::TableNextColumn();
			ImGui::Checkbox("Scale Faces", &shouldScale);
			tooltip(g, "Scale up faces if both downscaling and subdivsion failed. This ensures every face "
				"has valid extents, but will misalign textures. In some cases that's ok (texture shifting "
				"often doesn't matter for noisy terrain textures).\n");

			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		ImGui::Dummy(ImVec2(0, 10));

		ImGui::BeginDisabled(!shouldSubdivide && !shouldScale && !shouldDownscale);
		if (ImGui::Button("Apply Fixes")) {
			LumpReplaceCommand* command = new LumpReplaceCommand("Fix Bad Surface Extents");
			if (shouldSubdivide)
				map->fix_all_bad_surface_extents_with_subdivide(subdivideMax);

			if (shouldDownscale)
				map->fix_bad_surface_extents_with_downscale(minDim);

			if (shouldScale)
				map->fix_bad_surface_extents_with_scale();

			command->pushUndoState();
			ImGui::CloseCurrentPopup();
			showDownscalePopup = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
			showDownscalePopup = false;
		}
		ImGui::EndPopup();
	}


	if (showRadPrepPopup) {
		ImGui::OpenPopup("Configure Texlights");
	}

	ImGui::SetNextWindowSize(ImVec2(400, max(400.0f, app->windowHeight*0.6f)), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(350, 300), ImVec2(FLT_MAX, app->windowHeight));
	if (ImGui::BeginPopupModal("Configure Texlights", NULL, 0)) {

		ImGui::TextWrapped("Texture light information may be needed for recompiling map lighting. "
			"Below are texlights found in this map's info_texlight entities."
		);

		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Button("Add Default")) {
			static unordered_map<string, string> hammer35_texlights = {
				{"+0~WHITE", "255 255 255 100"},
				{"+0~GENERIC65 ", "255 255 255 750"},
				{"+0~GENERIC85", "110 140 235 20000 "},
				{"+0~GENERIC86", "255 230 125 10000"},
				{"+0~GENERIC86B", "60 220 170 20000"},
				{"+0~GENERIC86R", "128 0 0 60000"},
				{"GENERIC87A", "100  255 100 1000"},
				{"GENERIC88A", "255 100 100 1000"},
				{"GENERIC89A", "40 40 130 1000"},
				{"GENERIC90A", "200 255 200 1000"},
				{"GENERIC105", "255 100 100 1000"},
				{"GENERIC106", "120 120 100 1000"},
				{"GENERIC107", "180 50 180 1000"},
				{"GEN_VEND1", "50 180 50 1000"},
				{"EMERGLIGHT", "255 200 100 50000"},
				{"+0~FIFTS_LGHT01", "160 170 220 4000"},
				{"+0~FIFTIES_LGT2", "160 170 220 5000"},
				{"+0~FIFTS_LGHT4", "160 170 220 4000"},
				{"+0~LIGHT1", "40 60 150 3000"},
				{"+0~LIGHT3A", "180 180 230 10000"},
				{"+0~LIGHT4A", "200 190 130 11000"},
				{"+0~LIGHT5A", "80 150 200 10000"},
				{"+0~LIGHT6A", "150 5 5 25000"},
				{"+0~TNNL_LGT1", "240 230 100 10000"},
				{"+0~TNNL_LGT2", "190 255 255 12000"},
				{"+0~TNNL_LGT3", "150 150 210 17000"},
				{"+0~TNNL_LGT4", "170 90 40 10000"},
				{"+0LAB1_W6D", "165 230 255 4000"},
				{"+0LAB1_W6", "150 160 210 8800"},
				{"+0LAB1_W7", "245 240 210 4000"},
				{"SKKYLITE", "165 230 255 1000"},
				{"+0~DRKMTLS1", "205 0 0 6000"},
				{"+0~DRKMTLGT1", "200 200 180 6000"},
				{"+0~DRKMTLS2", "150 120 20 30000"},
				{"+0~DRKMTLS2C", "255 200 100 50000"},
				{"+0DRKMTL_SCRN", "60 80 255 10000"},
				{"~LAB_CRT9A", "225 150 150 100"},
				{"~LAB_CRT9B", "100 100 255 100"},
				{"~LAB_CRT9C", "100 200 150 100"},
				{"~LIGHT3A", "190 20 20 3000"},
				{"~LIGHT3B", "155 155 235 2000"},
				{"~LIGHT3C", "220 210 150 2500"},
				{"~LIGHT3E", "90 190 140 6000"},
				{"C1A3C_MAP", "100 100 255 100"},
				{"FIFTIES_MON1B", "100 100 180 30"},
				{"+0~LAB_CRT8", "50 50 255 100"},
				{"ELEV2_CIEL", "255 200 100 800"},
				{"YELLOW", "255 200 100 2000"},
				{"RED", "255 0 0 1000"},
				{"C14_LIGHT_BIG00", "160 170 220 567"},
				{"SUBWAY_LIGHTS", "160 170 220 8000"},
				{"grate_light_ury", "160 170 220 8888"},
				{"quaintLight1bar", "160 170 220 8888"},
				{"C14_LIGHT_null", "160 170 220 200"},
				{"comp_lights2", "255 200 100 200"},
				{"light_newblue", "123 123 255 2222"},
				{"C3A2_LIGHT", "255 255 128 4567"},
				{"hera_light1", "0 128 255 2000"},
				{"LITEPANEL1", "160 170 220 4000"},
			};

			unordered_map<string, string> addLights = map->filter_tex_lights(hammer35_texlights);
			for (auto item : addLights) {
				texlights[toUpperCase(item.first)] = item.second;
			}
			refreshTexlightList = true;
		}
		tooltip(g, "Add default texlight values shipped with Valve Hammer Editor 3.5. Texture names not used in the map are ignored.");

		ImGui::SameLine();
		if (ImGui::Button("Add Custom")) {
			char* fname = tinyfd_openFileDialog("Import lights.rad file", "",
				1, radFilterPatterns, "Texlight File (*.rad)", 1);

			if (fname) {
				unordered_map<string, string> addLights = map->load_texlights_from_file(fname);
				for (auto item : addLights) {
					texlights[toUpperCase(item.first)] = item.second;
				}
				refreshTexlightList = true;
			}
		}
		tooltip(g, "Add texlights from a custom lights.rad file. Texture names not used in the map are ignored.");

		ImGui::SameLine();
		if (ImGui::Button("Estimate")) {

			string msg = "False positives can be reduced by comparing this map with a copy "
				"which was recompiled without lights.rad. Texlights identified in both maps "
				"cannot possibly be texlights if only one map had texlights enabled.\n\n"
				"Compare with a recompiled map?";
			int ret = tinyfd_messageBox(
				"Estimation Accuracy", /* NULL or "" */
				msg.c_str(), /* NULL or "" may contain \n \t */
				"yesno", /* "ok" "okcancel" "yesno" "yesnocancel" */
				"question", /* "info" "warning" "error" "question" */
				0);

			unordered_map<string, string> addLights;

			if (ret == 1) {
				// bigger epsilon to catch more edge cases
				addLights = map->estimate_texlights(8);

				char* fname = tinyfd_openFileDialog("Select the recompiled BSP", "",
					1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

				if (fname) {
					Bsp* othermap = new Bsp(fname);
						
					if (othermap->faceCount == map->faceCount && othermap->vertCount == map->vertCount) {
						unordered_map<string, string> otherTexLights = othermap->get_tex_lights();
						unordered_map<string, string> otherEstLights = othermap->estimate_texlights();
						int oldSize = addLights.size();
						for (auto item : otherEstLights) {
							if (otherTexLights.count(item.first)) {
								continue; // a texlight defined in info_texlights. this is valid.
							}
								
							// any other texlight found in the recompiled map can't actually be a
							// texlight because it's assumed to have have been recompiled with
							// texlights disabled.
							addLights.erase(item.first);
						}
						int removed = oldSize - addLights.size();
						if (removed) {
							logf("Ignored %d false positives (both maps guessed that a texture as a texlight, "
								"but only one map has texlights enabled).\n", removed);
						}
						else {
							logf("No false positives were removed after comparing with the recompiled map.\n");
						}
					}
					else {
						string msg = "The map you selected did not appear to the same map with recompiled lighting. Estimation cancelled.";
						int ret = tinyfd_messageBox(
							"Invalid Map", /* NULL or "" */
							msg.c_str(), /* NULL or "" may contain \n \t */
							"ok", /* "ok" "okcancel" "yesno" "yesnocancel" */
							"error", /* "info" "warning" "error" "question" */
							0);
						addLights.clear();
					}

					delete othermap;
				}
			}
			else {
				// small epsilon to prevent too many false positives
				addLights = map->estimate_texlights(8);
			}

			for (auto item : addLights) {
				texlights[toUpperCase(item.first)] = item.second;
			}
			refreshTexlightList = true;
		}
		tooltip(g, "Estimate texlight values by analyzing lightmaps. Colors "
			"may be slightly off, and brightness will be very wrong, but this can give you "
			"a good starting point.\n\n"

			"Some texlights won't be detected if other lights are shining onto them to create inconsistent lightmaps. "
			"Normal textures are falsely detected as texlights if they have perfectly consistent lightmaps."
		);

		static string oldBuffer;
		static char buffer[65536] = "";
		static int numtexlight = 0;

		if (refreshTexlightList) {
			string texlightString = "";

			std::vector<std::string> keys;
			for (const auto& item : texlights) {
				keys.push_back(item.first);
			}

			sort(keys.begin(), keys.end());

			for (const string& key : keys) {
				texlightString += key + "\t\t" + texlights[key] + "\n";
			}
			strncpy(buffer, texlightString.c_str(), 65535);
			buffer[65535] = 0;

			refreshTexlightList = false;
			numtexlight = texlights.size();
		}

		ImVec2 multisize = ImGui::GetContentRegionAvail();
		multisize.y -= 80.0f;
		ImGui::InputTextMultiline("##texlights", buffer, sizeof(buffer), multisize);
		if (string(buffer) != oldBuffer) {
			oldBuffer = buffer;
			refreshTexlightList = true;
			texlights.clear();
			vector<string> lines = splitString(buffer, "\n");
			for (string line : lines) {
				string name, args;
				if (map->load_texlight_from_string(line, name, args)) {
					texlights[name] = args;
				}
			}
		}
		ImGui::Text("%d texlights", numtexlight);

		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Button("Prepare")) {
			
			LumpReplaceCommand* command = new LumpReplaceCommand("RAD Preparation");

			bool didAnything = false;

			if (map->replace_texlights(buffer)) {
				didAnything = true;
			}

			int numDeletedRadTextures = map->delete_embedded_rad_textures(NULL);

			if (numDeletedRadTextures == -1) {
				string msg = "Embedded RAD textures are corrupted. If you have the original "
					"unedited BSP file, you can try to recover texture data from it.\n\n"
					"Do you want to recover data from the original BSP?";
				int ret = tinyfd_messageBox(
					"Corrupt Data", /* NULL or "" */
					msg.c_str(), /* NULL or "" may contain \n \t */
					"yesno", /* "ok" "okcancel" "yesno" "yesnocancel" */
					"question", /* "info" "warning" "error" "question" */
					0);

				if (ret == 1) { // yes
					char* fname = tinyfd_openFileDialog("Select the original BSP", "",
						1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

					if (fname) {
						Bsp* ogbsp = new Bsp(fname);
						numDeletedRadTextures = map->delete_embedded_rad_textures(ogbsp);

						if (numDeletedRadTextures == -1) {
							logf("Failed to delete embedded RAD textures. The original texture data no longer exists and could not be recovered.\n");
						}
						else if (numDeletedRadTextures > 0) {
							didAnything = true;
						}

						delete ogbsp;
					}
				}
				else {
					logf("Failed to delete embedded RAD textures. The original texture data no longer exists.\n");
				}
			}
			else if (numDeletedRadTextures > 0) {
				didAnything = true;
			}

			// convert light_surface back to its original state
			for (Entity* ent : map->ents) {
				if (ent->hasKey("_tex")) {
					string cname = ent->getClassname();
					if (cname == "light" || cname == "light_spot") {
						ent->setOrAddKeyvalue("classname", "light_surface");
						ent->setOrAddKeyvalue("convertto", cname);
						didAnything = true;
					}
				}
			}

			if (map->do_entities_share_models()) {
				int numDup = map->make_unique_texlight_models();
				if (numDup > 0) {
					logf("Duplicated %d models. You can Deduplicate these after running RAD to keep the model count low.\n", numDup);
					didAnything = true;
				}
				else {
					logf("No models needed to be duplicated.\n");
				}
			}

			int numMissing = map->count_missing_textures();

			if (!didAnything)
				logf("No changes were needed to prepare for RAD compilation.\n");

			if (numMissing) {
				string msg = "VHLT requires texture data for embedded lightmaps and texture reflection. "
					"This map is missing " + to_string(numMissing) + " textures. To compile, you will need to choose one of these options:\n\n"
					"1. Compile with -notextures to disable modern RAD features.\n"
					"2. Update your settings to find missing WAD files.\n"
					"3. Replace missing textures with the Face Editor.";
				int ret = tinyfd_messageBox(
					"Missing Textures", /* NULL or "" */
					msg.c_str(), /* NULL or "" may contain \n \t */
					"ok", /* "ok" "okcancel" "yesno" "yesnocancel" */
					"warning", /* "info" "warning" "error" "question" */
					0);
			}
			else {
				map->generate_wa_file();
			}

			if (!didAnything) {
				delete command;
			}
			else {
				command->pushUndoState();
			}

			ImGui::CloseCurrentPopup();
			showRadPrepPopup = false;
		}
		tooltip(g, ("Prepararing this map will:\n\n"
			"1. Create/update an info_texlights entity according to the above configuration.\n"
			"2. Convert light/light_spot entities to light_surface, if that's what they were originally.\n"
			"3. Delete embedded RAD textures (fixes \"Malformed face\" errors when these are invalid).\n"
			"4. Duplicate shared texlight models (deduplicated models can't emit light).\n"
			"5. Generate a \"" + map->name + ".wa_\" file for running RAD without the -notextures option.").c_str()
		);

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showRadPrepPopup = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (showMergePopup) {
		ImGui::OpenPopup("Merge Multiple");
	}

	ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(500, 300), ImVec2(FLT_MAX, app->windowHeight));
	if (ImGui::BeginPopupModal("Merge Multiple", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
		static bool optimizeMerge = false;
		static bool forceNohull2 = false;
		static int ripentmode = 0;
		static char mapList[8192];
		static int numSelected = 0;
		static bool dirtyMaplist = true;

		if (dirtyMaplist) {
			numSelected = splitString(mapList, "|").size();
			dirtyMaplist = false;
		}

		ImGui::TextWrapped((to_string(numSelected) + " BSPs selected for merging:").c_str());

		ImGui::Columns(2, 0, false);
		ImGui::SetColumnWidth(0, 450);
		ImGui::SetColumnWidth(1, 50);
		ImGui::SetNextItemWidth(450);
		ImGui::InputText("##mergelist", mapList, 8192); ImGui::NextColumn();

		if (ImGui::Button(" ... ")) {
			char* fname = tinyfd_openFileDialog("Merge Multiple", "",
				1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

			if (fname) {
				strncpy(mapList, fname, 8192);
				mapList[8191] = 0;
				dirtyMaplist = true;
			}
		}

		ImGui::Dummy(ImVec2(0, 10));

		ImGui::Columns(4, 0, false);
		ImGui::SetColumnWidth(0, 130);
		ImGui::SetColumnWidth(1, 80);
		ImGui::SetColumnWidth(2, 80);
		ImGui::SetColumnWidth(3, 200);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Series Ripent: "); ImGui::NextColumn();

		ImGui::RadioButton("No", &ripentmode, 0); ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			tooltip(g, "Do not touch any of the map entity logic.\n");
		}

		ImGui::RadioButton("Yes", &ripentmode, 1); ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			tooltip(g,
				"Ripent the maps so that they play as a connected map series. This requires the bspguy\n"
				"map script/plugin to be added to the map CFG. The plugin ensures only one map's entities\n"
				"are active at once. This saves you time and reduces lag in maps with lots of entities.\n\n"

				"The following changes will be applied:\n"
				"- trigger_changelevel is replaced with trigger_once for map transition logic.\n"
				"- trigger_changesky is added for map transitions that change the skybox.\n"
				"- bspguy_equip entities are added where you can set up CFG loadouts for each map.\n"
				"- various entities and keyvalues are added for the bspguy plugin transition logic.\n"
			);
		}

		ImGui::RadioButton("Yes (scriptless)", &ripentmode, 2); ImGui::NextColumn();
		if (ImGui::IsItemHovered()) {
			tooltip(g,
				"This removes the need for the bspguy map script/plugin by emulating its functionality with map\n"
				"entities. The resulting entity logic will be more complex and error-prone. This also greatly\n"
				"increases the active entity count as entities from all maps will be loaded at once.\n\n"

				"Enabling this option will apply these additional ripent changes:\n"
				"- entities are renamed to prevent conflicts between maps.\n"
				"- monsters are replaced with squadmakers and spawned when needed to reduce lag.\n"
				"- spawns are disabled in all but the first map.\n"
				"- info_player_start/coop/dm2 is replaced with info_player_deathmatch.\n"
				"- trigger_auto is replaced with trigger_relay and triggered on map transitions.\n"
			);
		}

		ImGui::Columns(1);
		ImGui::Dummy(ImVec2(0, 2));
		ImGui::Text("Preparations:");
		ImGui::SameLine();
		ImGui::Dummy(ImVec2(3, 0));
		ImGui::SameLine();
		
		ImGui::Checkbox("Optimize", &optimizeMerge);
		tooltip(g, (string("Optimizes maps before merging. Try this if map limits are exceeded.\n\nOptimizing ") + g_optimize_tip).c_str());

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(10, 0));
		ImGui::SameLine();
		ImGui::Checkbox("No Hull 2", &forceNohull2);
		tooltip(g, "Forces redirection of hull 2 to hull 1 in each map before merging. This reduces "
			"clipnodes and collision accuracy for large monsters and pushables.");

		ImGui::Dummy(ImVec2(0, 2));
		ImGui::Text("Map Size: ");
		ImGui::SameLine();
		ImGui::Dummy(ImVec2(30, 0));
		ImGui::SameLine();
		ImGui::Text((string("+/-") + to_string(g_settings.mapsize_max)).c_str());
		if (ImGui::IsItemHovered())
			tooltip(g, "Change the Map Size in the Settings menu to adjust the bounds for merged maps.\n");

		ImGui::Dummy(ImVec2(0, 10));

		if (numSelected < 2) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Merge")) {
			vector<string> input_maps = splitString(mapList, "|");

			MergeResult result = BspMerger::createMergedMap(input_maps, "merged_map", optimizeMerge,
				forceNohull2, ripentmode);
		
			if (result.invalidMaps) {
				string msg = "One or more of the input maps failed to load.";
				int ret = tinyfd_messageBox(
					"Invalid Map", /* NULL or "" */
					msg.c_str(), /* NULL or "" may contain \n \t */
					"ok", /* "ok" "okcancel" "yesno" "yesnocancel" */
					"error", /* "info" "warning" "error" "question" */
					0);
			}
			else if (result.overflow) {
				g_app->mergeResult = result;
				showMergePopup = false;
				ImGui::CloseCurrentPopup();
				showMergePopupAfterFailPopup = true;
			}
			else if (result.notEnoughSpace) {
				string msg = "The merger failed to fit all maps inside the configured Map Size.\n\n"
					"Do you want to try arranging the maps yourself?";
				int ret = tinyfd_messageBox(
					"Packing Failure", /* NULL or "" */
					msg.c_str(), /* NULL or "" may contain \n \t */
					"yesno", /* "ok" "okcancel" "yesno" "yesnocancel" */
					"warning", /* "info" "warning" "error" "question" */
					0);

				if (result.map)
					delete result.map;

				if (ret == 1) {
					showMergePopup = false;
					showTransformWidget = true;
					ImGui::CloseCurrentPopup();
					g_app->mergeMultiple(input_maps, optimizeMerge, forceNohull2, ripentmode);
				}
			}
			else if (result.map->isWritable()) {
				g_app->openMap(result.map);
				showMergePopup = false;
				ImGui::CloseCurrentPopup();
			}
		}
		if (numSelected < 2) {
			ImGui::EndDisabled();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel")) {
			showMergePopup = false;
			ImGui::CloseCurrentPopup();
		}

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
		float iconWidth = (g_settings.fontSize / 22.0f) * 32;
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
			app->hiddenLeaves.clear();
			app->pickMode = PICK_OBJECT;
			showFaceEditorWidget = false;
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
			
			vector<int> modelIndexes = app->pickInfo.getModelIndexes();
			Bsp* map = app->pickInfo.getMap();
			BspRenderer* mapRenderer = app->mapRenderer;

			if (app->pickMode == PICK_OBJECT) {
				app->deselectFaces();
				app->deselectObject();

				// don't select all worldspawn faces because it lags the program
				if (modelIndexes.size() > 0 && modelIndexes[0] != 0) {
					for (int idx : modelIndexes) {
						BSPMODEL& model = map->models[idx];

						for (int i = 0; i < model.nFaces; i++) {
							int faceIdx = model.iFirstFace + i;
							app->pickInfo.selectFace(faceIdx);
						}
					}
				}
			}
			else {
				app->pickInfo.leaves.clear();
			}

			app->hiddenLeaves.clear();
			g_app->mapRenderer->highlightPickedFaces(true);
			g_app->updateTextureAxes();
			
			app->pickMode = PICK_FACE;
			app->pickCount++; // force texture tool refresh
			showFaceEditorWidget = true;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Face selection mode");
			ImGui::EndTooltip();
		}

		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_LEAF ? selectColor : dimColor);
		if (ImGui::ImageButton("leafpickicon", (ImTextureID)leafIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1))) {
			switchToLeafSelectMode(false, false);
		}
		
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Leaf selection mode");
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
	bool sharedStructs = app->modelUsesSharedStructures && app->pickInfo.ents.size() > 0;
	bool concave = !app->isTransformableSolid && app->pickInfo.ents.size() > 0;
	bool invalidsolid = app->invalidSolid && app->pickInfo.ents.size() == 1;
	bool dutchAngle = app->cameraAngles.y != 0;
	bool worldspawnOri = app->mapRenderer->mapOffset != vec3();
	bool showStatus = sharedStructs || concave || invalidsolid || badSurfaceExtents
		|| lightmapTooLarge || sharedStructs || app->forceAngleRotation
		|| dutchAngle || angleKey || worldspawnOri || app->mapArrangeMode;

	int mergeSolveResult = 0;
	if (app->mapArrangeMode) {
		vector<Bsp*> bsps;
		for (BspRenderer* renderer : app->arrangeBsps) {
			bsps.push_back(renderer->map);
		}
		vector<MapMergeOp> mergeOps;
		mergeSolveResult = BspMerger::solveMerge(bsps, mergeOps);
	}
	
	static int lastPickCount = 0;

	if (app->pickCount != lastPickCount) {
		lastPickCount = app->pickCount;
		checkFaceErrors();
	}

	if (showStatus) {
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2, app->windowHeight - (10.0f+mainMenuBarHeight));
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (concave) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "CONCAVE SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Some features of the Transformation widget are disabled for the selected model(s).\n";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			} else if (sharedStructs) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "SHARED DATA");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Model shares planes/clipnodes with other models.\n\nRight click the entity and select \"Duplicate BSP Model\" to enable all features of the Transformation widget.";
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
			if (worldspawnOri) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WORLD ORIGIN");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Worldspawn has an origin. This will break the game.\n\nEither apply the transformation or delete the origin key to fix.\n");
				}
			}
			if (app->mapArrangeMode) {
				if (mergeSolveResult == 0) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "MERGEABLE");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("The maps can be merged as is.\n");
					}
				}
				else if (mergeSolveResult == -1) {
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "MAP OVERLAP");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Maps are overlapping and can't be merged.\n");
					}
				}
				else if (mergeSolveResult == -2) {
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "MERGE UNSOLVED");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("The maps do not overlap, but the map merger is unable to figure out how to merge them.\n");
					}
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

			ImGui::PushFont(consoleFont, g_settings.fontSize);
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
				ImGui::Text("Entity ID: %d", app->pickInfo.getEntIndex());

				if (modelIndex > 0) {
					BSPMODEL& model = map->models[modelIndex];
					ImGui::Text("Model ID: %d", modelIndex);
					ImGui::Text("Model polys: %d", model.nFaces);
					ImGui::Text("Model leaves: %d", model.nVisLeafs);
					ImGui::Text("Model face offset: %d", model.iFirstFace);
					ImGui::Text("Model mins: %.2f %.2f %.2f", model.nMins.x, model.nMins.y, model.nMins.z);
					ImGui::Text("Model maxs: %.2f %.2f %.2f", model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);
					ImGui::Text("Model origin: %.2f %.2f %.2f", model.vOrigin.x, model.vOrigin.y, model.vOrigin.z);

					ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
					ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

					ImGui::Checkbox("Debug nodes", &app->debugNodes);
					ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
				}

				if (app->pickInfo.leaves.size()) {
					if (app->pickInfo.getLeafIndex() != -1) {
						BSPLEAF& leaf = map->leaves[app->pickInfo.getLeafIndex()];
						ImGui::Text("Leaf ID: %d", app->pickInfo.getLeafIndex());
						ImGui::Text("Leaf contents: %s (%d)", map->getLeafContentsName(leaf.nContents), leaf.nContents);
						ImGui::Text("Leaf faces: %d", leaf.nMarkSurfaces);
						ImGui::Text("Leaf first surf: %d", leaf.iFirstMarkSurface);
						ImGui::Text("Leaf VIS offset: %d", leaf.nVisOffset);
						ImGui::Text("Leaf ambient levels: %d %d %d %d", leaf.nAmbientLevels[0], leaf.nAmbientLevels[1], leaf.nAmbientLevels[2], leaf.nAmbientLevels[3]);
						ImGui::Text("Leaf mins: %d %d %d", (int)leaf.nMins[0], (int)leaf.nMins[1], (int)leaf.nMins[2]);
						ImGui::Text("Leaf maxs: %d %d %d", (int)leaf.nMaxs[0], (int)leaf.nMaxs[1], (int)leaf.nMaxs[2]);
					}
					else {
						unordered_set<int> uniqueFaces;
						for (int idx : app->pickInfo.leaves) {
							BSPLEAF& leaf = map->leaves[idx];
							for (int i = 0; i < leaf.nMarkSurfaces; i++) {
								uniqueFaces.insert(map->marksurfs[leaf.iFirstMarkSurface + i]);
							}
						}
						ImGui::Text("Leaf faces: %d", uniqueFaces.size());
					}
				}
				else if (app->pickInfo.getFaceIndex() != -1) {
					BSPMODEL& model = map->models[modelIndex];
					BSPFACE& face = *app->pickInfo.getFace();
					BSPPLANE& plane = map->planes[face.iPlane];

					ImGui::Text("Model ID: %d", modelIndex);
					ImGui::Text("Model polies: %d", model.nFaces);

					ImGui::Text("Face ID: %d", app->pickInfo.getFaceIndex());

					vec3 faceNormal = plane.vNormal * (face.nPlaneSide ? -1 : 1);

					if (face.iTextureInfo < map->texinfoCount) {
						BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
						ImGui::Text("Face S Axis: %.2f %.2f %.2f", info.vS.x, info.vS.y, info.vS.z);
						ImGui::Text("Face T Axis: %.2f %.2f %.2f", info.vT.x, info.vT.y, info.vT.z);
					}
					ImGui::Text("Face Normal: %.2f %.2f %.2f", faceNormal.x, faceNormal.y, faceNormal.z);

					ImGui::Text("Plane ID: %d", face.iPlane);

					if (face.iTextureInfo < map->texinfoCount) {
						BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
						BSPMIPTEX* tex = map->get_texture(info.iMiptex);
						ImGui::Text("Texinfo ID: %d", face.iTextureInfo);
						ImGui::Text("Texture ID: %d", info.iMiptex);
						if (tex) {
							ImGui::Text("Texture: %s (%dx%d)", tex->szName, tex->nWidth, tex->nHeight);
						}
						else {
							ImGui::Text("Texture: INVALID OFFSET");
						}
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

			if (app->pickInfo.getFace()) {
				BSPFACE& face = *app->pickInfo.getFace();
				if (ImGui::CollapsingHeader(("Face Edges: " + to_string(face.nEdges)).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (int i = 0; i < face.nEdges; i++) {
						int32_t edgeIdx = map->surfedges[face.iFirstEdge + i];
						BSPEDGE& edge = map->edges[abs(edgeIdx)];
						ImGui::Text("Edge %d = [%d, %d] ", edgeIdx, edge.iVertex[0], edge.iVertex[1]);

						app->drawBox(map->verts[edge.iVertex[0]], 8, COLOR4(0, 128, 0, 255));
					}
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
					if (app->pickInfo.getEnt()) {
						localCamera -= app->pickInfo.getEnt()->getOrigin();
					}

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
							else if (g_app->debugLeafNavMesh && i == g_app->debugLeafNavMesh->hull) {
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
						fgdClass = app->mergedFgd ? app->mergedFgd->getFgdClass(cname) : NULL;
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
			ImGui::PushFont(defaultFont, g_settings.fontSize * 1.1f);
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
					ImGui::SetTooltip("%s", (fgdClass->description).c_str());
				}
			}

			if (fgd) {
				ImGui::NextColumn();
				ImGui::PushFont(defaultFont, g_settings.fontSize * 1.1f);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("FGD:");
				if (ImGui::IsItemHovered()) {
					ImGui::PopFont();
					ImGui::SetTooltip("The Game Definition File determines which keyvalues/flags to display.\n\n"
						"This will change automatically if an entity definition isn't found\n"
						"in the FGD you previously selected.\n");
					ImGui::PushFont(defaultFont, g_settings.fontSize * 1.1f);
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
									app->updateSelectionSize();
									app->updateEntConnections();
									app->updateEntDirectionVectors();
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
	if (app->fgds.empty()) {
		ImGui::Text("No FGD loaded.");
		ImGui::Text("Add an FGD in Settings or use the Raw Edit tab instead.");
		return;
	}
	if (!fgd) {
		ImGui::Text("No entity definition found for %s.", ent->getClassname().c_str());
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
			vector<KeyvalueDef*> keys;
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
			tempGroup.keys.push_back(&keyvalue);
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

void Gui::drawKeyvalueEditor_SmartEditTab_GroupKeys(vector<KeyvalueDef*>& keys, float inputWidth, bool isGrouped, int keyOffset) {
	ImGuiContext& g = *GImGui;
	
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
		KeyvalueDef& keyvalue = *keys[i];
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
		string niceName = keyvalue.smartName.length() ? keyvalue.smartName : keyvalue.name;

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
			string tooltip = key;
			if (keyvalue.smartName.length())
				tooltip += " : " + keyvalue.smartName;
			if (keyvalue.description.size()) {
				tooltip += " : " + keyvalue.description;
			}
			//ImGui::SetTooltip((key + "(" + keyvalue.valueType + ") : " + niceName).c_str());
			ImGui::SetTooltip("%s", tooltip.c_str());
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

					if (ImGui::Selectable((choice.name).c_str(), selected)) {
						for (int i = 0; i < g_app->pickInfo.ents.size(); i++) {
							int idx = g_app->pickInfo.ents[i];
							Entity* ent = g_app->pickInfo.getMap()->ents[idx];
							ent->setOrAddKeyvalue(key, choice.svalue);
							app->mapRenderer->refreshEnt(idx);
						}
						
						app->updateEntConnections();
						app->pushEntityUndoState("Edit Keyvalue");
					}
					if (ImGui::IsItemHovered()) {
						string tooltip = choice.svalue + " : " + choice.name;
						if (choice.desc.size()) {
							tooltip += " : " + choice.desc;
						}
						ImGui::SetTooltip("%s", tooltip.c_str());
					}
				}

				ImGui::EndCombo();
			}
		}
		else {
			struct InputChangeCallback {
				static int keyValueChangedCommon(ImGuiInputTextCallbackData* data) {
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

				static int keyValueChangedNumber(ImGuiInputTextCallbackData* data) {
					if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
						if (data->EventChar < 256) {
							if (strchr("-0123456789", (char)data->EventChar))
								return 0;
						}
						return 1;
					}

					return keyValueChangedCommon(data);
				}

				static int keyValueChangedText(ImGuiInputTextCallbackData* data) {
					if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
						if (data->EventChar == '\n' || data->EventChar == '\r')
							return 1;
						return 0;
					}

					return keyValueChangedCommon(data);
				}
			};

			if (keyvalue.iType == FGD_KEY_INTEGER) {
				ImGui::InputText(("##val" + to_string(bufferIdx) + "_" + to_string(app->pickCount)).c_str(), keyValues[bufferIdx], 64,
					ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
					InputChangeCallback::keyValueChangedNumber, &inputData[bufferIdx]);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					app->updateEntConnections();
				}
			}
			else {
				// calc field size for text wrapping
				ImVec2 textSize = ImGui::CalcTextSize(keyValues[bufferIdx]);
				int fieldWidth = inputWidth;
				ImVec2 framePadding = ImGui::GetStyle().FramePadding;
				int lineWidth = fieldWidth - framePadding.x * 2;
				int extraLines = (int)(textSize.x / lineWidth) * 1.2f; // scaled up to account for wasted space during wraps
				int fieldHeight = ImGui::GetFrameHeight() + textSize.y * std::min(extraLines, 8);
				int draggableHeight = fieldHeight - framePadding.y * 2;

				ImGui::InputTextMultiline(("##val" + to_string(bufferIdx) + "_" + to_string(app->pickCount)).c_str(),
					keyValues[bufferIdx], MAX_VAL_LEN, ImVec2(fieldWidth, fieldHeight),
					ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
					InputChangeCallback::keyValueChangedText, &inputData[bufferIdx]);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					app->updateEntConnections();
				}
			}
		}
		if (ImGui::IsItemHovered() && ImGui::GetItemRectSize().x < ImGui::CalcTextSize(keyValues[bufferIdx]).x) {
			tooltip(g, keyValues[bufferIdx], 0);
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
	if (app->fgds.empty()) {
		ImGui::Text("No FGD loaded.");
		ImGui::Text("Add an FGD in Settings or use the Raw Edit tab instead.");
		return;
	}
	if (!fgd) {
		Entity* ent = g_app->pickInfo.getEnt();
		ImGui::Text("No entity definition found for %s.", ent->getClassname().c_str());
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
		string desc;
		if (fgdClass != NULL) {
			name = fgdClass->spawnFlagNames[i];
			desc = fgdClass->spawnFlagDescs[i];
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
		if (ImGui::IsItemHovered()) {
			if (flagsDiffer) {
				ImGui::PopStyleColor(1);
				ImGui::SetTooltip("This flag is not enabled on all selected entities");
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
			}
			else {
				string tip = to_string(1U << i) + " : " + name;
				if (desc.length()) {
					tip += " : " + desc;
				}
				ImGui::SetTooltip("%s", tip.c_str());
			}
		}

		if (colorChanged) {
			ImGui::PopStyleColor(5);
		}
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab() {
	ImGuiContext& g = *GImGui;
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, "keyvalcols", false);

	float butColWidth = defaultFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
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
			if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
				if (data->EventChar == '\n' || data->EventChar == '\r')
					return 1; // block character
				return 0;
			}

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
			if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
				if (data->EventChar == '\n' || data->EventChar == '\r')
					return 1; // block character
				return 0;
			}

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
	static int dragPointsY[MAX_KEYS_PER_ENT];

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

		// calc field size for text wrapping
		ImVec2 textSize = ImGui::CalcTextSize(keyValues[i]);
		int wrapPad = ImGui::CalcTextSize(" X ").x + 20;
		int fieldWidth = inputWidth - wrapPad;
		ImVec2 framePadding = ImGui::GetStyle().FramePadding;
		int lineWidth = fieldWidth - framePadding.x * 2;
		int extraLines = (int)(textSize.x / lineWidth) * 1.2f; // scaled up to account for wasted space during wraps
		int fieldHeight = ImGui::GetFrameHeight() + textSize.y * std::min(extraLines, 8);
		int draggableHeight = fieldHeight - framePadding.y * 2;

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
			dragPointsY[i] = ImGui::GetItemRectMin().y - startY;

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				Entity* ent = app->pickInfo.getEnt();
				int deltaY = (ImGui::GetMousePos().y - startY) - textSize.y;

				int bestDist = INT32_MAX;
				int bestDropIdx = -1;
				for (int k = 0; k < combinedKeys.size() && i < MAX_KEYS_PER_ENT; k++) {
					int dist = abs(dragPointsY[k] - deltaY);
					if (dist < bestDist && deltaY < dragPointsY[k]) {
						bestDist = dist;
						bestDropIdx = k;
					}
				}

				int n_next = bestDropIdx;
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

			strncpy(keyNames[i], key.c_str(), MAX_KEY_LEN);
			keyNames[i][MAX_KEY_LEN - 1] = 0;

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

			ImGui::InputTextMultiline(("##key" + to_string(i) + "_" + to_string(app->pickCount)).c_str(),
				keyNames[i], MAX_KEY_LEN, ImVec2(fieldWidth, fieldHeight),
				ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyNameChanged, &keyIds[i]);
			
			if (ImGui::IsItemHovered()) {
				if (invalidKey) {
					ImGui::SetTooltip("Key already exists");
				}
				else if (!sharedKey) {
					ImGui::SetTooltip("This key does not exist in all selected entities");
				}
				else if (ImGui::GetItemRectSize().x - 50 < ImGui::CalcTextSize(keyNames[i]).x) {
					tooltip(g, keyNames[i], 0);
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
			strncpy(keyValues[i], value.c_str(), MAX_VAL_LEN);
			keyValues[i][MAX_VAL_LEN - 1] = 0;

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

			ImGui::InputTextMultiline(("##val" + to_string(i) + to_string(app->pickCount)).c_str(), 
				keyValues[i], MAX_VAL_LEN, ImVec2(fieldWidth, fieldHeight),
				ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways,
				TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (ImGui::IsItemHovered()) {
				if (!matchingValues) {
					ImGui::SetTooltip("This value differs between the selected entities");
				}
				else if (ImGui::GetItemRectSize().x - 50 < textSize.x) {
					tooltip(g, keyValues[i], 0);
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
			ent->setOrAddKeyvalue(keyName, "");
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
			int plural = app->pickInfo.ents.size() > 1;
			app->pushEntityUndoState(plural ? "Transform Entities" : "Transform Entity");

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

				sx = sy = sz = 1;
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

			for (int i = 0; i < map->ents.size(); i++) {
				Entity* ent = map->ents[i];
				if (ent->getBspModelIdx() != -1)
					bspRenderer->refreshEnt(i);
			}
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
				vec3 moveAmount = map->ents[0]->getOrigin();
				map->ents[0]->removeKeyvalue("origin");
				LumpReplaceCommand* command = new LumpReplaceCommand("Apply Worldspawn Transform");
				
				map->move(moveAmount);
				map->zero_entity_origins("func_ladder");
				map->zero_entity_origins("func_water"); // water is sometimes invisible after moving in sven
				map->zero_entity_origins("func_mortar_field"); // mortars don't appear in sven

				command->pushUndoState();
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
	
	static bool loggedAlready = true;
	if (!loggedAlready) {
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
	byte* consoleFontData = NULL;
	int notosans_mono_sz = 0;
	if (lzmaDecompress((uint8_t*)notosans_mono, sizeof(notosans_mono), decompressed)) {
		notosans_mono_sz = decompressed.size();
		consoleFontData = new byte[notosans_mono_sz];
		memcpy(consoleFontData, &decompressed[0], notosans_mono_sz);
	}
	else {
		logf("Failed to decompress font! Crash imminent.\n");
	}

	decompressed.clear();
	byte* smallFontData = NULL;

	int notosans_unicode_sz = 0;
	if (lzmaDecompress((uint8_t*)notosans_unicode, sizeof(notosans_unicode), decompressed)) {
		notosans_unicode_sz = decompressed.size();
		smallFontData = new byte[notosans_unicode_sz];
		memcpy(smallFontData, &decompressed[0], notosans_unicode_sz);
	}
	else {
		logf("Failed to decompress font! Crash imminent.\n");
	}

	//defaultFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, notosans_unicode_sz, g_settings.fontSize, NULL, ranges.Data);
	//consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, notosans_mono_sz, g_settings.fontSize, NULL, ranges.Data);
	
	defaultFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, notosans_unicode_sz, g_settings.fontSize);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, notosans_mono_sz, g_settings.fontSize);

	updateFontScale();
}

void Gui::updateFontScale() {
	// 1.1 to keep consistent with previous version size
	ImGui::GetStyle().FontScaleMain = (g_settings.fontSize / 22.0f) * 1.1f;
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

		ImGui::Separator();

		if (ImGui::MenuItem("Auto-scroll", NULL, &AutoScroll)) {
			toggledAutoScroll = true;
		}
		if (ImGui::MenuItem("Verbose Logging", "", g_verbose)) {
			g_verbose = !g_verbose;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont, g_settings.fontSize * 0.9f);
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

	ImGui::SetNextWindowPos(ImVec2(5, 50), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(790, 460), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(740, 200), ImVec2(FLT_MAX, app->windowHeight - 40));

	if (ImGui::Begin("Editor Setup", &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 4;
		static const char* tab_titles[settings_tabs] = {
			"General",
			"Rendering",
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
		bool hasFooter = settingsTab >= 2;
		ImGui::BeginGroup();
		int footerHeight = hasFooter ? ImGui::GetFrameHeightWithSpacing() + 4 : 0;
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
			if (ImGui::DragInt("Font Size", &g_settings.fontSize, 0.1f, 8, 48, "%d pixels")) {
				updateFontScale();
			}
			ImGui::DragInt("Undo Levels", &app->undoLevels, 0.05f, 0, 64);

			ImGui::Columns(2);
			ImGui::Checkbox("Verbose Logging", &g_verbose);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("For troubleshooting the program");
			}
			ImGui::NextColumn();

			ImGui::Checkbox("Confirm Close", &g_settings.confirm_exit);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Show a warning dialog if closing the map without saving changes.\n");
			}
			ImGui::NextColumn();

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("The unicode font may take a long time to load depending on your specs.\nA new version of ImGui is coming soon to improve that.\n");
			}
		}
		else if (settingsTab == 1) {
			static const char* renderers[RENDERER_COUNT] = {
				"OpenGL",
				"OpenGL (Legacy)",
			};

			if (ImGui::BeginCombo("Renderer", renderers[g_settings.renderer]))
			{
				if (ImGui::Selectable(renderers[0], g_settings.renderer == RENDERER_OPENGL_21)) {
					g_settings.renderer = RENDERER_OPENGL_21;
					app->deselectObject();
					g_app->compileShaders();
					g_app->mapRenderer->reload();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("This renderer asks your OpenGL driver what it supports and conditionally enables faster rendering features.\n\nFor hardware supporting OpenGL 2.1 and up.\n");
				}

				if (ImGui::Selectable(renderers[1], g_settings.renderer == RENDERER_OPENGL_21_LEGACY)) {
					g_settings.renderer = RENDERER_OPENGL_21_LEGACY;
					app->deselectObject();
					g_app->compileShaders();
					g_app->mapRenderer->reload();
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("This renderer forces use of the most compatible/slowest rendering methods. Sometimes graphics drivers lie about which features they support.\n\nChoose this if textures/objects are black or completely missing.\n");
				}
				ImGui::EndCombo();
			}

			ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
			ImGui::DragFloat("Back Clipping Plane", &app->zFar, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::DragFloat("Model Render Distance", &app->zFarMdl, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);

			ImGui::Columns(2);
			ImGui::Checkbox("Animate Models", &g_settings.animate_models);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Animations have a big impact on FPS with the legacy renderer.\n");
			}

			ImGui::NextColumn();

			if (ImGui::Checkbox("VSync", &vsync)) {
				glfwSwapInterval(vsync ? 1 : 0);
			}
			ImGui::Checkbox("wpoly", &g_settings.show_wpoly);
			tooltip(g, "Display the number of polygons that would be rendered in-game. Enabling this reduces FPS when wpoly counts are high.");

			ImGui::NextColumn();

			/*
						ImGui::Checkbox("Texture Filtering", &g_settings.texture_filtering);
						if (ImGui::IsItemHovered()) {
							ImGui::SetTooltip("Smooths far-away textures.\n");
						}

						ImGui::NextColumn();
			*/
		}
		else if (settingsTab == 2) {
			if (ImGui::InputText("##GameDir", gamedir, 256, ImGuiInputTextFlags_ElideLeft)) {
				g_settings.gamedir = string(gamedir);
			}

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
					vector<string> paths = getAssetPaths(tmpResPaths[i]);
					string pathStr;
					for (string& path : paths) {
						pathStr += "\n" + path;
					}

					ImGui::SetTooltip(("This asset path adds the following search paths:\n" + pathStr).c_str());
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
					"You can use paths relative to your Game Directory or absolute paths.\nSuffixed paths are searched automatically (_addon and _downloads)");
			}
		}
		else if (settingsTab == 3) {
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
					"You can use paths relative to your Asset Paths or absolute paths.");
			}
		}

		ImGui::EndChild();
		ImGui::EndChild();

		if (hasFooter) {
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
				app->studioModelPaths.clear();
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

			if (ImGui::BeginTabItem("Optimizing BSP data")) {
				ImGui::Dummy(ImVec2(0, 10));

				ImGuiIO& io = ImGui::GetIO();
				ImGui::TextWrapped("Optimizing BSP data is essential for merging and porting maps. "
					"The Optimize Tool tries to reduce every data type, but you may need to make manual edits "
					"if it doesn't remove enough. Below are tips on how to reduce the most problematic data types.\n\n");

				ImGui::BulletText("AllocBlock\n");
				ImGui::Indent();
				ImGui::BulletText("Downscale textures\n");
				ImGui::BulletText("Scale up textures\n");
				ImGui::Unindent();
				ImGui::BulletText("Clipnodes\n");
				ImGui::Indent();
				ImGui::BulletText("Redirect Hull 2 --> Hull 1\n");
				ImGui::Indent();
				ImGui::BulletText("You will need to address problems with large monster/pushables\n");
				ImGui::BulletText("Selectively simplify hulls per model (right click solid entities)\n");
				ImGui::Unindent();
				ImGui::Unindent();
				ImGui::BulletText("Models\n");
				ImGui::Indent();
				ImGui::BulletText("Deduplicate Models Tool\n");
				ImGui::BulletText("Merge BSP Models (select 2 solid entities)\n");
				ImGui::Unindent();
				ImGui::BulletText("Lightstyles\n");
				ImGui::Indent();
				ImGui::BulletText("Delete light entities which don't need to be toggled.\n");
				ImGui::Unindent();

				ImGui::TextWrapped("\nIn most cases you need to run the Clean command to remove data "
					"after a manual edit. The Map Limits widget has tabs for finding which "
					"entities/faces are contributing the most toward map limits.");

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
		ImGui::InputText("Version", (char*)g_version_string, strlen(g_version_string)+1, ImGuiInputTextFlags_ReadOnly);

		static char* author = "w00tguy";
		ImGui::InputText("Author", author, strlen(author)+1, ImGuiInputTextFlags_ReadOnly);

		static char* url = "https://github.com/wootguy/bspguy";
		ImGui::InputText("Contact", url, strlen(url)+1, ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::End();
}

void Gui::drawWelcomePopup() {
	ImGui::SetNextWindowSize(ImVec2(600, 250), ImGuiCond_FirstUseEver);
	
	ImGui::OpenPopup("Welcome to bspguy!!!");

	if (ImGui::BeginPopupModal("Welcome to bspguy!!!", NULL))
	{
		ImGui::TextWrapped(
			"This editor requires some setup for maps to display properly.\n\n"
			
			"Go to Settings and configure your game directory, asset paths, and at least one FGD. "
			"If you don't do this, you will be seeing a lot of pink cubes and missing textures."
		);

		ImGui::Dummy(ImVec2(0, 10));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, 10));

		if (ImGui::Button("OK", ImVec2(140, 0))) {
			g_settings.first_load = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Open Settings", ImVec2(140, 0))) {
			g_settings.first_load = false;
			showSettingsWidget = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}
}

void Gui::drawLimitsSummary(Bsp* map, bool modalMode) {
	if (!loadedStats) {
		int worldleafCount = map->modelCount > 0 ? map->models[0].nVisLeafs : 0;

		stats.clear();
		stats.push_back(calcStat("AllocBlock", map->calc_allocblock_usage(), g_limits.max_allocblocks, false));
		stats.push_back(calcStat("clipnodes", map->clipnodeCount, g_limits.max_clipnodes, false));
		stats.push_back(calcStat("nodes", map->nodeCount, g_limits.max_nodes, false));
		stats.push_back(calcStat("leaves", map->leafCount, g_limits.max_leaves, false));
		stats.push_back(calcStat("worldleaves", worldleafCount, g_limits.max_worldleaves, false));
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
	ImGui::PushFont(consoleFont, g_settings.fontSize);

	int fontSize = g_settings.fontSize;
	int midWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
	int nameWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "marksurfaces").x;

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

				if (ImGui::BeginTabItem("Extents")) {
					loadedStats = false;
					drawFaceExtentsLimitTab();
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
	ImGui::PushFont(consoleFont, g_settings.fontSize);

	int fontSize = g_settings.fontSize;
	int valWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Model ").x;
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
			BSPFACE& f = map->faces[i];
			BSPTEXTUREINFO& tinfo = map->texinfos[f.iTextureInfo];
			if (tinfo.nFlags & TEX_SPECIAL)
				continue; // does not use lightmaps

			int size[2];
			GetFaceLightmapSize(map, i, size);

			BSPMIPTEX* tex = map->get_texture(tinfo.iMiptex);

			string texname = tex ? tex->szName : "INVALID OFFSET";
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
	ImGui::PushFont(consoleFont, g_settings.fontSize);

	int fontSize = g_settings.fontSize;
	int valWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	int usageWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, "  Usage   ").x;
	int modelWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Model ").x;
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

			g_app->mapRenderer->highlightPickedFaces(false);
			app->deselectFaces();
			app->pickInfo.selectFace(faceIdx);
			g_app->mapRenderer->highlightPickedFaces(true);
			app->updateTextureAxes();
			app->pickMode = PICK_FACE;
			showFaceEditorWidget = true;
			app->pickCount++;
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

void Gui::drawFaceExtentsLimitTab() {
	if (app->isLoading)
		return; // counting subdivisions messes with lumps so don't do that while loading
	
	Bsp* map = app->mapRenderer->map;

	int maxCount;
	const int allocBlockSize = 128 * 128;

	if (!loadedLimit[SORT_EXTENTS]) {
		limitExtents.clear();

		unordered_set<int> bad_extent_mips;
		for (int fa = 0; fa < map->faceCount; fa++) {
			BSPFACE& face = map->faces[fa];
			BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

			if (info.nFlags & TEX_SPECIAL)
				continue;

			int size[2];
			if (GetFaceLightmapSize(map, fa, size)) {
				continue;
			}

			bad_extent_mips.insert(info.iMiptex);
		}

		for (int mip : bad_extent_mips) {
			BSPMIPTEX* tex = map->get_texture(mip);
			if (!tex) {
				continue;
			}

			ExtentInfo info;
			info.texname = tex->szName;
			info.faceCount = to_string(map->count_faces_for_mip(mip));
			info.dimensions = to_string(tex->nWidth) + "x" + to_string(tex->nHeight);
			info.sort = map->get_subdivisions_needed_to_fix_mip_extents(mip);
			info.subsNeeded = to_string(info.sort);
			info.mip = mip;
			limitExtents.push_back(info);
		}
		sort(limitExtents.begin(), limitExtents.end(), [](const ExtentInfo& a, const ExtentInfo& b) {
			return a.sort > b.sort;
		});

		loadedLimit[SORT_EXTENTS] = true;
	}
	vector<ExtentInfo>& allocInfos = limitExtents;

	ImGui::BeginChild("content");
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFont, g_settings.fontSize);

	int fontSize = g_settings.fontSize;
	int facesWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Faces ").x;
	int subsWidth = consoleFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " Subs Needed ").x;
	int bigWidth = ImGui::GetWindowWidth() - (facesWidth + subsWidth);
	ImGui::Columns(3);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, facesWidth);
	ImGui::SetColumnWidth(2, subsWidth);

	ImGui::Text("Texture"); ImGui::NextColumn();
	ImGui::Text("Faces"); ImGui::NextColumn();
	ImGui::Text("Subs Needed"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
	ImGui::Columns(3);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, facesWidth);
	ImGui::SetColumnWidth(2, subsWidth);

	int selected = app->pickInfo.getFaceIndex();

	for (int i = 0; i < limitExtents.size(); i++) {
		string texname = limitExtents[i].texname + "##" + "select" + to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(texname.c_str(), false, flags)) {
			selected = i;

			app->deselectFaces();

			for (int k = 0; k < map->faceCount; k++) {
				BSPFACE& face = map->faces[k];
				BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

				if (info.iMiptex == limitExtents[i].mip) {
					app->pickInfo.selectFace(k);
				}
			}
			app->mapRenderer->highlightPickedFaces(true);

			app->updateTextureAxes();
			app->pickMode = PICK_FACE;
			showFaceEditorWidget = true;
			app->pickCount++;
		}

		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitExtents[i].faceCount.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitExtents[i].faceCount.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(limitExtents[i].subsNeeded.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(limitExtents[i].subsNeeded.c_str()); ImGui::NextColumn();
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
		float scrollY;
		string cname;
	};

	static vector<ReportEnt> filteredEnts;

	Bsp* map = app->mapRenderer->map;
	string plural = filteredEnts.size() == 1 ? "" : "s";
	string title = "Entity Report  (" + to_string(filteredEnts.size()) + " result" + plural + ")";

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
			static int lastKeyboardNavSelect = 0;
			static string classFilter = "(none)";
			static bool partialMatches = true;
			static bool invertMatch = false;

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

					if (invertMatch) {
						visible = !visible;
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
						lastKeyboardNavSelect = i;

						if (app->pickMode == PICK_FACE) {
							g_app->mapRenderer->highlightPickedFaces(false);
							app->pickInfo.deselect();
							app->pickMode = PICK_OBJECT;
						}

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

			entityReportReselectNeeded = false;

			if (filteredEnts.size() && ImGui::IsWindowHovered()) {
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
					lastKeyboardNavSelect = clamp(lastKeyboardNavSelect + 1, 0, filteredEnts.size() - 1);
					app->deselectObject();
					app->pickInfo.selectEnt(filteredEnts[lastKeyboardNavSelect].idx);
					app->postSelectEnt();
					entityReportReselectNeeded = true;
					ImGui::SetScrollY(clipper.ItemsHeight * lastKeyboardNavSelect - clipper.ItemsHeight*2);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
					lastKeyboardNavSelect = clamp(lastKeyboardNavSelect - 1, 0, filteredEnts.size() - 1);
					app->deselectObject();
					app->pickInfo.selectEnt(filteredEnts[lastKeyboardNavSelect].idx);
					app->postSelectEnt();
					entityReportReselectNeeded = true;
					ImGui::SetScrollY(clipper.ItemsHeight * lastKeyboardNavSelect - clipper.ItemsHeight * 2);
				}
			}

			if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
				app->deselectObject();
				for (int i = 0; i < filteredEnts.size(); i++) {
					app->pickInfo.selectEnt(filteredEnts[i].idx);
				}
				app->postSelectEnt();
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
			int fontSize = g_settings.fontSize;
			inputWidth -= defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " = ").x;

			for (int i = 0; i < MAX_FILTERS; i++) {
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Key" + to_string(i)).c_str(), keyFilter[i], 64)) {
					entityReportFilterNeeded = true;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Filter entities by key name. Leave blank to include all key names.");
				}

				ImGui::SameLine();
				ImGui::Text(" = "); ImGui::SameLine();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Value" + to_string(i)).c_str(), valueFilter[i], 64)) {
					entityReportFilterNeeded = true;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Filter entities by key value. Leave blank to include all values.");
				}
			}

			if (ImGui::Checkbox("Partial Matching", &partialMatches)) {
				entityReportFilterNeeded = true;
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Do not force entity keys/values to match your input exactly.");
			}

			ImGui::SameLine();
			ImGui::Dummy(ImVec2(10, 0));
			ImGui::SameLine();
			
			if (ImGui::Checkbox("Invert", &invertMatch)) {
				entityReportFilterNeeded = true;
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Invert search filter.");
			}

			ImGui::EndChild();

			ImGui::EndGroup();
		}
	}

	ImGui::End();
}

void Gui::drawDebugText() {
	ImDrawList* imdl = ImGui::GetBackgroundDrawList();

	for (Text2D& text : texts) {
		ImU32 color = IM_COL32(text.color.r, text.color.g, text.color.b, text.color.a);
		ImVec2 pos = ImVec2(text.x, text.y);

		if (text.align != TEXT2D_ALIGN_LEFT) {
			ImVec2 textSz = ImGui::CalcTextSize(text.text.c_str());
			
			if (text.align == TEXT2D_ALIGN_CENTER) {
				pos = ImVec2(text.x - textSz.x * 0.5f, text.y);
			}
			else if (text.align == TEXT2D_ALIGN_RIGHT) {
				pos = ImVec2(text.x - textSz.x, text.y);
			}
		}

		imdl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), text.text.c_str()); // shadow
		imdl->AddText(pos, color, text.text.c_str());
	}

	texts.clear();
}

void Gui::drawLightmapsEditor() {
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
	
	if (lightmapEditorNeedsUpdate) {
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

		lightmapEditorNeedsUpdate = false;
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
				tooltip(g, cstrf("Copy lightmap layer", i));

				if (ImGui::MenuItem("Paste", "", false, copiedLightmapFace >= 0)) {
					pasteLightmap(faceIdx, i);
				}
				tooltip(g, "Paste lightmap layer. If the target lightmap dimensions don't match "
					"what you copied, the lightmap will be scaled to fit using bilinear filtering.");

				if (ImGui::MenuItem("Bake")) {
					LightmapsEditCommand* command = new LightmapsEditCommand("Bake Lightmap");

					map->bake_lightmap_style(face.nStyles[i], false, false, faceIdx);

					command->pushUndoState();
				}
				tooltip(g, "Merge this lightmap with the base lightmap layer");

				if (ImGui::MenuItem("Delete")) {
					LightmapsEditCommand* command = new LightmapsEditCommand("Delete Lightmap");

					map->bake_lightmap_style(face.nStyles[i], true, false, faceIdx);

					command->pushUndoState();
				}
				tooltip(g, "Delete this lightmap layer");

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
				tooltip(g, "Import a 24-bit PNG file to replace this lightmap. If dimensions don't match, "
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
				tooltip(g, "Export this lightmap as a 24-bit PNG file");

				ImGui::Separator();

				if (ImGui::BeginMenu(cstrf("Style %d", face.nStyles[i]))) {
					if (ImGui::MenuItem("Bake", "", false, face.nStyles[i] != 0)) {
						LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Bake Light Style %d", face.nStyles[i]));

						int numBakes = map->bake_lightmap_style(face.nStyles[i], false, true);
						logf("Baked %d lightmaps\n", numBakes);

						command->pushUndoState();
					}
					tooltip(g, cstrf("Merge all lightmaps for style %d into the base lightmap layer. "
						"This will reduce lightstyles and disable animations/toggling for all lights "
						"linked to this style.", face.nStyles[i]));

					if (ImGui::MenuItem("Delete", "", false, face.nStyles[i] != 0)) {
						LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Delete Light Style %d", face.nStyles[i]));

						int numBakes = map->bake_lightmap_style(face.nStyles[i], true, true);
						logf("Baked %d lightmaps\n", numBakes);

						command->pushUndoState();
					}
					tooltip(g, cstrf("Delete all lightmaps linked to style %d. This reduces light style count.", face.nStyles[i]));

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
					tooltip(g, cstrf("Select all faces that use light style %d", face.nStyles[i]));

					ImGui::Separator();

					if (ImGui::MenuItem("Import")) {
						char* fname = tinyfd_openFileDialog("Import Lightmaps", "", 1, pngFilterPatterns, "PNG (*.png)", 1);

						if (fname) {
							LightmapsEditCommand* command = new LightmapsEditCommand(cstrf("Import Light Style %d", face.nStyles[i]));
							map->import_lightmap_style(face.nStyles[i], fname);
							command->pushUndoState();
						}
					}
					tooltip(g, cstrf("Import a PNG file to replace all lightmaps for style %d", face.nStyles[i]));

					if (ImGui::MenuItem("Export")) {
						char* fname = tinyfd_saveFileDialog("Export Lightmaps",
							cstrf("lightmap_style_%d.png", face.nStyles[i]),
							1, pngFilterPatterns, "PNG (*.png)");

						if (fname) {
							map->export_lightmap_style(face.nStyles[i], fname);
						}
					}
					tooltip(g, cstrf("Export all lightmaps for style %d. Use this to bulk edit all faces affected by a light source.", face.nStyles[i]));

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

void Gui::drawFaceEditor() {
	ImGui::SetNextWindowSize(ImVec2(300, 570), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 420), ImVec2(FLT_MAX, app->windowHeight));

	ImGuiContext& g = *GImGui;
	BspRenderer* mapRenderer = app->mapRenderer ? app->mapRenderer : NULL;
	Bsp* map = app->pickInfo.getMap();

	if (ImGui::Begin("Face Editor", &showFaceEditorWidget)) {

		if (mapRenderer == NULL || map == NULL || app->pickMode != PICK_FACE || app->pickInfo.faces.size() == 0)
		{
			ImGui::Text("No face selected");

			static char findtex[MAXTEXTURENAME];
			static char findfaceid[256];
			static char findtexid[256];

			ImGui::Dummy(ImVec2(0, 20));

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
			tooltip(g, "Select all faces that use the given texture");

			ImGui::Dummy(ImVec2(0, 20));

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
			tooltip(g, "Select all faces that use the given texture ID.");

			ImGui::Dummy(ImVec2(0, 20));

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
			tooltip(g, "Select the face with the given ID. Useful for toubleshooting errors in the engine or compilers.");
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
						lightmapEditorNeedsUpdate = true;
					currentTab = 1;
					drawLightmapsEditor();
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
	}

	ImGui::End();
}

void Gui::drawTextureEditor() {
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

	ImGui::SameLine(0, 20);
		
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

	ImGui::Dummy(ImVec2(0, 8));

	ImGui::Text("Texture name");
	ImGui::SetNextItemWidth(inputWidth + 40);
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
		BSPMIPTEX* tex = map->get_texture(copiedMiptex);
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
			loadedLimit[SORT_EXTENTS] = false;
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
					textureName[MAXTEXTURENAME-1] = 0;
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
			textureId = (ImTextureID)mapRenderer->getFaceTextureId(app->pickInfo.faces[0]);
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

	float imgWidth = min(256.0f, inputWidth * 2 - 2);
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
		tooltip(g, "Import a PNG/BMP file to replace this texture");

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
		tooltip(g, "Export this texture as an 8-bit BMP file.");

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
		tooltip(g, "Export this texture as a 32-bit PNG file.");

		ImGui::EndPopup();
	}

	ImGui::Text(("Source: " + texture_src).c_str());
	ImGui::Text(("Size: " + to_string(tex_size_kb) + " KB").c_str());

	ImGuiIO& io = ImGui::GetIO();
	int bestWidth = app->windowWidth * 0.5f;
	ImGui::SetNextWindowSize(ImVec2(bestWidth, bestWidth*0.8f), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(340, 140), ImVec2(FLT_MAX, app->windowHeight - 40));
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

		ImGui::SetNextItemWidth(200);
		if (ImGui::InputScalar("Width", ImGuiDataType_U16, (void*)&resizeWidth, &step)) {
			resizeWidth = min((int)resizeOriginalWidth, max(16, (resizeWidth / 16) * 16));
			reloadPreview = true;
		}
		ImGui::SetNextItemWidth(200);
		if (ImGui::InputScalar("Height", ImGuiDataType_U16, (void*)&resizeHeight, &step)) {
			resizeHeight = min((int)resizeOriginalHeight, max(16, (resizeHeight / 16) * 16));
			reloadPreview = true;
		}
		ImGui::SetNextItemWidth(200);

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

		ImGui::Dummy(ImVec2(0, 5));
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
			ImGui::Image(originalTexture->id, ImVec2(imgWidth, imgWidth* aspect));
		}
		ImGui::NextColumn();
		imgWidth = ImGui::GetContentRegionAvail().x;
		if (previewTexture) {
			float aspect = (float)previewTexture->height / previewTexture->width;
			ImGui::Image(previewTexture->id, ImVec2(imgWidth, imgWidth* aspect));
		}
		ImGui::Columns(1);

		ImGui::Dummy(ImVec2(0, 5));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, 5));

		ImGui::BeginDisabled((resizeWidth == resizeOriginalWidth && resizeHeight == resizeOriginalHeight) || !originalTexture);
		if (ImGui::Button("Resize", ImVec2(120, 0))) {
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
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
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
	lightmapEditorNeedsUpdate = true;
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

void Gui::createSeriesWad() {
	char* fnamestr = tinyfd_openFileDialog("Select Series Maps", "",
		1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

	vector<string> fnames;

	if (fnamestr)
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
				logf("ERROR: %s does not have a worldspawn entity to update.\n", temp->name);
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

void Gui::addText(Text2D text) {
	texts.push_back(text);
}

void Gui::switchToLeafSelectMode(bool selectFaceLeaves, bool strictFaceLeafSelection) {
	vector<int> faces = app->pickInfo.faces;
	Bsp* map = app->mapRenderer->map;

	app->deselectFaces();
	app->deselectObject();
	g_app->mapRenderer->hideLeaves(false);
	g_app->mapRenderer->highlightPickedLeaves(false);
	g_app->mapRenderer->hideFaces(false);
	app->hiddenLeaves.clear();
	app->hiddenFaces.clear();

	if (selectFaceLeaves && faces.size()) {
		for (int i = 0; i < map->models[0].nVisLeafs; i++)
			app->hiddenLeaves.insert(i);

		unordered_set<int> selectedFaces;
		for (int idx : faces) {
			selectedFaces.insert(idx);
		}

		for (int i = 1; i < map->models[0].nVisLeafs; i++) {
			BSPLEAF& leaf = map->leaves[i];

			if (!leaf.nMarkSurfaces)
				continue;

			bool anySelected = false;
			bool allSelected = true;
			for (int k = 0; k < leaf.nMarkSurfaces; k++) {
				if (selectedFaces.count(map->marksurfs[leaf.iFirstMarkSurface + k])) {
					anySelected = true;
				}
				else {
					allSelected = false;
				}
			}

			if (allSelected || (!strictFaceLeafSelection && anySelected)) {
				app->pickInfo.selectLeaf(i);
				app->hiddenLeaves.erase(i);
			}
		}
	}

	app->pickInfo.selectLeafFaces();
	g_app->mapRenderer->hideLeaves(true);
	g_app->mapRenderer->highlightPickedLeaves(true);
	g_app->mapRenderer->highlightPickedFaces(true);
	g_app->updateTextureAxes();

	app->pickMode = PICK_LEAF;
	showFaceEditorWidget = false;
	app->mapRenderer->delayLoadLeaves();
}

void Gui::selectLeafPvs() {
	if (app->pickInfo.leaves.empty())
		return;

	vector<int> pickLeaves = app->pickInfo.leaves;
	app->mapRenderer->highlightPickedFaces(false);
	app->mapRenderer->highlightPickedLeaves(false);
	app->pickInfo.deselect();

	for (int i = 0; i < pickLeaves.size(); i++) {
		vector<int> pvs = g_app->mapRenderer->map->get_pvs(pickLeaves[i]);
		for (int k = 0; k < pvs.size(); k++) {
			app->pickInfo.selectLeaf(pvs[k]);
		}
	}

	app->pickInfo.selectLeafFaces();
	app->mapRenderer->highlightPickedFaces(true);
	app->mapRenderer->highlightPickedLeaves(true);
	app->updateTextureAxes();
}