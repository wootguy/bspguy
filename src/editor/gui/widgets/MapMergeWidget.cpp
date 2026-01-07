#include "Widget.h"
#include "Entity.h"
#include "LimitsWidget.h"

void MergeOverlapWidget::draw() {
	string name = stripExt(basename(g_app->mergeResult.fpath));
	vec3 mergeMove = g_app->mergeResult.moveFixes;
	vec3 mergeMove2 = g_app->mergeResult.moveFixes2;

	ImGui::Text((map->name + " overlaps " + name + " and must be moved before merging.").c_str());
	ImGui::Dummy(ImVec2(0.0f, 20.0f));
	ImGui::Text(("How do you want to move " + map->name + "?").c_str());

	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 20.0f));

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
		close();
		adjustment = vec3(mergeMove.x, 0, 0);
	}

	ImGui::NextColumn();
	if (ImGui::Button(ymove.c_str(), ImVec2(inputWidth, 0))) {
		close();
		adjustment = vec3(0, mergeMove.y, 0);
	}

	ImGui::NextColumn();
	if (ImGui::Button(zmove.c_str(), ImVec2(inputWidth, 0))) {
		close();
		adjustment = vec3(0, 0, mergeMove.z);
	}

	ImGui::NextColumn();
	if (ImGui::Button(xmove2.c_str(), ImVec2(inputWidth, 0))) {
		close();
		adjustment = vec3(mergeMove2.x, 0, 0);
	}

	ImGui::NextColumn();
	if (ImGui::Button(ymove2.c_str(), ImVec2(inputWidth, 0))) {
		close();
		adjustment = vec3(0, mergeMove2.y, 0);
	}

	ImGui::NextColumn();
	if (ImGui::Button(zmove2.c_str(), ImVec2(inputWidth, 0))) {
		close();
		adjustment = vec3(0, 0, mergeMove2.z);
	}

	if (adjustment != vec3()) {
		vec3 newOri = map->ents[0]->getOrigin() + adjustment;
		map->ents[0]->setOrAddKeyvalue("origin", newOri.toKeyvalueString());
		g_app->merge(g_app->mergeResult.fpath);
	}

	ImGui::Dummy(ImVec2(0.0f, 20.0f));

	ImGui::NextColumn();
	ImGui::NextColumn();
	if (ImGui::Button("Cancel", ImVec2(inputWidth, 0))) {
		close();
		g_app->mergeResult.fpath = "";
	}
	ImGui::Dummy(ImVec2(0.0f, 20.0f));
}

void MergeFailedWidget::open() {
	gui->loadedStats = false;
}

void MergeFailedWidget::draw() {
	string engineName = g_settings.engine == ENGINE_HALF_LIFE ? "Half-Life" : "Sven Co-op";
	ImGui::Text(("Merging the selected maps would overflow \"" + engineName + "\" engine limits.\n"
		"Optimize the maps or manually remove unused structures before trying again.").c_str());

	ImGui::Dummy(ImVec2(0.0f, 20.0f));

	ImGui::Separator();

	((LimitsWidget*)gui->widgets[WIDGET_LIMITS])->drawLimitsSummary(g_app->mergeResult.map, true);

	ImGuiStyle& style = ImGui::GetStyle();
	float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
	float inputWidth = (ImGui::GetWindowWidth() - padding) * 0.33f;

	ImGui::Dummy(ImVec2(0.0f, 20.0f));
	ImGui::Columns(3, 0, false);

	ImGui::NextColumn();
	if (ImGui::Button("OK", ImVec2(inputWidth, 0))) {
		close();
		g_app->mergeResult.fpath = "";
		delete g_app->mergeResult.map;
		g_app->mergeResult.map = NULL;
		gui->loadedStats = false;
	}
	ImGui::Dummy(ImVec2(0.0f, 20.0f));
}

