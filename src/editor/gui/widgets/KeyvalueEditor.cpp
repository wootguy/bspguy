#include "Widget.h"
#include "Entity.h"

void KeyvalueEditor::draw() {
	static int selectedFgdIdx = -1;

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
		ImGui::PushFont(gui->defaultFont, g_font_scale_base * 1.1f);
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
			ImGui::PushFont(gui->defaultFont, g_font_scale_base * 1.1f);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("FGD:");
			if (ImGui::IsItemHovered()) {
				ImGui::PopFont();
				ImGui::SetTooltip("The Game Definition File determines which keyvalues/flags to display.\n\n"
					"This will change automatically if an entity definition isn't found\n"
					"in the FGD you previously selected.\n");
				ImGui::PushFont(gui->defaultFont, g_font_scale_base * 1.1f);
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
							gui->entityReportFilterNeeded = true;
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

		ImGui::Dummy(ImVec2(0, 10 * uiScale));

		float tabBarY = ImGui::GetCursorPosY() - style.ItemSpacing.y * 2;

		if (ImGui::BeginTabBar("##tabs"))
		{
			ImVec2 condensedPadding = ImVec2(style.FramePadding.x, roundf(0.70f * uiScale));

			if (ImGui::BeginTabItem("Attributes")) {
				ImGui::Dummy(ImVec2(0, 10 * uiScale));
				if (!sameClassesSelected) {
					ImGui::Text("Multiple entity classes selected.");
					ImGui::Text("Use the Raw Edit tab instead.");
				}
				else {
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, condensedPadding);
					ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.FramePadding.x, roundf(1.0f * uiScale)));
					drawSmartEditTab(fgd);
					ImGui::PopStyleVar(2);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Flags")) {
				ImGui::Dummy(ImVec2(0, 10 * uiScale));
				if (!sameClassesSelected) {
					ImGui::Text("Multiple entity classes selected.");
					ImGui::Text("Use the Raw Edit tab instead.");
				}
				else {
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, condensedPadding);
					drawFlagsTab(fgd);
					ImGui::PopStyleVar();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Raw Edit")) {
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, condensedPadding);
				drawRawEditTab();
				ImGui::PopStyleVar(1);
				ImGui::EndTabItem();
			}
		}

		// Compute widths
		float wPurge = ImGui::CalcTextSize("Purge").x + style.FramePadding.x * 2.0f;
		float wCopy = ImGui::CalcTextSize("Copy").x + style.FramePadding.x * 2.0f;
		float wPaste = ImGui::CalcTextSize("Paste").x + style.FramePadding.x * 2.0f;
		float totalW = wPurge + wCopy + wPaste + (style.ItemSpacing.x * 4.0f);
		float right = ImGui::GetContentRegionMax().x;

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
		ImGui::SameLine();
		ImGui::SetCursorPosX(right - totalW);
		ImGui::SetCursorPosY(tabBarY);

		if (ImGui::Button("Purge")) {
			app->updateEntityUndoState();

			vector<Entity*> ents = app->pickInfo.getEnts();
			for (Entity* ent : ents) {
				StringMap allKeys = ent->getAllKeyvalues();
				allKeys.del("origin");
				allKeys.del("classname");
				if (g_settings.ripent_safe_mode && ent->getClassname() == "worldspawn")
					allKeys.del("wad");
					
				StringMap::iterator_t iter;
				while (allKeys.iterate(iter)) {
					ent->removeKeyvalue(iter.key);
				}
			}

			app->pushEntityUndoState("Purge keyvalues");
		}
		tooltip("Delete all keyvalues except for origin and classname.");

		ImGui::SameLine();
		ImGui::SetCursorPosY(tabBarY);
		if (ImGui::Button("Copy")) {
			copiedKeyvalues.clear();
			unordered_set<string> conflictedKeys;

			vector<Entity*> ents = app->pickInfo.getEnts();
			for (Entity* ent : ents) {
				StringMap allKeys = ent->getAllKeyvalues();
				allKeys.del("origin");
				allKeys.del("classname");
				StringMap::iterator_t iter;
				while (allKeys.iterate(iter)) {
					if (copiedKeyvalues.find(iter.key) != copiedKeyvalues.end()) {
						if (copiedKeyvalues[iter.key] != iter.value) {
							conflictedKeys.insert(iter.key);
						}
					}

					copiedKeyvalues[iter.key] = iter.value;
				}
			}

			for (string item : conflictedKeys) {
				copiedKeyvalues.erase(item);
			}
		}
		tooltip("Copy all keyvalues except for origin and classname. Keyvalues with conflicts "
			"are excluded when multiple entities are selected.");

		if (copiedKeyvalues.empty())
			ImGui::BeginDisabled();
		ImGui::SameLine();
		ImGui::SetCursorPosY(tabBarY);
		if (ImGui::Button("Paste")) {
			app->updateEntityUndoState();

			vector<Entity*> ents = app->pickInfo.getEnts();
			for (Entity* ent : ents) {
				for (auto item : copiedKeyvalues) {
					if (g_settings.ripent_safe_mode && item.first == "wad" && ent->getClassname() == "worldspawn")
						continue;
					ent->setOrAddKeyvalue(item.first, item.second);
				}
			}

			app->pushEntityUndoState("Paste keyvalues");
		}
		tooltip("Apply copied keyvalues to the selected entities.");

		if (copiedKeyvalues.empty())
			ImGui::EndDisabled();

		ImGui::PopStyleVar();

		ImGui::EndTabBar();

	}
	else {
		if (app->pickInfo.ents.size() > 1)
			ImGui::Text("Multiple entities selected");
		else if (!app->pickInfo.getEnt())
			ImGui::Text("No entity selected");
	}
}

