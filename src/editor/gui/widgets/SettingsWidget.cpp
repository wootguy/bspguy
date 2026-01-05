#include "Widget.h"
#include "ModelRenderer.h"

void SettingsWidget::setup() {
	ImGui::SetNextWindowPos(ImVec2(5 * uiScale, 50 * uiScale), ImGuiCond_FirstUseEver);
}


void SettingsWidget::draw() {
	const int settings_tabs = 4;
	static const char* tab_titles[settings_tabs] = {
		"General",
		"Rendering",
		"Asset Paths",
		"FGDs",
	};

	// left
	ImGui::BeginChild("left pane", ImVec2(130 * uiScale, 0), true);

	for (int i = 0; i < settings_tabs; i++) {
		if (ImGui::Selectable(tab_titles[i], gui->settingsTab == i))
			gui->settingsTab = i;
	}

	ImGui::EndChild();
	ImGui::SameLine();

	// right
	bool hasFooter = gui->settingsTab >= 2;
	ImGui::BeginGroup();
	int footerHeight = hasFooter ? ImGui::GetFrameHeightWithSpacing() + roundf(4 * uiScale) : 0;
	ImGui::BeginChild("item view", ImVec2(0, -footerHeight)); // Leave room for 1 line below us
	ImGui::Text(tab_titles[gui->settingsTab]);
	ImGui::Separator();

	static char gamedir[256];
	static char workingdir[256];
	static int numFgds = 0;
	static int numRes = 0;

	static std::vector<std::string> tmpFgdPaths;
	static std::vector<std::string> tmpResPaths;

	if (gui->reloadSettings) {
		strncpy(gamedir, g_settings.gamedir.c_str(), 256);
		tmpFgdPaths = g_settings.fgdPaths;
		tmpResPaths = g_settings.resPaths;

		numFgds = tmpFgdPaths.size();
		numRes = tmpResPaths.size();

		gui->reloadSettings = false;
	}

	int pathWidth = ImGui::GetWindowWidth() - 60 * uiScale;
	int delWidth = 50 * uiScale;

	ImGui::BeginChild("right pane content");
	if (gui->settingsTab == 0) {
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 180 * uiScale);
		ImGui::DragFloat("Movement Speed", &app->moveSpeed, 0.1f, 0.1f, 1000, "%.1f");
		ImGui::DragFloat("Rotation Speed", &app->rotationSpeed, 0.01f, 0.1f, 100, "%.1f");

		if (ImGui::DragInt("UI Scale", &g_settings.ui_scale, 0.1f, 50, 200, "%d%%")) {
			gui->updateUiScale();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Recommended scales per screen resolution:\n"
				"720p = 65%% (small but enough room for all widgets)\n"
				"1080p = 85%%\n"
				"1440p = 110%%\n"
				"2160p = 170%%\n"
				"\n110%% is the default scaling in older versions of bspguy.");
		}

		ImGui::DragInt("Undo Levels", &app->undoLevels, 0.05f, 0, 64);
		ImGui::PopItemWidth();

		ImGui::Columns(2);
		ImGui::Checkbox("Verbose Logging", &g_verbose);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("For troubleshooting the program");
		}
		ImGui::Checkbox("Invert Y Axis", &g_settings.invert_y_axis);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Invert Y axis camera rotation.\n");
		}
		ImGui::NextColumn();

		ImGui::Checkbox("Confirm Close", &g_settings.confirm_exit);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Show a warning dialog if closing the map without saving changes.\n");
		}

		if (ImGui::Checkbox("FreeType Font", &g_settings.freetype_font)) {
			gui->shouldReloadFonts = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Enable the FreeType font renderer. This makes text less blurry. "
				"Quality improvement is subjective.\n");
		}
	}
	else if (gui->settingsTab == 1) {
		static const char* renderers[RENDERER_COUNT] = {
			"OpenGL",
			"OpenGL (Legacy)",
		};

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 210 * uiScale);

		bool isLoading = app->isLoading;
		if (isLoading)
			ImGui::BeginDisabled();

		if (ImGui::BeginCombo("Renderer", renderers[g_settings.renderer]))
		{
			if (ImGui::Selectable(renderers[0], g_settings.renderer == RENDERER_OPENGL_21)) {
				g_settings.renderer = RENDERER_OPENGL_21;
				app->deselectObject();
				g_app->updateGpuSupportFlags();
				g_app->mapRenderer->reload();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("This renderer asks your OpenGL driver what it supports and conditionally enables faster rendering features.\n\nFor hardware supporting OpenGL 2.1 and up.\n");
			}

			if (ImGui::Selectable(renderers[1], g_settings.renderer == RENDERER_OPENGL_21_LEGACY)) {
				g_settings.renderer = RENDERER_OPENGL_21_LEGACY;
				app->deselectObject();
				g_app->updateGpuSupportFlags();
				g_app->mapRenderer->reload();
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("This renderer forces use of the most compatible/slowest rendering methods. Sometimes graphics drivers lie about which features they support.\n\nChoose this if textures/objects are black or completely missing.\n");
			}
			ImGui::EndCombo();
		}

		static const char* texture_qualities[2] = {
			"Maximum",
			"High",
			//"Medium",
		};
		int selectedTexQuality = 0;
		if (g_settings.texture_atlas) {
			selectedTexQuality = 1;
			//if (g_settings.max_texture_size == 128)
			//	selectedLevel = 2;
		}

		if (ImGui::BeginCombo("Texture Quality", texture_qualities[selectedTexQuality]))
		{
			if (ImGui::Selectable(texture_qualities[0], selectedTexQuality == 0)) {
				g_settings.texture_atlas = false;
				g_settings.texture_filtering = true;
				g_settings.pal_textures = false;
				g_settings.max_texture_size = 0;
				app->deselectObject();
				g_app->updateGpuSupportFlags();
				g_app->mapRenderer->reload();
			}
			tooltip("Full resolution textures WITH filtering and mipmap support. "
					"This may greatly reduce FPS due to the slower rendering techniques required.\n", 0);

			if (ImGui::Selectable(texture_qualities[1], selectedTexQuality == 1)) {
				g_settings.texture_atlas = true;
				g_settings.texture_filtering = false;
				g_settings.pal_textures = true;
				g_settings.max_texture_size = 0;
				app->deselectObject();
				g_app->updateGpuSupportFlags();
				g_app->mapRenderer->reload();
			}
			tooltip("Full resolution textures without filtering and mipmap support.", 0);
			/*
			if (ImGui::Selectable(texture_qualities[2], selectedLevel == 2)) {
				g_settings.texture_atlas = true;
				g_settings.texture_filtering = false;
				g_settings.max_texture_size = 128;
				app->deselectObject();
				g_app->updateGpuSupportFlags();
				g_app->mapRenderer->reload();
			}
			tooltip("Full resolution textures without filtering and mipmap support.", 0);
			*/
			ImGui::EndCombo();
		}

		//ImGui::DragFloat("Mipmap Bias", &g_app->tex_lod_bias, 0.02f, -4.0f, 4.0f, "%.1f");
		//tooltip("Controls how quickly texture detail decreases with distance. Higher = lower detail sooner.");

		if (isLoading)
			ImGui::EndDisabled();

		ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
		ImGui::DragFloat("Back Clipping Plane", &app->zFar, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::DragFloat("Model Render Distance", &app->modelRenderer->renderDist, 10.0f, -99999.f, 99999.f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::PopItemWidth();

		ImGui::Columns(2);

		bool canUsePalettedTextures = selectedTexQuality > 0;
		if (isLoading || !canUsePalettedTextures)
			ImGui::BeginDisabled();
		if (ImGui::Checkbox("Paletted GPU Textures", &g_settings.pal_textures)) {
			app->deselectObject();
			g_app->mapRenderer->reloadTextures();
		}
		tooltip("Greatly reduces VRAM usage, which can improve your FPS and allow big maps to load. "
			"If you aren't starved for VRAM then this may slightly lower FPS instead. "
			"Selectable at high texture quality or lower.\n", 0);
		if (isLoading || !canUsePalettedTextures)
			ImGui::EndDisabled();

		bool canUseFiltering = !g_settings.texture_atlas && !g_settings.pal_textures;
		if (isLoading || !canUseFiltering)
			ImGui::BeginDisabled();
		ImGui::Checkbox("Texture Filtering", &g_settings.texture_filtering);
		tooltip("Smooth textures. Requires maximum texture quality.\n", 0);
		if (isLoading || !canUseFiltering)
			ImGui::EndDisabled();

		ImGui::Checkbox("Animate Models", &g_settings.animate_models);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Animations have a big impact on FPS with the legacy renderer.\n");

		ImGui::NextColumn();

		if (ImGui::Checkbox("VSync", &gui->vsync)) {
			glfwSwapInterval(gui->vsync ? 1 : 0);
		}
		ImGui::Checkbox("wpoly", &g_settings.show_wpoly);
		tooltip("Display the number of polygons that would be rendered in-game. Enabling this reduces FPS when wpoly counts are high.");

		ImGui::NextColumn();
	}
	else if (gui->settingsTab == 2) {
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150 * uiScale);
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
		ImGui::Dummy(ImVec2(0, 10 * uiScale));

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
	else if (gui->settingsTab == 3) {
		for (int i = 0; i < numFgds; i++) {
			ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 100 * uiScale);
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
			app->modelRenderer->clearCache();
		}
		ImGui::EndDisabled();
	}

	ImGui::EndGroup();
}