void MergeMultipleWidget::draw() {
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
	ImGui::SetColumnWidth(0, 450 * uiScale);
	ImGui::SetColumnWidth(1, 50 * uiScale);
	ImGui::SetNextItemWidth(450 * uiScale);
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

	ImGui::Dummy(ImVec2(0, 10 * uiScale));

	ImGui::Columns(4, 0, false);
	ImGui::SetColumnWidth(0, 130 * uiScale);
	ImGui::SetColumnWidth(1, 80 * uiScale);
	ImGui::SetColumnWidth(2, 80 * uiScale);
	ImGui::SetColumnWidth(3, 200 * uiScale);
	ImGui::AlignTextToFramePadding();
	ImGui::Text("Series Ripent: "); ImGui::NextColumn();

	ImGui::RadioButton("No", &ripentmode, 0); ImGui::NextColumn();
	if (ImGui::IsItemHovered()) {
		tooltip("Do not touch any of the map entity logic.\n");
	}

	ImGui::RadioButton("Yes", &ripentmode, 1); ImGui::NextColumn();
	if (ImGui::IsItemHovered()) {
		tooltip(
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
		tooltip(
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
	ImGui::Dummy(ImVec2(0, roundf(2 * uiScale)));
	ImGui::Text("Preparations:");
	ImGui::SameLine();
	ImGui::Dummy(ImVec2(roundf(3 * uiScale), 0));
	ImGui::SameLine();

	ImGui::Checkbox("Optimize", &optimizeMerge);
	tooltip((string("Optimizes maps before merging. Try this if map limits are exceeded.\n\nOptimizing ") + g_optimize_tip).c_str());

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(10 * uiScale, 0));
	ImGui::SameLine();
	ImGui::Checkbox("No Hull 2", &forceNohull2);
	tooltip("Forces redirection of hull 2 to hull 1 in each map before merging. This reduces "
		"clipnodes and collision accuracy for large monsters and pushables.");

	ImGui::Dummy(ImVec2(0, roundf(2 * uiScale)));
	ImGui::Text("Map Size: ");
	ImGui::SameLine();
	ImGui::Dummy(ImVec2(30 * uiScale, 0));
	ImGui::SameLine();
	ImGui::Text((string("+/-") + to_string(g_settings.mapsize_max)).c_str());
	if (ImGui::IsItemHovered())
		tooltip("Change the Map Size in the Settings menu to adjust the bounds for merged maps.\n");

	ImGui::Dummy(ImVec2(0, 10 * uiScale));

	if (numSelected < 2) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Merge")) {
		vector<string> input_maps = splitString(mapList, "|");

		MergeResult result = BspMerger::createMergedMap(input_maps, "merged_map", optimizeMerge,
			forceNohull2, ripentmode);

		if (result.invalidMaps) {
			string msg = "One or more of the input maps failed to load.";
			int ret = Alert(
				"Invalid Map", /* NULL or "" */
				msg.c_str(), /* NULL or "" may contain \n \t */
				"ok", /* "ok" "okcancel" "yesno" "yesnocancel" */
				"error", /* "info" "warning" "error" "question" */
				0);
		}
		else if (result.overflow) {
			g_app->mergeResult = result;
			close(true);
			gui->showWidget(WIDGET_MERGE_FAILED, true);
		}
		else if (result.notEnoughSpace) {
			string msg = "The merger failed to fit all maps inside the configured Map Size.\n\n"
				"Do you want to try arranging the maps yourself?";
			int ret = Alert(
				"Packing Failure", /* NULL or "" */
				msg.c_str(), /* NULL or "" may contain \n \t */
				"yesno", /* "ok" "okcancel" "yesno" "yesnocancel" */
				"warning", /* "info" "warning" "error" "question" */
				0);

			if (result.map)
				delete result.map;

			if (ret == 1) {
				gui->showWidget(WIDGET_TRANSFORM, true);
				close();
				g_app->mergeMultiple(input_maps, optimizeMerge, forceNohull2, ripentmode);
			}
		}
		else if (result.map->isWritable()) {
			g_app->openMap(result.map);
			close();
		}
	}
	if (numSelected < 2) {
		ImGui::EndDisabled();
	}

	ImGui::SameLine();

	if (ImGui::Button("Cancel")) {
		close();
	}
}