void KeyvalueEditor::drawSmartEditTab(Fgd* fgd) {
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

	static int lastPickCount = 0;

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
		bool lastWasCollapsible = false;
		for (int k = 0; k < groups.size(); k++) {
			COLOR3 c2 = groups[k].color;
			ImVec4 c = ImVec4(c2.r / 255.0f, c2.g / 255.0f, c2.b / 255.0f, 1.0f);

			bool isSelf = toLowerCase(groups[k].name) == lowerClass;

			if (groups[k].keys.size() > 3 && !isSelf) {
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(c.x, c.y, c.z, 0.3f)); // Set background color (blue)
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(c.x, c.y, c.z, 0.4f)); // Set hovered color (lighter blue)
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(c.x, c.y, c.z, 0.2f)); // Set active color (darker blue)

				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(c.x, c.y, c.z, 0.1f));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f));  // Set InputText background to fully opaque (white)
				ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f)); // Hovered state
				ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.1137f, 0.1882f, 0.2824f, 1.0f));

				static bool isExpanded = true;

				// fix too little space before collapsible group after a non-collapsible one
				if (!lastWasCollapsible) {
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, style.FramePadding.y * 2));
					ImGui::Dummy(ImVec2(0, 0));
					ImGui::PopStyleVar(1);
				}

				ImGui::BeginChild(("ChildArea" + groups[k].name).c_str(), ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY);
				string title = groups[k].name;

				if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
					drawSmartEditTab_GroupKeys(groups[k].keys, true, keyOffset);
				}

				ImGui::EndChild();
				ImGui::PopStyleColor(7);
				lastWasCollapsible = true;
			}
			else {
				drawSmartEditTab_GroupKeys(groups[k].keys, false, keyOffset);
			}

			keyOffset += groups[k].keys.size();
		}

		lastPickCount = app->pickCount;
	}

	ImGui::EndChild();
}

