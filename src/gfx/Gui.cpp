#include "Gui.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Renderer.h"

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

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	const char* glsl_version = "#version 130";
	ImGui_ImplOpenGL3_Init(glsl_version);

	vsync = true;
	smallFont = io.Fonts->AddFontFromFileTTF("../imgui/misc/fonts/Roboto-Medium.ttf", 20.0f);
	largeFont = io.Fonts->AddFontFromFileTTF("../imgui/misc/fonts/Roboto-Medium.ttf", 24.0f);

	io.ConfigWindowsMoveFromTitleBarOnly = true;

	showDebugWidget = true;
	showKeyvalueWidget = true;

	contextMenuEnt = -1;
	emptyContextMenu = 0;
}

void Gui::draw() {
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::ShowDemoWindow();

	drawMenuBar();

	drawFpsOverlay();
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

	if (contextMenuEnt != -1) {
		ImGui::OpenPopup("ent_context");
		contextMenuEnt = -1;
	}
	if (emptyContextMenu) {
		emptyContextMenu = 0;
		ImGui::OpenPopup("empty_context");
	}

	draw3dContextMenus();

	// Rendering
	ImGui::Render();
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Gui::openContextMenu(int entIdx) {
	if (entIdx == -1) {
		emptyContextMenu = 1;
	}
	contextMenuEnt = entIdx;
}

void Gui::draw3dContextMenus() {

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
		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "G")) {
			app->movingEnt = !app->movingEnt;
			if (app->movingEnt)
				app->grabEnt();
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

void Gui::drawMenuBar() {
	ImGui::BeginMainMenuBar();

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", NULL)) {
			Bsp* map = app->getMapContainingCamera()->map;
			for (int i = 0; i < map->ents.size(); i++) {
				if (map->ents[i]->keyvalues["classname"] == "info_node")
					map->ents[i]->keyvalues["classname"] = "info_bode";
			}
			map->update_ent_lump();
			map->write("yabma_move.bsp");
			map->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Map"))
	{
		if (ImGui::MenuItem("Clean", NULL)) {
			for (int i = 0; i < app->mapRenderers.size(); i++) {
				Bsp* map = app->mapRenderers[i]->map;
				printf("Cleaning %s\n", map->name.c_str());
				app->pickInfo.valid = false;
				map->remove_unused_model_structures().print_delete_stats(0);
			}
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
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Render"))
	{
		if (ImGui::MenuItem("Textures", NULL, g_render_flags & RENDER_TEXTURES)) {
			g_render_flags ^= RENDER_TEXTURES;
		}
		if (ImGui::MenuItem("Lightmaps", NULL, g_render_flags & RENDER_LIGHTMAPS)) {
			g_render_flags ^= RENDER_LIGHTMAPS;
		}
		if (ImGui::MenuItem("Wireframe", NULL, g_render_flags & RENDER_WIREFRAME)) {
			g_render_flags ^= RENDER_WIREFRAME;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Entities", NULL, g_render_flags & RENDER_ENTS)) {
			g_render_flags ^= RENDER_ENTS;
		}
		if (ImGui::MenuItem("Special", NULL, g_render_flags & RENDER_SPECIAL)) {
			g_render_flags ^= RENDER_SPECIAL;
		}
		if (ImGui::MenuItem("Special Entities", NULL, g_render_flags & RENDER_SPECIAL_ENTS)) {
			g_render_flags ^= RENDER_SPECIAL_ENTS;
		}
		if (ImGui::MenuItem("Point Entities", NULL, g_render_flags & RENDER_POINT_ENTS)) {
			g_render_flags ^= RENDER_POINT_ENTS;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Create"))
	{
		if (ImGui::MenuItem("Entity")) {
			Entity* newEnt = new Entity();
			vec3 origin = (app->cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");
			BspRenderer* destMap = app->getMapContainingCamera();
			destMap->map->ents.push_back(newEnt);
			destMap->preRenderEnts();
		}

		if (ImGui::MenuItem("BSP Model")) {

			BspRenderer* destMap = app->getMapContainingCamera();

			vec3 origin = app->cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			vec3 mins = vec3(-16, -16, -16);
			vec3 maxs = vec3(16, 16, 16);

			int modelIdx = destMap->map->create_solid(mins, maxs, 3);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("model", "*" + to_string(modelIdx));
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");
			destMap->map->ents.push_back(newEnt);

			destMap->updateLightmapInfos();
			destMap->calcFaceMaths();
			destMap->preRenderFaces();
			destMap->preRenderEnts();
			destMap->map->validate();

			destMap->map->print_model_hull(modelIdx, 1);
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void Gui::drawFpsOverlay() {
	ImVec2 window_pos = ImVec2(10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
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
	static int lastWindowWidth = 32;
	static int windowWidth = 32;

	bool showStatus = app->invalidSolid || !app->isTransformableSolid;
	if (showStatus) {
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2, app->windowHeight - 10.0f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (!app->isTransformableSolid) {
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONCAVE SOLID");
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Vertex manipulation doesn't work for concave solids yet\n";
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(info);
					ImGui::PopTextWrapPos();
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
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(info);
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}
			windowWidth = ImGui::GetWindowWidth();
		}
		ImGui::End();
	}
}

void Gui::drawDebugWidget() {
	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, 800));
	if (ImGui::Begin("Debug info", &showDebugWidget, ImGuiWindowFlags_AlwaysAutoResize)) {

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Origin: %d %d %d", (int)app->cameraOrigin.x, (int)app->cameraOrigin.y, (int)app->cameraOrigin.z);
			ImGui::Text("Angles: %d %d %d", (int)app->cameraAngles.x, (int)app->cameraAngles.y, (int)app->cameraAngles.z);
		}

		if (app->pickInfo.valid) {
			Bsp* map = app->pickInfo.map;
			Entity* ent =app->pickInfo.ent;

			if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen) && app->pickInfo.valid)
			{
				ImGui::Text("Name: %s", map->name.c_str());
			}

			if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen) && app->pickInfo.valid)
			{
				ImGui::Text("Entity ID: %d", app->pickInfo.entIdx);

				if (app->pickInfo.modelIdx > 0) {
					ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);
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
					
				}

			}

			if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen) && app->pickInfo.valid)
			{
				ImGui::Text("DebugVec0 %6.2f %6.2f %6.2f", app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
				ImGui::Text("DebugVec1 %6.2f %6.2f %6.2f", app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
				ImGui::Text("DebugVec2 %6.2f %6.2f %6.2f", app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
				ImGui::Text("DebugVec3 %6.2f %6.2f %6.2f", app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);
			}
		}
		else {
			ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen);
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor() {
	//ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin("Keyvalue Editor", &showKeyvalueWidget, 0)) {
		if (app->pickInfo.valid) {
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
								ent->keyvalues["classname"] = group.classes[k]->name;
								app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
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

			strcpy(keyNames[i], niceName.c_str());
			strcpy(keyValues[i], value.c_str());

			inputData[i].key = key;
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
						bool selected = choice.svalue == value || value.empty() && choice.svalue == keyvalue.defaultValue;

						if (ImGui::Selectable(choice.name.c_str(), selected)) {
							if (keyvalue.defaultValue == choice.svalue) {
								ent->removeKeyvalue(key);
							}
							else {
								ent->setOrAddKeyvalue(key, choice.svalue);
							}
							app->mapRenderers[app->pickInfo.mapIdx]->refreshEnt(app->pickInfo.entIdx);
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
							ent->removeKeyvalue(inputData->key);
						}
						else {
							ent->setOrAddKeyvalue(inputData->key, newVal);
						}
						inputData->bspRenderer->refreshEnt(inputData->entIdx);
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

	static char keyNames[128][64];
	static char keyValues[128][64];

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
			}

			return 1;
		}

		static int keyValueChanged(ImGuiInputTextCallbackData* data) {
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;
			string key = ent->keyOrder[inputData->idx];

			if (ent->keyvalues[key] != data->Buf) {
				ent->keyvalues[key] = data->Buf;
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
			}

			return 1;
		}
	};

	static InputData keyIds[128];
	static InputData valueIds[128];
	static int lastPickCount = -1;
	static string dragNames[128];
	static const char* dragIds[128];

	if (dragNames[0].empty()) {
		for (int i = 0; i < 128; i++) {
			string name = "::##drag" + to_string(i);
			dragNames[i] = name;
		}
	}

	if (lastPickCount != app->pickCount) {
		for (int i = 0; i < 128; i++) {
			dragIds[i] = dragNames[i].c_str();
		}
	}

	ImVec4 dragColor = style.Colors[ImGuiCol_FrameBg];
	dragColor.x *= 2;
	dragColor.y *= 2;
	dragColor.z *= 2;

	ImVec4 dragButColor = style.Colors[ImGuiCol_Header];

	static bool hoveredDrag[128];
	static int ignoreErrors = 0;

	float startY = 0;
	for (int i = 0; i < ent->keyOrder.size() && i < 128; i++) {
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

			if (i == 0) {
				startY = ImGui::GetItemRectMin().y;
			}

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				int n_next = (ImGui::GetMousePos().y - startY) / (ImGui::GetItemRectSize().y + style.FramePadding.y * 2);
				if (n_next >= 0 && n_next < ent->keyOrder.size() && n_next < 128)
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
			ImGui::InputText(("##key" + to_string(i) + "_" + to_string(app->pickCount)).c_str(), keyNames[i], 64, ImGuiInputTextFlags_CallbackAlways,
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
			ImGui::InputText(("##val" + to_string(i) + to_string(app->pickCount)).c_str(), keyValues[i], 64, ImGuiInputTextFlags_CallbackAlways,
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
				ignoreErrors = 2;
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

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
		ignoreErrors = 2;
	}

	if (ignoreErrors > 0) {
		ignoreErrors--;
	}

	ImGui::EndChild();
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

	ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, app->windowHeight - 40));
	if (ImGui::Begin("Transformation", &showTransformWidget, 0)) {
		static int x, y, z;
		static float fx, fy, fz;
		static float last_fx, last_fy, last_fz;
		static float sx, sy, sz;

		static int lastPickCount = -1;
		static int lastVertPickCount = -1;
		static int oldSnappingEnabled = app->gridSnappingEnabled;
		static int oldTransformTarget = -1;

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

		if (ImGui::BeginTabBar("##tabs"))
		{
			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;

			if (ImGui::BeginTabItem("Move")) {
				app->transformMode = TRANSFORM_MOVE;
				ImGui::Dummy(ImVec2(0, style.FramePadding.y));
				ImGui::PushItemWidth(inputWidth);

				if (app->gridSnappingEnabled) {
					if (ImGui::DragInt("##xpos", &x, 0.1f, 0, 0, "X: %d")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 0;
					ImGui::SameLine();

					if (ImGui::DragInt("##ypos", &y, 0.1f, 0, 0, "Y: %d")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 1;
					ImGui::SameLine();

					if (ImGui::DragInt("##zpos", &z, 0.1f, 0, 0, "Z: %d")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 2;
				}
				else {
					if (ImGui::DragFloat("##xpos", &fx, 0.1f, 0, 0, "X: %.2f")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 0;
					ImGui::SameLine();

					if (ImGui::DragFloat("##ypos", &fy, 0.1f, 0, 0, "Y: %.2f")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 1;
					ImGui::SameLine();

					if (ImGui::DragFloat("##zpos", &fz, 0.1f, 0, 0, "Z: %.2f")) { originChanged = true; }
					if (ImGui::IsItemHovered() || ImGui::IsItemActive())
						guiHoverAxis = 2;
				}

				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Scale")) {
				app->transformMode = TRANSFORM_SCALE;
				ImGui::Dummy(ImVec2(0, style.FramePadding.y));
				ImGui::PushItemWidth(inputWidth);

				if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f")) { scaled = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 0;
				ImGui::SameLine();

				if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f")) { scaled = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 1;
				ImGui::SameLine();

				if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f")) { scaled = true; }
				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
					guiHoverAxis = 2;


				ImGui::Checkbox("Texture lock", &app->textureLock);
				ImGui::SameLine();
				ImGui::TextDisabled("(WIP)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted("Doesn't work for angled faces yet.");
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}

				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Options")) {
				ImGui::Dummy(ImVec2(0, style.FramePadding.y));

				static int e = 0;
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Target: ");  ImGui::SameLine();
				ImGui::RadioButton("Object", &app->transformTarget, TRANSFORM_OBJECT); ImGui::SameLine();
				ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX);

				const int grid_snap_modes = 11;
				const char* element_names[grid_snap_modes] = { "0", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
				static int current_element = app->gridSnapLevel;
				current_element = app->gridSnapLevel+1;
				if (ImGui::SliderInt("Grid snap", &current_element, 0, grid_snap_modes - 1, element_names[current_element])) {
					app->gridSnapLevel = current_element - 1;
					app->gridSnappingEnabled = current_element != 0;
					originChanged = true;
				}

				ImGui::Checkbox("3D Axes", &app->showDragAxes);

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
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
					app->applyTransform();

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
				else {
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
				}
			}
			if (scaled && ent->isBspModel()) {
				if (app->transformTarget == TRANSFORM_VERTEX) {
					app->scaleSelectedVerts(sx, sy, sz);
				}
				else {
					int modelIdx = ent->getBspModelIdx();
					app->scaleSelectedObject(sx, sy, sz);
					app->mapRenderers[app->pickInfo.mapIdx]->refreshModel(ent->getBspModelIdx());
				}
			}
		}
	}
	ImGui::End();
}