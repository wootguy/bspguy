#include "Widget.h"
#include "Entity.h"

void EntityReport::setup() {
	const char* plural = filteredEnts.size() == 1 ? "" : "s";
	title = cstrf("Entity Report  (%d result%s)###entreport", (int)filteredEnts.size(), plural);
	widgetName = title.c_str();
}

void EntityReport::draw() {
	if (map == NULL) {
		ImGui::Text("No map selected");
		return;
	}

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

	int footerHeight = ImGui::GetFrameHeightWithSpacing() * 5 + 16 * uiScale;
	ImGui::BeginChild("entlist", ImVec2(0, -footerHeight));

	if (gui->entityReportFilterNeeded) {
		filteredEnts.clear();
		static string searchKeys[MAX_FILTERS];
		static string searchValues[MAX_FILTERS];
		for (int i = 0; i < MAX_FILTERS; i++) {
			searchKeys[i] = trimSpaces(toLowerCase(keyFilter[i]));
			searchValues[i] = trimSpaces(toLowerCase(valueFilter[i]));
		}

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
					string searchKey = searchKeys[k];

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

					string searchValue = searchValues[k];
					if (searchKey == "spawnflags") {
						int spawnflags = atoi(ent->getKeyvalue(actualKey).c_str());
						int searchFlags = 0;

						if (isNumeric(searchValue)) {
							searchFlags = atoi(searchValue.c_str());
						}
						else {
							Fgd* fgd = app->mergedFgd;
							FgdClass* fgdClass = fgd ? fgd->getFgdClass(ent->getClassname()) : NULL;

							for (int i = 0; i < 32 && fgdClass; i++) {
								string flagName = toLowerCase(fgdClass->spawnFlagNames[i]);
								if (flagName.find(searchValue) != string::npos) {
									searchFlags |= (1 << i);
								}
							}
						}

						bool partialMatch = partialMatches && !(spawnflags & searchFlags);
						bool exactMatch = !partialMatches && spawnflags == searchFlags;
						if (searchFlags == 0 || (!partialMatch && !exactMatch)) {
							visible = false;
							break;
						}
					}
					else if (!searchValue.empty()) {
						if ((partialMatches && ent->getKeyvalue(actualKey).find(searchValue) == string::npos) ||
							(!partialMatches && ent->getKeyvalue(actualKey) != searchValue)) {
							visible = false;
							break;
						}
					}
				}
				else if (strlen(valueFilter[k]) > 0) {
					string searchValue = searchValues[k];
					bool foundMatch = false;
					for (int c = 0; c < ent->keyOrder.size(); c++) {
						string val = toLowerCase(ent->getKeyvalue(ent->keyOrder[c]));
						if (val == searchValue || (partialMatches && val.find(searchValue) != string::npos)) {
							foundMatch = true;
							break;
						}
					}

					int entSpawnflags = atoi(ent->getKeyvalue("spawnflags").c_str());
					Fgd* fgd = app->mergedFgd;
					FgdClass* fgdClass = fgd ? fgd->getFgdClass(ent->getClassname()) : NULL;
					int searchFlags = atoi(searchValue.c_str());

					for (int i = 0; i < 32 && fgdClass; i++) {
						string flagName = toLowerCase(fgdClass->spawnFlagNames[i]);
						bool partialMatch = partialMatches && flagName.find(searchValue) != string::npos;
						bool exactMatch = !partialMatches && flagName == searchValue;
						if ((partialMatch || exactMatch) && (entSpawnflags & (1 << i))) {
							foundMatch = true;
							break;
						}

						if (partialMatches && (searchFlags & entSpawnflags)) {
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
	gui->entityReportFilterNeeded = false;

	if (gui->entityReportReselectNeeded) {
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
				gui->openContextMenu(app->pickInfo.getEntIndex());
			}
		}
	}
	clipper.End();

	gui->entityReportReselectNeeded = false;

	if (filteredEnts.size() && ImGui::IsWindowHovered()) {
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			lastKeyboardNavSelect = clamp(lastKeyboardNavSelect + 1, 0, filteredEnts.size() - 1);
			app->deselectObject();
			app->pickInfo.selectEnt(filteredEnts[lastKeyboardNavSelect].idx);
			app->postSelectEnt();
			gui->entityReportReselectNeeded = true;
			ImGui::SetScrollY(clipper.ItemsHeight * lastKeyboardNavSelect - clipper.ItemsHeight * 2);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			lastKeyboardNavSelect = clamp(lastKeyboardNavSelect - 1, 0, filteredEnts.size() - 1);
			app->deselectObject();
			app->pickInfo.selectEnt(filteredEnts[lastKeyboardNavSelect].idx);
			app->postSelectEnt();
			gui->entityReportReselectNeeded = true;
			ImGui::SetScrollY(clipper.ItemsHeight * lastKeyboardNavSelect - clipper.ItemsHeight * 2);
		}

		// for delete/copy/cut
		app->shortcutControls();
	}

	if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
		app->deselectObject();
		for (int i = 0; i < filteredEnts.size(); i++) {
			app->pickInfo.selectEnt(filteredEnts[i].idx);
		}
		app->postSelectEnt();
	}

	ImGui::EndChild();

	ImGui::BeginChild("filters", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);

	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, 8 * uiScale));

	static vector<string> usedClasses;
	static set<string> uniqueClasses;

	static bool comboWasOpen = false;

	ImGui::Text("Classname Filter");
	if (ImGui::BeginCombo("##classfilter", classFilter.c_str(), ImGuiComboFlags_HeightLarge))
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
				gui->entityReportFilterNeeded = true;
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

	ImGui::Dummy(ImVec2(0, 8 * uiScale));
	ImGui::Text("Keyvalue Filter");

	ImGuiStyle& style = ImGui::GetStyle();
	float inputWidth = ImGui::GetWindowWidth() * 0.5f;
	int fontSize = g_font_scale_base;
	inputWidth -= gui->defaultFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " = ").x;

	for (int i = 0; i < MAX_FILTERS; i++) {
		ImGui::SetNextItemWidth(inputWidth);
		if (ImGui::InputText(("##Key" + to_string(i)).c_str(), keyFilter[i], 64)) {
			gui->entityReportFilterNeeded = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Filter entities by key name. Leave blank to include all key names.");
		}

		ImGui::SameLine();
		ImGui::Text(" = "); ImGui::SameLine();
		ImGui::SetNextItemWidth(inputWidth);
		if (ImGui::InputText(("##Value" + to_string(i)).c_str(), valueFilter[i], 64)) {
			gui->entityReportFilterNeeded = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Filter entities by key value. Leave blank to include all values."
				"\n\nWhen searching for a spawnflag, you can use either the integer value or its label.");
		}
	}

	if (ImGui::Checkbox("Partial Matching", &partialMatches)) {
		gui->entityReportFilterNeeded = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Do not force entity keys/values to match your input exactly.");
	}

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(10, 0));
	ImGui::SameLine();

	if (ImGui::Checkbox("Invert", &invertMatch)) {
		gui->entityReportFilterNeeded = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Invert search filter.");
	}

	ImGui::EndChild();

	ImGui::EndGroup();
}