void KeyvalueEditor::drawSmartEditTab_GroupKeys(vector<KeyvalueDef*>& keys, bool isGrouped, int keyOffset) {
	ImGuiContext& g = *GImGui;
	ImGuiStyle& style = ImGui::GetStyle();

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
	int keynameColWidth = std::min(400 * uiScale, ImGui::GetContentRegionAvail().x * 0.5f);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	if (ImGui::BeginTable("table", 2, ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, keynameColWidth);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

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

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

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

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - style.FramePadding.x);

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
					// calc field size for text wrapping
					int textWidth = ImGui::CalcTextSize(keyValues[bufferIdx]).x;
					int fieldWidth = ImGui::GetContentRegionAvail().x - style.FramePadding.x;
					ImVec2 framePadding = style.FramePadding;
					int lineWidth = fieldWidth - (framePadding.x * 2 + style.ScrollbarSize);
					int fieldHeight = ImGui::GetFrameHeight();
					if (textWidth > lineWidth) {
						// scaled up to account for wasted space during wraps
						int extraLines = std::max(1.0f, (int)((textWidth + lineWidth * 0.5f) / lineWidth) * 1.2f);
						fieldHeight += ImGui::GetTextLineHeight() * std::min(extraLines, 8);
					}

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
				tooltip(keyValues[bufferIdx], 0);
			}

			if (ImGui::IsItemHovered()) {
				if (!matchingValues) {
					ImGui::SetTooltip("This value differs between the selected entities");
				}
			}

			if (colorChanged) {
				ImGui::PopStyleColor(2);
			}
		}

		ImGui::EndTable();
	}
	ImGui::PopStyleVar(1);
}

