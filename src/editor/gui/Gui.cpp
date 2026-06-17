#include "Gui.h"
#include "primitives.h"
#include "Editor.h"
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
#include "MenuBar.h"

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

const char* g_optimize_tip =
"Removes \"unnecesary\" structures in the BSP data. Potentially unsafe.\n\n"

"What the program considers unnecesary for Half-Life may become a fatal error for another game."
"In most cases mods do not significantly change default entity behavior, but there is a risk.\n\n"

"An example of commonly deleted structures would be the visible hull 0 for entities like "
"trigger_once, which are invisible and so don't need textured faces. Entities "
"like func_illusionary also don't need any clipnodes because they're not meant to be collidable.\n\n"

"Check the Messages widget to see which entities had their hulls deleted. You may want to selectively "
"delete hulls yourself if you run into problems.";

void tooltip(const char* text, float hoverDelay) {
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && GImGui->HoveredIdTimer > hoverDelay) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(min(ImGui::GetFontSize() * 35.0f, (float)g_app->windowWidth));
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

size_t g_imgui_alloc_bytes = 0;
int numAllocs = 0;
unordered_map<void*, int> g_imgui_alloc_sizes;

static void* ImGuiAlloc(size_t sz, void*)
{
	g_imgui_alloc_bytes += sz;
	void* ptr = malloc(sz);
	g_imgui_alloc_sizes[ptr] = sz;
	numAllocs++;
	return ptr;
}

static void ImGuiFree(void* ptr, void*)
{
	g_imgui_alloc_bytes -= g_imgui_alloc_sizes[ptr];
	g_imgui_alloc_sizes.erase(ptr);
	free(ptr);
}

Gui::Gui(Editor* app) {
	this->app = app;
	init();
}

void Gui::init() {
	float startTime = glfwGetTime();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();

#ifdef DEBUG_MODE
	ImGui::SetAllocatorFunctions(ImGuiAlloc, ImGuiFree);
#endif

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

	menuBar = new MenuBar(app, this);

	// init widgets
	memset(widgets, 0, sizeof(widgets));
	widgets[WIDGET_DEBUG] = new DebugWidget(this, "Debug info", ImVec2(100, 100), ImVec2(100, 100),
		ImGuiWindowFlags_AlwaysAutoResize);
	widgets[WIDGET_DEBUG]->allowInMapArrangeMode = true;

	widgets[WIDGET_KEYVALUE_EDITOR] = new KeyvalueEditor(this, "Keyvalue Editor",
		ImVec2(610, 610), ImVec2(470, 250), 0);

	widgets[WIDGET_TRANSFORM] = new TransformWidget(this, "Transformation",
		ImVec2(400, 320), ImVec2(350, 140), 0);
	widgets[WIDGET_TRANSFORM]->allowInMapArrangeMode = true;

	widgets[WIDGET_MESSAGES] = new LogWidget(this, "Messages", ImVec2(750, 300), ImVec2(200, 100), 0);
	widgets[WIDGET_TRANSFORM]->allowInMapArrangeMode = true;

	widgets[WIDGET_SETTINGS] = new SettingsWidget(this, "Editor Setup", ImVec2(790, 460), ImVec2(550, 270), 0);

	widgets[WIDGET_HELP] = new HelpWidget(this, "Help", ImVec2(620, 400), ImVec2(620, 300), 0);
	widgets[WIDGET_HELP]->allowInMapArrangeMode = true;

	widgets[WIDGET_ABOUT] = new AboutWidget(this, "About", ImVec2(500, 140), ImVec2(250, 140), 0);
	widgets[WIDGET_ABOUT]->allowInMapArrangeMode = true;

	widgets[WIDGET_LIMITS] = new LimitsWidget(this, "Map Limits###limits",
		ImVec2(460, 520), ImVec2(460, 200), 0);

	widgets[WIDGET_ENT_REPORT] = new EntityReport(this, "###entreport",
		ImVec2(400, 600), ImVec2(300, 350), 0);

	widgets[WIDGET_FACE_EDITOR] = new FaceEditor(this, "Face Editor",
		ImVec2(260, 530), ImVec2(260, 510), ImGuiWindowFlags_NoScrollbar);
	((FaceEditor*)widgets[WIDGET_FACE_EDITOR])->clearTextureBrowserCache();

	widgets[WIDGET_LEAF] = new LeafWidget(this, "Leaf Graph",
		ImVec2(500, 500), ImVec2(200, 200), 0);

	// pop ups
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

	loadFonts();

	glCheckError("ImGui font load");
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

	menuBar->draw();

	if (!app->mapArrangeMode && !g_settings.ripent_safe_mode)
		drawToolbar();

	drawStatusMessage();

	drawWidgets();

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

	ImGuiIO& io = ImGui::GetIO();
	imguiDrawListBytes = io.MetricsRenderVertices * sizeof(ImDrawVert) +
		io.MetricsRenderIndices * sizeof(ImDrawIdx);

	if (shouldReloadFonts) {
		shouldReloadFonts = false;
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		loadFonts();
	}
}

