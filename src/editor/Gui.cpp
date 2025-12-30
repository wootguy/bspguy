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
#include "Widget.h"
#include "LimitsWidget.h"

// embedded binary data
#include "fonts/notosans_mono.h"
#include "fonts/notosans_unicode.h"
#include "icons/object.h"
#include "icons/face.h"
#include "icons/leaf.h"

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

// absolute font scale which every other font is relatively scaled on
int g_font_scale_base = 22;

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

void tooltip(const char* text, float hoverDelay=g_tooltip_delay) {

	if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > hoverDelay) {
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

	// init widgets
	memset(widgets, 0, sizeof(widgets));
	widgets[WIDGET_DEBUG] = new DebugWidget(this, "Debug info", ImVec2(100, 100), ImVec2(100, 100),
		ImGuiWindowFlags_AlwaysAutoResize);
	widgets[WIDGET_DEBUG]->allowInMapArrangeMode = true;

	widgets[WIDGET_KEYVALUE_EDITOR] = new KeyvalueEditor(this, "Keyvalue Editor",
		ImVec2(610, 610), ImVec2(470, 250), 0);

	widgets[WIDGET_TRANSFORM] = new TransformWidget(this, "Transformation",
		ImVec2(430, 380), ImVec2(340, 140), 0);
	widgets[WIDGET_TRANSFORM]->allowInMapArrangeMode = true;

	widgets[WIDGET_MESSAGES] = new LogWidget(this, "Messages", ImVec2(750, 300), ImVec2(200, 100), 0);
	widgets[WIDGET_TRANSFORM]->allowInMapArrangeMode = true;

	widgets[WIDGET_SETTINGS] = new SettingsWidget(this, "Editor Setup", ImVec2(790, 460), ImVec2(550, 270), 0);

	widgets[WIDGET_HELP] = new HelpWidget(this, "Help", ImVec2(620, 400), ImVec2(620, 300), 0);
	widgets[WIDGET_HELP]->allowInMapArrangeMode = true;

	widgets[WIDGET_ABOUT] = new AboutWidget(this, "About", ImVec2(500, 140), ImVec2(250, 140), 0);
	widgets[WIDGET_ABOUT]->allowInMapArrangeMode = true;

	widgets[WIDGET_LIMITS] = new LimitsWidget(this, "Map Limits###limits",
		ImVec2(550, 630), ImVec2(450, 200), 0);

	widgets[WIDGET_ENT_REPORT] = new EntityReport(this, "###entreport",
		ImVec2(400, 600), ImVec2(300, 350), 0);

	widgets[WIDGET_FACE_EDITOR] = new FaceEditor(this, "Face Editor",
		ImVec2(300, 570), ImVec2(260, 510), ImGuiWindowFlags_NoScrollbar);

	widgets[WIDGET_RAD_PREP] = new RadWidget(this, "Configure Texlights",
		ImVec2(350, 300), ImVec2(350, 300), 0);

	widgets[WIDGET_DEDUP_MODELS] = new DedupModelsWidget(this, "Deduplicate Models",
		ImVec2(400, 0), ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize);

	widgets[WIDGET_MERGE_OVERLAP] = new MergeOverlapWidget(this, "Merge Overlap",
		ImVec2(0, 0), ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize);

	widgets[WIDGET_MERGE_FAILED] = new MergeFailedWidget(this, "Merge Failed",
		ImVec2(0, 0), ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
	
	widgets[WIDGET_MERGE_MULTI] = new MergeMultipleWidget(this, "Merge Multiple",
		ImVec2(600, 0), ImVec2(500, 300), ImGuiWindowFlags_AlwaysAutoResize);

	widgets[WIDGET_FIX_EXTENTS] = new FixExtentsWidget(this, "Fix Bad Surface Extents",
		ImVec2(600, 0), ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize);

	widgets[WIDGET_MODEL_MERGE_CONFIRM] = new ModelMergeWidget(this, "Confirm Merge",
		ImVec2(400, 0), ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize);
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

	for (Widget* widget : widgets) {
		if (!widget->widgetVisible)
			continue;
		if (widget->isPopup)
			continue; // drawn in 2nd pass later
		if (g_app->mapArrangeMode && !widget->allowInMapArrangeMode)
			continue;

		ImGui::SetNextWindowSize(ImVec2(widget->widgetSizeDefault.x * uiScale, widget->widgetSizeDefault.y * uiScale), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(widget->widgetSizeMin.x * uiScale, widget->widgetSizeMin.y * uiScale), ImVec2(FLT_MAX, app->windowHeight));
		
		widget->uiScale = uiScale;
		widget->map = g_app->mapRenderer->map;
		widget->setup();
		if (ImGui::Begin(widget->widgetName, &widget->widgetVisible, widget->widgetFlags)) {
			widget->draw();
		}
		ImGui::End();
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

	if (shouldReloadFonts) {
		shouldReloadFonts = false;
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		loadFonts();
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
	((FaceEditor*)widgets[WIDGET_FACE_EDITOR])->refreshAfterFacePaste = true;
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
			tooltip("Unhides entities you previously marked as hidden.");

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
				tooltip("Select every face in the map.");

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
				tooltip("Select every visible face in the map (excludes hidden models).");

				ImGui::Separator();

				if (ImGui::MenuItem("Unhide All", 0, false, app->hiddenFaces.size())) {
					app->unhideFaces();
				}
				tooltip("Unhides faces you previously marked as hidden.");
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
					tooltip("Recursively select faces connected by vertices.");

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
					tooltip("Recursively select faces connected by vertices which share the selected textures.");

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
					tooltip("Selects faces connected to this one which lie on the same plane and use the same texture");

					ImGui::Separator();

					if (ImGui::MenuItem("Leaves", "", false, app->pickInfo.faces.size())) {
						switchToLeafSelectMode(true, false);
					}
					tooltip("Select all leaves which mark the selected faces.");

					if (ImGui::MenuItem("Leaves (strict)", "", false, app->pickInfo.faces.size())) {
						switchToLeafSelectMode(true, true);
					}
					tooltip("Select leaves which mark only the selected faces, and nothing "
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
					tooltip("Select every face in the map which has this texture.");

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
					tooltip("Select faces with bad surface extents that use this texture.");

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
				tooltip("Split selected faces across the axis with the most texture pixels.");

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
				tooltip("Subdivide selected faces until they have valid surface extents.");

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
				tooltip("Scale selected faces until they have valid surface extents.");

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
				tooltip("Select all leaves that the program failed to generate a mesh for.");

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
				tooltip("Select all leaves with SKY contents.");

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
				tooltip("Merge leaves that are unreachable by the player and contain no faces.");
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
				tooltip("Select all world leaves in the map (excluding the shared solid leaf 0).");

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
				tooltip("Select all world leaves that haven't been marked as hidden.");

				ImGui::Separator();

				if (ImGui::MenuItem("Unhide All", 0, false, app->hiddenLeaves.size())) {
					app->unhideLeaves();
				}
				tooltip("Unhides leaves you previously marked as hidden.");
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
				tooltip("Recursively select all leaves that are touching the selected leaf(s). Hidden leaves are not connected through.");

				if (ImGui::MenuItem("Select PVS", "P", false, app->pickInfo.leaves.size() >= 1)) {
					selectLeafPvs();
				}
				tooltip("Select all leaves in the potentially visible set (PVS) of the selected leaf(s).");

				ImGui::Separator();

				if (ImGui::MenuItem("Copy as PVS", "", false, app->pickInfo.leaves.size() >= 1)) {
					app->pvsCopyLeaves = app->pickInfo.leaves;
				}
				tooltip("Copy the selected leaves as a potentially visible set (PVS).");

				if (ImGui::BeginMenu("Apply PVS", app->pickInfo.leaves.size() >= 1 && app->pvsCopyLeaves.size())) {
					if (ImGui::MenuItem("Add", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, 1);
					}
					tooltip("Add copied leaves to the target leaf PVS.");

					if (ImGui::MenuItem("Replace", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, 0);
					}
					tooltip("Replace target leaf PVS with the copied leaves.");
					
					if (ImGui::MenuItem("Subtract", "", false)) {
						map->apply_pvs(app->pickInfo.leaves, app->pvsCopyLeaves, -1);
					}
					tooltip("Remove copied leaves from the target leaf PVS.");

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
				tooltip("Merges the selected leaves and converts leaf faces to a fully solid BSP model. "
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
				tooltip("Hide selected leaves from view. This can be used to disconnect clusters of "
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
			if (ImGui::MenuItem("Copy", 0, false, !app->isLoading && anySolidSelected)) {
				app->copyEnts(true);
			}
			tooltip("Stores the entity and BSP model to the clipboard. Used to transfer BSP models between maps.\n\n"
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
			tooltip("Force entities to use the same BSP model.\n\n"
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
						showWidget(WIDGET_MODEL_MERGE_CONFIRM, true);
					}
				}
			}
			tooltip("Merge solid entity models together.");

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
						checkValidHulls();

						map->remove_unused_model_structures(false).print_delete_stats(1);
						reloadLimits();

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
					checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					reloadLimits();

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
					checkValidHulls();

					logf("Cleaning %s\n", map->name.c_str());
					map->remove_unused_model_structures(false).print_delete_stats(1);
					reloadLimits();

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
						checkValidHulls();

						logf("Cleaning %s\n", map->name.c_str());
						map->remove_unused_model_structures(false).print_delete_stats(1);
						reloadLimits();

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
					reloadLimits();

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
						reloadLimits();

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
							checkValidHulls();

							logf("Cleaning %s\n", map->name.c_str());
							map->remove_unused_model_structures(false).print_delete_stats(1);
							reloadLimits();

							command->pushUndoState();
						}
						tooltip("Redirect a clipnode hull to another clipnode hull. This is safer than deleting but can make collision detection less accurate.");
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
		widgets[WIDGET_TRANSFORM]->widgetVisible = !widgets[WIDGET_TRANSFORM]->widgetVisible;
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Properties", "Alt+Enter")) {
		widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = !widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
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
					entityReportFilterNeeded = true;
					app->pickInfo.deselect();
					app->postSelectEnt();
				}
			}
			tooltip("Delete all map entities and load new ones from a file."
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
		tooltip(("Merge one other BSP into the current file.\n\n"
			"Equivalent CLI command:\nbspguy merge " + map->name + " -noripent -maps \""
			+ map->name + ",other_map\"").c_str());

		if (ImGui::MenuItem("Merge Multiple", NULL, false, !app->isLoading) && g_app->confirmMapExit()) {
			showWidget(WIDGET_MERGE_MULTI, true);
		}
		tooltip("Merge multiple BSPs into a new file.");

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading)) {
			app->reloadMaps();
			refresh();
		}
		tooltip("Discard all changes and reload the map.\n");

		if (ImGui::MenuItem("Validate")) {
			logf("\n-------- Validating %s --------\n", map->name.c_str());
			map->validate();
			logf("-----------------------------------------\n", map->name.c_str());
			ImGui::SetWindowCollapsed("Messages", false);
			widgets[WIDGET_MESSAGES]->widgetVisible = true;
		}
		tooltip("Checks BSP data structures for invalid values and references. Trivial problems are fixed automatically. Results are output to the Messages widget.");
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

		if (ImGui::BeginMenu("Clipnodes")) {
			if (ImGui::MenuItem("World", 0, g_settings.render_flags & RENDER_WORLD_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			tooltip("Render clipnode hulls for worldspawn");

			if (ImGui::MenuItem("Entities", 0, g_settings.render_flags & RENDER_ENT_CLIPNODES)) {
				g_settings.render_flags ^= RENDER_ENT_CLIPNODES;
			}
			tooltip("Render clipnode hulls for solid entities");

			if (ImGui::MenuItem("Transparency", 0, transparentClipnodes)) {
				transparentClipnodes = !transparentClipnodes;
				g_app->mapRenderer->updateClipnodeOpacity(transparentClipnodes ? 128 : 255);
			}
			tooltip("Render clipnode meshes with transparency.");
			g_settings.render_flags = g_settings.render_flags;

			ImGui::Separator();

			if (ImGui::MenuItem("Auto Hulls", 0, app->clipnodeRenderHull == -1)) {
				app->clipnodeRenderHull = -1;
			}
			tooltip("Render collision hulls for things which would otherwise be invisible."
				"\n\nAn example of this is a trigger_once which had the NULL texture applied to it by the mapper. "
				"That entity would have no visible faces and so would be invisible if its collision hull were not rendered instead.");

			if (ImGui::MenuItem("Hull 0 (Point)", 0, app->clipnodeRenderHull == 0)) {
				app->clipnodeRenderHull = 0;
			}
			tooltip("Renders hull 0 regardless of object visibility.\n\n"
				"This hull is used for point-sized object collision and mesh rendering.");

			if (ImGui::MenuItem("Hull 1 (Human)", 0, app->clipnodeRenderHull == 1)) {
				app->clipnodeRenderHull = 1;
			}
			tooltip("Renders hull 1 regardless of object visibility."
				"\n\nThis is a collision hull used by standing players and human-sized monsters.");

			if (ImGui::MenuItem("Hull 2 (Large)", 0, app->clipnodeRenderHull == 2)) {
				app->clipnodeRenderHull = 2;
			}
			tooltip("Renders hull 2 regardless of object visibility.\n\n"
				"This is a collision hull used by large monsters and pushable objects.");

			if (ImGui::MenuItem("Hull 3 (Head)", 0, app->clipnodeRenderHull == 3)) {
				app->clipnodeRenderHull = 3;
			}
			tooltip("Renders hull 3 regardless of object visibility.\n\n"
				"This is a collision hull used by crouching players and small monsters.");

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

			if (ImGui::MenuItem("Name Tags", 0, g_settings.render_flags & RENDER_NAME_TAGS)) {
				g_settings.render_flags ^= RENDER_NAME_TAGS;
			}
			tooltip("Display entity target names");

			if (ImGui::MenuItem("Render Modes", 0, g_settings.render_flags & RENDER_RENDER_MODES)) {
				g_settings.render_flags ^= RENDER_RENDER_MODES;
			}
			tooltip("Models, sprites, and brushes render as they would in-game. "
				"Entity rendermode, renderamt, and rendercolor keys are respected.\n\n"
				"In some cases this makes entities completely invisible and difficult to select."
			);

			ImGui::Separator();

			if (ImGui::MenuItem("Models", 0, g_settings.render_flags & RENDER_STUDIO_MDL)) {
				g_settings.render_flags ^= RENDER_STUDIO_MDL;

				if (!(g_settings.render_flags & RENDER_STUDIO_MDL)) {
					for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
						app->mapRenderer->map->ents[i]->didStudioDraw = false;
					}
				}
			}
			tooltip("Display game models instead of colored cubes where available.");

			if (ImGui::MenuItem("Sprites", 0, g_settings.render_flags & RENDER_SPRITES)) {
				g_settings.render_flags ^= RENDER_SPRITES;

				if (!(g_settings.render_flags & RENDER_SPRITES)) {
					for (int i = 0; i < app->mapRenderer->map->ents.size(); i++) {
						app->mapRenderer->map->ents[i]->didStudioDraw = false;
					}
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
		tooltip("Outline all faces.");

		ImGui::Separator();

		if (ImGui::MenuItem("Leaf Links", 0, g_settings.render_flags & RENDER_LEAF_GRAPH)) {
			g_settings.render_flags ^= RENDER_LEAF_GRAPH;
		}
		tooltip("Display leaf connectivity graph when in leaf selection mode.\n\n"
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

	if (ImGui::BeginMenu("Settings"))
	{
		if (ImGui::MenuItem("Editor Setup", NULL)) {
			if (!widgets[WIDGET_SETTINGS]->widgetVisible) {
				reloadSettings = true;
			}
			ImGui::SetWindowCollapsed("Editor Setup", false);
			widgets[WIDGET_SETTINGS]->widgetVisible = true;
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
			tooltip("The standard GoldSrc engine.\n");

			if (ImGui::MenuItem("Sven Co-op", 0, g_settings.engine == ENGINE_SVEN_COOP, !app->isLoading)) {
				changed = g_settings.engine != ENGINE_SVEN_COOP;
				g_settings.engine = ENGINE_SVEN_COOP;
				if (g_settings.mapsize_auto) {
					g_settings.mapsize_min = -32768;
					g_settings.mapsize_max = 32768;
				}
			}
			tooltip("Sven Co-op has higher map limits than Half-Life. Some maps need this selected to display correctly in the editor."
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
		tooltip("Create a point entity. This is a ripent-only operation which does not affect BSP structures.\n");

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
		tooltip("Create a BSP model and attach it to a new entity. This is not a ripent-only operation and will create new BSP structures.\n");

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
		tooltip("Create a point entity for use with the culling tool. 2 of these define the bounding box for the Cull Box deletion tool.\n");

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

		if (ImGui::BeginMenu("Delete BSP Data", !app->isLoading)) {
			if (ImGui::MenuItem("Clean", 0, false, !app->isLoading)) {
				LumpReplaceCommand* command = new LumpReplaceCommand("Clean " + map->name);

				logf("Cleaning %s\n", map->name.c_str());
				map->remove_unused_model_structures().print_delete_stats(1);

				command->pushUndoState();
			}
			tooltip("Removes unreferenced structures in the BSP data. Run this after editing BSP models.\n\nWhen you edit BSP models or delete"
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
			tooltip(g_optimize_tip);

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
						tooltip("Redirects a clipnode hull in all models including worldspawn. This frees up a large "
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
					hoveredOOB = i;
				}
				tooltip(("Deletes out-of-bounds BSP structures and entities " + string(optionDesc[i]) + ".\n\n"
					"Enable the Map Boundary setting in the View menu to see what will be deleted.").c_str());
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Faces")) {
			if (ImGui::MenuItem("Fix Bad Surface Extents", 0, false, !app->isLoading)) {
				showWidget(WIDGET_FIX_EXTENTS, true);
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

		if (ImGui::BeginMenu("Models")) {
			if (ImGui::MenuItem("Deduplicate Models", 0, false, !app->isLoading)) {
				showWidget(WIDGET_DEDUP_MODELS, true);
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

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Textures")) {
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
			tooltip("Removes unused WADs from the worldspawn 'wad' keyvalue and strips folder paths.");

			ImGui::EndMenu();
		}
		
		if (ImGui::MenuItem("RAD Preparation", 0, false, !app->isLoading)) {
			showWidget(WIDGET_RAD_PREP, true);
		}
		tooltip("Prepare the map for light recompilation with VHLT.");

		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Widgets"))
	{
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

		if (ImGui::MenuItem("Debug", NULL, widgets[WIDGET_DEBUG]->widgetVisible)) {
			widgets[WIDGET_DEBUG]->widgetVisible = !widgets[WIDGET_DEBUG]->widgetVisible;
			if (widgets[WIDGET_DEBUG]->widgetVisible)
				ImGui::SetWindowCollapsed(widgets[WIDGET_DEBUG]->widgetName, false);
		}
		tooltip("For developers and those curious about BSP internals.");

		if (ImGui::MenuItem("Entity Report", "Ctrl+F", widgets[WIDGET_ENT_REPORT]->widgetVisible)) {
			widgets[WIDGET_ENT_REPORT]->widgetVisible = !widgets[WIDGET_ENT_REPORT]->widgetVisible;
			if (widgets[WIDGET_ENT_REPORT]->widgetVisible)
				ImGui::SetWindowCollapsed("###entreport", false);
		}
		tooltip("Search for entities by name, class, and/or other properties.");

		if (ImGui::MenuItem("Face Editor", "", widgets[WIDGET_FACE_EDITOR]->widgetVisible)) {
			widgets[WIDGET_FACE_EDITOR]->widgetVisible = !widgets[WIDGET_FACE_EDITOR]->widgetVisible;
			if (widgets[WIDGET_FACE_EDITOR]->widgetVisible)
				ImGui::SetWindowCollapsed("Face Editor", false);
		}
		tooltip("Edit faces and textures.");

		if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible)) {
			widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible = !widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible;
			if (widgets[WIDGET_KEYVALUE_EDITOR]->widgetVisible)
				ImGui::SetWindowCollapsed("Keyvalue Editor", false);
		}
		tooltip("Edit entity properties.");

		if (ImGui::MenuItem("Map Limits", NULL, widgets[WIDGET_LIMITS]->widgetVisible)) {
			widgets[WIDGET_LIMITS]->widgetVisible = !widgets[WIDGET_LIMITS]->widgetVisible;
			if (widgets[WIDGET_LIMITS]->widgetVisible)
				ImGui::SetWindowCollapsed("Map Limits", false);
		}
		tooltip("Shows how close the map is to exceeding engine limits.");

		if (ImGui::MenuItem("Messages", "", widgets[WIDGET_MESSAGES]->widgetVisible)) {
			widgets[WIDGET_MESSAGES]->widgetVisible = !widgets[WIDGET_MESSAGES]->widgetVisible;
			if (widgets[WIDGET_MESSAGES]->widgetVisible)
				ImGui::SetWindowCollapsed("Messages", false);
		}
		tooltip("Show program messages.");

		if (ImGui::MenuItem("Transform", "Ctrl+M", widgets[WIDGET_TRANSFORM]->widgetVisible)) {
			widgets[WIDGET_TRANSFORM]->widgetVisible = !widgets[WIDGET_TRANSFORM]->widgetVisible;
			if (widgets[WIDGET_TRANSFORM]->widgetVisible)
				ImGui::SetWindowCollapsed("Transformation", false);
		}
		tooltip("Move, rotate, and scale entities.");

		ImGui::Separator();

		ImGui::PopItemFlag();

		static string userLayout = getUserLayoutPath();

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
			ImGui::ClearWindowSettings("###limits");
			ImGui::ClearWindowSettings("###entreport");
			ImGui::ClearWindowSettings("Messages");
			ImGui::ClearWindowSettings("Transformation");
			ImGui::ClearWindowSettings("Keyvalue Editor");
			ImGui::ClearWindowSettings("Editor Setup");
			ImGui::ClearWindowSettings("Face Editor");
			ImGui::ClearWindowSettings("Help");
			ImGui::ClearWindowSettings("About");
		}
		tooltip("Reset widget sizes to their defaults.\n");

		ImGui::ClearIniSettings();

		ImGui::PopItemFlag();

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("View help")) {
			widgets[WIDGET_HELP]->widgetVisible = true;
		}
		if (ImGui::MenuItem("About")) {
			widgets[WIDGET_ABOUT]->widgetVisible = true;
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
		if (ImGui::MenuItem("VSync", NULL, vsync)) {
			vsync = !vsync;
			glfwSwapInterval(vsync ? 1 : 0);
		}
		if (ImGui::MenuItem("wpoly", NULL, g_settings.show_wpoly)) {
			g_settings.show_wpoly = !g_settings.show_wpoly;
		}
		tooltip("Display the number of polygons that would be rendered in-game. Enabling this reduces FPS when wpoly counts are high.");

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
		int fontSize = g_font_scale_base;
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

		ImGui::SameLine();
		float labelW = ImGui::CalcTextSize(selectStr.c_str()).x;
		float avail = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - labelW);
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
	ImGuiIO& io = ImGui::GetIO();

	for (int id = 0; id < NUM_WIDGET_IDS; id++) {
		Widget* widget = widgets[id];
		if (!widget->widgetVisible || !widget->isPopup)
			continue;

		ImGui::SetNextWindowSize(ImVec2(widget->widgetSizeDefault.x * uiScale, widget->widgetSizeDefault.y * uiScale), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(widget->widgetSizeMin.x * uiScale, widget->widgetSizeMin.y * uiScale), ImVec2(FLT_MAX, app->windowHeight));
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		widget->uiScale = uiScale;
		widget->map = g_app->mapRenderer->map;
		widget->setup();

		if (!widget->popupWasOpen) {
			widget->popupWasOpen = true;
			widget->open();
		}

		ImGui::OpenPopup(widget->widgetName);

		if (ImGui::BeginPopupModal(widget->widgetName, NULL, widget->widgetFlags)) {
			widget->draw();

			if (!widget->widgetVisible) {
				ImGui::SetItemDefaultFocus();
				ImGui::CloseCurrentPopup();
				widget->popupWasOpen = false;

				if (popupStack.size()) {
					showWidget(popupStack.back(), true);
					popupStack.pop_back();
				}

				if (widget->shouldReturnToThisPopup) {
					widget->shouldReturnToThisPopup = false;
					popupStack.push_back(id);
				}
			}

			ImGui::EndPopup();
		}
	}
}

void Gui::drawToolbar() {
	ImVec2 window_pos = ImVec2(6.0f*uiScale, 35.0f*uiScale);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("toolbar", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = uiScale * 32;
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
			widgets[WIDGET_FACE_EDITOR]->widgetVisible = false;
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
			widgets[WIDGET_FACE_EDITOR]->widgetVisible = true;
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
		((FaceEditor*)widgets[WIDGET_FACE_EDITOR])->checkFaceErrors();
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

			ImGui::PushFont(consoleFont, g_font_scale_base);
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

	io.Fonts->SetFontLoader(g_settings.freetype_font ? ImGuiFreeType::GetFontLoader() : NULL);

	defaultFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, notosans_unicode_sz, g_font_scale_base);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, notosans_mono_sz, g_font_scale_base);

	updateUiScale();
}

void Gui::updateUiScale() {
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	
	g_settings.ui_scale = clamp(g_settings.ui_scale, 50, 200);
	uiScale = g_settings.ui_scale * 0.01f;

	// 1.1 to keep consistent with previous version size
	style.FontScaleMain = uiScale;

	style.FramePadding = ImVec2(roundf(4.0f * uiScale), roundf(2.0f * uiScale));
	style.ItemSpacing = ImVec2(roundf(8.0f * uiScale), roundf(3.0f * uiScale));
	style.ItemInnerSpacing = ImVec2(roundf(4.0f * uiScale), roundf(4.0f * uiScale));
	style.WindowPadding = ImVec2(roundf(8.0f * uiScale), roundf(8.0f * uiScale));
	style.IndentSpacing = roundf(21.0f * uiScale);
	style.GrabMinSize = roundf(12.0f * uiScale);
	style.ScrollbarSize = roundf(12.0f * uiScale);
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
			widgets[WIDGET_SETTINGS]->widgetVisible = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::EndPopup();
	}
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
	widgets[WIDGET_FACE_EDITOR]->widgetVisible = false;
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

void Gui::showWidget(int id, bool showNotHide) {
	widgets[id]->widgetVisible = showNotHide;
}