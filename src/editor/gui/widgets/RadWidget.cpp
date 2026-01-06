#include "Widget.h"
#include "Entity.h"
#include <algorithm>

void RadWidget::open() {
	refreshTexlightList = true;
	texlights = map->get_tex_lights();
}

void RadWidget::setup() {
	ImGui::SetNextWindowSize(ImVec2(400 * uiScale, max(400.0f * uiScale, app->windowHeight * 0.6f)), ImGuiCond_Appearing);
}

void RadWidget::draw() {
	
	//ImGui::SetNextWindowSizeConstraints(ImVec2(350 * uiScale, 300 * uiScale), ImVec2(FLT_MAX, app->windowHeight));

	ImGui::TextWrapped("Texture light information may be needed for recompiling map lighting. "
		"Below are texlights found in this map's info_texlight entities."
	);

	ImGui::Dummy(ImVec2(0, 10 * uiScale));

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
	tooltip("Add default texlight values shipped with Valve Hammer Editor 3.5. Texture names not used in the map are ignored.");

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
	tooltip("Add texlights from a custom lights.rad file. Texture names not used in the map are ignored.");

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
	tooltip("Estimate texlight values by analyzing lightmaps. Colors "
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
	multisize.y -= 80.0f * uiScale;
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

	ImGui::Dummy(ImVec2(0, 10 * uiScale));

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

		map->update_ent_lump();
		map->write(map->path);

		close();
	}
	tooltip(("Prepararing this map will:\n\n"
		"1. Create/update an info_texlights entity according to the above configuration.\n"
		"2. Convert light/light_spot entities to light_surface, if that's what they were originally.\n"
		"3. Delete embedded RAD textures (fixes \"Malformed face\" errors when these are invalid).\n"
		"4. Duplicate shared texlight models (deduplicated models can't emit light).\n"
		"5. Generate a \"" + map->name + ".wa_\" file for running RAD without the -notextures option.\n"
		"6. Save the map.").c_str()
	);

	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		close();
	}
}