void Gui::drawWidgets() {
	for (Widget* widget : widgets) {
		widget->map = g_app->mapRenderer->map;

		if (!widget->widgetVisible)
			continue;
		if (widget->isPopup)
			continue; // drawn in 2nd pass later
		if (g_app->mapArrangeMode && !widget->allowInMapArrangeMode)
			continue;

		ImGui::SetNextWindowSize(ImVec2(widget->widgetSizeDefault.x * uiScale, widget->widgetSizeDefault.y * uiScale), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(widget->widgetSizeMin.x * uiScale, widget->widgetSizeMin.y * uiScale), ImVec2(FLT_MAX, app->windowHeight));

		widget->uiScale = uiScale;
		widget->setup();

		if (widget->shouldResetPosition) {
			ImGui::SetNextWindowPos(widget->lastPosition, ImGuiCond_Always);
			widget->shouldResetPosition = false;
		}

		if (ImGui::Begin(widget->widgetName, &widget->widgetVisible, widget->widgetFlags)) {
			widget->lastPosition = ImGui::GetWindowPos();
			widget->draw();
		}
		ImGui::End();
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
			menuBar->drawEditOptions(false);

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
						selectFacesByTexture(selectedMiptex, false);
					}
					tooltip("Select every face in the map which has this texture.");

					if (ImGui::MenuItem("Texture (bad extents)", "", false, app->pickInfo.faces.size() == 1)) {
						Bsp* map = app->pickInfo.getMap();
						BSPTEXTUREINFO& texinfo = map->texinfos[app->pickInfo.getFace()->iTextureInfo];
						uint32_t selectedMiptex = texinfo.iMiptex;						
						selectFacesByTexture(selectedMiptex, true);
					}
					tooltip("Select faces with bad surface extents that use this texture.");

					ImGui::EndMenu();
				}

				if (ImGui::MenuItem("Delete", "", false, !app->isLoading)) {
					bool plural = app->pickInfo.faces.size() > 1;
					LumpReplaceCommand* command = new LumpReplaceCommand(plural ? "Delete Faces" : "Delete Face");

					Bsp* map = app->pickInfo.getMap();

					map->delete_faces(app->pickInfo.faces);

					command->pushUndoState();
				}
				tooltip("Delete visible faces. This reduces face count and does not affect collision detection.");

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

				/*
				if (ImGui::MenuItem("Select Branch", "", false, app->pickInfo.leaves.size() >= 1)) {
					int commonNode = map->get_lowest_common_node(app->pickInfo.leaves);

					app->mapRenderer->highlightPickedFaces(false);
					app->mapRenderer->highlightPickedLeaves(false);
					app->pickInfo.deselect();

					vector<int> selectLeaves;
					map->get_child_leaves(commonNode, selectLeaves);
					for (int idx : selectLeaves) {
						app->pickInfo.selectLeaf(idx);
					}

					app->pickInfo.selectLeafFaces();
					app->mapRenderer->highlightPickedFaces(true);
					app->mapRenderer->highlightPickedLeaves(true);
					app->updateTextureAxes();
					((LeafWidget*)widgets[WIDGET_LEAF])->selectLeaves(app->pickInfo.leaves);
				}
				tooltip("Select all leaves that connect to the same branch as the selected leaf.");
				*/

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

				/*
				if (ImGui::MenuItem("Merge Leaves", "", false, app->pickInfo.leaves.size() > 1)) {
					LumpReplaceCommand* command = new LumpReplaceCommand("Merge Leaves");

					if (map->merge_leaves(app->pickInfo.leaves, false)) {
						map->remove_unused_model_structures(false).print_delete_stats(1);
						command->pushUndoState();
						app->mapRenderer->reloadLeaves();
					}
					else {
						delete command;
					}
				}
				tooltip("test");
				*/

				if (ImGui::MenuItem("Convert to Model", "", false, !app->isLoading && app->pickInfo.leaves.size() > 1)) {
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
				tooltip("Merges the selected leaves/faces and moves them to a new solid BSP model. "
					"The new model is then attached to a func_illusionary entity which overlaps "
					"the original faces."
					"\n\nThis reduces world leaf count, but has these drawbacks:\n"
					"- Decals won't work on affected faces.\n"
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

void Gui::drawStatusBar() {
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	float height = menuBar->height; // Status bar height
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
		int fontSize = g_font_scale_base*uiScale;
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
		ImGui::Dummy(ImVec2(5* uiScale, 0));
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
				//app->deselectFaces(); // breaks ungrab logic
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
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2, app->windowHeight - (10.0f+menuBar->height));
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

byte* Gui::loadFont(string path, const unsigned char* fallbackData, int fallbackLen, int& loadedLen) {
	loadedLen = 0;

	bool fontLoaded = false;
	if (fileExists(path)) {
		char* dat = loadFile(path.c_str(), loadedLen);
		if (dat) {
			debugf("Loaded cached font: %s\n", path.c_str());
			return (byte*)dat;
			fontLoaded = true;
		}
		else {
			warnf("Failed to load cached font: %s\n", path.c_str());
		}
	}

	if (!fontLoaded) {
		// data copied to new array so that ImGui doesn't delete static data
		vector<uint8_t> decompressed;
		if (lzmaDecompress((uint8_t*)fallbackData, fallbackLen, decompressed)) {
			loadedLen = decompressed.size();
			byte* dat = new byte[loadedLen];
			memcpy(dat, &decompressed[0], loadedLen);

			if (!fileExists(path)) {
				FILE* fout = fopen(path.c_str(), "wb");
				if (fout) {
					fwrite(dat, loadedLen, 1, fout);
					fclose(fout);
					debugf("Decompressed font to: %s\n", path.c_str());
				}
				else {
					warnf("Failed to write font to: %s\n", path.c_str());
				}
			}

			return dat;
		}
		else {
			errorf("Failed to decompress font! Crash imminent.\n");
		}
	}

	return NULL;
}

void Gui::loadFonts() {
	float startTime = glfwGetTime();

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

	static byte* smallFontData;
	static byte* consoleFontData;
	
	string configDir = getConfigDir();
	static int smallFontSz, consoleFontSz;
	smallFontData = loadFont(configDir + "font_v6.ttf", notosans_unicode, sizeof(notosans_unicode), smallFontSz);
	consoleFontData = loadFont(configDir + "font_mono_v6.ttf", notosans_mono, sizeof(notosans_mono), consoleFontSz);

	io.Fonts->SetFontLoader(g_settings.freetype_font ? ImGuiFreeType::GetFontLoader() : NULL);

	defaultFont = io.Fonts->AddFontFromMemoryTTF((void*)smallFontData, smallFontSz, g_font_scale_base);
	consoleFont = io.Fonts->AddFontFromMemoryTTF((void*)consoleFontData, consoleFontSz, g_font_scale_base);

	fontBytes = smallFontSz + consoleFontSz;

	updateUiScale();

	debugf("Fonts loaded in %.2f\n", glfwGetTime() - startTime);
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

	if (g_settings.freetype_font) {
		// less blurry pixels means overall brighter text than I'm used to. Compensate for that.
		style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
	}
	else {
		style.Colors[ImGuiCol_Text] = ImVec4(1,1,1,1);
	}
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

	if (numWantTextChars > MAX_GUI_NAME_TAG_CHARS) {
		debugf("Too many GUI text chars! %d > %d\n", numWantTextChars, MAX_GUI_NAME_TAG_CHARS);
	}
	numWantTextChars = 0;

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
	((LeafWidget*)widgets[WIDGET_LEAF])->needsRefresh = true;
	entityReportFilterNeeded = true;
	lightmapEditorNeedsUpdate = true;
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

void Gui::addText(Text2D text) {
	if (text.text.size() > 64) {
		text.text = text.text.substr(0, 61) + "...";
	}

	numWantTextChars += text.text.size();

	if (numWantTextChars >= MAX_GUI_NAME_TAG_CHARS) {
		return;
	}

	texts.push_back(text);
}

void Gui::switchToLeafSelectMode(bool selectFaceLeaves, bool strictFaceLeafSelection) {
	vector<int> faces = app->pickInfo.faces;
	Bsp* map = app->mapRenderer->map;

	if (app->pickMode == PICK_OBJECT) {
		app->deselectObject();
	}
	else {
		app->deselectFaces();
	}
	
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

int Gui::calcMemUsage() {
	int bytes = sizeof(Gui) + sizeof(MenuBar);

	for (int i = 0; i < NUM_WIDGET_IDS; i++) {
		bytes += widgets[i]->calcMemoryUsage();
	}
	bytes += popupStack.size() * sizeof(int);
	bytes += objectIconTexture->calcMemoryUsage();
	bytes += faceIconTexture->calcMemoryUsage();
	bytes += leafIconTexture->calcMemoryUsage();

	for (int i = 0; i < texts.size(); i++) {
		bytes += sizeof(Text2D) + texts[i].text.size();
	}

	ImFontAtlas* atlas = ImGui::GetIO().Fonts;
	int w, h;
	atlas->GetTexDataAsRGBA32(nullptr, &w, &h);
	bytes += imguiDrawListBytes + w*h*4 + g_imgui_alloc_bytes;

	bytes += g_log_buffer.size() * sizeof(LogEntry);
	for (const LogEntry& s : g_log_buffer) {
		bytes += s.msg.size();
	}
		

	bytes += g_shaders.bsp->calcMemoryUsage();
	bytes += g_shaders.clipnode->calcMemoryUsage();
	bytes += g_shaders.color->calcMemoryUsage();
	bytes += g_shaders.mdl->calcMemoryUsage();
	bytes += g_shaders.spr->calcMemoryUsage();
	bytes += g_shaders.texture->calcMemoryUsage();
	bytes += g_shaders.vec3->calcMemoryUsage();

	bytes += fontBytes;

	return bytes;
}

void Gui::resetWidgetSizes() {
	for (Widget* widget : widgets) {
		ImGui::ClearWindowSettings(widget->widgetName);
		widget->shouldResetPosition = true;
	}
}

void Gui::selectFacesByTexture(uint32_t selectedMiptex, bool badExtentsOnly) {
	Bsp* map = app->pickInfo.getMap();

	g_app->mapRenderer->highlightPickedFaces(false);

	app->pickInfo.deselect();

	int startIdx = 0;
	int count = map->faceCount;

	if (!(g_settings.render_flags & RENDER_ENTS)) {
		startIdx = map->models[0].iFirstFace;
		count = map->models[0].nFaces;
	}

	for (int i = startIdx; i < count; i++) {
		BSPTEXTUREINFO& info = map->texinfos[map->faces[i].iTextureInfo];
		if (info.iMiptex == selectedMiptex) {

			int size[2];
			if (badExtentsOnly && GetFaceLightmapSize(map, i, size)) {
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