void KeyvalueEditor::drawFlagsTab(Fgd* fgd) {
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

			gui->entityReportFilterNeeded = true;
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

void KeyvalueEditor::drawRawEditTab() {
	ImGuiContext& g = *GImGui;
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, "keyvalcols", false);

	float butColWidth = gui->defaultFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2);
	float keyColWidth = std::min(300 * uiScale, textColWidth * 0.5f);
	float valColWidth = textColWidth - keyColWidth;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, keyColWidth);
	ImGui::SetColumnWidth(2, valColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text("  Key"); ImGui::NextColumn();
	ImGui::Text("Value"); ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild("RawValuesWindow");

	ImGui::Columns(4, "keyvalcols2", false);

	textColWidth -= style.ScrollbarSize * 2; // double space to prevent accidental deletes
	keyColWidth = std::min(300 * uiScale, textColWidth * 0.5f);
	valColWidth = textColWidth - keyColWidth;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, keyColWidth);
	ImGui::SetColumnWidth(2, valColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	static char keyNames[MAX_KEYS_PER_ENT][MAX_KEY_LEN];
	static char keyValues[MAX_KEYS_PER_ENT][MAX_VAL_LEN];

	float paddingx = style.WindowPadding.x + style.FramePadding.x;

	unordered_set<string> addedKeys;
	static vector<string> combinedKeys;
	vector<string> oldCombinedKeys = combinedKeys;
	vector<Entity*> pickEnts = app->pickInfo.getEnts();
	combinedKeys.clear();
	bool multiedit = pickEnts.size() > 1;
	Bsp* map = app->pickInfo.getMap();
	bool fullRefreshNeeded = false;
	bool worldSpawnSelected = false;

	if (multiedit) {
		for (int i = 0; i < app->pickInfo.ents.size(); i++) {
			Entity* ent = map->ents[app->pickInfo.ents[i]];

			if (ent->getClassname() == "worldspawn")
				worldSpawnSelected = true;

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
		Entity* ent = app->pickInfo.getEnt();
		combinedKeys = ent->keyOrder;
		worldSpawnSelected = ent->getClassname() == "worldspawn";
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
		ImVec2 framePadding = ImGui::GetStyle().FramePadding;
		int textWidthKey = ImGui::CalcTextSize(keyNames[i]).x;
		int textWidthVal = ImGui::CalcTextSize(keyValues[i]).x;
		int lineWidthKey = keyColWidth - (framePadding.x * 2 + style.ScrollbarSize);
		int lineWidthVal = valColWidth - (framePadding.x * 2 + style.ScrollbarSize);
		int fieldHeight = ImGui::GetFrameHeight();
		bool wrappingNeededKey = textWidthKey > lineWidthKey;
		bool wrappingNeededVal = textWidthVal > lineWidthVal;
		if (wrappingNeededKey || wrappingNeededVal) {
			// scaled up to account for wasted space during wraps
			int extraLinesKey = std::max(1.0f, (int)((textWidthKey + lineWidthKey * 0.5f) / lineWidthKey) * 1.2f);
			int extraLinesVal = std::max(1.0f, (int)((textWidthVal + lineWidthVal * 0.5f) / lineWidthVal) * 1.2f);
			int extraLines = std::max(extraLinesKey, extraLinesVal);
			fieldHeight += ImGui::GetTextLineHeight() * std::min(extraLines, 8);
		}

		// drag buttons
		{
			ImGui::BeginDisabled(multiedit);
			style.SelectableTextAlign.x = 0.5f;
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Header, hoveredDrag[i] ? dragColor : dragButColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, dragColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, dragColor);
			ImGui::Selectable(item, true, 0, ImVec2(0, 19 * uiScale));
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
				int deltaY = (ImGui::GetMousePos().y - startY) - ImGui::GetTextLineHeight();

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
					ent->cachedAllKvStr = "";

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
		bool ripentUnsafe = g_settings.ripent_safe_mode && worldSpawnSelected && !strcmp(keyNames[i], "wad");

		// key column
		{
			bool invalidKey = ignoreErrors == 0 && lastPickCount == app->pickCount && key != keyNames[i];

			strncpy(keyNames[i], key.c_str(), MAX_KEY_LEN);
			keyNames[i][MAX_KEY_LEN - 1] = 0;

			keyIds[i].idx = i;
			keyIds[i].bspRenderer = app->mapRenderer;
			keyIds[i].gui = gui;
			keyIds[i].matchingValues = matchingValues;
			keyIds[i].commonValue = matchValue;

			bool coloredKey = true;
			if (invalidKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (!sharedKey) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			}
			else if (ripentUnsafe) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImVec4(0.5f, 0.5f, 0.0f, 0.5f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			else {
				coloredKey = false;
			}
			int flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways;
			if (ripentUnsafe)
				flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap;
			ImGui::InputTextMultiline(("##key" + to_string(i) + "_" + to_string(app->pickCount)).c_str(),
				keyNames[i], MAX_KEY_LEN, ImVec2(keyColWidth, fieldHeight),
				flags,
				TextChangeCallback::keyNameChanged, &keyIds[i]);

			if (ImGui::IsItemHovered()) {
				if (invalidKey) {
					tooltip("Key already exists", 0);
				}
				else if (!sharedKey) {
					tooltip("This key does not exist in all selected entities", 0);
				}
				else if (ripentUnsafe) {
					tooltip("This key cannot be meaningfully be changed server-side. Editing doesn't "
						"trigger a map-differs error, but clients won't receive the updated value. "
						"Clients use whatever worldspawn keyvalues are stored in their local copy of "
						"the BSP."
						"\n\nOther worldspawn keyvalues are safe to edit because they're only "
						"used server-side, or sent to clients as CVars (sv_skyname, sv_zmax, etc.)", 0);
				}
				else if (wrappingNeededKey) {
					tooltip(keyNames[i], 0);
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
			valueIds[i].gui = gui;
			valueIds[i].matchingValues = matchingValues;
			valueIds[i].commonValue = matchValue;

			bool colorChanged = true;
			if (!matchingValues) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.5f, 0.0f, 0.5f));
			}
			else if (hoveredDrag[i]) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			else if (ripentUnsafe) {
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.0f, 0.5f));
			}
			else {
				colorChanged = false;
			}

			int flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackAlways;
			if (ripentUnsafe)
				flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap;
			ImGui::InputTextMultiline(("##val" + to_string(i) + to_string(app->pickCount)).c_str(),
				keyValues[i], MAX_VAL_LEN, ImVec2(valColWidth, fieldHeight),
				flags,
				TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				if (!matchingValues) {
					tooltip("This value differs between the selected entities", 0);
				}
				else if (wrappingNeededVal) {
					tooltip(keyValues[i], 0);
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
		if (!ripentUnsafe) {

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
		}
		ImGui::NextColumn();
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
